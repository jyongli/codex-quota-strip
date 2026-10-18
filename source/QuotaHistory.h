#pragma once
#include "QuotaCore.h"
#include <filesystem>
#include <optional>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <deque>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace quota {
// 每条记录保存真实快照；变化以百分点计，缺失窗口和新周期不计算差值。
struct HistoryEntry {
    Snapshot snapshot;
    long long recorded = 0;
    std::optional<double> fiveChange, weeklyChange;
    std::string source, kind;
};
struct HistoryReadResult {
    std::vector<HistoryEntry> entries; // 按记录顺序从旧到新，查看器可倒序显示。
    bool truncated = false;
    std::string error;
};
class HistoryStore {
    mutable std::mutex mutex_;
    bool enabled_ = false, hasLast_ = false;
    std::filesystem::path directory_;
    Snapshot last_;
    std::string source_, error_;
public:
    void configure(bool enabled, const std::filesystem::path& directory);
    void reset(); // 账号切换后断开差值基线，避免把两个账号的额度相减。
    bool record(const Snapshot& snapshot, const std::string& source);
    std::string error() const;
    HistoryReadResult read(size_t maxRows = 2000) const;
    static bool ValidateDirectory(const std::filesystem::path& directory, std::string& error);
};
namespace history_detail {
inline long long Now() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
inline std::string Day(long long milliseconds) {
    std::time_t time = static_cast<std::time_t>(milliseconds / 1000); std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    char value[32]{}; std::strftime(value, sizeof(value), "%Y-%m-%d", &utc); return value;
}
inline const std::string& Header() {
    static const std::string value = "observed_ms,recorded_ms,source,event,five_remaining_percent,weekly_remaining_percent,five_change_pp,weekly_change_pp,five_reset_at,weekly_reset_at\n";
    return value;
}
// 只锁定本条日志文件，立即失败而不等待其他进程；不启动常驻服务或额外轮询。
inline bool Append(const std::filesystem::path& path, const std::string& row, const std::string& headerText = Header()) {
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    OVERLAPPED lock{};
    if (!LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD, &lock)) { CloseHandle(file); return false; }
    LARGE_INTEGER size{}; bool ok = GetFileSizeEx(file, &size) != FALSE;
    bool partial = false;
    if (ok && size.QuadPart > 0) {
        std::string header(headerText.size(), '\0'); DWORD read = 0;
        ok = ReadFile(file, header.data(), static_cast<DWORD>(header.size()), &read, nullptr) && read == header.size() && header == headerText;
        LARGE_INTEGER tail{}; tail.QuadPart = -1; char byte = '\0';
        if (ok) ok = SetFilePointerEx(file, tail, nullptr, FILE_END) && ReadFile(file, &byte, 1, &read, nullptr) && read == 1;
        partial = byte != '\n';
    }
    // 保留损坏尾行但补换行，让下一条完整记录可以独立解析。
    const auto bytes = size.QuadPart == 0 ? headerText + row : (partial ? "\n" + row : row);
    DWORD written = 0;
    if (ok) ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
    UnlockFileEx(file, 0, MAXDWORD, MAXDWORD, &lock); CloseHandle(file); return ok;
#else
    int file = open(path.c_str(), O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (file < 0) return false;
    struct stat status{};
    bool ok = fstat(file, &status) == 0 && S_ISREG(status.st_mode) && flock(file, LOCK_EX | LOCK_NB) == 0;
    bool partial = false;
    if (ok && status.st_size > 0) {
        std::string header(headerText.size(), '\0'); auto count = pread(file, header.data(), header.size(), 0);
        ok = count == static_cast<ssize_t>(header.size()) && header == headerText;
        char byte = '\0'; if (ok) ok = pread(file, &byte, 1, status.st_size - 1) == 1;
        partial = byte != '\n';
    }
    const auto bytes = status.st_size == 0 ? headerText + row : (partial ? "\n" + row : row);
    if (ok) { ssize_t count; do { count = write(file, bytes.data(), bytes.size()); } while (count < 0 && errno == EINTR); ok = count == static_cast<ssize_t>(bytes.size()); }
    close(file); return ok;
#endif
}
inline bool WindowValid(const Window& value) { return !value.present || (std::isfinite(value.used) && value.used >= 0 && value.used <= 100 && value.resets >= 0 && value.resets < 100000000000LL); }
inline bool Equal(const Window& a, const Window& b) { return a.present == b.present && (!a.present || (a.used == b.used && a.resets == b.resets)); }
inline std::optional<double> Difference(const Window& current, const Window& previous) {
    if (!current.present || !previous.present || current.resets != previous.resets) return std::nullopt;
    return previous.used - current.used;
}
inline std::string Number(std::optional<double> value) {
    if (!value) return {};
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(15) << *value; return out.str();
}
inline std::string Serialize(const HistoryEntry& entry) {
    const auto& s = entry.snapshot;
    return std::to_string(s.observed) + ',' + std::to_string(entry.recorded) + ',' + entry.source + ',' + entry.kind + ',' +
        Number(s.fiveHour.present ? std::optional<double>(100 - s.fiveHour.used) : std::nullopt) + ',' +
        Number(s.weekly.present ? std::optional<double>(100 - s.weekly.used) : std::nullopt) + ',' + Number(entry.fiveChange) + ',' + Number(entry.weeklyChange) + ',' +
        (s.fiveHour.present ? std::to_string(s.fiveHour.resets) : "") + ',' + (s.weekly.present ? std::to_string(s.weekly.resets) : "") + '\n';
}
inline long long Integer(const std::string& value) { size_t end = 0; auto result = std::stoll(value, &end); if (end != value.size() || result < 0) throw std::runtime_error("Invalid history integer"); return result; }
inline std::optional<double> Decimal(const std::string& value, double min, double max) {
    if (value.empty()) return std::nullopt;
    std::istringstream in(value); in.imbue(std::locale::classic()); double result;
    if (!(in >> result) || in.peek() != EOF || !std::isfinite(result) || result < min || result > max) throw std::runtime_error("Invalid history number");
    return result;
}
inline bool Parse(std::string line, HistoryEntry& entry) {
    if (line.size() > 1024) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::vector<std::string> fields; size_t start = 0;
    for (;;) { auto comma = line.find(',', start); fields.push_back(line.substr(start, comma == std::string::npos ? comma : comma - start)); if (comma == std::string::npos) break; start = comma + 1; }
    if (fields.size() != 10 || (fields[2] != "account" && fields[2] != "local-event") || (fields[3] != "baseline" && fields[3] != "change" && fields[3] != "reset")) return false;
    try {
        entry.snapshot.observed = Integer(fields[0]); entry.recorded = Integer(fields[1]); entry.source = fields[2]; entry.kind = fields[3];
        if (!entry.snapshot.observed || !entry.recorded) return false;
        auto five = Decimal(fields[4], 0, 100), weekly = Decimal(fields[5], 0, 100);
        if (five) entry.snapshot.fiveHour = {true, 100 - *five, Integer(fields[8])};
        if (weekly) entry.snapshot.weekly = {true, 100 - *weekly, Integer(fields[9])};
        entry.fiveChange = Decimal(fields[6], -100, 100); entry.weeklyChange = Decimal(fields[7], -100, 100);
        return (five || weekly) && WindowValid(entry.snapshot.fiveHour) && WindowValid(entry.snapshot.weekly) &&
            (five || !entry.fiveChange) && (weekly || !entry.weeklyChange);
    } catch (...) { return false; }
}
}
inline void HistoryStore::configure(bool enabled, const std::filesystem::path& directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (enabled_ != enabled || directory_ != directory) { hasLast_ = false; error_.clear(); }
    enabled_ = enabled; directory_ = directory;
}
inline void HistoryStore::reset() { std::lock_guard<std::mutex> lock(mutex_); hasLast_ = false; }
inline bool HistoryStore::record(const Snapshot& snapshot, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_ || snapshot.observed <= 0 || (!snapshot.fiveHour.present && !snapshot.weekly.present) ||
        !history_detail::WindowValid(snapshot.fiveHour) || !history_detail::WindowValid(snapshot.weekly) || (source != "account" && source != "local-event")) return false;
    if (hasLast_ && snapshot.observed < last_.observed) return false;
    if (hasLast_ && source_ == source && history_detail::Equal(snapshot.fiveHour, last_.fiveHour) && history_detail::Equal(snapshot.weekly, last_.weekly)) { last_ = snapshot; return false; }
    HistoryEntry entry; entry.snapshot = snapshot; entry.recorded = history_detail::Now(); entry.source = source; entry.kind = "baseline";
    if (hasLast_ && source_ == source) {
        auto reset = [](const Window& a, const Window& b) { return a.present && b.present && a.resets != b.resets; };
        entry.kind = reset(snapshot.fiveHour, last_.fiveHour) || reset(snapshot.weekly, last_.weekly) ? "reset" : "change";
        entry.fiveChange = history_detail::Difference(snapshot.fiveHour, last_.fiveHour); entry.weeklyChange = history_detail::Difference(snapshot.weekly, last_.weekly);
    }
    try {
        if (directory_.empty() || !directory_.is_absolute()) throw std::runtime_error("Invalid history directory");
        std::filesystem::create_directories(directory_);
        if (!history_detail::Append(directory_ / ("quota-history-" + history_detail::Day(entry.recorded) + ".csv"), history_detail::Serialize(entry))) throw std::runtime_error("History append failed");
        last_ = snapshot; source_ = source; hasLast_ = true; error_.clear(); return true;
    } catch (...) { error_ = "Cannot write quota history; check directory and permissions"; return false; }
}
inline std::string HistoryStore::error() const { std::lock_guard<std::mutex> lock(mutex_); return error_; }
inline HistoryReadResult HistoryStore::read(size_t maxRows) const {
    std::lock_guard<std::mutex> lock(mutex_); HistoryReadResult result;
    maxRows = std::min<size_t>(maxRows, 2000); if (!maxRows || directory_.empty()) return result;
    // 固定检查最近 30 个 UTC 日期，不枚举整个目录；总读量上限 2MiB。
    size_t budget = 2 * 1024 * 1024; auto now = history_detail::Now();
    try {
        for (int day = 0; day < 30 && budget; ++day) {
            auto path = directory_ / ("quota-history-" + history_detail::Day(now - day * 86400000LL) + ".csv");
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) { if (ec) throw std::runtime_error("History access failed"); continue; }
            if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("History is not a file");
            std::ifstream in(path, std::ios::binary | std::ios::ate); if (!in) throw std::runtime_error("History open failed");
            auto end = in.tellg(); if (end < 0) throw std::runtime_error("History size failed");
            auto count = std::min<size_t>(static_cast<size_t>(end), budget); auto offset = static_cast<std::streamoff>(end) - static_cast<std::streamoff>(count);
            std::string bytes(count, '\0'); in.seekg(offset); in.read(bytes.data(), static_cast<std::streamsize>(count)); bytes.resize(static_cast<size_t>(in.gcount())); budget -= count;
            size_t at = 0; if (offset > 0) { result.truncated = true; auto newline = bytes.find('\n'); at = newline == std::string::npos ? bytes.size() : newline + 1; }
            std::deque<HistoryEntry> rows;
            while (at < bytes.size()) {
                auto newline = bytes.find('\n', at); if (newline == std::string::npos) break; HistoryEntry entry;
                if (history_detail::Parse(bytes.substr(at, newline - at), entry)) {
                    if (rows.size() == maxRows) { rows.pop_front(); result.truncated = true; }
                    rows.push_back(std::move(entry));
                }
                at = newline + 1;
            }
            for (auto row = rows.rbegin(); row != rows.rend(); ++row) { if (result.entries.size() == maxRows) { result.truncated = true; break; } result.entries.push_back(std::move(*row)); }
            if (result.entries.size() == maxRows) { result.truncated = true; break; }
        }
        if (!budget) result.truncated = true;
    } catch (...) { result.error = "Cannot read quota history; check directory and permissions"; }
    // 即使旧文件读取失败，也保持已读部分的旧到新顺序，图表不会得到负时间跨度。
    std::reverse(result.entries.begin(), result.entries.end());
    return result;
}
inline bool HistoryStore::ValidateDirectory(const std::filesystem::path& directory, std::string& error) {
    try {
        if (directory.empty() || !directory.is_absolute()) throw std::runtime_error("History directory must be absolute");
        std::filesystem::create_directories(directory);
        static std::atomic<unsigned long long> sequence{0};
#ifdef _WIN32
        auto pid = GetCurrentProcessId();
#else
        auto pid = getpid();
#endif
        auto probe = directory / (".quota-history-probe-" + std::to_string(pid) + '-' + std::to_string(history_detail::Now()) + '-' + std::to_string(++sequence));
#ifdef _WIN32
        HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("History directory is not writable");
        CloseHandle(file);
#else
        int file = open(probe.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (file < 0) throw std::runtime_error("History directory is not writable");
        close(file); unlink(probe.c_str());
#endif
        error.clear(); return true;
    } catch (...) { error = "Cannot write quota history; check directory and permissions"; return false; }
}
}

#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include "QuotaCore.h"
#include "RequestUsage.h"

namespace quota {
class Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
public:
    explicit Handle(HANDLE v = INVALID_HANDLE_VALUE) : h(v) {}
    ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    operator HANDLE() const { return h; }
    bool valid() const { return h && h != INVALID_HANDLE_VALUE; }
};
inline long long FileTimeMs(FILETIME f) {
    ULARGE_INTEGER u{}; u.LowPart = f.dwLowDateTime; u.HighPart = f.dwHighDateTime;
    return static_cast<long long>(u.QuadPart / 10000) - 11644473600000LL;
}
inline long long NowMs() { FILETIME f{}; GetSystemTimeAsFileTime(&f); return FileTimeMs(f); }

class FileFollower {
    struct Cursor {
        long long offset = 0; MetadataLines lines; RequestTracker requests;
        unsigned long long touched = 0, identity = 0; DWORD volume = 0;
        explicit Cursor(long long since) : requests(since) {}
    };
    std::unordered_map<std::wstring, Cursor> files;
    unsigned long long serial = 0;
    // 整个监听器共用启动时间；新发现文件不能按首次读盘时刻漏掉刚完成的请求。
    long long requestSince_ = NowMs();
public:
    unsigned long long bytesRead = 0;
    unsigned long long reads = 0;
    void clear() { files.clear(); }
    void forget(const std::wstring& name) { files.erase(name); }
    bool read(const std::wstring& name, const std::function<void(const Snapshot&)>& accept,
              const std::function<void(const RequestEntry&)>& request = {}) {
        Handle file(CreateFileW(name.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (!file.valid()) return false;
        LARGE_INTEGER end{}; if (!GetFileSizeEx(file, &end)) return false;
        auto& c = files.try_emplace(name, requestSince_).first->second; c.touched = ++serial;
        BY_HANDLE_FILE_INFORMATION info{};
        if (GetFileInformationByHandle(file, &info)) {
            unsigned long long identity = (static_cast<unsigned long long>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
            if (c.identity != identity || c.volume != info.dwVolumeSerialNumber) { c.offset = 0; c.lines.reset(); c.requests.reset(); }
            c.identity = identity; c.volume = info.dwVolumeSerialNumber;
        }
        const long long maxBytes = 256 * 1024;
        if (end.QuadPart < c.offset) { c.offset = 0; c.lines.reset(); c.requests.reset(); }
        if (end.QuadPart - c.offset > maxBytes) { c.offset = end.QuadPart - maxBytes; c.lines.reset(true); c.requests.reset(); }
        LARGE_INTEGER start{}; start.QuadPart = c.offset;
        if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) return false;
        char bytes[16384]; long long remaining = end.QuadPart - c.offset;
        while (remaining > 0) {
            DWORD count = 0; DWORD wanted = static_cast<DWORD>(std::min<long long>(remaining, sizeof(bytes)));
            if (!ReadFile(file, bytes, wanted, &count, nullptr) || !count) break;
            c.offset += count; remaining -= count; bytesRead += count; ++reads;
            c.lines.feed(bytes, count, [&](const std::string& line) {
                Snapshot snapshot; if (ParseEvent(line, snapshot) && accept) accept(snapshot);
                if (request) c.requests.consume(line, request);
            });
        }
        if (files.size() > 64) {
            auto oldest = files.begin(); for (auto it = files.begin(); it != files.end(); ++it) if (it->second.touched < oldest->second.touched) oldest = it;
            files.erase(oldest);
        }
        return true;
    }
    size_t cachedFiles() const { return files.size(); }
};

// Metadata only at startup/recovery. Actual content reads are bounded to the newest 24 files.
inline std::vector<std::wstring> RecentFiles(const std::wstring& root) {
    struct Entry { std::wstring path; unsigned long long modified; };
    std::vector<Entry> latest;
    auto walk = [&](auto&& self, const std::wstring& dir, int depth) -> void {
        if (depth > 5) return;
        WIN32_FIND_DATAW item{}; HANDLE find = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &item, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE) return;
        do {
            if (item.cFileName[0] == L'.') continue;
            std::wstring path = dir + L"\\" + item.cFileName;
            if (item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            if (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) self(self, path, depth + 1);
            else if (path.size() >= 6 && path.compare(path.size() - 6, 6, L".jsonl") == 0) {
                ULARGE_INTEGER t{}; t.HighPart = item.ftLastWriteTime.dwHighDateTime; t.LowPart = item.ftLastWriteTime.dwLowDateTime;
                latest.push_back({path, t.QuadPart});
                std::sort(latest.begin(), latest.end(), [](const Entry& a, const Entry& b) { return a.modified > b.modified; });
                if (latest.size() > 24) latest.pop_back();
            }
        } while (FindNextFileW(find, &item));
        FindClose(find);
    };
    walk(walk, root, 0);
    std::vector<std::wstring> paths; for (const auto& f : latest) paths.push_back(f.path);
    return paths;
}
}

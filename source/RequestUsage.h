#pragma once
#include "QuotaHistory.h"
#include <functional>
#include <set>

namespace quota {
// cachedInput 属于 input，reasoningOutput 属于 output；不可重复加进 total。
struct TokenCounts {
    long long input = 0, cachedInput = 0, cacheWriteInput = 0, output = 0, reasoningOutput = 0, total = 0;
};
struct RequestEntry {
    std::string threadId, turnId, model, status;
    long long started = 0, ended = 0;
    bool partial = false;
    std::optional<TokenCounts> tokens;
    Snapshot quotaStart, quotaEnd;
    std::optional<double> fiveChange, weeklyChange;
    // 本地 rollout 没有实际账单字段；保留空值，绝不按订阅百分比推算金额。
};
struct RequestReadResult { std::vector<RequestEntry> entries; bool truncated = false; std::string error; };
namespace request_detail { inline RequestReadResult Read(const std::filesystem::path& directory, size_t maxRows); }
class RequestTracker {
    long long minimumCompleted_ = 0, lastTime_ = 0;
    std::string thread_;
    std::optional<TokenCounts> previous_;
    Snapshot latestQuota_;
    std::optional<RequestEntry> active_;
    bool seenStart_ = false, quotaDuring_ = false;
    std::set<std::string> finished_;
    std::deque<std::string> finishedOrder_;
    void finish(long long time, const std::string& status, const std::function<void(const RequestEntry&)>& accept);
public:
    explicit RequestTracker(long long minimumCompletedMs = 0) : minimumCompleted_(minimumCompletedMs) {}
    void reset() { thread_.clear(); previous_.reset(); latestQuota_ = {}; active_.reset(); seenStart_ = false; quotaDuring_ = false; lastTime_ = 0; finished_.clear(); finishedOrder_.clear(); }
    void consume(const std::string&, const std::function<void(const RequestEntry&)>&);
};
class RequestStore {
    mutable std::mutex mutex_;
    bool enabled_ = false, loaded_ = false;
    std::filesystem::path directory_;
    std::string error_;
    std::set<std::string> seen_;
    std::deque<std::string> order_;
public:
    void configure(bool, const std::filesystem::path&);
    bool record(const RequestEntry&);
    RequestReadResult read(size_t = 2000) const;
    std::string error() const { std::lock_guard<std::mutex> guard(mutex_); return error_; }
};
// 与额度事件共用固定上限的行切分，不为会话内容建立大字符串。
class MetadataLines {
    std::string pending_;
    bool discard_ = false;
public:
    explicit MetadataLines(bool skipPartial = false) : discard_(skipPartial) {}
    void reset(bool skipPartial = false) { pending_.clear(); discard_ = skipPartial; }
    template<class F> void feed(const char* bytes, size_t length, F accept) {
        for (size_t i = 0; i < length; ++i) {
            if (bytes[i] == '\n') { if (!discard_) accept(pending_); pending_.clear(); discard_ = false; }
            else if (!discard_) { if (pending_.size() >= 32768) { pending_.clear(); discard_ = true; } else pending_ += bytes[i]; }
        }
    }
};

namespace request_detail {
constexpr long long MaxTokens = 9007199254740991LL;
inline bool Label(const std::string& value) {
    if (value.size() > 160) return false;
    for (unsigned char c : value) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ':' || c == '/')) return false;
    return true;
}
inline bool Counts(const Json& value, TokenCounts& counts) {
    if (value.kind != Json::Object) return false;
    auto integer = [&](const char* name, long long& out, bool optional = false) {
        const auto& field = value.get(name);
        if (optional && field.kind == Json::Null) { out = 0; return true; }
        if (field.kind != Json::Number || field.num < 0 || field.num > static_cast<double>(MaxTokens) || field.num != std::floor(field.num)) return false;
        out = static_cast<long long>(field.num); return true;
    };
    return integer("input_tokens", counts.input) && integer("cached_input_tokens", counts.cachedInput) && integer("cache_write_input_tokens", counts.cacheWriteInput, true) &&
        integer("output_tokens", counts.output) && integer("reasoning_output_tokens", counts.reasoningOutput) && integer("total_tokens", counts.total) &&
        counts.cachedInput <= counts.input && counts.reasoningOutput <= counts.output && counts.input <= MaxTokens - counts.output && counts.total == counts.input + counts.output;
}
inline bool Equal(const TokenCounts& a, const TokenCounts& b) {
    return a.input == b.input && a.cachedInput == b.cachedInput && a.cacheWriteInput == b.cacheWriteInput && a.output == b.output && a.reasoningOutput == b.reasoningOutput && a.total == b.total;
}
inline bool AddDelta(const TokenCounts& current, const TokenCounts& previous, std::optional<TokenCounts>& sum) {
    TokenCounts delta{current.input - previous.input, current.cachedInput - previous.cachedInput, current.cacheWriteInput - previous.cacheWriteInput,
        current.output - previous.output, current.reasoningOutput - previous.reasoningOutput, current.total - previous.total};
    if (delta.input < 0 || delta.cachedInput < 0 || delta.cacheWriteInput < 0 || delta.output < 0 || delta.reasoningOutput < 0 || delta.total < 0 || delta.cachedInput > delta.input || delta.reasoningOutput > delta.output) return false;
    TokenCounts value = sum.value_or(TokenCounts{});
    if (value.total > MaxTokens - delta.total || value.cacheWriteInput > MaxTokens - delta.cacheWriteInput) return false;
    value.input += delta.input; value.cachedInput += delta.cachedInput; value.cacheWriteInput += delta.cacheWriteInput;
    value.output += delta.output; value.reasoningOutput += delta.reasoningOutput; value.total += delta.total;
    sum = value; return true;
}
inline const std::string& Header() {
    static const std::string value = "started_ms,ended_ms,thread_id,turn_id,model,status,partial,input_tokens,cached_input_tokens,cache_write_input_tokens,output_tokens,reasoning_output_tokens,total_tokens,account_five_change_pp,account_weekly_change_pp,actual_amount,currency,account_start_observed_ms,account_five_start_remaining_percent,account_five_start_reset_at,account_weekly_start_remaining_percent,account_weekly_start_reset_at,account_end_observed_ms,account_five_end_remaining_percent,account_five_end_reset_at,account_weekly_end_remaining_percent,account_weekly_end_reset_at\n";
    return value;
}
inline std::string Serialize(const RequestEntry& value) {
    std::string out = std::to_string(value.started) + ',' + std::to_string(value.ended) + ',' + value.threadId + ',' + value.turnId + ',' + value.model + ',' + value.status + ',' + (value.partial ? "1" : "0") + ',';
    if (value.tokens) { const auto& t = *value.tokens; out += std::to_string(t.input) + ',' + std::to_string(t.cachedInput) + ',' + std::to_string(t.cacheWriteInput) + ',' + std::to_string(t.output) + ',' + std::to_string(t.reasoningOutput) + ',' + std::to_string(t.total); }
    else out += ",,,,,";
    auto snapshot = [](const Snapshot& s) {
        auto window = [](const Window& w) { return w.present ? history_detail::Number(100 - w.used) + ',' + std::to_string(w.resets) : std::string(","); };
        return std::to_string(s.observed) + ',' + window(s.fiveHour) + ',' + window(s.weekly);
    };
    return out + ',' + history_detail::Number(value.fiveChange) + ',' + history_detail::Number(value.weeklyChange) + ",,," + snapshot(value.quotaStart) + ',' + snapshot(value.quotaEnd) + '\n';
}
inline bool Parse(std::string line, RequestEntry& value) {
    if (line.size() > 2048) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::vector<std::string> fields; size_t at = 0;
    for (;;) { auto comma = line.find(',', at); fields.push_back(line.substr(at, comma == std::string::npos ? comma : comma - at)); if (comma == std::string::npos) break; at = comma + 1; }
    if (fields.size() != 27 || !Label(fields[2]) || fields[3].empty() || !Label(fields[3]) || !Label(fields[4]) ||
        (fields[5] != "completed" && fields[5] != "interrupted" && fields[5] != "incomplete") || (fields[6] != "0" && fields[6] != "1") || !fields[15].empty() || !fields[16].empty()) return false;
    try {
        value.started = history_detail::Integer(fields[0]); value.ended = history_detail::Integer(fields[1]);
        if (!value.ended || value.started > value.ended) return false;
        value.threadId = fields[2]; value.turnId = fields[3]; value.model = fields[4]; value.status = fields[5]; value.partial = fields[6] == "1";
        if (!fields[7].empty()) {
            TokenCounts t{history_detail::Integer(fields[7]), history_detail::Integer(fields[8]), history_detail::Integer(fields[9]), history_detail::Integer(fields[10]), history_detail::Integer(fields[11]), history_detail::Integer(fields[12])};
            if (t.total > MaxTokens || t.cacheWriteInput > MaxTokens || t.input > MaxTokens - t.output || t.total != t.input + t.output || t.cachedInput > t.input || t.reasoningOutput > t.output) return false;
            value.tokens = t;
        } else { for (size_t i = 8; i <= 12; ++i) if (!fields[i].empty()) return false; }
        value.fiveChange = history_detail::Decimal(fields[13], -100, 100); value.weeklyChange = history_detail::Decimal(fields[14], -100, 100);
        auto snapshot = [&](size_t index, Snapshot& s) {
            s.observed = history_detail::Integer(fields[index]);
            auto window = [&](size_t column, Window& w) {
                auto remaining = history_detail::Decimal(fields[column], 0, 100);
                if (remaining) w = {true, 100 - *remaining, history_detail::Integer(fields[column + 1])};
                else if (!fields[column + 1].empty()) return false;
                return history_detail::WindowValid(w);
            };
            return window(index + 1, s.fiveHour) && window(index + 3, s.weekly) && ((!s.fiveHour.present && !s.weekly.present) || s.observed > 0);
        };
        if (!snapshot(17, value.quotaStart) || !snapshot(22, value.quotaEnd)) return false;
        return true;
    } catch (...) { return false; }
}
}
inline void RequestTracker::finish(long long time, const std::string& status, const std::function<void(const RequestEntry&)>& accept) {
    if (!active_) return;
    auto value = *active_; active_.reset();
    if (!finished_.insert(value.turnId).second) return;
    finishedOrder_.push_back(value.turnId);
    if (finishedOrder_.size() > 128) { finished_.erase(finishedOrder_.front()); finishedOrder_.pop_front(); }
    if (time < minimumCompleted_ || time < value.started) return;
    value.ended = time; value.status = status; value.partial = value.partial || !seenStart_ || !value.tokens || status == "incomplete";
    if (quotaDuring_) value.quotaEnd = latestQuota_;
    // 这些只是请求期间看到的账户变化，可能包含并发请求；不归因为该请求的消耗。
    value.fiveChange = history_detail::Difference(value.quotaEnd.fiveHour, value.quotaStart.fiveHour);
    value.weeklyChange = history_detail::Difference(value.quotaEnd.weekly, value.quotaStart.weekly);
    if (accept) accept(value);
}
inline void RequestTracker::consume(const std::string& line, const std::function<void(const RequestEntry&)>& accept) {
    if (line.size() > 32768 || (line.find("token_count") == std::string::npos && line.find("task_started") == std::string::npos && line.find("task_complete") == std::string::npos && line.find("turn_aborted") == std::string::npos && line.find("turn_context") == std::string::npos && line.find("session_meta") == std::string::npos)) return;
    try {
        const auto root = JsonParser(line).parse(); const auto& payload = root.get("payload");
        auto time = Timestamp(root.get("timestamp").str); if (time <= 0 || time < lastTime_) return;
        const auto& rootType = root.get("type").str;
        if (rootType == "session_meta") { auto id = payload.get("id").str; if (request_detail::Label(id)) thread_ = id; return; }
        const auto& kind = payload.get("type").str;
        if (rootType != "turn_context" && rootType != "event_msg") return;
        if (rootType == "turn_context" || kind == "task_started") {
            auto id = payload.get("turn_id").str; if (id.empty() || !request_detail::Label(id)) return;
            if (active_ && active_->turnId != id) finish(time, "incomplete", accept);
            if (!active_) { RequestEntry entry; entry.threadId = thread_; entry.turnId = id; entry.started = time; entry.quotaStart = latestQuota_; active_ = entry; seenStart_ = false; quotaDuring_ = false; }
            if (kind == "task_started") seenStart_ = true;
            auto model = payload.get("model").str; if (!model.empty() && request_detail::Label(model)) active_->model = model;
        } else if (kind == "token_count") {
            Snapshot snapshot; snapshot.observed = time;
            if (ReadLimits(payload, snapshot)) { latestQuota_ = snapshot; if (active_) quotaDuring_ = true; }
            const auto& info = payload.get("info"); TokenCounts total;
            if (!request_detail::Counts(info.get("total_token_usage"), total)) { if (active_ && info.kind != Json::Null) active_->partial = true; return; }
            if (active_) {
                if (previous_) { if (!request_detail::AddDelta(total, *previous_, active_->tokens)) active_->partial = true; }
                else {
                    TokenCounts last;
                    // 新会话第一份累计值等于 last 才能证明不存在遗漏的旧响应。
                    if (seenStart_ && request_detail::Counts(info.get("last_token_usage"), last) && request_detail::Equal(total, last)) active_->tokens = total;
                    else active_->partial = true;
                }
            }
            previous_ = total;
        } else if (kind == "task_complete" || kind == "turn_aborted") {
            auto id = payload.get("turn_id").str;
            // 尾部上限可能跳过开始和上下文；保留已知完成事件，不能把整条请求静默丢掉。
            if (!active_ && !id.empty() && request_detail::Label(id) && !finished_.count(id)) {
                RequestEntry missing; missing.threadId = thread_; missing.turnId = id; missing.partial = true;
                active_ = missing; seenStart_ = false; quotaDuring_ = false;
            }
            if (active_ && (id.empty() || active_->turnId == id)) finish(time, kind == "task_complete" ? "completed" : "interrupted", accept);
        }
        lastTime_ = time;
    } catch (...) { /* 损坏和过大的会话行不会终止其他请求的监听。 */ }
}
inline void RequestStore::configure(bool enabled, const std::filesystem::path& directory) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (directory != directory_ || enabled != enabled_) { seen_.clear(); order_.clear(); error_.clear(); loaded_ = false; }
    enabled_ = enabled; directory_ = directory;
}
inline bool RequestStore::record(const RequestEntry& value) {
    std::lock_guard<std::mutex> guard(mutex_);
    RequestEntry validated; const auto serialized = request_detail::Serialize(value);
    if (!enabled_ || !request_detail::Parse(serialized.substr(0, serialized.size() - 1), validated)) return false;
    // 首次写入前只读一次有上限的最近记录，重启后的文件重放不会重复追加。
    if (!loaded_) {
        const auto existing = request_detail::Read(directory_, 2000);
        if (!existing.error.empty()) { error_ = "Cannot write request usage; check directory and permissions"; return false; }
        for (const auto& entry : existing.entries) { auto prior = entry.threadId + '\n' + entry.turnId; if (seen_.insert(prior).second) order_.push_back(prior); }
        loaded_ = true;
    }
    auto key = value.threadId + '\n' + value.turnId; if (seen_.count(key)) return false;
    try {
        if (directory_.empty() || !directory_.is_absolute()) throw std::runtime_error("Invalid request directory");
        std::filesystem::create_directories(directory_);
        if (!history_detail::Append(directory_ / ("request-usage-" + history_detail::Day(history_detail::Now()) + ".csv"), serialized, request_detail::Header())) throw std::runtime_error("Request append failed");
        seen_.insert(key); order_.push_back(key); if (order_.size() > 4096) { seen_.erase(order_.front()); order_.pop_front(); }
        error_.clear(); return true;
    } catch (...) { error_ = "Cannot write request usage; check directory and permissions"; return false; }
}
inline RequestReadResult request_detail::Read(const std::filesystem::path& directory, size_t maxRows) {
    RequestReadResult result;
    maxRows = std::min<size_t>(maxRows, 2000); if (!maxRows || directory.empty()) return result;
    size_t budget = 2 * 1024 * 1024; const auto now = history_detail::Now();
    try {
        for (int day = 0; day < 30 && budget; ++day) {
            auto path = directory / ("request-usage-" + history_detail::Day(now - day * 86400000LL) + ".csv"); std::error_code ec;
            if (!std::filesystem::exists(path, ec)) { if (ec) throw std::runtime_error("Request access failed"); continue; }
            if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("Request not file");
            std::ifstream in(path, std::ios::binary | std::ios::ate); if (!in) throw std::runtime_error("Request open failed");
            auto end = in.tellg(); if (end < 0) throw std::runtime_error("Request size failed");
            auto count = std::min<size_t>(static_cast<size_t>(end), budget); auto offset = static_cast<std::streamoff>(end) - static_cast<std::streamoff>(count);
            std::string bytes(count, '\0'); in.seekg(offset); in.read(bytes.data(), static_cast<std::streamsize>(count)); bytes.resize(static_cast<size_t>(in.gcount())); budget -= count;
            size_t at = 0; if (offset > 0) { result.truncated = true; auto nl = bytes.find('\n'); at = nl == std::string::npos ? bytes.size() : nl + 1; }
            std::deque<RequestEntry> rows;
            while (at < bytes.size()) { auto nl = bytes.find('\n', at); if (nl == std::string::npos) break; RequestEntry entry;
                if (request_detail::Parse(bytes.substr(at, nl - at), entry)) { if (rows.size() == maxRows) { rows.pop_front(); result.truncated = true; } rows.push_back(std::move(entry)); } at = nl + 1;
            }
            for (auto row = rows.rbegin(); row != rows.rend(); ++row) { if (result.entries.size() == maxRows) { result.truncated = true; break; } result.entries.push_back(std::move(*row)); }
            if (result.entries.size() == maxRows) { result.truncated = true; break; }
        }
        if (!budget) result.truncated = true;
    } catch (...) { result.error = "Cannot read request usage; check directory and permissions"; }
    std::stable_sort(result.entries.begin(), result.entries.end(), [](const RequestEntry& a, const RequestEntry& b) { return a.ended < b.ended; }); return result;
}
inline RequestReadResult RequestStore::read(size_t maxRows) const {
    std::lock_guard<std::mutex> guard(mutex_); return request_detail::Read(directory_, maxRows);
}
}

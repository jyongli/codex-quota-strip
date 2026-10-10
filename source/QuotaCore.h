#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace quota {
struct Window { bool present = false; double used = 0; long long resets = 0; };
struct Snapshot { Window fiveHour, weekly; long long observed = 0; };

// Only small, genuine quota events enter this parser. Conversation text is never parsed.
struct Json {
    enum Kind { Null, Object, Array, String, Number, Boolean } kind = Null;
    std::map<std::string, Json> members;
    std::vector<Json> elements;
    std::string str;
    double num = 0;
    const Json& get(const char* key) const {
        static const Json missing;
        auto it = members.find(key);
        return it == members.end() ? missing : it->second;
    }
    const Json& either(const char* a, const char* b) const {
        auto it = members.find(a);
        return it == members.end() ? get(b) : it->second;
    }
};
class JsonParser {
    const std::string& text;
    size_t pos = 0;
    [[noreturn]] void fail() { throw std::runtime_error("Invalid JSON"); }
    void spaces() { while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\r' || text[pos] == '\n' || text[pos] == '\t')) ++pos; }
    char take() { if (pos == text.size()) fail(); return text[pos++]; }
    bool eat(char c) { spaces(); if (pos < text.size() && text[pos] == c) { ++pos; return true; } return false; }
    std::string string() {
        if (take() != '"') fail();
        std::string out;
        while (true) {
            char c = take();
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 32) fail();
            if (c != '\\') { out += c; continue; }
            c = take();
            switch (c) {
            case '"': case '\\': case '/': out += c; break;
            case 'b': out += '\b'; break; case 'f': out += '\f'; break;
            case 'n': out += '\n'; break; case 'r': out += '\r'; break; case 't': out += '\t'; break;
            case 'u': {
                unsigned code = 0;
                for (int i = 0; i < 4; ++i) {
                    char h = take(); int v = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
                    if (v < 0) fail(); code = (code << 4) | v;
                }
                // Quota keys are ASCII. Non-ASCII labels are not used by the display.
                out += code < 128 ? static_cast<char>(code) : '?';
                break;
            }
            default: fail();
            }
        }
    }
    Json value(int depth) {
        if (depth > 24) fail(); spaces(); Json j;
        if (eat('{')) {
            j.kind = Json::Object; if (eat('}')) return j;
            do { spaces(); auto key = string(); if (!eat(':')) fail(); if (!j.members.emplace(key, value(depth + 1)).second) fail(); } while (eat(','));
            if (!eat('}')) fail(); return j;
        }
        if (eat('[')) {
            j.kind = Json::Array; if (eat(']')) return j;
            do { j.elements.push_back(value(depth + 1)); } while (eat(','));
            if (!eat(']')) fail(); return j;
        }
        if (pos < text.size() && text[pos] == '"') { j.kind = Json::String; j.str = string(); return j; }
        if (text.compare(pos, 4, "null") == 0) { pos += 4; return j; }
        if (text.compare(pos, 4, "true") == 0) { pos += 4; j.kind = Json::Boolean; j.num = 1; return j; }
        if (text.compare(pos, 5, "false") == 0) { pos += 5; j.kind = Json::Boolean; return j; }
        size_t start = pos;
        if (pos < text.size() && text[pos] == '-') ++pos;
        if (pos == text.size()) fail();
        if (text[pos] == '0') ++pos;
        else { if (text[pos] < '1' || text[pos] > '9') fail(); while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') ++pos; }
        if (pos < text.size() && text[pos] == '.') {
            ++pos; size_t digit = pos; while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') ++pos; if (digit == pos) fail();
        }
        if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
            ++pos; if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) ++pos;
            size_t digit = pos; while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') ++pos; if (digit == pos) fail();
        }
        j.kind = Json::Number; j.num = std::strtod(text.substr(start, pos - start).c_str(), nullptr);
        if (!std::isfinite(j.num)) fail(); return j;
    }
public:
    explicit JsonParser(const std::string& s) : text(s) {}
    Json parse() { if (text.size() > 32768) fail(); auto j = value(0); spaces(); if (pos != text.size()) fail(); return j; }
};

inline long long Timestamp(const std::string& iso) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6 || iso.empty() || iso.back() != 'Z') return 0;
    if (y < 2000 || y > 2200 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 60) return 0;
    std::tm t{}; t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_sec = sec;
    // 两个平台分支都按 UTC 换算，避免使用受本机时区影响的 mktime。
#ifdef _WIN32
    long long ms = _mkgmtime64(&t) * 1000;
#else
    long long ms = static_cast<long long>(timegm(&t)) * 1000;
#endif
    auto dot = iso.find('.', 19);
    if (dot != std::string::npos) { int place = 100; for (size_t i = dot + 1; i < iso.size() && place; ++i, place /= 10) { if (iso[i] < '0' || iso[i] > '9') break; ms += (iso[i] - '0') * place; } }
    return ms;
}
inline bool ReadLimits(const Json& payload, Snapshot& out) {
    const auto& buckets = payload.either("rate_limits_by_limit_id", "rateLimitsByLimitId");
    const Json& limits = buckets.kind == Json::Object ? buckets.get("codex") : payload.either("rate_limits", "rateLimits");
    if (limits.kind != Json::Object) return false;
    const auto& id = limits.either("limit_id", "limitId");
    if (id.kind == Json::String && id.str != "codex") return false;
    auto read = [&out](const Json& j) {
        if (j.kind != Json::Object) return;
        const auto& used = j.either("used_percent", "usedPercent");
        const auto& mins = j.either("window_minutes", "windowDurationMins");
        if (used.kind != Json::Number || mins.kind != Json::Number) return;
        Window* w = mins.num == 300 ? &out.fiveHour : mins.num == 10080 ? &out.weekly : nullptr;
        if (!w) return;
        w->present = true; w->used = std::clamp(used.num, 0.0, 100.0);
        const auto& reset = j.either("resets_at", "resetsAt");
        if (reset.kind == Json::Number && reset.num >= 0 && reset.num < 100000000000.0) w->resets = static_cast<long long>(reset.num);
    };
    read(limits.get("primary")); read(limits.get("secondary"));
    return true;
}
inline bool ParseEvent(const std::string& line, Snapshot& out) {
    if (line.size() > 32768 || line.find("rate_limits") == std::string::npos) return false;
    try {
        auto j = JsonParser(line).parse();
        if (j.get("type").str != "event_msg" || j.get("payload").get("type").str != "token_count") return false;
        Snapshot next; next.observed = Timestamp(j.get("timestamp").str);
        if (next.observed <= 0 || !ReadLimits(j.get("payload"), next)) return false;
        out = next; return true;
    } catch (...) { return false; }
}
inline bool Apply(const Snapshot& next, Snapshot& current) {
    if (next.observed < current.observed) return false;
    current = next; return true;
}
inline std::wstring Remaining(const Window& w) {
    if (!w.present) return L"-";
    wchar_t text[40]; double remaining = std::clamp(100.0 - w.used, 0.0, 100.0);
    if (std::abs(remaining - std::round(remaining)) < 0.001) std::swprintf(text, 40, L"%.0f%%", remaining);
    else std::swprintf(text, 40, L"%.1f%%", remaining);
    return text;
}
// Bounded line framing survives partial writes and skips enormous conversation records.
class EventLines {
    std::string pending;
    bool discard = false;
public:
    explicit EventLines(bool skipPartial = false) : discard(skipPartial) {}
    void reset(bool skipPartial = false) { pending.clear(); discard = skipPartial; }
    template<class F> void feed(const char* bytes, size_t length, F accept) {
        for (size_t i = 0; i < length; ++i) {
            char c = bytes[i];
            if (c == '\n') {
                if (!discard) { Snapshot s; if (ParseEvent(pending, s)) accept(s); }
                pending.clear(); discard = false;
            } else if (!discard) {
                if (pending.size() >= 32768) { pending.clear(); discard = true; }
                else pending += c;
            }
        }
    }
    size_t buffered() const { return pending.size(); }
};
}

#include "QuotaFiles.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        quota::Snapshot s;
        check(quota::ParseEvent(R"({"timestamp":"2026-09-30T13:00:00.000Z","type":"event_msg","payload":{"type":"token_count","rate_limits":{"limit_id":"codex","primary":{"used_percent":88,"window_minutes":10080,"resets_at":1791046711},"secondary":null}}})", s), "A real quota event must be accepted");
        check(s.weekly.present && s.weekly.used == 88, "Weekly quota must be identified by duration even when primary");
        check(!s.fiveHour.present && quota::Remaining(s.fiveHour) == L"-", "Missing five-hour quota must show a hyphen");
        check(quota::Remaining(s.weekly) == L"12%", "88 percent used must display 12 percent remaining");
        std::cout << "PASS: weekly-only event and missing five-hour display\n";
        quota::Snapshot other;
        check(quota::ParseEvent(R"({"timestamp":"2026-09-30T13:01:00.000Z","type":"event_msg","payload":{"type":"token_count","rate_limits":{"primary":{"used_percent":20,"window_minutes":300},"secondary":{"used_percent":90,"window_minutes":10080}}}})", other), "Two-window event must parse");
        check(quota::Remaining(other.fiveHour) == L"80%" && quota::Remaining(other.weekly) == L"10%", "Both windows must display remaining quotas");
        check(!quota::Apply(s, other), "An older concurrent session must not roll the display back");
        quota::Snapshot none;
        check(quota::ParseEvent(R"({"timestamp":"2026-09-30T13:02:00.000Z","type":"event_msg","payload":{"type":"token_count","rate_limits":{"primary":null,"secondary":null}}})", none), "Null windows are a valid unavailable snapshot");
        check(quota::Apply(none, other) && !other.fiveHour.present && !other.weekly.present, "Missing windows must clear previous values");
        check(!quota::ParseEvent(R"({"type":"response_item","payload":{"rate_limits":{"primary":{"used_percent":0,"window_minutes":300}}}})", none), "Conversation text must not become a quota event");
        check(!quota::ParseEvent(R"({"timestamp":"2026-09-30T13:03:00.000Z","type":"event_msg","payload":{"type":"token_count","rate_limits":{"limit_id":"other","primary":{"used_percent":0,"window_minutes":300}}}})", none), "Different metered buckets must be ignored");
        auto raw = quota::JsonParser(R"({"rateLimits":{"primary":{"usedPercent":1,"windowDurationMins":300}},"rateLimitsByLimitId":{"codex":{"primary":{"usedPercent":91,"windowDurationMins":10080}}}})").parse();
        quota::Snapshot multi;
        check(quota::ReadLimits(raw, multi) && !multi.fiveHour.present && quota::Remaining(multi.weekly) == L"9%", "Camel-case response must prefer the codex multi-bucket view");
        quota::Window bad; bad.present = true; bad.used = 110; check(quota::Remaining(bad) == L"0%", "Remaining percentage must be bounded");
        bad.used = -5; check(quota::Remaining(bad) == L"100%", "Remaining percentage must be bounded above");
        std::cout << "PASS: window mapping, null values, ordering, bucket selection, bounds\n";

        std::string event = R"({"timestamp":"2026-09-30T13:05:00.321Z","type":"event_msg","payload":{"type":"token_count","rate_limits":{"primary":{"used_percent":88,"window_minutes":10080},"secondary":null}}})";
        quota::EventLines frames; int accepted = 0;
        frames.feed(event.data(), event.size() / 2, [&](const quota::Snapshot&) { ++accepted; });
        check(accepted == 0, "A partial event must not update the display");
        std::string rest = event.substr(event.size() / 2) + "\n";
        frames.feed(rest.data(), rest.size(), [&](const quota::Snapshot& q) { ++accepted; check(q.observed % 1000 == 321, "Event milliseconds must be retained"); });
        check(accepted == 1, "The completed split event must update exactly once");
        std::string giant(1000000, 'x'); giant += "\n" + event + "\n";
        frames.feed(giant.data(), giant.size(), [&](const quota::Snapshot&) { ++accepted; });
        check(accepted == 2 && frames.buffered() == 0, "Large unrelated records must be skipped without losing the next event");
        std::cout << "PASS: split writes, millisecond timestamps, bounded large-record recovery\n";

        wchar_t temp[MAX_PATH]; GetTempPathW(MAX_PATH, temp);
        auto path = std::filesystem::path(temp) / (L"quota-strip-test-" + std::to_wstring(GetCurrentProcessId()) + L".jsonl");
        { std::ofstream out(path, std::ios::binary); out << event << '\n'; }
        quota::FileFollower follower; int updates = 0;
        check(follower.read(path.wstring(), [&](const quota::Snapshot&) { ++updates; }), "The real file reader must open a shared log");
        auto initialBytes = follower.bytesRead;
        follower.read(path.wstring(), [&](const quota::Snapshot&) { ++updates; });
        check(updates == 1 && follower.bytesRead == initialBytes, "An unchanged file must read zero additional bytes");
        { std::ofstream out(path, std::ios::binary | std::ios::app); out << event.substr(0, 40); }
        follower.read(path.wstring(), [&](const quota::Snapshot&) { ++updates; });
        check(updates == 1, "An incomplete appended event must not update");
        { std::ofstream out(path, std::ios::binary | std::ios::app); out << event.substr(40) << '\n'; }
        follower.read(path.wstring(), [&](const quota::Snapshot&) { ++updates; });
        check(updates == 2 && follower.bytesRead == initialBytes * 2, "Appended bytes must be read incrementally");
        { std::ofstream out(path, std::ios::binary); out << event << '\n'; }
        follower.read(path.wstring(), [&](const quota::Snapshot&) { ++updates; });
        check(updates == 3, "Truncating a log must reset its read cursor");
        auto replacement = path; replacement += L".replacement";
        { std::ofstream out(replacement, std::ios::binary); out << event << '\n'; }
        std::filesystem::remove(path); std::filesystem::rename(replacement, path);
        follower.read(path.wstring(), [&](const quota::Snapshot&) { ++updates; });
        check(updates == 4, "Replacing a same-size log must reset its file identity and cursor");
        std::filesystem::remove(path);
        std::cout << "PASS: real-file incremental reads, partial appends, truncation, replacement\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << "\n"; return 1; }
}

#include "RequestUsage.h"
#include <iostream>
static void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
static std::string event(const std::string& payload, const char* type = "event_msg") {
    return std::string("{\"timestamp\":\"2026-10-18T01:00:00Z\",\"type\":\"") + type + "\",\"payload\":" + payload + "}";
}
static std::string usage(int input, int output, int cached = 0) {
    return event("{\"type\":\"token_count\",\"info\":{\"total_token_usage\":{\"input_tokens\":" + std::to_string(input) + ",\"output_tokens\":" + std::to_string(output) + ",\"cached_input_tokens\":" + std::to_string(cached) + ",\"reasoning_output_tokens\":0,\"total_tokens\":" + std::to_string(input + output) + "}}}");
}
static std::string firstUsage() {
    return event("{\"type\":\"token_count\",\"info\":{\"total_token_usage\":{\"input_tokens\":100,\"output_tokens\":30,\"cached_input_tokens\":20,\"reasoning_output_tokens\":5,\"total_tokens\":130},\"last_token_usage\":{\"input_tokens\":100,\"output_tokens\":30,\"cached_input_tokens\":20,\"reasoning_output_tokens\":5,\"total_tokens\":130}}}");
}
int main() { try {
    quota::RequestTracker tracker; std::vector<quota::RequestEntry> rows;
    auto accept = [&](const auto& row) { rows.push_back(row); };
    tracker.consume(event("{\"id\":\"thread-1\"}", "session_meta"), accept);
    tracker.consume(usage(100, 10, 20), accept);
    tracker.consume(event("{\"type\":\"task_started\",\"turn_id\":\"turn-1\"}"), accept);
    tracker.consume(event("{\"turn_id\":\"turn-1\",\"model\":\"gpt-test\"}", "turn_context"), accept);
    tracker.consume(usage(160, 30, 50), accept);
    tracker.consume(usage(160, 30, 50), accept); // 配额事件可能重复携带同一份计数。
    tracker.consume(usage(220, 60, 60), accept);
    tracker.consume(event("{\"type\":\"task_complete\",\"turn_id\":\"turn-1\",\"last_agent_message\":\"PRIVATE RESPONSE\"}"), accept);
    require(rows.size() == 1, "a completed work request must produce one usage row");
    require(rows[0].tokens && rows[0].tokens->input == 120 && rows[0].tokens->output == 50 && rows[0].tokens->total == 170 && rows[0].tokens->cachedInput == 40 && !rows[0].partial, "sum cumulative deltas across responses once, without double-counting cache or duplicate samples");
    require(rows[0].threadId == "thread-1" && rows[0].model == "gpt-test" && rows[0].status == "completed", "request identity and model must survive metadata parsing");
    quota::RequestTracker staleQuota; std::vector<quota::RequestEntry> noFresh;
    auto quotaAccept = [&](const auto& row) { noFresh.push_back(row); };
    staleQuota.consume(event("{\"type\":\"token_count\",\"rate_limits\":{\"primary\":{\"used_percent\":20,\"window_minutes\":10080,\"resets_at\":2000000000}}}"), quotaAccept);
    staleQuota.consume(event("{\"type\":\"task_started\",\"turn_id\":\"stale-quota\"}"), quotaAccept);
    staleQuota.consume(event("{\"type\":\"task_complete\",\"turn_id\":\"stale-quota\"}"), quotaAccept);
    require(noFresh.size() == 1 && !noFresh[0].weeklyChange && !noFresh[0].tokens && noFresh[0].partial,
        "without new quota or tokens, completed request must show unknown values rather than fabricated zero consumption");
    quota::RequestTracker first; std::vector<quota::RequestEntry> extra;
    auto collect = [&](const auto& row) { extra.push_back(row); };
    first.consume(event("{\"type\":\"task_started\",\"turn_id\":\"first\"}"), collect);
    first.consume(firstUsage(), collect);
    first.consume(event("{\"type\":\"turn_aborted\",\"turn_id\":\"first\"}"), collect);
    require(extra.size() == 1 && extra.back().tokens->total == 130 && extra.back().tokens->reasoningOutput == 5 && !extra.back().partial && extra.back().status == "interrupted", "new-session first response and interrupted requests must preserve known tokens");
    quota::RequestTracker partial;
    partial.consume(event("{\"turn_id\":\"tail\",\"model\":\"gpt-test\"}", "turn_context"), collect);
    partial.consume(usage(1000, 200), collect); partial.consume(usage(1100, 230, 20), collect);
    partial.consume(event("{\"type\":\"task_complete\",\"turn_id\":\"tail\"}"), collect);
    require(extra.back().partial && extra.back().tokens->total == 130, "tail-only reconstruction must count only observed cumulative deltas and mark partial");
    quota::RequestTracker missingStart;
    missingStart.consume(usage(2000, 300), collect);
    missingStart.consume(event("{\"type\":\"task_complete\",\"turn_id\":\"tail-completion-only\"}"), collect);
    require(extra.back().turnId == "tail-completion-only" && extra.back().started == 0 && extra.back().partial && !extra.back().tokens, "a bounded tail missing both start and context must retain the completed request as partial with unknown tokens");
    quota::RequestTracker cutoff(quota::Timestamp("2026-10-18T02:00:00Z"));
    const auto before = extra.size();
    cutoff.consume(event("{\"type\":\"task_started\",\"turn_id\":\"old\"}"), collect); cutoff.consume(firstUsage(), collect);
    cutoff.consume(event("{\"type\":\"task_complete\",\"turn_id\":\"old\"}"), collect);
    require(extra.size() == before, "listener startup must not import completed historical requests");
    quota::RequestTracker reset;
    reset.consume(usage(500, 100, 100), collect); reset.consume(event("{\"type\":\"task_started\",\"turn_id\":\"reset\"}"), collect);
    reset.consume(usage(10, 2), collect); reset.consume(usage(30, 7), collect);
    reset.consume(event("{\"type\":\"task_complete\",\"turn_id\":\"reset\"}"), collect);
    require(extra.back().partial && extra.back().tokens->total == 25, "counter resets must not subtract unrelated lifetime totals or invent a complete total");
    auto prior = extra.size();
    reset.consume(event("{\"type\":\"task_complete\",\"turn_id\":\"reset\"}"), collect);
    reset.consume(event("{\"type\":\"message\",\"text\":\"token_count task_started PRIVATE USER PROMPT\"}"), collect);
    require(extra.size() == prior, "duplicate completions and conversation text must not create usage rows");
    quota::MetadataLines framing; std::vector<std::string> lines;
    std::string large(40000, 'x'); large += '\n'; auto metadataRow = event("{\"type\":\"task_started\",\"turn_id\":\"framed\"}") + '\n'; large += metadataRow;
    framing.feed(large.data(), 17000, [&](const auto& line) { lines.push_back(line); });
    framing.feed(large.data() + 17000, large.size() - 17000, [&](const auto& line) { lines.push_back(line); });
    require(lines.size() == 1 && lines.front() + '\n' == metadataRow, "bounded metadata framing must recover after oversized conversation records and split writes");
    auto root = std::filesystem::temp_directory_path() / ("request-usage-test-" + std::to_string(quota::history_detail::Now()));
    quota::RequestStore store; store.configure(true, root);
    rows[0].quotaStart.observed = rows[0].started - 100; rows[0].quotaStart.weekly = {true, 10, 2000000000};
    rows[0].quotaEnd.observed = rows[0].ended; rows[0].quotaEnd.weekly = {true, 12, 2000000000}; rows[0].weeklyChange = -2;
    require(store.record(rows[0]), "request row must append to selected directory");
    auto saved = store.read(); require(saved.entries.size() == 1 && saved.entries[0].tokens->total == 170, "numeric request data must round-trip through CSV");
    require(saved.entries[0].quotaStart.weekly.present && saved.entries[0].quotaStart.weekly.used == 10 && saved.entries[0].quotaEnd.weekly.used == 12 && saved.entries[0].quotaStart.observed == rows[0].started - 100, "before/after account snapshots and their observation times must survive reopening the request log");
    quota::RequestStore restarted; restarted.configure(true, root);
    require(!restarted.record(rows[0]) && restarted.read().entries.size() == 1, "replaying a completed request after restart must not duplicate it");
    auto earlier = rows[0]; earlier.turnId = "earlier"; earlier.started -= 1000; earlier.ended -= 1000;
    require(store.record(earlier) && store.read().entries.front().turnId == "earlier", "concurrent completion notifications may arrive out of order; viewer must sort by completion time");
    auto file = std::filesystem::directory_iterator(root)->path();
    { std::ifstream in(file, std::ios::binary); std::string csv((std::istreambuf_iterator<char>(in)), {}); require(csv.find("PRIVATE") == std::string::npos && csv.find("actual_amount,currency") != std::string::npos && csv.find("-2,,,") != std::string::npos, "CSV must omit prompts/responses and leave unsupported actual money empty"); }
    auto missing = noFresh.front(); missing.turnId = "unknown"; require(store.record(missing), "unknown-token row must write");
    auto unknownRows = store.read(); require(std::any_of(unknownRows.entries.begin(), unknownRows.entries.end(), [](const auto& row) { return row.turnId == "unknown" && !row.tokens; }), "unknown tokens must round-trip as missing, never zero");
    auto bad = root / "file"; { std::ofstream out(bad); out << "preserve"; }
    store.configure(true, bad); auto newer = rows[0]; newer.turnId = "io-error";
    require(!store.record(newer) && !store.error().empty(), "invalid directory must expose a nonfatal write error");
    auto unicode = root / std::filesystem::u8path(u8"请求日志"); store.configure(true, unicode);
    require(store.record(rows[0]) && store.read().entries.size() == 1, "request logging must honor a Unicode selected path");
    store.configure(false, unicode); newer.turnId = "disabled"; require(!store.record(newer) && store.read().entries.size() == 1, "logging switch must disable request writes too");
    store.configure(true, unicode);
    for (int i = 0; i < 2005; ++i) { newer.turnId = "row-" + std::to_string(i); newer.ended += 1; require(store.record(newer), "bounded history fixture append"); }
    auto bounded = store.read(); require(bounded.entries.size() == 2000 && bounded.truncated && store.read(3).entries.size() == 3 && store.read(0).entries.empty(), "request viewer must bound rows and report truncation");
    std::filesystem::remove_all(root);
    std::cout << "PASS request aggregation\n"; return 0;
} catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; } }

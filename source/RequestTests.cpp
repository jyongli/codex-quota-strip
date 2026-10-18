#include "RequestUsage.h"
#include <iostream>
static void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
static std::string event(const std::string& payload, const char* type = "event_msg") {
    return std::string("{\"timestamp\":\"2026-10-18T01:00:00Z\",\"type\":\"") + type + "\",\"payload\":" + payload + "}";
}
static std::string usage(int input, int output, int cached = 0) {
    return event("{\"type\":\"token_count\",\"info\":{\"total_token_usage\":{\"input_tokens\":" + std::to_string(input) + ",\"output_tokens\":" + std::to_string(output) + ",\"cached_input_tokens\":" + std::to_string(cached) + ",\"reasoning_output_tokens\":0,\"total_tokens\":" + std::to_string(input + output) + "}}}");
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
    std::cout << "PASS request aggregation\n"; return 0;
} catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; } }

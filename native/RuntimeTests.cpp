#include "NativeCore.h"
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
// 使用真实 JSONL 追加路径验证监听，时间取当前值以穿过启动截止点，不读取真实账号。
std::string iso(long long milliseconds) {
    std::time_t seconds = milliseconds / 1000; std::tm utc{}; gmtime_r(&seconds, &utc);
    char value[64]; std::strftime(value, sizeof(value), "%Y-%m-%dT%H:%M:%S", &utc);
    char fraction[16]; std::snprintf(fraction, sizeof(fraction), ".%03lldZ", milliseconds % 1000); return std::string(value) + fraction;
}
std::string requestFixture(const std::string& turn, long long now) {
    auto line = [&](const char* type, const std::string& payload) { return "{\"timestamp\":\"" + iso(now) + "\",\"type\":\"" + type + "\",\"payload\":" + payload + "}\n"; };
    return line("session_meta", "{\"id\":\"runtime-thread\"}") + line("turn_context", "{\"turn_id\":\"" + turn + "\",\"model\":\"synthetic-model\"}") +
        line("event_msg", "{\"type\":\"task_started\",\"turn_id\":\"" + turn + "\"}") +
        line("event_msg", "{\"type\":\"token_count\",\"info\":{\"total_token_usage\":{\"input_tokens\":100,\"cached_input_tokens\":20,\"output_tokens\":30,\"reasoning_output_tokens\":5,\"total_tokens\":130},\"last_token_usage\":{\"input_tokens\":100,\"cached_input_tokens\":20,\"output_tokens\":30,\"reasoning_output_tokens\":5,\"total_tokens\":130}}}") +
        line("event_msg", "{\"type\":\"task_complete\",\"turn_id\":\"" + turn + "\"}");
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    auto root = std::filesystem::temp_directory_path() / ("quota-native-test-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    try {
        auto settings = nativequota::Defaults(); settings.home = (root / "home with spaces").string();
        settings.historyDirectory = (root / "history with spaces").string();
        std::filesystem::create_directories(settings.home);
        { std::ofstream auth(std::filesystem::path(settings.home) / "auth.json"); auth << "synthetic-account-a"; }
        settings.seconds = 45; settings.staleMinutes = 2; settings.left = true; settings.onlyWithCodex = false; settings.chinese = true;
        auto file = root / "settings.ini";
        require(nativequota::Save(file, settings), "Valid settings must save");
        auto restored = nativequota::Load(file);
        require(restored.seconds == 45 && restored.staleMinutes == 2 && restored.left && restored.home == settings.home, "Settings must persist across restart");
        require(restored.chinese, "Native UI language must persist across restart");
        require(restored.historyEnabled && restored.historyDirectory == settings.historyDirectory, "History enable/path settings must persist across restart");
        settings.historyEnabled = false;
        require(nativequota::Save(file, settings) && !nativequota::Load(file).historyEnabled, "Disabled recording must persist without deleting its directory setting");
        settings.historyEnabled = true;
        struct stat attributes{}; stat(file.c_str(), &attributes);
        require((attributes.st_mode & 0777) == 0600, "Settings must be private to the current user");
        settings.seconds = 0; require(!nativequota::Save(file, settings) && nativequota::Load(file).seconds == 45, "Invalid settings must not overwrite valid file"); settings.seconds = 45;
        settings.host = "Codex\nonline=0"; require(!nativequota::Save(file, settings), "Multiline setting injection must be rejected"); settings.host = "Codex";
        { std::ofstream out(file); out << std::string(70000, 'x'); }
        require(nativequota::Load(file).seconds == 120, "Oversized settings must fall back to defaults");
        auto top = nativequota::Placement({100, 50, 900, 600}, false), left = nativequota::Placement({100, 50, 900, 600}, true);
        require(top.x == 426 && top.y == 56 && top.width == 248 && left.x == 106 && left.y == 294, "Overlay must follow host coordinates");
        require(nativequota::HostMatches("com.openai.Codex", "codex") && !nativequota::HostMatches("Browser", "Codex"), "Host identification must exclude unrelated apps");
        require(nativequota::Remaining({true, 20, 1}, 2000) == "-", "Expired quota must become unknown");
        nativequota::State weekly; weekly.snapshot.weekly = {true, 46, 0}; weekly.snapshot.observed = nativequota::NowMs();
        require(nativequota::Summary(weekly, settings, nativequota::NowMs()) == "W 54%", "Weekly-only native menu bar must omit five-hour label");
        require(nativequota::Placement({100, 50, 900, 600}, true, true).height == 56, "Weekly-only native overlay must compact its geometry");
        { nativequota::InstanceLock first(root / "instance.lock"); require(first.acquired(), "First instance must acquire lock"); nativequota::InstanceLock second(root / "instance.lock"); require(!second.acquired(), "Duplicate instance must fail lock"); }
        { nativequota::InstanceLock third(root / "instance.lock"); require(third.acquired(), "Exited instance must release lock"); }
        settings.binary = std::filesystem::absolute(argv[1]).string(); settings.online = true;
        unsetenv("QUOTA_FAKE_MODE");
        nativequota::Engine engine(settings); std::atomic<int> notifications{0};
        engine.start([&] { ++notifications; });
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while ((!engine.state().snapshot.observed || notifications.load() < 2) && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(engine.state().snapshot.fiveHour.used == 20, "Refresh engine must query real fake-server process");
        auto history = engine.history();
        require(history.entries.size() == 1 && history.entries[0].snapshot.fiveHour.used == 20 && history.entries[0].source == "account", "Successful engine query must be readable from the selected quota history directory");
        int before = notifications.load(); std::this_thread::sleep_for(std::chrono::milliseconds(200));
        require(notifications.load() == before, "Idle engine must block rather than poll its UI");
        settings.online = false; engine.configure(settings);
        { std::ofstream auth(std::filesystem::path(settings.home) / "auth.json"); auth << "synthetic-account-b-with-new-metadata"; }
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (engine.state().snapshot.observed && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(engine.state().snapshot.observed == 0, "Auth-file change must clear the previous account even while online refresh is disabled");
        settings.online = true; engine.configure(settings);
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(7);
        while (!engine.state().snapshot.observed && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(engine.state().snapshot.observed > 0, "Online refresh must resume after account change");
        history = engine.history();
        require(history.entries.size() == 2 && history.entries.back().kind == "baseline" && !history.entries.back().fiveChange, "Account changes must begin another independent history baseline");
        auto originalHistory = settings.historyDirectory;
        auto blocked = root / "blocked-history";
        settings.historyDirectory = blocked.string(); engine.configure(settings);
        { std::ofstream out(blocked); out << "synthetic existing file"; }
        engine.refresh();
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(7);
        while (engine.state().historyError.empty() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(!engine.state().historyError.empty() && engine.state().error.empty() && engine.state().snapshot.fiveHour.used == 20,
            "History disk errors must be visible separately and preserve the successful account quota");
        std::filesystem::remove(blocked); settings.historyDirectory = originalHistory;
        settings.online = false; engine.configure(settings);
        require(engine.state().snapshot.observed > 0, "Rename regression must begin with a real snapshot");
        std::filesystem::rename(std::filesystem::path(settings.home) / "auth.json", std::filesystem::path(settings.home) / "auth.saved");
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (engine.state().snapshot.observed && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(engine.state().snapshot.observed == 0, "Moving auth.json away must clear cached quota even with online queries off");
        settings.home = (root / "second account").string(); settings.online = false; engine.configure(settings);
        require(engine.state().snapshot.observed == 0, "Changing account home must clear previous snapshot"); engine.stop();
        // 在线查询关闭时，新建多级会话目录和追加完成事件仍应触发真实 token 记录。
        settings.home = (root / "request-home").string(); settings.historyDirectory = (root / "request-history").string(); settings.online = false;
        std::filesystem::create_directories(settings.home);
        { std::ofstream auth(std::filesystem::path(settings.home) / "auth.json"); auth << "request-account-a"; }
        nativequota::Engine listener(settings); listener.start([] {});
        auto sessions = std::filesystem::path(settings.home) / "sessions/2099/01/01"; std::filesystem::create_directories(sessions);
        auto session = sessions / "rollout-runtime.jsonl";
        { std::ofstream out(session); out << requestFixture("runtime-turn", nativequota::NowMs() + 10); }
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        quota::RequestReadResult requests;
        do { requests = listener.requests(); if (!requests.entries.empty()) break; std::this_thread::sleep_for(std::chrono::milliseconds(10)); } while (std::chrono::steady_clock::now() < deadline);
        require(requests.entries.size() == 1 && requests.entries[0].turnId == "runtime-turn" && requests.entries[0].tokens && requests.entries[0].tokens->input == 100 && requests.entries[0].tokens->total == 130,
            "Native event-driven session follower must record real request tokens with online queries disabled");
        require(listener.state().snapshot.observed == 0, "Session logging must not launch an account query");
        // 旧请求仍在文件尾部：即便认证通知先处理，重放后的旧开始行也不能跨账号接上新 token／额度。
        auto crossing = sessions / "rollout-auth-boundary.jsonl"; auto start = nativequota::NowMs() + 10;
        auto prefix = requestFixture("cross-account-turn", start); prefix.erase(prefix.rfind("{\"timestamp\":"));
        auto sample = "{\"timestamp\":\"" + iso(start) + "\",\"type\":\"event_msg\",\"payload\":{\"type\":\"token_count\",\"info\":{\"total_token_usage\":{\"input_tokens\":0,\"cached_input_tokens\":0,\"output_tokens\":0,\"reasoning_output_tokens\":0,\"total_tokens\":0}},\"rate_limits\":{\"primary\":{\"used_percent\":20,\"window_minutes\":300,\"resets_at\":2000000000}}}}\n";
        { std::ofstream out(crossing); out << sample << prefix; }
        // 给旧账号的真实文件事件留出处理机会；接下来明确等待 auth 状态通知再完成旧请求。
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        { std::ofstream replacement(std::filesystem::path(settings.home) / "auth.next"); replacement << "request-account-b"; }
        std::filesystem::rename(std::filesystem::path(settings.home) / "auth.next", std::filesystem::path(settings.home) / "auth.json");
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (listener.state().error != "Account changed; waiting for new quota" && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(listener.state().error == "Account changed; waiting for new quota", "Auth replacement must be observed before replay completion regression");
        auto ready = sessions / "rollout-new-account-ready.jsonl";
        { std::ofstream out(ready); out << requestFixture("new-account-ready", nativequota::NowMs() + 10); }
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        auto hasTurn = [&](const char* turn) { auto rows = listener.requests(); return std::any_of(rows.entries.begin(), rows.entries.end(), [&](const auto& row) { return row.turnId == turn; }); };
        while (!hasTurn("new-account-ready") && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(hasTurn("new-account-ready"), "New account listener generation must process real session events");
        auto completion = nativequota::NowMs() + 10;
        { std::ofstream out(crossing, std::ios::app);
          out << "{\"timestamp\":\"" << iso(completion) << "\",\"type\":\"event_msg\",\"payload\":{\"type\":\"token_count\",\"info\":{\"total_token_usage\":{\"input_tokens\":200,\"cached_input_tokens\":40,\"output_tokens\":60,\"reasoning_output_tokens\":10,\"total_tokens\":260}},\"rate_limits\":{\"primary\":{\"used_percent\":40,\"window_minutes\":300,\"resets_at\":2000000000}}}}\n";
          out << "{\"timestamp\":\"" << iso(completion) << "\",\"type\":\"event_msg\",\"payload\":{\"type\":\"task_complete\",\"turn_id\":\"cross-account-turn\"}}\n"; }
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (!hasTurn("cross-account-turn") && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(!hasTurn("cross-account-turn"), "Auth boundary must reject a replayed old-account request instead of combining cross-account tokens or quota");
        const auto recordedBeforeDisable = listener.requests().entries.size();
        settings.historyEnabled = false; listener.configure(settings);
        { std::ofstream out(session, std::ios::app); out << requestFixture("disabled-turn", nativequota::NowMs() + 10); }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        require(listener.requests().entries.size() == recordedBeforeDisable, "Disabled logging must stop following session appends"); listener.stop();
        // root 完全由测试创建，包含新监听器的会话与日志目录，结束后统一清理。
        std::filesystem::remove_all(root);
        std::cout << "PASS: preferences, input bounds, overlay geometry, expiry, single instance and blocking refresh lifecycle\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}

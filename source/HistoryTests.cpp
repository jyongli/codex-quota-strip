#include "QuotaHistory.h"
#include <chrono>
#include <iostream>
#include <fstream>

static void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }

int main() {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto root = std::filesystem::temp_directory_path() / ("quota-history-test-" + std::to_string(now));
    try {
    quota::HistoryStore history;
    history.configure(true, root);
    quota::Snapshot snapshot; snapshot.observed = now;
    snapshot.weekly = {true, 20, now / 1000 + 600};
    history.record(snapshot, "account");
    auto rows = history.read();
    require(rows.entries.size() == 1 && rows.entries[0].snapshot.weekly.used == 20 && !rows.entries[0].snapshot.fiveHour.present,
        "a weekly-only quota snapshot must be written and readable in the selected directory");
    auto file = std::filesystem::directory_iterator(root)->path(); auto size = std::filesystem::file_size(file);
    snapshot.observed += 1; require(!history.record(snapshot, "account") && std::filesystem::file_size(file) == size, "unchanged quota must not rewrite a log");
    snapshot.weekly.used = 22.5; snapshot.observed += 1;
    require(history.record(snapshot, "account"), "changed fractional quota must append");
    rows = history.read(); require(rows.entries.size() == 2 && rows.entries.back().weeklyChange == -2.5 && !rows.entries.back().fiveChange, "deltas must use remaining percentage points, never fabricate missing five-hour quota");
    // 模拟进程异常中断只留下半行；后续有效变动不能被半行吞掉。
    { std::ofstream out(file, std::ios::app | std::ios::binary); out << "interrupted-partial-record"; }
    snapshot.weekly.used = 23; snapshot.observed += 1; history.record(snapshot, "account");
    require(history.read().entries.size() == 3, "a valid change after an interrupted CSV write must remain readable");
    snapshot.weekly.resets += 600; snapshot.weekly.used = 0; snapshot.observed += 1; history.record(snapshot, "account");
    rows = history.read(); require(rows.entries.back().kind == "reset" && !rows.entries.back().weeklyChange, "new quota cycle must not be reported as negative consumption");
    history.reset(); snapshot.weekly.used = 80; snapshot.observed += 1; history.record(snapshot, "account");
    require(history.read().entries.back().kind == "baseline" && !history.read().entries.back().weeklyChange, "account reset must start an independent baseline");
    size = std::filesystem::file_size(file); auto old = snapshot; old.observed -= 1; old.weekly.used = 99; history.record(old, "account");
    quota::Snapshot missing; missing.observed = now + 999; history.record(missing, "account");
    auto invalid = snapshot; invalid.weekly.used = std::nan(""); history.record(invalid, "account");
    require(std::filesystem::file_size(file) == size, "old, missing and invalid samples must not become new quota changes");
    history.configure(false, root); snapshot.weekly.used = 81; snapshot.observed += 1; history.record(snapshot, "account");
    require(std::filesystem::file_size(file) == size, "disabled logging must not write");
    auto changedDirectory = root / std::filesystem::u8path(u8"日志 history"); std::string error;
    require(quota::HistoryStore::ValidateDirectory(changedDirectory, error), "Unicode directory must be selectable and writable");
    history.configure(true, changedDirectory); history.record(snapshot, "account");
    require(history.read().entries.size() == 1 && std::filesystem::file_size(file) == size, "changing directory must preserve earlier logs without moving them");
    for (int i = 0; i < 2050; ++i) { snapshot.observed += 1; snapshot.weekly.used = i % 100; require(history.record(snapshot, "account"), "changed quota must append reliably"); }
    rows = history.read(); require(rows.entries.size() == 2000 && rows.truncated && rows.entries.back().snapshot.weekly.used == 49, "viewer must return only the latest bounded rows");
    require(history.read(3).entries.size() == 3 && history.read(0).entries.empty(), "caller row limits must be honored");
    auto mixed = root / "mixed-archive"; history.configure(true, mixed);
    snapshot.observed += 1; history.record(snapshot, "account"); snapshot.weekly.used = 50; snapshot.observed += 1; history.record(snapshot, "account");
    std::time_t yesterday = static_cast<std::time_t>(now / 1000 - 86400); std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &yesterday);
#else
    gmtime_r(&yesterday, &utc);
#endif
    char brokenName[64]{}; std::strftime(brokenName, sizeof(brokenName), "quota-history-%Y-%m-%d.csv", &utc);
    std::filesystem::create_directory(mixed / brokenName);
    rows = history.read();
    require(!rows.error.empty() && rows.entries.size() == 2 && rows.entries[0].snapshot.observed < rows.entries[1].snapshot.observed,
        "partial history read failure must still return rows in chronological order for charts");
    auto bad = root / "not-a-directory"; { std::ofstream out(bad); out << "preserve"; }
    require(!quota::HistoryStore::ValidateDirectory(bad, error), "a file must not be accepted as a directory");
    history.configure(true, bad); snapshot.observed += 1; snapshot.weekly.used = 25;
    require(!history.record(snapshot, "account") && !history.error().empty(), "write failure must remain visible and nonfatal");
    require(!quota::HistoryStore::ValidateDirectory("relative-directory", error), "relative directories must not depend on launcher working directory");
    std::cout << "PASS: selected-directory history, exact changes, write deduplication and partial-write recovery\n";
    std::cout << "PASS: reset/account boundaries, disable, Unicode paths, bounded reads, preserved old logs and nonfatal IO errors\n";
    // 仅删除本测试在 TEMP 创建的专用目录，绝不按用户配置清理历史。
    require(std::filesystem::equivalent(root.parent_path(), std::filesystem::temp_directory_path()) && root.filename().string().find("quota-history-test-") == 0, "invalid fixture cleanup path");
    std::filesystem::remove_all(root);
    return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << " (fixture: " << root.string() << ")\n"; return 1; }
}

#pragma once
#include "../source/QuotaCore.h"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>

namespace nativequota {
class AuthWatcher;
struct Preferences {
    int seconds = 120, staleMinutes = 5;
    bool online = true, onlyWithCodex = true, overlay = true, left = false;
    std::string home, binary, host = "Codex";
};
struct State {
    quota::Snapshot snapshot;
    bool refreshing = false;
    std::string error;
};
struct Rect { double x = 0, y = 0, width = 0, height = 0; };
struct QueryResult {
    bool ok = false;
    quota::Snapshot snapshot;
    std::string error;
    int childPid = 0;
};
long long NowMs();
std::filesystem::path ConfigDirectory();
Preferences Defaults();
bool Valid(const Preferences& settings);
Preferences Load(const std::filesystem::path& file);
bool Save(const std::filesystem::path& file, const Preferences& settings);
std::string FindBinary(const Preferences& settings);
std::string Remaining(quota::Window value, long long now);
std::string Summary(const State& state, const Preferences& settings, long long now);
bool Stale(const State& state, const Preferences& settings, long long now);
bool HostMatches(const std::string& identifier, const std::string& configured);
Rect Placement(Rect host, bool left);

// 每次查询拥有独立进程组，退出、超时及取消时清理，不复用常驻服务。
QueryResult Query(const std::string& binary, const std::string& home,
                  const std::atomic<bool>& cancel, int timeoutMs = 15000);
class InstanceLock {
    int fd_ = -1;
public:
    explicit InstanceLock(const std::filesystem::path& file);
    ~InstanceLock();
    bool acquired() const { return fd_ >= 0; }
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;
};
class Engine {
    std::mutex mutex_;
    std::condition_variable wake_;
    Preferences preferences_;
    State state_;
    std::function<void()> changed_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> queryCancelled_{false};
    std::unique_ptr<AuthWatcher> authWatcher_;
    std::string authIdentity_;
    bool requested_ = true, manual_ = false, hostAvailable_ = false;
    unsigned generation_ = 0;
    void run();
    void checkAccount();
public:
    explicit Engine(Preferences settings);
    ~Engine();
    Preferences preferences();
    State state();
    void configure(Preferences settings);
    void hostAvailable(bool value);
    void start(std::function<void()> changed);
    void refresh();
    void stop();
};
struct Options { bool settings = false, smoke = false, smokeWayland = false; Preferences preferences; };
Options ParseOptions(int argc, char** argv);
}

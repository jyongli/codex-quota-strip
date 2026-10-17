#include "NativeCore.h"
#include "../source/ProxyConfig.h"
#include <spawn.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <cctype>
#include <cstring>
#ifdef __APPLE__
#include <sys/event.h>
#else
#include <sys/inotify.h>
#endif
extern char** environ;

namespace nativequota {
namespace {
std::string Environment(const char* name) { auto value = getenv(name); return value ? value : ""; }
bool SafeValue(const std::string& value) { return value.size() <= 4096 && value.find_first_of("\r\n") == std::string::npos && value.find('\0') == std::string::npos; }
std::string Lower(std::string value) { for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return value; }
std::string AuthStamp(const std::string& home) {
    struct stat value{};
    if (stat((std::filesystem::path(home) / "auth.json").c_str(), &value)) return "missing";
#ifdef __APPLE__
    auto ns = value.st_mtimespec.tv_nsec;
#else
    auto ns = value.st_mtim.tv_nsec;
#endif
    return std::to_string(value.st_ino) + "/" + std::to_string(value.st_mtime) + "/" + std::to_string(ns) + "/" + std::to_string(value.st_size);
}
struct Fd {
    int value = -1;
    Fd() = default;
    explicit Fd(int next) : value(next) {}
    ~Fd() { if (value >= 0) close(value); }
    Fd(Fd&& other) noexcept : value(other.value) { other.value = -1; }
    Fd& operator=(Fd&& other) noexcept { if (value >= 0) close(value); value = other.value; other.value = -1; return *this; }
    Fd(const Fd&) = delete;
};
std::pair<Fd, Fd> Pipe() {
    int raw[2];
    if (pipe(raw)) throw std::runtime_error("pipe");
    Fd read(raw[0]), write(raw[1]);
    // 标准描述符可能在 GUI 启动时关闭；通信端必须避开 0/1/2。
    for (Fd* fd : {&read, &write}) {
        if (fd->value < 3) { int next = fcntl(fd->value, F_DUPFD, 3); if (next < 0) throw std::runtime_error("dup"); *fd = Fd(next); }
        if (fcntl(fd->value, F_SETFD, FD_CLOEXEC)) throw std::runtime_error("cloexec");
    }
    return {std::move(read), std::move(write)};
}
struct Child {
    pid_t pid = -1;
    ~Child() {
        if (pid <= 0) return;
        // posix_spawn 为子进程建立独立进程组，负 PID 只指向本次查询组。
        kill(-pid, SIGTERM);
        // 先清理整组再回收根进程，避免根 PID 被复用后再按组发送信号。
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        kill(-pid, SIGKILL);
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
};
struct SpawnSetup {
    posix_spawn_file_actions_t files{};
    posix_spawnattr_t attributes{};
    bool hasFiles = false, hasAttributes = false;
    ~SpawnSetup() { if (hasFiles) posix_spawn_file_actions_destroy(&files); if (hasAttributes) posix_spawnattr_destroy(&attributes); }
};
}
// 认证文件只检查元数据，事件线程在内核中阻塞；关闭在线查询也能及时清空旧账号。
class AuthWatcher {
    std::mutex mutex_;
    std::string home_;
    std::function<void()> changed_;
    Fd wakeRead_, wakeWrite_;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
    void signal() { char byte = 1; (void)write(wakeWrite_.value, &byte, 1); }
    std::string currentHome() { std::lock_guard<std::mutex> lock(mutex_); return home_; }
    std::filesystem::path watchDirectory(const std::string& home) {
        std::filesystem::path directory(home); std::error_code error;
        while (!directory.empty() && !std::filesystem::is_directory(directory, error)) {
            auto parent = directory.parent_path(); if (parent == directory) break; directory = parent; error.clear();
        }
        return directory;
    }
    void drain() { char bytes[128]; while (read(wakeRead_.value, bytes, sizeof(bytes)) > 0) {} }
    void run() {
        while (!stopping_.load()) {
            auto home = currentHome(); auto directory = watchDirectory(home);
#ifdef __APPLE__
            Fd notification(kqueue()), folder(open(directory.c_str(), O_EVTONLY | O_CLOEXEC));
            Fd auth(open((std::filesystem::path(home) / "auth.json").c_str(), O_EVTONLY | O_CLOEXEC));
            bool installed = notification.value >= 0 && folder.value >= 0;
            if (installed) {
                fcntl(notification.value, F_SETFD, FD_CLOEXEC);
                struct kevent changes[2]; int count = 0;
                EV_SET(&changes[count++], folder.value, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE, 0, nullptr);
                if (auth.value >= 0) EV_SET(&changes[count++], auth.value, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE, 0, nullptr);
                installed = kevent(notification.value, changes, count, nullptr, 0, nullptr) == 0;
            }
#else
            Fd notification(inotify_init1(IN_NONBLOCK | IN_CLOEXEC));
            bool installed = notification.value >= 0 && inotify_add_watch(notification.value, directory.c_str(), IN_CREATE | IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF) >= 0;
#endif
            changed_(); // 注册监听期间发生的改动也必须补查，避免启动竞争。
            bool rebuild = false;
            while (!stopping_.load() && !rebuild) {
                pollfd descriptors[2]{{wakeRead_.value, POLLIN, 0}, {installed ? notification.value : -1, POLLIN, 0}};
                int ready = poll(descriptors, 2, installed ? -1 : 10000);
                if (ready < 0 && errno == EINTR) continue;
                if (ready < 0) break;
                if (descriptors[0].revents) { drain(); break; }
                if (!installed) { changed_(); rebuild = true; continue; }
                if (!descriptors[1].revents) continue;
#ifdef __APPLE__
                struct kevent events[8]; timespec immediate{};
                if (kevent(notification.value, nullptr, 0, events, 8, &immediate) > 0) { changed_(); rebuild = true; }
#else
                alignas(inotify_event) char bytes[8192]; ssize_t count = read(notification.value, bytes, sizeof(bytes));
                bool authChanged = directory != std::filesystem::path(home);
                for (size_t offset = 0; count > 0 && offset + sizeof(inotify_event) <= static_cast<size_t>(count);) {
                    auto event = reinterpret_cast<const inotify_event*>(bytes + offset);
                    if (event->len && std::strcmp(event->name, "auth.json") == 0) authChanged = true;
                    if (event->mask & IN_Q_OVERFLOW) authChanged = true;
                    if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED)) rebuild = true;
                    offset += sizeof(inotify_event) + event->len;
                }
                if (authChanged || rebuild) changed_();
                if (watchDirectory(home) != directory) rebuild = true;
#endif
            }
        }
    }
public:
    AuthWatcher(std::string home, std::function<void()> changed) : home_(std::move(home)), changed_(std::move(changed)) {
        auto descriptors = Pipe(); wakeRead_ = std::move(descriptors.first); wakeWrite_ = std::move(descriptors.second);
        fcntl(wakeRead_.value, F_SETFL, O_NONBLOCK); fcntl(wakeWrite_.value, F_SETFL, O_NONBLOCK);
        thread_ = std::thread(&AuthWatcher::run, this);
    }
    void home(std::string value) { { std::lock_guard<std::mutex> lock(mutex_); home_ = std::move(value); } signal(); }
    void stop() { stopping_ = true; signal(); if (thread_.joinable()) thread_.join(); }
    ~AuthWatcher() { stop(); }
};
long long NowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
std::filesystem::path ConfigDirectory() {
    auto home = Environment("HOME");
    if (home.empty()) throw std::runtime_error("HOME is not configured");
#ifdef __APPLE__
    return std::filesystem::path(home) / "Library/Application Support/CodexQuotaStrip";
#else
    auto config = Environment("XDG_CONFIG_HOME");
    return (config.empty() ? std::filesystem::path(home) / ".config" : std::filesystem::path(config)) / "codex-quota-strip";
#endif
}
Preferences Defaults() {
    Preferences value;
    value.historyDirectory = (ConfigDirectory() / "history").string();
    value.home = Environment("CODEX_HOME");
    if (value.home.empty()) value.home = (std::filesystem::path(Environment("HOME")) / ".codex").string();
    return value;
}
bool Valid(const Preferences& value) {
    return value.seconds >= 30 && value.seconds <= 3600 && value.staleMinutes >= 1 && value.staleMinutes <= 120 &&
        !value.home.empty() && std::filesystem::path(value.home).is_absolute() && SafeValue(value.home) && SafeValue(value.binary) &&
        !value.host.empty() && value.host.size() <= 128 && SafeValue(value.host) &&
        (value.historyDirectory.empty() || (std::filesystem::path(value.historyDirectory).is_absolute() && SafeValue(value.historyDirectory)));
}
Preferences Load(const std::filesystem::path& file) {
    Preferences next = Defaults();
    std::error_code sizeError;
    auto size = std::filesystem::file_size(file, sizeError);
    if (sizeError || size > 65536) return next;
    std::ifstream in(file); std::string line;
    try {
        while (std::getline(in, line)) {
            if (line.size() > 8192) return Defaults();
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto equal = line.find('='); if (equal == std::string::npos) continue;
            auto key = line.substr(0, equal), value = line.substr(equal + 1);
            if (key == "seconds") next.seconds = std::stoi(value);
            else if (key == "stale_minutes") next.staleMinutes = std::stoi(value);
            else if (key == "online") next.online = value == "1";
            else if (key == "only_with_codex") next.onlyWithCodex = value == "1";
            else if (key == "overlay") next.overlay = value == "1";
            else if (key == "position") next.left = value == "left";
            else if (key == "language") next.chinese = value == "zh-CN";
            else if (key == "home") next.home = value;
            else if (key == "binary") next.binary = value;
            else if (key == "host") next.host = value;
            else if (key == "history_enabled") next.historyEnabled = value == "1";
            else if (key == "history_directory") next.historyDirectory = value;
        }
    } catch (...) { return Defaults(); }
    return Valid(next) ? next : Defaults();
}
bool Save(const std::filesystem::path& file, const Preferences& value) {
    if (!Valid(value)) return false;
    try {
        std::filesystem::create_directories(file.parent_path());
        auto temporary = file.string() + ".tmp-" + std::to_string(getpid());
        Fd out(open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
        if (out.value < 0) return false;
        std::ostringstream text;
        text << "seconds=" << value.seconds << "\nstale_minutes=" << value.staleMinutes << "\nonline=" << value.online
             << "\nonly_with_codex=" << value.onlyWithCodex << "\noverlay=" << value.overlay << "\nposition=" << (value.left ? "left" : "top")
             << "\nhome=" << value.home << "\nbinary=" << value.binary << "\nhost=" << value.host << "\nlanguage=" << (value.chinese ? "zh-CN" : "en")
             << "\nhistory_enabled=" << value.historyEnabled << "\nhistory_directory=" << value.historyDirectory << '\n';
        auto bytes = text.str(); size_t offset = 0;
        while (offset < bytes.size()) { auto count = write(out.value, bytes.data() + offset, bytes.size() - offset); if (count < 0 && errno == EINTR) continue; if (count <= 0) { unlink(temporary.c_str()); return false; } offset += static_cast<size_t>(count); }
        if (rename(temporary.c_str(), file.c_str())) { unlink(temporary.c_str()); return false; }
        return true;
    } catch (...) { return false; }
}
std::string FindBinary(const Preferences& value) {
    if (!value.binary.empty()) return access(value.binary.c_str(), X_OK) == 0 ? value.binary : "";
    std::vector<std::filesystem::path> candidates;
    auto path = Environment("PATH"); std::stringstream stream(path); std::string part;
    while (std::getline(stream, part, ':')) if (!part.empty()) candidates.push_back(std::filesystem::path(part) / "codex");
    for (const char* root : {"/opt/homebrew/bin", "/usr/local/bin", "/usr/bin"}) candidates.push_back(std::filesystem::path(root) / "codex");
    candidates.push_back(std::filesystem::path(Environment("HOME")) / ".local/bin/codex");
    for (const char* root : {"/Applications/Codex.app/Contents/Resources/codex", "/Applications/ChatGPT.app/Contents/Resources/codex", "/opt/Codex/resources/codex", "/opt/ChatGPT/resources/codex"}) candidates.emplace_back(root);
    for (const auto& candidate : candidates) if (access(candidate.c_str(), X_OK) == 0) return candidate.string();
    return "";
}
std::string Remaining(quota::Window value, long long now) {
    if (!value.present || (value.resets > 0 && now >= value.resets * 1000)) return "-";
    double remaining = std::clamp(100.0 - value.used, 0.0, 100.0);
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::fixed << std::setprecision(std::abs(remaining - std::round(remaining)) < 0.001 ? 0 : 1) << remaining << '%'; return out.str();
}
bool Stale(const State& state, const Preferences& value, long long now) { return !state.snapshot.observed || now - state.snapshot.observed >= value.staleMinutes * 60000LL; }
std::string Summary(const State& state, const Preferences& value, long long now) { return (quota::WeeklyOnly(state.snapshot) ? "" : "5h " + Remaining(state.snapshot.fiveHour, now) + "  ") + "W " + Remaining(state.snapshot.weekly, now) + (Stale(state, value, now) ? " *" : ""); }
bool HostMatches(const std::string& identifier, const std::string& configured) {
    auto name = Lower(identifier), expected = Lower(configured);
    return !expected.empty() && (name.find(expected) != std::string::npos || (expected == "codex" && name.find("chatgpt") != std::string::npos));
}
Rect Placement(Rect host, bool left, bool weeklyOnly) {
    double height = weeklyOnly ? 56 : 112;
    if (left) return {host.x + 6, host.y + std::max(40.0, (host.height - height) / 2), std::min(70.0, std::max(40.0, host.width - 12)), std::min(height, std::max(28.0, host.height - 12))};
    double width = std::min(weeklyOnly ? 124.0 : 248.0, std::max(100.0, host.width - 16));
    return {host.x + std::max(8.0, (host.width - width) / 2), host.y + 6, width, 28};
}
InstanceLock::InstanceLock(const std::filesystem::path& file) {
    std::filesystem::create_directories(file.parent_path());
    int fd = open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0) fd_ = fd; else if (fd >= 0) close(fd);
}
InstanceLock::~InstanceLock() { if (fd_ >= 0) close(fd_); }

QueryResult Query(const std::string& binary, const std::string& home, const std::atomic<bool>& cancel, int timeoutMs) {
    QueryResult answer;
    if (cancel.load()) { answer.error = "Query cancelled"; return answer; }
    if (binary.empty() || home.empty()) { answer.error = "Codex executable not found; set its path in Settings"; return answer; }
    static std::once_flag ignoredPipe;
    std::call_once(ignoredPipe, [] { signal(SIGPIPE, SIG_IGN); });
    try {
        quota::ProxySettings proxy;
        if(!quota::LoadCodexProxy(std::filesystem::path(home),proxy)){answer.error="Invalid Codex proxy configuration; check config.toml";return answer;}
        auto input = Pipe(), output = Pipe(); SpawnSetup setup;
        if (posix_spawn_file_actions_init(&setup.files)) throw std::runtime_error("spawn"); setup.hasFiles = true;
        if (posix_spawnattr_init(&setup.attributes)) throw std::runtime_error("spawn"); setup.hasAttributes = true;
        sigset_t defaults; sigemptyset(&defaults); sigaddset(&defaults, SIGPIPE);
        if (posix_spawnattr_setflags(&setup.attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF) || posix_spawnattr_setpgroup(&setup.attributes, 0) || posix_spawnattr_setsigdefault(&setup.attributes, &defaults)) throw std::runtime_error("spawn");
        for (auto pair : {std::pair<int,int>{input.first.value, STDIN_FILENO}, {output.second.value, STDOUT_FILENO}}) if (posix_spawn_file_actions_adddup2(&setup.files, pair.first, pair.second)) throw std::runtime_error("spawn");
        if (posix_spawn_file_actions_addopen(&setup.files, STDERR_FILENO, "/dev/null", O_WRONLY, 0)) throw std::runtime_error("spawn");
        for (int fd : {input.first.value, input.second.value, output.first.value, output.second.value}) if (posix_spawn_file_actions_addclose(&setup.files, fd)) throw std::runtime_error("spawn");
        std::vector<std::string> environment;
        for (char** entry = environ; entry && *entry; ++entry){
            std::string value(*entry);const auto equal=value.find('=');const auto key=quota::ProxyKey(value.substr(0,equal));
            if(strncmp(*entry,"CODEX_HOME=",11)!=0&&proxy.find(key)==proxy.end())environment.push_back(std::move(value));
        }
        environment.push_back("CODEX_HOME=" + home);
        // POSIX 代理消费者对大小写的偏好不同，统一写入两个别名并清除冲突旧值。
        for(const auto& item:proxy)if(!item.second.empty()){environment.push_back(item.first+"="+item.second);environment.push_back(Lower(item.first)+"="+item.second);}
        std::vector<char*> env; for (auto& entry : environment) env.push_back(entry.data()); env.push_back(nullptr);
        std::string command = "app-server"; char* arguments[] = {const_cast<char*>(binary.c_str()), command.data(), nullptr};
        Child child;
        if (posix_spawn(&child.pid, binary.c_str(), &setup.files, &setup.attributes, arguments, env.data())) { child.pid = -1; answer.error = "Cannot start Codex app-server"; return answer; }
        answer.childPid = child.pid;
        input.first = Fd(); output.second = Fd();
        if (fcntl(output.first.value, F_SETFL, O_NONBLOCK) || fcntl(input.second.value, F_SETFL, O_NONBLOCK)) throw std::runtime_error("pipe");
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(1, timeoutMs));
        auto available = [&] { return !cancel.load() && std::chrono::steady_clock::now() < deadline; };
        auto send = [&](const std::string& value) {
            size_t position = 0;
            while (position < value.size() && available()) {
                ssize_t count = write(input.second.value, value.data() + position, value.size() - position);
                if (count > 0) position += static_cast<size_t>(count);
                else if (count < 0 && (errno == EAGAIN || errno == EINTR)) { pollfd fd{input.second.value, POLLOUT, 0}; poll(&fd, 1, 20); }
                else return false;
            }
            return position == value.size();
        };
        std::string buffer; size_t total = 0;
        auto receive = [&](int id, quota::Json& result) {
            while (available()) {
                size_t end;
                while ((end = buffer.find('\n')) != std::string::npos) {
                    if (end > 32768) { answer.error = "Codex response exceeded size limit"; return false; }
                    auto line = buffer.substr(0, end); buffer.erase(0, end + 1);
                    try {
                        auto json = quota::JsonParser(line).parse(); const auto& identity = json.get("id");
                        if (identity.kind != quota::Json::Number || identity.num != id) continue;
                        if (json.get("error").kind != quota::Json::Null) { answer.error = "Codex account query failed"; return false; }
                        result = json.get("result"); return result.kind == quota::Json::Object;
                    } catch (...) { continue; }
                }
                if (buffer.size() > 32768 || total > 256 * 1024) { answer.error = "Codex response exceeded size limit"; return false; }
                pollfd fd{output.first.value, POLLIN, 0}; int ready = poll(&fd, 1, 50);
                if (ready < 0 && errno != EINTR) return false;
                if (ready <= 0) continue;
                char bytes[4096]; ssize_t count = read(output.first.value, bytes, sizeof(bytes));
                if (count > 0) { buffer.append(bytes, static_cast<size_t>(count)); total += static_cast<size_t>(count); }
                else if (count == 0) { answer.error = "Codex app-server exited"; return false; }
                else if (errno != EINTR && errno != EAGAIN) return false;
            }
            answer.error = cancel.load() ? "Query cancelled" : "Codex query timed out"; return false;
        };
        quota::Json reply;
        if (!send("{\"id\":1,\"method\":\"initialize\",\"params\":{\"clientInfo\":{\"name\":\"codex_quota_strip\",\"version\":\"3.0.0\"}}}\n") || !receive(1, reply)) { if (answer.error.empty()) answer.error = "Cannot communicate with Codex"; return answer; }
        if (!send("{\"method\":\"initialized\"}\n{\"id\":2,\"method\":\"account/rateLimits/read\"}\n") || !receive(2, reply)) { if (answer.error.empty()) answer.error = "Cannot read Codex account quota"; return answer; }
        if (!quota::ReadLimits(reply, answer.snapshot)) { answer.error = "Codex returned no supported quota windows"; return answer; }
        answer.snapshot.observed = NowMs(); answer.ok = true;
        return answer;
    } catch (...) { answer.error = "Codex query failed"; return answer; }
}

Engine::Engine(Preferences settings) : preferences_(std::move(settings)) {
    history_.configure(preferences_.historyEnabled, preferences_.historyDirectory.empty() ? ConfigDirectory() / "history" : std::filesystem::path(preferences_.historyDirectory));
}
Engine::~Engine() { stop(); }
Preferences Engine::preferences() { std::lock_guard<std::mutex> lock(mutex_); return preferences_; }
State Engine::state() { std::lock_guard<std::mutex> lock(mutex_); return state_; }
quota::HistoryReadResult Engine::history(size_t maxRows) { return history_.read(maxRows); }
void Engine::configure(Preferences value) {
    if (!Valid(value)) return;
    bool homeChanged; std::string currentHome;
    { std::lock_guard<std::mutex> lock(mutex_); homeChanged = value.home != preferences_.home; if (homeChanged) { state_.snapshot = {}; authIdentity_ = AuthStamp(value.home); history_.reset(); }
      history_.configure(value.historyEnabled, value.historyDirectory.empty() ? ConfigDirectory() / "history" : std::filesystem::path(value.historyDirectory));
      state_.historyError = history_.error(); preferences_ = std::move(value); currentHome = preferences_.home; ++generation_; queryCancelled_ = true; requested_ = true; }
    if (homeChanged && authWatcher_) authWatcher_->home(currentHome);
    wake_.notify_one();
}
void Engine::checkAccount() {
    if (stop_.load()) return;
    std::string home;
    { std::lock_guard<std::mutex> lock(mutex_); home = preferences_.home; }
    auto identity = AuthStamp(home); bool changed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (home == preferences_.home && identity != authIdentity_) {
            authIdentity_ = identity; state_.snapshot = {}; state_.error = "Account changed; waiting for new quota";
            history_.reset();
            ++generation_; queryCancelled_ = true; requested_ = true; changed = true;
        }
    }
    if (changed) { wake_.notify_one(); if (changed_) changed_(); }
}
void Engine::hostAvailable(bool value) { { std::lock_guard<std::mutex> lock(mutex_); if (hostAvailable_ == value) return; hostAvailable_ = value; requested_ = true; } wake_.notify_one(); }
void Engine::start(std::function<void()> changed) {
    changed_ = std::move(changed);
    { std::lock_guard<std::mutex> lock(mutex_); authIdentity_ = AuthStamp(preferences_.home); }
    authWatcher_ = std::make_unique<AuthWatcher>(preferences_.home, [this] { checkAccount(); });
    thread_ = std::thread(&Engine::run, this);
}
void Engine::refresh() { { std::lock_guard<std::mutex> lock(mutex_); requested_ = true; manual_ = true; } wake_.notify_one(); }
void Engine::stop() { stop_ = true; queryCancelled_ = true; wake_.notify_one(); if (authWatcher_) authWatcher_->stop(); if (thread_.joinable()) thread_.join(); }
void Engine::run() {
    unsigned failures = 0; auto nextDue = std::chrono::steady_clock::now(), lastAttempt = nextDue - std::chrono::seconds(5);
    std::string previousHome, previousAuth;
    while (!stop_.load()) {
        Preferences settings; unsigned generation;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (nextDue == std::chrono::steady_clock::time_point::max()) wake_.wait(lock, [&] { return stop_.load() || requested_; });
            else wake_.wait_until(lock, nextDue, [&] { return stop_.load() || requested_; });
            if (stop_.load()) break;
            settings = preferences_; bool manual = manual_; requested_ = false; manual_ = false;
            if (!settings.online || (settings.onlyWithCodex && !hostAvailable_ && !manual)) { nextDue = std::chrono::steady_clock::time_point::max(); continue; }
            auto earliest = lastAttempt + std::chrono::seconds(5);
            if (std::chrono::steady_clock::now() < earliest) { nextDue = earliest; manual_ = manual; continue; }
            queryCancelled_ = false; if (stop_.load()) break;
            generation = generation_; state_.refreshing = true;
        }
        std::string stamp = AuthStamp(settings.home);
        if (previousHome != settings.home || (!previousAuth.empty() && previousAuth != stamp)) { std::lock_guard<std::mutex> lock(mutex_); state_.snapshot = {}; history_.reset(); }
        previousHome = settings.home; previousAuth = stamp;
        if (changed_) changed_();
        lastAttempt = std::chrono::steady_clock::now();
        auto answer = Query(FindBinary(settings), settings.home, queryCancelled_);
        {
            std::lock_guard<std::mutex> lock(mutex_); state_.refreshing = false;
            if (generation == generation_) {
                if (AuthStamp(settings.home) != stamp) { state_.snapshot = {}; state_.error = "Account changed; refresh again"; history_.reset(); }
                else if (answer.ok) {
                    state_.snapshot = answer.snapshot; state_.error.clear(); failures = 0;
                    // 仅接受当前账号/配置代次的真实成功结果，日志失败不覆盖查询结果。
                    history_.record(answer.snapshot, "account"); state_.historyError = history_.error();
                }
                else { state_.error = answer.error; ++failures; }
            }
            int delay = failures ? std::min(900, preferences_.seconds * (1 << std::min(failures, 3u))) : preferences_.seconds;
            nextDue = std::chrono::steady_clock::now() + std::chrono::seconds(delay);
        }
        if (!stop_.load() && changed_) changed_();
    }
}
Options ParseOptions(int argc, char** argv) {
    Options next; next.preferences = Load(ConfigDirectory() / "settings.ini");
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&] { if (++i >= argc) throw std::runtime_error("Missing option value"); return std::string(argv[i]); };
        if (arg == "--home") next.preferences.home = std::filesystem::absolute(value()).lexically_normal().string();
        else if (arg == "--codex-binary") next.preferences.binary = std::filesystem::absolute(value()).string();
        else if (arg == "--host") next.preferences.host = value();
        else if (arg == "--interval") next.preferences.seconds = std::stoi(value());
        else if (arg == "--settings") next.settings = true;
        else if (arg == "--tray-only") next.preferences.overlay = false;
        else if (arg == "--top" || arg == "--left") { next.preferences.overlay = true; next.preferences.left = arg == "--left"; }
        else if (arg == "--smoke-test") { next.smoke = true; next.preferences.online = false; next.preferences.onlyWithCodex = false; }
        else if (arg == "--smoke-wayland") { next.smoke = true; next.smokeWayland = true; next.preferences.online = false; next.preferences.onlyWithCodex = true; next.preferences.overlay = true; }
        else throw std::runtime_error("Unknown option; use --settings, --home, --codex-binary, --host, --interval, --tray-only, --top or --left");
    }
    if (!Valid(next.preferences)) throw std::runtime_error("Invalid settings or refresh interval");
    return next;
}
}

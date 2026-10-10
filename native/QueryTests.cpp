#include "NativeCore.h"
#include <sys/wait.h>
#include <iostream>
#include <chrono>
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::atomic<bool> cancel{false};
    auto query = [&](const char* mode, int timeout = 1500) {
        setenv("QUOTA_FAKE_MODE", mode, 1);
        auto result = nativequota::Query(std::filesystem::absolute(argv[1]).string(), "/tmp/quota-synthetic-home", cancel, timeout);
        if (result.childPid) { int status; require(waitpid(result.childPid, &status, WNOHANG) == -1 && errno == ECHILD, "Query must reap its child"); }
        return result;
    };
    try {
        auto result = query("success");
        require(result.ok && result.snapshot.fiveHour.used == 20 && result.snapshot.weekly.used == 88, "Real stdio query must accept fragmented rate-limit response");
        require(result.snapshot.observed > 0, "Successful result must have observation time");
        result = query("error");
        require(!result.ok && result.error.find("private-account-text") == std::string::npos, "Server errors must not expose account text");
        require(!query("exit").ok, "Early child exit must be handled without SIGPIPE crash");
        require(!query("giant").ok, "Oversized reply must be bounded");
        auto begin = std::chrono::steady_clock::now();
        require(!query("timeout", 100).ok, "Timeout must cancel child");
        require(std::chrono::steady_clock::now() - begin < std::chrono::seconds(2), "Timeout cleanup must be prompt");
        std::thread trigger([&] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); cancel = true; });
        result = query("timeout", 1500); trigger.join();
        require(!result.ok, "Cancellation during a live query must clean up its child");
        cancel = true;
        require(!query("success").ok, "Pre-cancelled query must not succeed");
        unsetenv("QUOTA_FAKE_MODE");
        std::cout << "PASS: real stdio protocol, fragmented replies, early exit, errors, bounds, timeout and child cleanup\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}

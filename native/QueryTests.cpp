#include "NativeCore.h"
#include <sys/wait.h>
#include <iostream>
#include <chrono>
#include <fstream>
#include <unistd.h>
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    struct Fixture{
        std::filesystem::path home=std::filesystem::temp_directory_path()/("quota-query-"+std::to_string(getpid()));
        Fixture(){std::filesystem::create_directories(home);}
        ~Fixture(){std::error_code error;std::filesystem::remove_all(home,error);}
    } fixture;
    std::atomic<bool> cancel{false};
    auto query = [&](const char* mode, int timeout = 1500) {
        setenv("QUOTA_FAKE_MODE", mode, 1);
        auto result = nativequota::Query(std::filesystem::absolute(argv[1]).string(), fixture.home.string(), cancel, timeout);
        if (result.childPid) { int status; require(waitpid(result.childPid, &status, WNOHANG) == -1 && errno == ECHILD, "Query must reap its child"); }
        return result;
    };
    try {
        auto result = query("success");
        require(result.ok && result.snapshot.fiveHour.used == 20 && result.snapshot.weekly.used == 88, "Real stdio query must accept fragmented rate-limit response");
        require(result.snapshot.observed > 0, "Successful result must have observation time");
        for(auto name:{"HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","NO_PROXY","http_proxy","https_proxy","all_proxy","no_proxy"})setenv(name,"stale-parent-proxy",1);
        unsetenv("QUOTA_CONFIG_SECRET");
        {std::ofstream file(fixture.home/"config.toml");file<<"[shell_environment_policy.set]\nhttp_proxy='http://127.0.0.1:23456'\nHTTPS_PROXY='http://127.0.0.1:23456'\nALL_PROXY='socks5://127.0.0.1:23457'\nNO_PROXY=''\nQUOTA_CONFIG_SECRET='not-injected'\n";}
        setenv("QUOTA_EXPECT_HTTP_PROXY","http://127.0.0.1:23456",1);setenv("QUOTA_EXPECT_HTTPS_PROXY","http://127.0.0.1:23456",1);setenv("QUOTA_EXPECT_ALL_PROXY","socks5://127.0.0.1:23457",1);setenv("QUOTA_EXPECT_NO_PROXY","",1);
        require(query("proxy").ok,"Real query child must inherit configured proxies, clear old aliases, and exclude unrelated secrets");
        require(std::string(getenv("https_proxy"))=="stale-parent-proxy","Proxy configuration must not alter parent environment");
        {std::ofstream file(fixture.home/"config.toml");file<<"[shell_environment_policy.set]\nHTTPS_PROXY=\"private-proxy-password\n";}
        result=query("proxy");require(!result.ok&&!result.childPid&&result.error.find("private-proxy-password")==std::string::npos,"Invalid config must stop before spawning and redact credentials");
        std::filesystem::remove(fixture.home/"config.toml");
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
        std::cout << "PASS: real stdio protocol, proxy config/aliases/isolation, fragmented replies, errors, bounds, timeout and child cleanup\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}

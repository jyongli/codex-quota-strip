#include <iostream>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
int main() {
    const std::string mode = std::getenv("QUOTA_FAKE_MODE") ? std::getenv("QUOTA_FAKE_MODE") : "success";
    if (mode == "exit") return 0;
    if (mode == "timeout") { sleep(60); return 0; }
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.find("\"initialize\"") != std::string::npos) {
            std::cout << "{\"id\":1,\"result\":{}}\n" << std::flush;
        } else if (line.find("account/rateLimits/read") != std::string::npos) {
            if (mode == "error") std::cout << "{\"id\":2,\"error\":{\"message\":\"private-account-text\"}}\n" << std::flush;
            else if (mode == "giant") std::cout << std::string(60000, 'x') << '\n' << std::flush;
            else {
                std::cout << "{\"method\":\"notification\",\"params\":{}}\n{\"id\":2,\"result\":{\"rateLimits\":{\"limitId\":\"codex\",\"primary\":{\"usedPercent\":20,\"windowDurationMins\":300}," << std::flush;
                usleep(10000);
                std::cout << "\"secondary\":{\"usedPercent\":88,\"windowDurationMins\":10080}}}}\n" << std::flush;
            }
        }
    }
}

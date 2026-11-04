#include <iostream>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
int main() {
    const std::string mode = std::getenv("QUOTA_FAKE_MODE") ? std::getenv("QUOTA_FAKE_MODE") : "success";
    if(mode=="proxy"){
        // 配置代理必须覆盖两个大小写别名；不通过输出回传任何真实环境变量。
        for(auto name:{"HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","NO_PROXY","http_proxy","https_proxy","all_proxy","no_proxy"}){
            std::string key(name);for(char& c:key)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');
            auto expected=std::getenv(("QUOTA_EXPECT_"+key).c_str()),actual=std::getenv(name);
            if(std::string(expected?expected:"")!=std::string(actual?actual:""))return 12;
        }
        if(std::getenv("QUOTA_CONFIG_SECRET"))return 13;
    }
    if (mode == "exit") return 0;
    if (mode == "timeout") { sleep(60); return 0; }
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.find("\"initialize\"") != std::string::npos) {
            std::cout << "{\"id\":1,\"result\":{}}\n" << std::flush;
        } else if (line.find("account/rateLimits/read") != std::string::npos) {
            if (mode == "api-key" || mode == "external" || mode == "chatgpt-token" || mode == "signed-out" || mode == "legacy-account") std::cout << "{\"id\":2,\"error\":{\"message\":\"quota unavailable\"}}\n" << std::flush;
            else if (mode == "api-empty") std::cout << "{\"id\":2,\"result\":{\"rateLimits\":{\"limitId\":\"codex\"}}}\n" << std::flush;
            else if (mode == "error") std::cout << "{\"id\":2,\"error\":{\"message\":\"private-account-text\"}}\n" << std::flush;
            else if (mode == "giant") std::cout << std::string(60000, 'x') << '\n' << std::flush;
            else {
                std::cout << "{\"method\":\"notification\",\"params\":{}}\n{\"id\":2,\"result\":{\"rateLimits\":{\"limitId\":\"codex\",\"primary\":{\"usedPercent\":20,\"windowDurationMins\":300}," << std::flush;
                usleep(10000);
                std::cout << "\"secondary\":{\"usedPercent\":88,\"windowDurationMins\":10080}}}}\n" << std::flush;
            }
        } else if (line.find("account/read") != std::string::npos) {
            // 只允许同一 app-server 的只读模式查询，测试禁止刷新 token 或凭据读取。
            if (line.find("\"id\":3") == std::string::npos || line.find("\"refreshToken\":false") == std::string::npos) return 14;
            if (mode == "api-key" || mode == "api-empty") std::cout << "{\"id\":3,\"result\":{\"account\":{\"type\":\"apiKey\"},\"requiresOpenaiAuth\":true}}\n" << std::flush;
            else if (mode == "external") std::cout << "{\"id\":3,\"result\":{\"account\":null,\"requiresOpenaiAuth\":false}}\n" << std::flush;
            else if (mode == "chatgpt-token") std::cout << "{\"id\":3,\"result\":{\"account\":{\"type\":\"chatgpt\",\"email\":\"synthetic@example.invalid\",\"planType\":\"plus\"},\"requiresOpenaiAuth\":true}}\n" << std::flush;
            else if (mode == "signed-out") std::cout << "{\"id\":3,\"result\":{\"account\":null,\"requiresOpenaiAuth\":true}}\n" << std::flush;
            else std::cout << "{\"id\":3,\"error\":{\"message\":\"private-account-text\"}}\n" << std::flush;
        }
    }
}

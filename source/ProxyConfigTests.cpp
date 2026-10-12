#include "ProxyConfig.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
}
int main(){
    auto home=std::filesystem::temp_directory_path()/("quota-proxy-config-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(home);
    try{
        quota::ProxySettings values;
        auto write=[&](const std::string& text){std::ofstream out(home/"config.toml",std::ios::binary);out<<text;require(out.good(),"Fixture config must be writable");};
        require(quota::LoadCodexProxy(home,values)&&values.empty(),"Absent config must preserve environment fallback");
        write("\xEF\xBB\xBF[shell_environment_policy.set]\n'HTTPS_PROXY' = \"http://user:p\\\"ass@localhost:1234/#fragment\" # comment\nhttp_proxy = 'http://localhost:1234'\nNO_PROXY = ''\nQUOTA_SECRET='not-a-proxy'\n[features.network_proxy]\nproxy_url='http://localhost:9999'\n[ mcp_servers.example.env ]\nHTTPS_PROXY='unrelated'\n");
        require(quota::LoadCodexProxy(home,values)&&values.size()==3,"Only shell policy proxy fields may be selected");
        require(values["HTTPS_PROXY"]=="http://user:p\"ass@localhost:1234/#fragment"&&values["NO_PROXY"].empty(),"TOML quotes, comments, BOM and explicit clearing must retain their meaning");
        write("profile='office'\nshell_environment_policy.set={HTTPS_PROXY='global',NO_PROXY='localhost'}\n[profiles.office.shell_environment_policy.set]\nhttps_proxy='active'\n[profiles.other.shell_environment_policy.set]\nHTTPS_PROXY='inactive'\n");
        require(quota::LoadCodexProxy(home,values)&&values["HTTPS_PROXY"]=="active"&&values["NO_PROXY"]=="localhost","Only the selected profile may override global proxy settings");
        // 不展开环境变量或执行值中的命令；上游 CLI 自行决定支持哪些代理协议。
        write("[shell_environment_policy.set]\nHTTPS_PROXY='http://localhost/$PRIVATE_SECRET'\n");
        require(quota::LoadCodexProxy(home,values)&&values["HTTPS_PROXY"]=="http://localhost/$PRIVATE_SECRET","Proxy values must stay literal");
        for(const std::string& text:{"[shell_environment_policy.set]\nHTTPS_PROXY=123\n","[shell_environment_policy.set]\nHTTPS_PROXY='one'\nhttps_proxy='two'\n","[shell_environment_policy.set]\nHTTPS_PROXY=\"bad\\nline\"\n","[shell_environment_policy.set]\nHTTPS_PROXY=\"bad\\u0000value\"\n","[shell_environment_policy.set]\nHTTPS_PROXY=\"unterminated\n"}){
            write(text);require(!quota::LoadCodexProxy(home,values)&&values.empty(),"Invalid config must be rejected without partial proxy overrides");
        }
        write(std::string(512*1024+1,' '));require(!quota::LoadCodexProxy(home,values),"Config reads must be bounded");
        write("[shell_environment_policy.set]\nHTTPS_PROXY='"+std::string(4097,'a')+"'\n");require(!quota::LoadCodexProxy(home,values),"Proxy variables must be bounded");
        std::filesystem::remove_all(home);std::cout<<"PASS: proxy TOML scope, quoting, profiles, literals, invalid data and size bounds\n";return 0;
    }catch(const std::exception& error){std::filesystem::remove_all(home);std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}

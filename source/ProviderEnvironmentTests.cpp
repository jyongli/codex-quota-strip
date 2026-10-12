#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "QuotaProvider.h"

namespace {
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
std::wstring environment(const wchar_t* name){DWORD length=GetEnvironmentVariableW(name,nullptr,0);if(!length)return {};std::wstring value(length,L'\0');DWORD size=GetEnvironmentVariableW(name,value.data(),length);value.resize(size);return value;}
const wchar_t* keys[]{L"HTTP_PROXY",L"HTTPS_PROXY",L"ALL_PROXY",L"NO_PROXY"};
// 假 app-server 只验证自己的环境，返回合成额度，不连接网络也不访问账号。
int fakeServer(){
    for(auto key:keys){std::wstring expected=environment((L"QUOTA_EXPECT_"+std::wstring(key)).c_str());if(environment(key)!=expected)return 12;}
    if(!environment(L"QUOTA_CONFIG_SECRET").empty())return 13;
    if(environment(L"CODEX_HOME")!=environment(L"QUOTA_EXPECT_HOME"))return 14;
    std::string line;
    while(std::getline(std::cin,line)){
        if(line.find("\"initialize\"")!=std::string::npos)std::cout<<"{\"id\":1,\"result\":{}}\n"<<std::flush;
        else if(line.find("account/rateLimits/read")!=std::string::npos)std::cout<<"{\"id\":2,\"result\":{\"rateLimits\":{\"primary\":{\"usedPercent\":20,\"windowDurationMins\":300}}}}\n"<<std::flush;
    }
    return 0;
}
}
int wmain(int argc,wchar_t** argv){
    if(argc>1&&std::wstring(argv[1])==L"app-server")return fakeServer();
    wchar_t temporary[MAX_PATH]{};GetTempPathW(MAX_PATH,temporary);
    auto home=std::filesystem::path(temporary)/(L"quota-proxy-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(home);
    try{
        SetEnvironmentVariableW(L"CODEX_HOME",L"unrelated-parent-home");SetEnvironmentVariableW(L"QUOTA_EXPECT_HOME",home.c_str());
        for(auto key:keys){SetEnvironmentVariableW(key,L"parent-proxy");SetEnvironmentVariableW((L"QUOTA_EXPECT_"+std::wstring(key)).c_str(),L"parent-proxy");}
        SetEnvironmentVariableW(L"QUOTA_CONFIG_SECRET",nullptr);
        auto write=[&](const char* text){std::ofstream out(home/L"config.toml",std::ios::binary);out<<text;require(out.good(),"Synthetic config must be written");};
        auto query=[&]{auto result=quota::ReadAccountQuota(std::filesystem::absolute(argv[0]).wstring(),home.wstring(),nullptr,2500);require(result.ok,"Query child must receive configured Codex proxy variables");require(result.snapshot.fiveHour.used==20,"Synthetic quota response must survive proxy environment setup");HANDLE child=OpenProcess(SYNCHRONIZE,FALSE,result.childPid);if(child){require(WaitForSingleObject(child,0)==WAIT_OBJECT_0,"Query child must exit");CloseHandle(child);}require(environment(L"HTTPS_PROXY")==L"parent-proxy"&&environment(L"CODEX_HOME")==L"unrelated-parent-home","Query must not mutate the parent environment");};
        write("[shell_environment_policy.set]\nHTTP_PROXY='http://127.0.0.1:23456'\nHTTPS_PROXY=\"http://127.0.0.1:23456\"\nALL_PROXY='socks5://127.0.0.1:23457'\nNO_PROXY='localhost,127.0.0.1'\nQUOTA_CONFIG_SECRET='must-not-be-injected'\n");
        for(auto key:{L"HTTP_PROXY",L"HTTPS_PROXY"})SetEnvironmentVariableW((L"QUOTA_EXPECT_"+std::wstring(key)).c_str(),L"http://127.0.0.1:23456");
        SetEnvironmentVariableW(L"QUOTA_EXPECT_ALL_PROXY",L"socks5://127.0.0.1:23457");SetEnvironmentVariableW(L"QUOTA_EXPECT_NO_PROXY",L"localhost,127.0.0.1");
        query();
        // 下次查询重新读取配置；活跃 profile、大小写别名和显式清空均作用到真实子进程。
        write("profile='office'\nshell_environment_policy.set={HTTPS_PROXY='global-proxy'}\n[profiles.office.shell_environment_policy.set]\nhttps_proxy='http://127.0.0.1:23458/#literal'\nHTTP_PROXY=''\nALL_PROXY=''\nNO_PROXY=''\n");
        SetEnvironmentVariableW(L"QUOTA_EXPECT_HTTPS_PROXY",L"http://127.0.0.1:23458/#literal");
        for(auto key:{L"HTTP_PROXY",L"ALL_PROXY",L"NO_PROXY"})SetEnvironmentVariableW((L"QUOTA_EXPECT_"+std::wstring(key)).c_str(),nullptr);
        query();
        std::filesystem::remove(home/L"config.toml");
        for(auto key:keys)SetEnvironmentVariableW((L"QUOTA_EXPECT_"+std::wstring(key)).c_str(),L"parent-proxy");
        query();
        write("[shell_environment_policy.set]\nHTTPS_PROXY=\"private-proxy-password\n");
        auto invalid=quota::ReadAccountQuota(std::filesystem::absolute(argv[0]).wstring(),home.wstring(),nullptr,2500);
        require(!invalid.ok&&!invalid.childPid,"Invalid proxy config must stop before spawning instead of silently bypassing the proxy");
        require(invalid.error.find(L"private-proxy-password")==std::wstring::npos,"Configuration errors must not reveal proxy credentials");
        std::filesystem::remove_all(home);std::cout<<"PASS: child proxy inheritance, live config/profile changes, clearing, fallback, whitelist, isolation and cleanup\n";return 0;
    }catch(const std::exception& error){std::filesystem::remove_all(home);std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}

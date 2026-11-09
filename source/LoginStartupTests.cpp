#include "LoginStartup.h"
#include <shellapi.h>
#include <iostream>
#include <stdexcept>
#pragma comment(lib,"advapi32.lib")

static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}

int main(){
    // 真实注册表 API，所有读写限制在专用根下，不接触当前用户的登录启动项。
    const auto fixture=L"Software\\CodexQuotaStripTests\\LoginStartup-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    HKEY root{};
    if(RegCreateKeyExW(HKEY_CURRENT_USER,fixture.c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&root,nullptr)!=ERROR_SUCCESS)return 2;
    int result=0;
    try{
        const std::wstring exe=L"C:\\Example Dir\\CodexQuotaStrip.exe",home=L"C:\\My data\\";
        require(quota_startup::Read(root,exe)==quota_startup::State::Off,"A new user must default to disabled");
        require(quota_startup::Set(root,exe,home,true)==ERROR_SUCCESS,"Checking login startup must register a real command");
        require(quota_startup::Read(root,exe)==quota_startup::State::On,"Settings must reflect the registered startup entry");
        wchar_t value[261]{};DWORD bytes=sizeof(value);
        require(RegGetValueW(root,quota_startup::RunKey,quota_startup::RunValue,RRF_RT_REG_SZ,nullptr,value,&bytes)==ERROR_SUCCESS,"Login command must use a plain string value");
        int argc=0;auto argv=CommandLineToArgvW(value,&argc);
        bool command=argv&&argc==4&&exe==argv[0]&&std::wstring(argv[1])==L"--login-start"&&std::wstring(argv[2])==L"--home"&&home==argv[3];
        if(argv)LocalFree(argv);require(command,"Space and trailing slash must survive the login command");
        const auto other=L"C:\\Other\\CodexQuotaStrip.exe";
        require(quota_startup::Read(root,other)==quota_startup::State::Other,"Other installations must be distinguished");
        require(quota_startup::Set(root,other,home,true)!=ERROR_SUCCESS&&quota_startup::Set(root,other,home,false)!=ERROR_SUCCESS,"Another installation must not overwrite or remove the registered command");
        require(quota_startup::Read(root,exe)==quota_startup::State::On,"A foreign operation must leave the original entry intact");
        require(quota_startup::Set(root,exe,L"",false)==ERROR_SUCCESS&&quota_startup::Read(root,exe)==quota_startup::State::Off,"Unchecking must remove only this install's entry");
        require(quota_startup::Set(root,exe,L"",false)==ERROR_SUCCESS,"Repeated cleanup must be safe");
        require(quota_startup::Set(root,exe,L"C:\\"+std::wstring(260,L'x'),true)!=ERROR_SUCCESS,"Windows Run command length limit must be enforced");
        std::cout<<"PASS: opt-in/out, quoted login command, installation ownership, idempotent cleanup and Run length\n";
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';result=1;}
    RegCloseKey(root);RegDeleteTreeW(HKEY_CURRENT_USER,fixture.c_str());
    return result;
}

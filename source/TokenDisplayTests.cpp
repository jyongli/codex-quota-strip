#define NOMINMAX
#include <windows.h>
#include "FixtureProcess.h"
#include "QuotaCore.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
struct Target { DWORD pid=0; HWND controller=nullptr,settings=nullptr; };
BOOL CALLBACK find(HWND h,LPARAM p){auto& t=*reinterpret_cast<Target*>(p);DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid!=t.pid)return TRUE;wchar_t name[200]{};GetClassNameW(h,name,200);std::wstring cls(name);if(cls.find(L"QuotaController_")==0)t.controller=h;if(cls.find(L"QuotaSettings_")==0)t.settings=h;return TRUE;}
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
bool wait(const std::function<bool()>& ready){auto start=GetTickCount64();do{if(ready())return true;Sleep(25);}while(GetTickCount64()-start<10000);return false;}
quota::Json state(const std::filesystem::path& file){std::ifstream input(file);std::string text((std::istreambuf_iterator<char>(input)),{});try{return quota::JsonParser(text).parse();}catch(...){return {};}}
std::wstring caption(HWND h){wchar_t value[1024]{};GetWindowTextW(h,value,1024);return value;}
// 测试替身始终只处理合成账号摘要，不连接账号服务或读取真实认证。
int server(){
    wchar_t home[32768]{};GetEnvironmentVariableW(L"CODEX_HOME",home,32768);std::string line;
    while(std::getline(std::cin,line)){
        std::ifstream input(std::filesystem::path(home)/L"mode.txt");std::string mode;input>>mode;
        if(line.find("\"initialize\"")!=std::string::npos)std::cout<<"{\"id\":1,\"result\":{}}\n"<<std::flush;
        else if(line.find("account/rateLimits/read")!=std::string::npos){
            if(mode=="subscription")std::cout<<"{\"id\":2,\"result\":{\"rateLimits\":{\"primary\":{\"usedPercent\":20,\"windowDurationMins\":300},\"secondary\":{\"usedPercent\":30,\"windowDurationMins\":10080}}}}\n"<<std::flush;
            else std::cout<<"{\"id\":2,\"error\":{\"message\":\"synthetic-private-error\"}}\n"<<std::flush;
        }else if(line.find("account/read")!=std::string::npos){
            if(mode=="api")std::cout<<"{\"id\":3,\"result\":{\"account\":{\"type\":\"apiKey\"},\"requiresOpenaiAuth\":true}}\n"<<std::flush;
            else if(mode=="external")std::cout<<"{\"id\":3,\"result\":{\"account\":null,\"requiresOpenaiAuth\":false}}\n"<<std::flush;
            else std::cout<<"{\"id\":3,\"result\":{\"account\":null,\"requiresOpenaiAuth\":true}}\n"<<std::flush;
        }
    }return 0;
}
int wmain(int argc,wchar_t** argv){
    if(argc>1&&std::wstring(argv[1])==L"app-server")return server();
    if(argc>1&&std::wstring(argv[1])==L"--fixture-hold"){Sleep(INFINITE);return 0;}
    if(argc!=2)return 2;
    wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);auto root=std::filesystem::path(temp)/(L"quota-token-ui-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    auto binary=root/L"OpenAI/Codex/bin/codex.exe",status=root/L"status.json";
    std::filesystem::create_directories(binary.parent_path());std::filesystem::create_directories(root/L"sessions");std::filesystem::create_directories(root/L"CodexQuotaStrip");
    PROCESS_INFORMATION fixture{},app{};Target target;HWND original=GetForegroundWindow();
    auto clean=[&]{if(target.controller)SendMessageTimeoutW(target.controller,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,2000,nullptr);for(auto process:{app,fixture})if(process.hProcess){if(WaitForSingleObject(process.hProcess,2000)==WAIT_TIMEOUT)TerminateProcess(process.hProcess,0);WaitForSingleObject(process.hProcess,2000);CloseHandle(process.hThread);CloseHandle(process.hProcess);}if(IsWindow(original))SetForegroundWindow(original);std::error_code error;std::filesystem::remove_all(root,error);};
    try{
        std::filesystem::copy_file(std::filesystem::absolute(argv[0]),binary,std::filesystem::copy_options::overwrite_existing);
        {std::ofstream out(root/L"CodexQuotaStrip/settings.ini");out<<"[refresh]\nonline=1\nonly_with_codex=0\nseconds=3600\n";}
        auto mode=[&](const char* value){std::ofstream out(root/L"mode.txt");out<<value;};mode("api");
        STARTUPINFOW start{};start.cb=sizeof(start);std::wstring command=L"\""+binary.wstring()+L"\" --fixture-hold";
        require(CreateQuotaFixtureProcess(command,root,start,fixture)!=FALSE,"Synthetic Codex binary must launch");
        command=L"\""+std::filesystem::absolute(argv[1]).wstring()+L"\" --home \""+root.wstring()+L"\" --status \""+status.wstring()+L"\" --settings";
        require(CreateQuotaFixtureProcess(command,root,start,app)!=FALSE,"Token UI fixture must launch");target.pid=app.dwProcessId;
        require(wait([&]{EnumWindows(find,reinterpret_cast<LPARAM>(&target));return target.settings&&state(status).get("account_mode").str=="api-key";}),"API login must expose explicit mode in the real controller");
        auto api=state(status);require(api.get("tray_icons").num==1&&api.get("tray_values").elements[0].str=="API","API mode must replace both empty quota icons with one legible API icon");
        require(caption(GetDlgItem(target.settings,211))==L"API / Token"&&!IsWindowVisible(GetDlgItem(target.settings,212)),"Settings must replace empty quota cards with an API / Token heading");
        require(caption(GetDlgItem(target.settings,206)).find(L"Token")!=std::wstring::npos,"Settings must explain where real Token usage can be viewed");
        // 同一进程重新查询，验证模式恢复不是仅启动时替换一次标签。
        mode("subscription");SendMessageW(target.controller,WM_COMMAND,3,0);
        require(wait([&]{auto current=state(status);return current.get("account_mode").str=="subscription"&&current.get("tray_icons").num==2&&current.get("weekly_remaining").num==70;}),"Subscription login must restore real quotas and both tray metrics");
        require(IsWindowVisible(GetDlgItem(target.settings,212))&&caption(GetDlgItem(target.settings,211))==L"80%","Subscription settings must restore quota values");
        mode("external");SendMessageW(target.controller,WM_COMMAND,3,0);
        require(wait([&]{auto current=state(status);return current.get("account_mode").str=="external-provider"&&current.get("tray_icons").num==1&&current.get("weekly_remaining").kind==quota::Json::Null;}),"External Token mode must clear previous subscription values");
        mode("signed-out");SendMessageW(target.controller,WM_COMMAND,3,0);
        require(wait([&]{return state(status).get("account_mode").str=="signed-out";}),"Logout must clear the API display state");
        std::cout<<"PASS: API / Token settings, single visible tray icon, subscription restoration and logout\n";clean();return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';clean();return 1;}
}

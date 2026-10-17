#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include "QuotaCore.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <chrono>

namespace {
struct Target { DWORD pid=0; HWND control=nullptr,settings=nullptr,history=nullptr; };
BOOL CALLBACK Find(HWND h,LPARAM pointer){
    auto& target=*reinterpret_cast<Target*>(pointer);DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid!=target.pid)return TRUE;
    wchar_t name[200]{};GetClassNameW(h,name,200);std::wstring type=name;
    if(type.find(L"QuotaController_")==0)target.control=h;
    if(type.find(L"QuotaSettings_")==0)target.settings=h;
    if(type.find(L"QuotaHistory_")==0)target.history=h;
    return TRUE;
}
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool Wait(const std::function<bool()>& ready){auto deadline=GetTickCount64()+5000;do{if(ready())return true;Sleep(20);}while(GetTickCount64()<deadline);return false;}
std::wstring Caption(HWND h){wchar_t text[1024]{};GetWindowTextW(h,text,1024);return text;}
quota::Json State(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);std::string text((std::istreambuf_iterator<char>(in)),{});try{return quota::JsonParser(text).parse();}catch(...){return {};}}
// 只产生真实 token_count 结构的合成额度，不写入认证或会话正文。
void Append(const std::filesystem::path& file,int fiveUsed,int weeklyUsed,long long observed){
    auto seconds=static_cast<time_t>(observed/1000);static const long long resetBase=seconds;std::tm utc{};gmtime_s(&utc,&seconds);
    char timestamp[64]{};sprintf_s(timestamp,"%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",utc.tm_year+1900,utc.tm_mon+1,utc.tm_mday,utc.tm_hour,utc.tm_min,utc.tm_sec,observed%1000);
    std::ofstream out(file,std::ios::app|std::ios::binary);
    out<<"{\"timestamp\":\""<<timestamp<<"\",\"type\":\"event_msg\",\"payload\":{\"type\":\"token_count\",\"rate_limits\":{\"primary\":{\"used_percent\":"<<fiveUsed<<",\"window_minutes\":300,\"resets_at\":"<<resetBase+3600<<"},\"secondary\":{\"used_percent\":"<<weeklyUsed<<",\"window_minutes\":10080,\"resets_at\":"<<resetBase+604800<<"}}}}\n";
}
}
int wmain(int argc,wchar_t** argv){
    if(argc<2)return 2;
    wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);
    auto root=std::filesystem::path(temp)/(L"quota-history-ui-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    auto settings=root/L"CodexQuotaStrip"/L"settings.ini",status=root/L"status.json",log=root/L"sessions"/L"synthetic.jsonl";
    std::filesystem::create_directories(settings.parent_path());std::filesystem::create_directories(log.parent_path());
    {std::ofstream out(settings);out<<"[refresh]\nonline=0\n[history]\nenabled=1\n";}
    PROCESS_INFORMATION process{};Target target{};HWND original=GetForegroundWindow();
    try{
        wchar_t previous[32768]{};GetEnvironmentVariableW(L"LOCALAPPDATA",previous,32768);SetEnvironmentVariableW(L"LOCALAPPDATA",root.c_str());
        auto command=L"\""+std::filesystem::absolute(argv[1]).wstring()+L"\" --local-only --home \""+root.wstring()+L"\" --status \""+status.wstring()+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        BOOL created=CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process);SetEnvironmentVariableW(L"LOCALAPPDATA",previous);Check(created!=FALSE,"History fixture must start");target.pid=process.dwProcessId;
        Check(Wait([&]{EnumWindows(Find,reinterpret_cast<LPARAM>(&target));return target.control&&State(status).get("watching").num==1;}),"History fixture watcher must initialize");
        SendMessageTimeoutW(target.control,WM_COMMAND,8,0,SMTO_ABORTIFHUNG,2000,nullptr);
        Check(Wait([&]{EnumWindows(Find,reinterpret_cast<LPARAM>(&target));return target.history&&IsWindowVisible(target.history);}),"History command must open a real native history window");
        Check(GetDlgItem(target.history,401)&&GetDlgItem(target.history,402)&&GetDlgItem(target.history,403),"History must expose list, chart toggle and refresh controls");
        std::cout<<"PASS: native history entry point\n";
        SendMessageW(target.control,WM_COMMAND,7,0);EnumWindows(Find,reinterpret_cast<LPARAM>(&target));Check(target.settings!=nullptr,"Settings must open alongside history");
        Check(GetDlgItem(target.settings,209)&&GetDlgItem(target.settings,210)&&GetDlgItem(target.settings,304)&&GetDlgItem(target.settings,305),"History settings must expose enable, directory, browse and viewer");
        // 保存相对目录应失败，不能让后续写入落入不可预测的工作目录。
        SendMessageW(GetDlgItem(target.settings,210),WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"relative-history"));SendMessageW(target.settings,WM_COMMAND,302,0);
        auto validation=Caption(GetDlgItem(target.settings,207));char validationUtf8[4096]{};WideCharToMultiByte(CP_UTF8,0,validation.c_str(),-1,validationUtf8,4096,nullptr,nullptr);
        std::cout<<"Directory validation message: "<<validationUtf8<<"; enabled="<<SendMessageW(GetDlgItem(target.settings,209),BM_GETCHECK,0,0)<<"; interval length="<<Caption(GetDlgItem(target.settings,202)).size()<<"\n";
        Check(Caption(GetDlgItem(target.settings,207)).find(L"目录")!=std::wstring::npos,"Relative history directory must be rejected with a specific error");
        auto directory=root/L"custom-history";SendMessageW(GetDlgItem(target.settings,210),WM_SETTEXT,0,reinterpret_cast<LPARAM>(directory.c_str()));
        SendMessageW(GetDlgItem(target.settings,208),CB_SETCURSEL,1,0);SendMessageW(target.settings,WM_COMMAND,302,0);
        Check(Caption(GetDlgItem(target.settings,305))==L"View quota changes","History controls must translate live to English");
        wchar_t saved[32768]{};GetPrivateProfileStringW(L"history",L"directory",L"",saved,32768,settings.c_str());Check(saved==directory.wstring(),"Chosen history directory must persist");
        auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();Append(log,20,40,stamp);Check(Wait([&]{return State(status).get("weekly_remaining").num==60;}),"Local baseline must reach the displayed quota");
        SendMessageW(target.history,WM_COMMAND,403,0);
        HWND list=GetDlgItem(target.history,401);Check(SendMessageW(list,LVM_GETITEMCOUNT,0,0)==1,"History list must include accepted baseline");
        // 绘制、手动刷新和无变化新事件均不应重复生成 CSV 行。
        Append(log,20,40,stamp+10);Check(Wait([&]{return State(status).get("observed_ms").num>=stamp+10;}),"Unchanged new snapshot must be accepted");
        SendMessageW(target.history,WM_COMMAND,403,0);Check(SendMessageW(list,LVM_GETITEMCOUNT,0,0)==1,"Unchanged quota and refresh must not add history rows");
        Append(log,24,43,stamp+20);Check(Wait([&]{return State(status).get("weekly_remaining").num==57;}),"Changed quota must reach display");
        // 查看器只在打开或手动刷新时读盘，后台额度事件不能刷新列表。
        Check(SendMessageW(list,LVM_GETITEMCOUNT,0,0)==1,"Open history must not poll or refresh on quota events");
        SendMessageW(target.history,WM_COMMAND,403,0);Check(SendMessageW(list,LVM_GETITEMCOUNT,0,0)==2,"Manual history refresh must read the new change");
        SendMessageW(target.history,WM_COMMAND,402,0);Check(IsWindowVisible(GetDlgItem(target.history,405))&&!IsWindowVisible(list),"Chart mode must expose the native GDI chart");
        SendMessageW(target.history,WM_COMMAND,402,0);Check(IsWindowVisible(list),"List mode must remain available");
        SendMessageW(GetDlgItem(target.settings,209),BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(target.settings,WM_COMMAND,302,0);
        Append(log,30,50,stamp+30);Check(Wait([&]{return State(status).get("weekly_remaining").num==50;}),"Disabled logging must not stop quota updates");
        SendMessageW(target.history,WM_COMMAND,403,0);Check(SendMessageW(list,LVM_GETITEMCOUNT,0,0)==2,"Disabled logging must preserve existing rows without writing new ones");
        SendMessageW(target.settings,WM_CLOSE,0,0);SendMessageW(target.history,WM_COMMAND,402,0);Check(IsWindow(target.history),"Closing settings must preserve history resources");
        SendMessageW(target.history,WM_CLOSE,0,0);Check(WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT,"Closing history must preserve the quota companion");
        SendMessageTimeoutW(target.control,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,2000,nullptr);Check(WaitForSingleObject(process.hProcess,5000)==WAIT_OBJECT_0,"History fixture must exit cleanly");
        CloseHandle(process.hThread);CloseHandle(process.hProcess);if(IsWindow(original))SetForegroundWindow(original);
        std::wcout<<L"PASS: history persistence, deduplication, manual list/chart, language, disabled capture and window lifetimes; fixture "<<root.wstring()<<L"\n";return 0;
    }catch(const std::exception& error){
        std::cerr<<"FAIL: "<<error.what()<<"\n";
        if(target.control)SendMessageTimeoutW(target.control,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,2000,nullptr);
        if(process.hProcess){if(WaitForSingleObject(process.hProcess,5000)!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,1);CloseHandle(process.hThread);CloseHandle(process.hProcess);}if(IsWindow(original))SetForegroundWindow(original);return 1;
    }
}

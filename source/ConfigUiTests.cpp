#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "QuotaCore.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <functional>
struct Target{DWORD pid;HWND control=nullptr,settings=nullptr;};
BOOL CALLBACK find(HWND h,LPARAM p){auto& t=*reinterpret_cast<Target*>(p);DWORD id=0;GetWindowThreadProcessId(h,&id);if(id!=t.pid)return TRUE;wchar_t cls[200]{};GetClassNameW(h,cls,200);std::wstring s=cls;if(s.find(L"QuotaController_")==0)t.control=h;else if(!t.control&&s.find(L"CodexQuotaStrip_")==0)t.control=h;if(s.find(L"QuotaSettings_")==0)t.settings=h;return TRUE;}
void check(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
bool wait(const std::function<bool()>& f){auto started=GetTickCount64();do{if(f())return true;Sleep(20);}while(GetTickCount64()-started<2500);return false;}
quota::Json state(const std::filesystem::path& f){std::ifstream in(f,std::ios::binary);std::string s((std::istreambuf_iterator<char>(in)),{});try{return quota::JsonParser(s).parse();}catch(...){return {};}}
int wmain(int argc,wchar_t** argv){
    if(argc<2)return 2;wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);auto root=std::filesystem::path(temp)/(L"quota-config-ui-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(root/L"sessions");std::filesystem::create_directories(root/L"CodexQuotaStrip");
    {std::ofstream out(root/L"CodexQuotaStrip"/L"settings.ini");out<<"[refresh]\nonline=0\nseconds=120\n";}
    PROCESS_INFORMATION process{};Target t{};HWND original=GetForegroundWindow();
    try{
        wchar_t previous[32768]{};GetEnvironmentVariableW(L"LOCALAPPDATA",previous,32768);SetEnvironmentVariableW(L"LOCALAPPDATA",root.c_str());
        std::wstring command=L"\""+std::filesystem::absolute(argv[1]).wstring()+L"\" --home \""+root.wstring()+L"\" --status \""+(root/L"status.json").wstring()+L"\"";
        STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESHOWWINDOW;start.wShowWindow=SW_HIDE;BOOL ok=CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&process);SetEnvironmentVariableW(L"LOCALAPPDATA",previous);check(ok!=FALSE,"Config fixture must start");t.pid=process.dwProcessId;
        check(wait([&]{EnumWindows(find,reinterpret_cast<LPARAM>(&t));return t.control!=nullptr;}),"Fixture control window must exist");
        SendMessageTimeoutW(t.control,WM_COMMAND,7,0,SMTO_ABORTIFHUNG,2000,nullptr);
        check(wait([&]{EnumWindows(find,reinterpret_cast<LPARAM>(&t));return t.settings&&IsWindowVisible(t.settings);}),"Settings command must open a real native configuration window");
        HWND toggle=GetDlgItem(t.settings,201);LRESULT before=SendMessageW(toggle,BM_GETCHECK,0,0);
        SendMessageW(toggle,WM_KEYDOWN,VK_SPACE,0);SendMessageW(toggle,WM_KEYUP,VK_SPACE,0);
        check(SendMessageW(toggle,BM_GETCHECK,0,0)!=before,"Styled switches must retain native Space-key behavior");SendMessageW(toggle,BM_SETCHECK,before,0);
        HWND combo=GetDlgItem(t.settings,204);SendMessageW(combo,CB_SHOWDROPDOWN,TRUE,0);check(SendMessageW(combo,CB_GETDROPPEDSTATE,0,0)!=0,"Styled display selector must still open its native dropdown");SendMessageW(combo,CB_SHOWDROPDOWN,FALSE,0);
        SendMessageW(GetDlgItem(t.settings,202),WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"45"));SendMessageW(GetDlgItem(t.settings,205),WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"2"));
        SendMessageW(GetDlgItem(t.settings,204),CB_SETCURSEL,1,0);SendMessageW(GetDlgItem(t.settings,203),BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(t.settings,WM_COMMAND,302,0);
        check(wait([&]{auto s=state(root/L"status.json");return s.get("refresh_seconds").num==45&&s.get("stale_minutes").num==2&&s.get("show_in_codex").num==1;}),"Apply must immediately change interval, stale threshold and display mode");
        check(GetPrivateProfileIntW(L"refresh",L"seconds",0,(root/L"CodexQuotaStrip"/L"settings.ini").c_str())==45,"Settings must persist for restart");
        SendMessageW(GetDlgItem(t.settings,202),WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"0"));SendMessageW(t.settings,WM_COMMAND,302,0);Sleep(50);check(state(root/L"status.json").get("refresh_seconds").num==45,"Invalid intervals must leave the active configuration unchanged");
        SendMessageW(t.settings,WM_COMMAND,303,0);check(!IsWindow(t.settings),"Close button must release the configuration window");check(WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT,"Closing settings must preserve the quota process");
        std::cout<<"PASS: native configuration apply, persistence, validation and close lifecycle\n";
        SendMessageTimeoutW(t.control,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,2000,nullptr);check(WaitForSingleObject(process.hProcess,3000)==WAIT_OBJECT_0,"Fixture must exit");CloseHandle(process.hThread);CloseHandle(process.hProcess);if(IsWindow(original))SetForegroundWindow(original);return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";if(t.control)SendMessageTimeoutW(t.control,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,1000,nullptr);if(process.hProcess){WaitForSingleObject(process.hProcess,3000);CloseHandle(process.hThread);CloseHandle(process.hProcess);}if(IsWindow(original))SetForegroundWindow(original);return 1;}
}

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "FixtureProcess.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <functional>

struct Target { DWORD pid; HWND window=nullptr; };
BOOL CALLBACK find(HWND h,LPARAM p){auto& t=*reinterpret_cast<Target*>(p);DWORD id=0;GetWindowThreadProcessId(h,&id);wchar_t cls[128]{};GetClassNameW(h,cls,128);if(id==t.pid&&std::wstring(cls).find(L"CodexQuotaStrip_")==0){t.window=h;return FALSE;}return TRUE;}
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool wait(const std::function<bool()>& condition){auto start=GetTickCount64();do{if(condition())return true;Sleep(20);}while(GetTickCount64()-start<2500);return false;}
bool front(HWND h){DWORD from=GetWindowThreadProcessId(GetForegroundWindow(),nullptr),self=GetCurrentThreadId();bool attached=from&&from!=self&&AttachThreadInput(self,from,TRUE);BOOL result=SetForegroundWindow(h);if(attached)AttachThreadInput(self,from,FALSE);return result&&GetAncestor(GetForegroundWindow(),GA_ROOT)==h;}
bool unobscured(HWND h){RECT r{};GetWindowRect(h,&r);POINT p{(r.left+r.right)/2,(r.top+r.bottom)/2};return IsWindowVisible(h)&&GetAncestor(WindowFromPoint(p),GA_ROOT)==h;}
int wmain(int argc,wchar_t** argv){
    if(argc<2)return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND original=GetAncestor(GetForegroundWindow(),GA_ROOT),codex=argc>2?reinterpret_cast<HWND>(std::stoull(argv[2])):original,other=nullptr;DWORD codexPid=0;GetWindowThreadProcessId(codex,&codexPid);
    HANDLE main=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,codexPid);wchar_t image[32768]{};DWORD count=32768;
    bool hostOk=main&&QueryFullProcessImageNameW(main,0,image,&count)&&std::wstring(image).find(L"OpenAI.Codex_")!=std::wstring::npos;if(main)CloseHandle(main);
    if(!hostOk){std::cerr<<"FAIL: run the layer regression while Codex is foreground\n";return 1;}
    if(!front(codex)){std::cerr<<"FAIL: Codex could not become foreground for the layer regression\n";return 1;}
    wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);auto root=std::filesystem::path(temp)/(L"quota-layer-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(root/L"sessions");
    PROCESS_INFORMATION process{};Target target{};
    try{
        std::wstring command=L"\""+std::filesystem::absolute(argv[1]).wstring()+L"\" --local-only --taskbar-and-codex --top --home \""+root.wstring()+L"\"";
        STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESHOWWINDOW;start.wShowWindow=SW_HIDE;
        check(CreateQuotaFixtureProcess(command,root,start,process)!=FALSE,"Fixture must start");target.pid=process.dwProcessId;
        check(wait([&]{EnumWindows(find,reinterpret_cast<LPARAM>(&target));return target.window!=nullptr;}),"Fixture must expose its window");
        check(wait([&]{return unobscured(target.window);}),"Quota bar must be the actual top window at its screen position while Codex is foreground");
        std::cout<<"PASS: quota bar is visibly above the real Codex window\n";
        other=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Quota layer test",WS_OVERLAPPEDWINDOW,32,64,240,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(other!=nullptr,"Other test window must exist");ShowWindow(other,SW_SHOW);check(front(other),"Other test window must become foreground");
        bool hidden=wait([&]{return !IsWindowVisible(target.window)&&!(GetWindowLongPtrW(target.window,GWL_EXSTYLE)&WS_EX_TOPMOST);});
        if(!hidden)std::cerr<<"State: visible="<<IsWindowVisible(target.window)<<" topmost="<<((GetWindowLongPtrW(target.window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0)<<" foreground="<<reinterpret_cast<UINT_PTR>(GetForegroundWindow())<<" other="<<reinterpret_cast<UINT_PTR>(other)<<" codex="<<reinterpret_cast<UINT_PTR>(codex)<<"\n";
        check(hidden,"Other applications must have no overlay and no retained topmost quota window");
        std::cout<<"PASS: switching apps hides the bar and clears topmost\n";
        check(front(codex),"Codex must regain foreground");check(wait([&]{return unobscured(target.window);}),"Returning to Codex must restore the actual visible bar");
        std::cout<<"PASS: returning to Codex restores unobscured quota display\n";
        SendMessageTimeoutW(target.window,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,2000,nullptr);check(WaitForSingleObject(process.hProcess,3000)==WAIT_OBJECT_0,"Fixture must exit");CloseHandle(process.hThread);CloseHandle(process.hProcess);DestroyWindow(other);if(IsWindow(original))front(original);return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";if(target.window)SendMessageTimeoutW(target.window,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,1000,nullptr);if(process.hProcess){WaitForSingleObject(process.hProcess,3000);CloseHandle(process.hThread);CloseHandle(process.hProcess);}if(other)DestroyWindow(other);if(IsWindow(original))front(original);return 1;}
}

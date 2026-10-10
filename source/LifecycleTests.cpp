#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <functional>
#include <stdexcept>
#include <string>
struct Target{DWORD pid;HWND control=nullptr,strip=nullptr;};
BOOL CALLBACK find(HWND h,LPARAM p){auto& t=*reinterpret_cast<Target*>(p);DWORD id=0;GetWindowThreadProcessId(h,&id);if(id!=t.pid)return TRUE;wchar_t cls[180]{};GetClassNameW(h,cls,180);std::wstring s=cls;if(s.find(L"QuotaController_")==0)t.control=h;if(s.find(L"CodexQuotaStrip_")==0)t.strip=h;return TRUE;}
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
bool wait(const std::function<bool()>& condition){auto start=GetTickCount64();do{if(condition())return true;MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(20);}while(GetTickCount64()-start<4000);return false;}
bool front(HWND h){DWORD from=GetWindowThreadProcessId(GetForegroundWindow(),nullptr),self=GetCurrentThreadId();bool attached=from&&from!=self&&AttachThreadInput(self,from,TRUE);BOOL ok=SetForegroundWindow(h);if(attached)AttachThreadInput(self,from,FALSE);return ok!=FALSE;}
HWND newHost(){HWND h=CreateWindowExW(0,L"QuotaLifecycleHost",L"Quota host lifecycle fixture",WS_OVERLAPPEDWINDOW,200,160,900,620,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);ShowWindow(h,SW_SHOW);front(h);return h;}
int wmain(int argc,wchar_t** argv){
    if(argc<2)return 2;HWND original=GetForegroundWindow(),host=nullptr;PROCESS_INFORMATION process{};Target t{};
    // 使用正常顶层窗口类；STATIC 顶层窗口会被 WindowFromPoint 当作静态文字跳过。
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=DefWindowProcW;cls.lpszClassName=L"QuotaLifecycleHost";cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
    wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);auto root=std::filesystem::path(temp)/(L"quota-life-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(root/L"sessions");
    try{
        host=newHost();check(host!=nullptr,"Fake host must exist");
        std::wstring cmd=L"\""+std::filesystem::absolute(argv[1]).wstring()+L"\" --local-only --taskbar-and-codex --top --host-pid "+std::to_wstring(GetCurrentProcessId())+L" --home \""+root.wstring()+L"\"";
        STARTUPINFOW start{};start.cb=sizeof(start);check(CreateProcessW(nullptr,cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&process)!=FALSE,"Helper must start");t.pid=process.dwProcessId;
        check(wait([&]{EnumWindows(find,reinterpret_cast<LPARAM>(&t));return t.strip!=nullptr;}),"Visible strip fixture must exist");
        // 旧版无独立控制器：在同一原生所属窗口边界复现其被主窗口连带销毁的问题。
        if(!t.control){t.control=t.strip;SetWindowLongPtrW(t.strip,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(host));}
        else check(wait([&]{return IsWindowVisible(t.strip)&&GetWindow(t.strip,GW_OWNER)==host;}),"Visible strip must attach to test host");
        HWND stable=t.control;DestroyWindow(host);host=nullptr;
        check(wait([&]{return WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT&&IsWindow(stable)&&!IsWindowVisible(t.strip);}),"Destroying the host must preserve the hidden quota controller and process");
        std::cout<<"PASS: host destruction preserves the stable controller\n";
        host=newHost();check(host!=nullptr,"Replacement host must exist");
        // 合成主窗口只验证重建与所属关系；实际屏幕层级由 LayerTests 在真实 Codex 上验证。
        bool restored=wait([&]{t.strip=nullptr;EnumWindows(find,reinterpret_cast<LPARAM>(&t));return t.strip&&IsWindowVisible(t.strip)&&GetWindow(t.strip,GW_OWNER)==host;});
        if(!restored){RECT r{};GetWindowRect(t.strip,&r);POINT p{(r.left+r.right)/2,(r.top+r.bottom)/2};std::cerr<<"Diagnostic: strip="<<reinterpret_cast<UINT_PTR>(t.strip)<<" visible="<<IsWindowVisible(t.strip)<<" owner="<<reinterpret_cast<UINT_PTR>(GetWindow(t.strip,GW_OWNER))<<" host="<<reinterpret_cast<UINT_PTR>(host)<<" foreground="<<reinterpret_cast<UINT_PTR>(GetForegroundWindow())<<" hit="<<reinterpret_cast<UINT_PTR>(GetAncestor(WindowFromPoint(p),GA_ROOT))<<" rect="<<r.left<<","<<r.top<<","<<r.right<<","<<r.bottom<<"\n";}
        check(restored,"A replacement host must recover strip visibility and ownership");
        check(t.control==stable,"Recovery must reuse the original controller");std::cout<<"PASS: replacement host recreates the strip and attaches it without restarting quota process\n";
        SendMessageTimeoutW(t.control,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,2000,nullptr);check(wait([&]{return WaitForSingleObject(process.hProcess,0)==WAIT_OBJECT_0;}),"Quota helper must exit cleanly");DestroyWindow(host);host=nullptr;CloseHandle(process.hThread);CloseHandle(process.hProcess);if(IsWindow(original))front(original);return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";if(t.control)SendMessageTimeoutW(t.control,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,1000,nullptr);if(process.hProcess){WaitForSingleObject(process.hProcess,3000);CloseHandle(process.hThread);CloseHandle(process.hProcess);}if(host)DestroyWindow(host);if(IsWindow(original))front(original);return 1;}
}

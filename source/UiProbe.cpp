#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <string>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <shellapi.h>
#include <cwctype>
#include "TrayIcons.h"

struct Target { DWORD pid; HWND handle = nullptr;bool config=false; };
BOOL CALLBACK find(HWND h, LPARAM p) { auto& t = *reinterpret_cast<Target*>(p); DWORD pid = 0; GetWindowThreadProcessId(h, &pid); wchar_t cls[180]; GetClassNameW(h, cls, 180); if (pid == t.pid && std::wstring(cls).find(t.config?L"QuotaSettings_":L"CodexQuotaStrip_") == 0) { t.handle = h; return FALSE; } return TRUE; }
int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(argc>=4&&std::wstring(argv[1])==L"--config-dpi"){
        Target t{static_cast<DWORD>(std::stoul(argv[2]))};t.config=true;EnumWindows(find,reinterpret_cast<LPARAM>(&t));if(!t.handle)return 1;
        UINT d=static_cast<UINT>(std::stoul(argv[3]));RECT r{24,12,24+MulDiv(620,d,96),12+MulDiv(640,d,96)};RECT frame{0,0,r.right-r.left,r.bottom-r.top};AdjustWindowRectExForDpi(&frame,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,0,d);r.right=r.left+frame.right-frame.left;r.bottom=r.top+frame.bottom-frame.top;
        SendMessageTimeoutW(t.handle,WM_DPICHANGED,MAKEWPARAM(d,d),reinterpret_cast<LPARAM>(&r),SMTO_ABORTIFHUNG,2000,nullptr);
        RECT client{};GetClientRect(t.handle,&client);bool clipped=false;for(int id:{201,202,203,204,205,206,207,211,212,301,302,303}){RECT child{};GetWindowRect(GetDlgItem(t.handle,id),&child);MapWindowPoints(nullptr,t.handle,reinterpret_cast<POINT*>(&child),2);if(child.left<0||child.top<0||child.right>client.right||child.bottom>client.bottom)clipped=true;}
        std::cout<<"Simulated DPI="<<d<<"; client="<<client.right<<"x"<<client.bottom<<"; clipped="<<clipped<<"\n";return clipped?1:0;
    }
    if(std::wstring(argv[1])==L"--config-values"){
        Target t{static_cast<DWORD>(std::stoul(argv[2]))};t.config=true;EnumWindows(find,reinterpret_cast<LPARAM>(&t));if(!t.handle)return 1;
        BOOL valid=FALSE;UINT seconds=GetDlgItemInt(t.handle,202,&valid,FALSE);UINT stale=GetDlgItemInt(t.handle,205,&valid,FALSE);
        std::cout<<"{\"online\":"<<(SendMessageW(GetDlgItem(t.handle,201),BM_GETCHECK,0,0)==BST_CHECKED?"true":"false")<<",\"onlyWithCodex\":"<<(SendMessageW(GetDlgItem(t.handle,203),BM_GETCHECK,0,0)==BST_CHECKED?"true":"false")<<",\"seconds\":"<<seconds<<",\"staleMinutes\":"<<stale<<",\"surface\":"<<SendMessageW(GetDlgItem(t.handle,204),CB_GETCURSEL,0,0)<<"}\n";return 0;
    }
    if(std::wstring(argv[1])==L"--close-settings"){
        Target t{static_cast<DWORD>(std::stoul(argv[2]))};t.config=true;EnumWindows(find,reinterpret_cast<LPARAM>(&t));if(!t.handle)return 1;return SendMessageTimeoutW(t.handle,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,2000,nullptr)?0:1;
    }
    if (std::wstring(argv[1]) == L"--activate") {
        HWND host=reinterpret_cast<HWND>(std::stoull(argv[2]));DWORD source=GetWindowThreadProcessId(GetForegroundWindow(),nullptr),self=GetCurrentThreadId();
        bool attached=source&&source!=self&&AttachThreadInput(self,source,TRUE);BOOL result=SetForegroundWindow(host);
        if(attached)AttachThreadInput(self,source,FALSE);return result?0:1;
    }
    if (std::wstring(argv[1]) == L"--raise") {
        Target t{static_cast<DWORD>(std::stoul(argv[2]))};EnumWindows(find,reinterpret_cast<LPARAM>(&t));if(!t.handle)return 1;
        SetLastError(0);BOOL result=SetWindowPos(t.handle,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);DWORD error=GetLastError();
        std::cout<<"Raise result="<<result<<" error="<<error<<" topmost="<<((GetWindowLongPtrW(t.handle,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0)<<"\n";return result?0:1;
    }
    if (argc>=4 && std::wstring(argv[1]) == L"--owner") {
        Target t{static_cast<DWORD>(std::stoul(argv[2]))};EnumWindows(find,reinterpret_cast<LPARAM>(&t));if(!t.handle)return 1;
        SetLastError(0);SetWindowLongPtrW(t.handle,GWLP_HWNDPARENT,static_cast<LONG_PTR>(std::stoull(argv[3])));DWORD error=GetLastError();
        SetWindowPos(t.handle,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        std::cout<<"Owner error="<<error<<" actual="<<reinterpret_cast<UINT_PTR>(GetWindow(t.handle,GW_OWNER))<<"\n";return error?1:0;
    }
    if (argc >= 4 && std::wstring(argv[1]) == L"--command") {
        Target t{static_cast<DWORD>(std::stoul(argv[2]))}; EnumWindows(find, reinterpret_cast<LPARAM>(&t));
        if (!t.handle) return 1;
        DWORD_PTR result = 0;
        return SendMessageTimeoutW(t.handle, WM_COMMAND, static_cast<WPARAM>(std::stoul(argv[3])), 0, SMTO_ABORTIFHUNG, 2000, &result) ? 0 : 1;
    }
    if (std::wstring(argv[1]) == L"--taskbar") {
        Target t{static_cast<DWORD>(std::stoul(argv[2]))}; EnumWindows(find, reinterpret_cast<LPARAM>(&t));
        if (!t.handle) return 1;
        wchar_t path[32768]{}; DWORD count = GetEnvironmentVariableW(L"CODEX_HOME", path, 32768);
        std::wstring home;
        if (count && count < 32768) home = path;
        else { GetEnvironmentVariableW(L"USERPROFILE", path, 32768); home = std::wstring(path) + L"\\.codex"; }
        home = std::filesystem::path(home).lexically_normal().wstring();
        for (auto& c : home) c = static_cast<wchar_t>(std::towlower(c));
        wchar_t module[32768]{}; DWORD moduleLength = 32768;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, t.pid);
        if (!process || !QueryFullProcessImageNameW(process, 0, module, &moduleLength)) { if (process) CloseHandle(process); return 1; }
        CloseHandle(process);
        auto hash = quota::MetricIdentity(home, std::wstring(module,moduleLength));
        std::wcout << L"Identity=" << hash << L"; module=" << module << L"; home=" << home << L"\n";
        RECT taskbar{}; GetWindowRect(FindWindowW(L"Shell_TrayWnd", nullptr), &taskbar);
        for (UINT id = 1; id <= 2; ++id) {
            NOTIFYICONIDENTIFIER icon{}; icon.cbSize = sizeof(icon); icon.hWnd = t.handle; icon.uID = id; icon.guidItem = quota::MetricGuid(hash, id);
            RECT r{}; HRESULT result = Shell_NotifyIconGetRect(&icon, &r);
            if (FAILED(result)) { icon.guidItem = GUID_NULL; result = Shell_NotifyIconGetRect(&icon, &r); }
            bool inTaskbar = SUCCEEDED(result) && r.left >= taskbar.left && r.right <= taskbar.right && r.top >= taskbar.top && r.bottom <= taskbar.bottom;
            std::cout << (id == 1 ? "5h" : "weekly") << " rect result=" << result << "; taskbar-visible=" << inTaskbar << "; bounds=" << r.left << "," << r.top << "," << r.right << "," << r.bottom << "\n";
        }
        return 0;
    }
    bool configImage=std::wstring(argv[1])==L"--config-image";
    if(configImage&&argc<4)return 2;
    Target t{static_cast<DWORD>(std::stoul(argv[configImage?2:1]))};t.config=configImage;EnumWindows(find,reinterpret_cast<LPARAM>(&t));
    if (!t.handle) { std::cerr << "Overlay window not found\n"; return 1; }
    wchar_t caption[256]{}; GetWindowTextW(t.handle,caption,256); std::string label; for(wchar_t c:std::wstring(caption))label+=c<128?static_cast<char>(c):'?';std::cout<<"Runtime title="<<label<<"\n";
    RECT r{}; GetWindowRect(t.handle, &r); int width = r.right-r.left, height = r.bottom-r.top;
    POINT center{(r.left+r.right)/2,(r.top+r.bottom)/2};
    HWND atPoint = GetAncestor(WindowFromPoint(center), GA_ROOT), foreground = GetAncestor(GetForegroundWindow(), GA_ROOT);
    using GetBand=BOOL(WINAPI*)(HWND,PDWORD);auto getBand=reinterpret_cast<GetBand>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetWindowBand"));
    DWORD overlayBand=0,hostBand=0;if(getBand){getBand(t.handle,&overlayBand);getBand(foreground,&hostBand);}
    SetLastError(0);auto style=GetWindowLongPtrW(t.handle,GWL_EXSTYLE);auto styleError=GetLastError();
    std::cout<<"Overlay band="<<overlayBand<<" foreground band="<<hostBand<<" exstyle="<<style<<" style-error="<<styleError<<"\n";
    std::cout<<"Window style="<<GetWindowLongPtrW(t.handle,GWL_STYLE)<<" owner="<<reinterpret_cast<UINT_PTR>(GetWindow(t.handle,GW_OWNER))<<"\n";
    if(configImage){wchar_t text[2048]{};GetWindowTextW(GetDlgItem(t.handle,206),text,2048);std::string safe;for(wchar_t c:std::wstring(text))safe+=c<128?static_cast<char>(c):'?';std::cout<<"Configuration state: "<<safe<<"\n";}
    DWORD pointPid=0; GetWindowThreadProcessId(atPoint,&pointPid);
    std::cout << "Overlay handle=" << reinterpret_cast<UINT_PTR>(t.handle) << "; topmost=" << ((GetWindowLongPtrW(t.handle,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0)
        << "; center-hit-overlay=" << (atPoint==t.handle) << "; center-root-PID=" << pointPid << "; foreground-handle=" << reinterpret_cast<UINT_PTR>(foreground) << "\n";
    HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    HBITMAP image = CreateCompatibleBitmap(screen, width, height); auto old = SelectObject(memory, image);
    BOOL printed = PrintWindow(t.handle, memory, configImage?2:3);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
    SelectObject(memory, old); int rows = GetDIBits(screen, image, 0, height, pixels.data(), &info, DIB_RGB_COLORS);
    BITMAPFILEHEADER header{}; header.bfType = 0x4D42; header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER); header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
    std::ofstream file{std::filesystem::path(argv[configImage?3:2]), std::ios::binary}; file.write(reinterpret_cast<char*>(&header), sizeof(header)); file.write(reinterpret_cast<char*>(&info.bmiHeader), sizeof(BITMAPINFOHEADER)); file.write(reinterpret_cast<char*>(pixels.data()), pixels.size());
    DeleteObject(image); DeleteDC(memory); ReleaseDC(nullptr, screen);
    std::cout << "Captured native renderer at " << width << "x" << height << "; printed=" << printed << "; visible=" << IsWindowVisible(t.handle) << "; bounds=" << r.left << "," << r.top << "," << r.right << "," << r.bottom << "\n";
    DWORD foregroundPid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
    unsigned long long sum = 0; for (auto pixel : pixels) sum += pixel;
    std::cout << "Foreground PID=" << foregroundPid << "; bitmap rows=" << rows << "; pixel sum=" << sum << "\n";
    return file.good() ? 0 : 1;
}

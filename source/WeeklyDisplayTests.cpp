#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include "QuotaCore.h"
struct Target { DWORD pid = 0; HWND controller = nullptr, strip = nullptr; };
BOOL CALLBACK find(HWND window, LPARAM data) {
    auto& target = *reinterpret_cast<Target*>(data); DWORD pid; GetWindowThreadProcessId(window, &pid);
    if (pid != target.pid) return TRUE;
    wchar_t name[200]{}; GetClassNameW(window, name, 200); std::wstring cls(name);
    if (cls.find(L"QuotaController_") == 0) target.controller = window;
    if (cls.find(L"CodexQuotaStrip_") == 0) target.strip = window;
    return TRUE;
}
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool foreground(HWND window) {
    // 测试由后台终端启动；沿用生命周期测试的前台授权处理，避免把禁止抢焦点当成布局失败。
    DWORD active=GetWindowThreadProcessId(GetForegroundWindow(),nullptr),self=GetCurrentThreadId();
    bool attached=active&&active!=self&&AttachThreadInput(self,active,TRUE);
    BOOL changed=SetForegroundWindow(window);if(attached)AttachThreadInput(self,active,FALSE);
    return changed&&GetForegroundWindow()==window;
}
bool wait(const std::function<bool()>& condition) {
    auto start = GetTickCount64();
    do { if (condition()) return true; MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } Sleep(20); } while (GetTickCount64() - start < 6000);
    return false;
}
quota::Json state(const std::filesystem::path& file) {
    std::ifstream in(file); std::string text((std::istreambuf_iterator<char>(in)), {});
    try { return quota::JsonParser(text).parse(); } catch (...) { return {}; }
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    HWND original = GetForegroundWindow(), host = nullptr; PROCESS_INFORMATION process{}; Target target;
    wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH, temp);
    auto root = std::filesystem::path(temp) / (L"quota-weekly-ui-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(root / L"sessions");
    auto status = root / L"status.json", log = root / L"sessions/rollout-test.jsonl";
    { std::ofstream out(log); }
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = DefWindowProcW; cls.lpszClassName = L"QuotaWeeklyHost"; RegisterClassW(&cls);
        host = CreateWindowExW(0, cls.lpszClassName, L"Weekly quota display fixture", WS_OVERLAPPEDWINDOW, 200, 120, 900, 700, nullptr, nullptr, cls.hInstance, nullptr);
        ShowWindow(host, SW_SHOW);check(foreground(host),"Synthetic host must become foreground before testing its overlay");
        std::wstring command = L"\"" + std::filesystem::absolute(argv[1]).wstring() + L"\" --local-only --taskbar-and-codex --left --host-pid " + std::to_wstring(GetCurrentProcessId()) + L" --home \"" + root.wstring() + L"\" --status \"" + status.wstring() + L"\"";
        STARTUPINFOW start{}; start.cb = sizeof(start);
        check(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &start, &process), "Weekly display fixture must launch"); target.pid = process.dwProcessId;
        check(wait([&] { EnumWindows(find, reinterpret_cast<LPARAM>(&target)); auto current = state(status); return target.controller && current.get("watching").num && current.get("tray_icons").num == 2; }), "Unknown startup must retain both placeholders");
        auto append = [&](const std::string& secondary, int used) {
            SYSTEMTIME now; GetSystemTime(&now); char timestamp[64]; sprintf_s(timestamp, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
            std::ofstream out(log, std::ios::app); out << "{\"timestamp\":\"" << timestamp << "\",\"type\":\"event_msg\",\"payload\":{\"type\":\"token_count\",\"rate_limits\":{\"primary\":{\"used_percent\":" << used << ",\"window_minutes\":10080},\"secondary\":" << secondary << "}}}\n";
        };
        append("null", 46);
        check(wait([&] { auto current = state(status); return current.get("weekly_remaining").num == 54 && current.get("tray_icons").num == 1; }), "Confirmed weekly-only account must unregister the five-hour tray icon");
        check(wait([&] { RECT rect{}; GetWindowRect(target.strip, &rect); return IsWindowVisible(target.strip) && rect.bottom - rect.top < MulDiv(70, GetDpiForWindow(target.strip), 96); }), "Weekly-only left strip must shrink instead of leaving an empty five-hour section");
        append("{\"used_percent\":20,\"window_minutes\":300}", 47);
        check(wait([&] { auto current = state(status); return current.get("weekly_remaining").num == 53 && current.get("tray_icons").num == 2; }), "A later five-hour window must restore its tray icon");
        check(wait([&] { RECT rect{}; GetWindowRect(target.strip, &rect); return rect.bottom - rect.top > MulDiv(90, GetDpiForWindow(target.strip), 96); }), "Two-window strip must restore full height");
        append("{\"used_percent\":20,\"window_minutes\":300,\"resets_at\":1}", 48);
        check(wait([&] { auto current = state(status); return current.get("weekly_remaining").num == 52 && current.get("five_hour_remaining").kind == quota::Json::Null && current.get("tray_icons").num == 2; }), "Expired known window must not be mistaken for an unsupported account window");
        { std::ofstream auth(root / L"auth.json"); auth << "{}"; }
        check(wait([&] { auto current = state(status); return current.get("weekly_remaining").kind == quota::Json::Null && current.get("tray_icons").num == 2; }), "Account invalidation must restore unknown placeholders");
        SendMessageTimeoutW(target.controller, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 2000, nullptr);
        // 合成宿主和被测条跨进程相属，退出期间仍需泵送宿主窗口消息。
        check(wait([&] { return WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0; }), "Weekly fixture must exit");
        std::cout << "PASS: weekly-only tray/compact strip, restoration, expiry and account reset\n";
        CloseHandle(process.hThread); CloseHandle(process.hProcess); DestroyWindow(host); if (IsWindow(original)) SetForegroundWindow(original); return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        if (target.controller) SendMessageTimeoutW(target.controller, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 2000, nullptr);
        if (process.hProcess) { if (WaitForSingleObject(process.hProcess, 3000) == WAIT_TIMEOUT) TerminateProcess(process.hProcess, 1); CloseHandle(process.hThread); CloseHandle(process.hProcess); }
        if (host) DestroyWindow(host); if (IsWindow(original)) SetForegroundWindow(original); return 1;
    }
}

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "QuotaCore.h"

struct Target { DWORD pid = 0; HWND handle = nullptr, controller = nullptr; };
BOOL CALLBACK find(HWND h, LPARAM p) {
    auto& t = *reinterpret_cast<Target*>(p); DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    wchar_t cls[128]{}; GetClassNameW(h, cls, 128);
    if (pid == t.pid) {
        if (std::wstring(cls).find(L"CodexQuotaStrip_") == 0) t.handle = h;
        if (std::wstring(cls).find(L"QuotaController_") == 0) t.controller = h;
    }
    return TRUE;
}
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool WaitUntil(bool (*condition)(HWND), HWND h, DWORD milliseconds = 2000) {
    auto start = GetTickCount64();
    do { if (condition(h)) return true; Sleep(20); } while (GetTickCount64() - start < milliseconds);
    return false;
}
bool Hidden(HWND h) { return !IsWindowVisible(h); }
bool ForegroundTestWindow(HWND h) {
    if (SetForegroundWindow(h) && GetForegroundWindow() == h) return true;
    DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr), currentThread = GetCurrentThreadId();
    bool attached = foregroundThread && foregroundThread != currentThread && AttachThreadInput(currentThread, foregroundThread, TRUE);
    BOOL activated = SetForegroundWindow(h);
    if (attached) AttachThreadInput(currentThread, foregroundThread, FALSE);
    return activated && GetForegroundWindow() == h;
}
quota::Json State(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary); std::string content((std::istreambuf_iterator<char>(in)), {});
    try { return quota::JsonParser(content).parse(); } catch (...) { return {}; }
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t temporary[MAX_PATH]; GetTempPathW(MAX_PATH, temporary);
    auto fixture = std::filesystem::path(temporary) / (L"quota-visibility-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(fixture / L"sessions");
    HWND originalForeground = GetForegroundWindow(), other = nullptr;
    PROCESS_INFORMATION process{};
    Target target;
    try {
        wchar_t previousLocalAppData[32768]{}; DWORD oldCount = GetEnvironmentVariableW(L"LOCALAPPDATA", previousLocalAppData, 32768);
        SetEnvironmentVariableW(L"LOCALAPPDATA", fixture.c_str());
        std::wstring command = L"\"" + std::filesystem::absolute(argv[1]).wstring() + L"\" --local-only --home \"" + fixture.wstring() + L"\" --status \"" + (fixture / L"status.json").wstring() + L"\"";
        STARTUPINFOW start{}; start.cb = sizeof(start); start.dwFlags = STARTF_USESHOWWINDOW; start.wShowWindow = SW_HIDE;
        BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &start, &process);
        SetEnvironmentVariableW(L"LOCALAPPDATA", oldCount ? previousLocalAppData : nullptr);
        check(created != FALSE, "Visibility test helper must start");
        target.pid = process.dwProcessId;
        bool iconsAdded = false;
        for (int i = 0; i < 100; ++i) { if (State(fixture / L"status.json").get("tray_icons").num == 2) { iconsAdded = true; break; } Sleep(20); }
        check(iconsAdded, "Windows must register both numeric notification icons");
        EnumWindows(find, reinterpret_cast<LPARAM>(&target));
        check(target.controller != nullptr, "Taskbar mode must retain its command window");
        check(target.handle == nullptr, "Taskbar-only mode must not allocate an unused overlay window");
        std::cout << "PASS: taskbar icons registered without allocating an overlay\n";

        // Only this test's small, temporary window is foregrounded; the user's app is restored.
        other = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"Quota visibility test", WS_OVERLAPPEDWINDOW, 32, 64, 240, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        check(other != nullptr, "Temporary test window must exist");
        ShowWindow(other, SW_SHOW);
        bool activated = ForegroundTestWindow(other);
        Sleep(150);
        check(activated && GetForegroundWindow() == other, "Temporary test window must become foreground for the application-switch check");
        check(Hidden(target.handle), "Other applications must not be covered by a floating quota window");
        check(GetForegroundWindow() == other, "Quota strip must not steal focus");
        std::cout << "PASS: another foreground window is unobscured and retains focus\n";

        SendMessageTimeoutW(target.controller, WM_COMMAND, 2, 0, SMTO_ABORTIFHUNG, 2000, nullptr);
        EnumWindows(find, reinterpret_cast<LPARAM>(&target));
        check(target.handle && WaitUntil(Hidden, target.handle), "Optional in-Codex strip must be created on demand and stay hidden when another application is foreground");
        check((GetWindowLongPtrW(target.handle, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0, "Optional in-Codex strip must never be globally topmost");
        wchar_t saved[32]{};
        GetPrivateProfileStringW(L"display", L"surface", L"", saved, 32, (fixture / L"CodexQuotaStrip" / L"settings.ini").c_str());
        check(std::wstring(saved) == L"taskbar-and-codex", "Optional in-Codex choice must persist in isolated test settings");
        SendMessageTimeoutW(target.controller, WM_COMMAND, 5, 0, SMTO_ABORTIFHUNG, 2000, nullptr);
        check(!IsWindow(target.handle), "Returning to taskbar-only mode must release the overlay");
        target.handle = nullptr;
        check(GetForegroundWindow() == other, "Changing visibility preference must preserve focus");
        std::cout << "PASS: taskbar-only and optional in-Codex choices persist without stealing focus\n";

        SYSTEMTIME utc{}; GetSystemTime(&utc); char timestamp[80];
        sprintf_s(timestamp, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond, utc.wMilliseconds);
        std::string event = std::string("{\"timestamp\":\"") + timestamp + "\",\"type\":\"event_msg\",\"payload\":{\"type\":\"token_count\",\"rate_limits\":{\"primary\":{\"used_percent\":88,\"window_minutes\":10080},\"secondary\":null}}}\n";
        { std::ofstream log(fixture / L"sessions" / L"test.jsonl", std::ios::binary); log << event; }
        bool updated = false;
        for (int i = 0; i < 100; ++i) {
            auto state = State(fixture / L"status.json"); const auto& values = state.get("tray_values").elements;
            if (values.size() == 2 && values[0].str.empty() && values[1].str == "12" && state.get("tray_icons").num == 1) { updated = true; break; }
            Sleep(20);
        }
        check(updated, "A real quota event must refresh the weekly icon and hide the missing five-hour window");
        EnumWindows(find, reinterpret_cast<LPARAM>(&target));
        check(!target.handle, "A quota update must not allocate an overlay in taskbar-only mode");
        std::cout << "PASS: real quota event updated taskbar digits without opening an overlay\n";

        // 模拟系统时间变更通知；不修改宿主机时钟，仍走控制窗口的到期重算路径。
        const auto statusPath = fixture / L"status.json";
        const auto beforeClockChange = std::filesystem::last_write_time(statusPath);
        SendMessageTimeoutW(target.controller, WM_TIMECHANGE, 0, 0, SMTO_ABORTIFHUNG, 2000, nullptr);
        check(std::filesystem::last_write_time(statusPath) != beforeClockChange &&
            State(statusPath).get("weekly_remaining").num == 12,
            "System time notification must recompute quota state without waiting for a log append");

        SendMessageTimeoutW(target.controller, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 2000, nullptr);
        check(WaitForSingleObject(process.hProcess, 3000) == WAIT_OBJECT_0, "Test helper must exit normally");
        CloseHandle(process.hThread); CloseHandle(process.hProcess); process = {};
        DestroyWindow(other); other = nullptr;
        if (originalForeground && IsWindow(originalForeground)) SetForegroundWindow(originalForeground);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        if (target.controller && IsWindow(target.controller)) SendMessageTimeoutW(target.controller, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 1000, nullptr);
        if (process.hProcess) {
            if (WaitForSingleObject(process.hProcess, 3000) == WAIT_TIMEOUT) TerminateProcess(process.hProcess, 1);
            CloseHandle(process.hThread); CloseHandle(process.hProcess);
        }
        if (other) DestroyWindow(other);
        if (originalForeground && IsWindow(originalForeground)) SetForegroundWindow(originalForeground);
        return 1;
    }
}

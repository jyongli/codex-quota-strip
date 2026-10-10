#include "QuotaFiles.h"
#include "TrayIcons.h"
#include "QuotaSettings.h"
#include "QuotaProvider.h"
#include "SettingsTheme.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <cwctype>
#include <atomic>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>
#include <imm.h>

namespace {
constexpr UINT StateChanged = WM_APP + 1, HostChanged = WM_APP + 2, TrayMessage = WM_APP + 3;
constexpr UINT_PTR ExpiryTimer = 1, RecoveryTimer = 2;
struct Data {
    quota::Snapshot snapshot;
    quota::Snapshot localSnapshot;
    unsigned long long events = 0, bytesRead = 0, notifications = 0;
    size_t cachedFiles = 0;
    bool watching = false;
    DWORD error = 0;
    // 查询失败时保留旧值；来源、时间与错误状态独立展示。
    bool fromServer = false, refreshing = false;
    long long lastAttempt = 0, lastSuccess = 0;
    std::wstring queryError;
};
Data data;
std::mutex dataMutex;
// 控制窗口不属于 Codex；Codex 关闭时只销毁可重建的显示条。
HWND window = nullptr, controller = nullptr, settingsWindow = nullptr, host = nullptr, tooltip = nullptr;
HINSTANCE instance = nullptr;
HANDLE stopEvent = nullptr, rescanEvent = nullptr;
HANDLE refreshEvent = nullptr;
std::thread worker;
std::thread cloudWorker;
std::wstring home, sessions, statusPath, settingsPath, windowClass, tipText, paintedText;
bool headless = false, leftSide = false, closing = false;
bool showInCodex = false;
bool hostMessagePending = false;
std::atomic<bool> workerDone{false};
quota::Settings preferences;
std::mutex preferencesMutex;
std::atomic<bool> codexRunning{false};
std::atomic<unsigned long long> accountGeneration{0};
std::atomic<bool> manualRefresh{false};
DWORD explicitHostPid = 0;
HFONT font = nullptr, smallFont = nullptr;
UINT dpi = 96, taskbarCreated = 0;
HWINEVENTHOOK foregroundHook = nullptr, locationHook = nullptr, minimizeHook = nullptr, destroyHook = nullptr;
NOTIFYICONDATAW trays[2]{};
bool trayAdded[2]{};
std::wstring trayValues[2], trayRenderKeys[2], trayTips[2];
unsigned long long trayIdentity = 0;
RECT lastPlacement{};
bool lastVisible = false;
long long nextWakeMs=0;UINT_PTR expiryTimerId=0;DWORD expiryTimerError=0;unsigned timerFired=0;
LRESULT CALLBACK Procedure(HWND,UINT,WPARAM,LPARAM);
void OpenSettings();
void RequestCloud(bool manual=false);
void UpdateState();
void PositionStrip();
quota::Settings GetPreferences(){std::lock_guard<std::mutex> lock(preferencesMutex);return preferences;}
bool IsStale(const Data& s,long long now){return !s.snapshot.observed || now-s.snapshot.observed>=GetPreferences().staleMinutes*60000LL || (!s.watching&&!s.fromServer);}
int px(int x) { return MulDiv(x, static_cast<int>(dpi), 96); }
std::wstring Lower(std::wstring s) { for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c)); return s; }
bool EndsWith(const std::wstring& s, const wchar_t* suffix) { size_t n = wcslen(suffix); return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0; }

quota::Window CurrentWindow(quota::Window w, long long now) {
    // A reset is not evidence that 100% is available. Clear until a newer server snapshot arrives.
    if (w.resets > 0 && now >= w.resets * 1000) w.present = false;
    return w;
}
Data GetData() { std::lock_guard<std::mutex> lock(dataMutex); return data; }
void SignalState() { if (controller) PostMessageW(controller, StateChanged, 0, 0); }

std::wstring LocalTime(long long milliseconds) {
    if (milliseconds <= 0) return L"尚无数据";
    ULARGE_INTEGER u{}; u.QuadPart = static_cast<unsigned long long>(milliseconds + 11644473600000LL) * 10000;
    FILETIME utc{u.LowPart,u.HighPart}, local{}; SYSTEMTIME t{};
    FileTimeToLocalFileTime(&utc, &local); FileTimeToSystemTime(&local, &t);
    wchar_t str[64]; swprintf_s(str, L"%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return str;
}
void WriteStatus(const Data& s) {
    if (statusPath.empty()) return;
    auto now = quota::NowMs(); auto five = CurrentWindow(s.snapshot.fiveHour, now), week = CurrentWindow(s.snapshot.weekly, now);
    std::string json = "{\"five_hour_remaining\":";
    auto number = [](const quota::Window& w) { return w.present ? std::to_string(std::clamp(100.0 - w.used, 0.0, 100.0)) : std::string("null"); };
    json += number(five) + ",\"weekly_remaining\":" + number(week);
    json += ",\"observed_ms\":" + std::to_string(s.snapshot.observed);
    auto config=GetPreferences();
    json += ",\"refresh_seconds\":"+std::to_string(config.intervalSeconds)+",\"stale_minutes\":"+std::to_string(config.staleMinutes);
    json += ",\"online\":"+std::string(config.online?"true":"false")+",\"refreshing\":"+std::string(s.refreshing?"true":"false");
    json += ",\"source\":\""+std::string(s.fromServer?"account":"local-event")+"\",\"last_success_ms\":"+std::to_string(s.lastSuccess);
    json += ",\"settings_open\":"+std::string(settingsWindow?"true":"false");
    json += ",\"next_wake_ms\":"+std::to_string(nextWakeMs)+",\"timer_id\":"+std::to_string(expiryTimerId)+",\"timer_error\":"+std::to_string(expiryTimerError)+",\"timer_fired\":"+std::to_string(timerFired);
    json += ",\"events\":" + std::to_string(s.events) + ",\"bytes_read\":" + std::to_string(s.bytesRead);
    json += ",\"notifications\":" + std::to_string(s.notifications) + ",\"cached_files\":" + std::to_string(s.cachedFiles);
    json += ",\"watching\":" + std::string(s.watching ? "true" : "false") + ",\"error\":" + std::to_string(s.error);
    json += ",\"visible\":" + std::string(lastVisible ? "true" : "false");
    json += ",\"always_visible\":false,\"show_in_codex\":" + std::string(showInCodex ? "true" : "false");
    json += ",\"topmost\":" + std::string((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) ? "true" : "false");
    json += ",\"tray_icons\":" + std::to_string(static_cast<int>(trayAdded[0]) + static_cast<int>(trayAdded[1]));
    json += ",\"tray_identity\":\"" + std::to_string(trayIdentity) + "\"";
    auto ascii = [](const std::wstring& v) { std::string out; for (wchar_t c : v) out += static_cast<char>(c); return out; };
    json += ",\"tray_values\":[\"" + ascii(trayValues[0]) + "\",\"" + ascii(trayValues[1]) + "\"]";
    json += ",\"position\":\"" + std::string(leftSide ? "left" : "top") + "\",\"bounds\":[";
    json += std::to_string(lastPlacement.left) + "," + std::to_string(lastPlacement.top) + "," + std::to_string(lastPlacement.right) + "," + std::to_string(lastPlacement.bottom) + "]}";
    std::wstring temp = statusPath + L".tmp";
    quota::Handle out(CreateFileW(temp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (out.valid()) { DWORD written = 0; WriteFile(out, json.data(), static_cast<DWORD>(json.size()), &written, nullptr); }
    // Close before replace; only diagnostic files inside the explicit --status path are written.
    // The handle above is released at scope exit in the inner block in AtomicStatus below.
}
void AtomicStatus(const Data& s) {
    if (statusPath.empty()) return;
    WriteStatus(s);
    MoveFileExW((statusPath + L".tmp").c_str(), statusPath.c_str(), MOVEFILE_REPLACE_EXISTING);
}

void WatchFiles() {
    quota::FileFollower follower;
    quota::Handle dir(CreateFileW(sessions.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr));
    DWORD openError = dir.valid() ? 0 : GetLastError();
    quota::Handle authDir(CreateFileW(home.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr));
    quota::Handle changed(CreateEventW(nullptr, TRUE, FALSE, nullptr)), authChanged(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    OVERLAPPED io{}, authIo{}; io.hEvent = changed; authIo.hEvent = authChanged;
    alignas(DWORD) BYTE buffer[32768], authBuffer[4096];
    auto arm = [&]() {
        ResetEvent(changed); ZeroMemory(&io, sizeof(io)); io.hEvent = changed;
        return ReadDirectoryChangesW(dir, buffer, sizeof(buffer), TRUE, FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME, nullptr, &io, nullptr) != FALSE;
    };
    auto armAuth = [&]() {
        ResetEvent(authChanged); ZeroMemory(&authIo, sizeof(authIo)); authIo.hEvent = authChanged;
        return authDir.valid() && ReadDirectoryChangesW(authDir, authBuffer, sizeof(authBuffer), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME, nullptr, &authIo, nullptr) != FALSE;
    };
    if (!dir.valid() || !arm()) {
        DWORD error = openError ? openError : GetLastError();
        { std::lock_guard<std::mutex> lock(dataMutex); data.watching = false; data.error = error; }
        SignalState(); return;
    }
    bool authArmed = armAuth();
    long long afterAuth = 0;
    // Metadata only: the companion never opens or stores auth.json contents.
    WIN32_FILE_ATTRIBUTE_DATA authMeta{};
    if (GetFileAttributesExW((home + L"\\auth.json").c_str(), GetFileExInfoStandard, &authMeta)) afterAuth = quota::FileTimeMs(authMeta.ftLastWriteTime);
    auto accept = [&](const quota::Snapshot& snapshot) {
        if (snapshot.observed < afterAuth) return;
        bool online=GetPreferences().online;
        std::lock_guard<std::mutex> lock(dataMutex);
        if(quota::Apply(snapshot,data.localSnapshot))++data.events;
        // 本地事件时间是写盘时间，不保证配额字段新鲜；在线成功后只用账户结果。
        if((!online||!data.lastSuccess)&&quota::Apply(snapshot,data.snapshot))data.fromServer=false;
    };
    unsigned long long publishedEvents = static_cast<unsigned long long>(-1);
    auto publish = [&]() {
        bool changedState = false;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            data.bytesRead = follower.bytesRead; data.cachedFiles = follower.cachedFiles();
            changedState = !data.watching || data.events != publishedEvents;
            data.watching = true; data.error = 0; publishedEvents = data.events;
        }
        if (changedState) SignalState();
    };
    auto scan = [&]() { for (const auto& path : quota::RecentFiles(sessions)) follower.read(path, accept); publish(); };
    scan();
    HANDLE waits[] = {stopEvent, rescanEvent, changed, authChanged};
    while (true) {
        DWORD waited = WaitForMultipleObjects(authArmed ? 4 : 3, waits, FALSE, INFINITE);
        if (waited == WAIT_OBJECT_0 || waited == WAIT_FAILED) break;
        if (waited == WAIT_OBJECT_0 + 1) { ResetEvent(rescanEvent); follower.clear(); scan(); continue; }
        if (waited == WAIT_OBJECT_0 + 3) {
            DWORD count = 0; bool reset = false;
            if (GetOverlappedResult(authDir, &authIo, &count, FALSE) && count) {
                size_t at = 0;
                while (at + sizeof(FILE_NOTIFY_INFORMATION) <= count) {
                    auto note = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(authBuffer + at);
                    if (at + offsetof(FILE_NOTIFY_INFORMATION, FileName) + note->FileNameLength > count) break;
                    std::wstring name(note->FileName, note->FileNameLength / sizeof(wchar_t));
                    if (Lower(name) == L"auth.json") reset = true;
                    if (!note->NextEntryOffset) break; at += note->NextEntryOffset;
                }
            } else reset = true;
            authArmed = armAuth();
            if (reset) {
                afterAuth = quota::NowMs(); follower.clear();
                ++accountGeneration;
                { std::lock_guard<std::mutex> lock(dataMutex); data.snapshot = {};data.localSnapshot={};data.error=0;data.fromServer=false;data.lastSuccess=0; }
                RequestCloud();
                publishedEvents = static_cast<unsigned long long>(-1); publish();
            }
            continue;
        }
        DWORD count = 0; bool ok = GetOverlappedResult(dir, &io, &count, FALSE) != FALSE;
        std::set<std::wstring> paths;
        if (ok && count) {
            size_t at = 0;
            while (at + offsetof(FILE_NOTIFY_INFORMATION, FileName) <= count) {
                auto note = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer + at);
                if (at + offsetof(FILE_NOTIFY_INFORMATION, FileName) + note->FileNameLength > count) break;
                std::wstring path = sessions + L"\\" + std::wstring(note->FileName, note->FileNameLength / sizeof(wchar_t));
                if (EndsWith(Lower(path), L".jsonl")) {
                    if (note->Action == FILE_ACTION_REMOVED || note->Action == FILE_ACTION_RENAMED_OLD_NAME) follower.forget(path);
                    else paths.insert(path);
                }
                if (!note->NextEntryOffset) break; at += note->NextEntryOffset;
            }
        }
        if (!arm()) {
            { std::lock_guard<std::mutex> lock(dataMutex); data.watching = false; data.error = GetLastError(); }
            SignalState(); break;
        }
        // A one-shot 60ms batch after an actual filesystem event. There is no periodic polling.
        if (WaitForSingleObject(stopEvent, 60) == WAIT_OBJECT_0) break;
        { std::lock_guard<std::mutex> lock(dataMutex); ++data.notifications; }
        if (!ok || !count) { follower.clear(); scan(); }
        else { for (const auto& path : paths) follower.read(path, accept); publish(); }
    }
    CancelIoEx(dir, nullptr); if (authArmed) CancelIoEx(authDir, nullptr);
    DWORD ignored = 0; GetOverlappedResult(dir, &io, &ignored, TRUE);
    if (authArmed) GetOverlappedResult(authDir, &authIo, &ignored, TRUE);
}

void StartWorker() {
    if (worker.joinable()) worker.join();
    workerDone = false;
    worker = std::thread([] {
        try { WatchFiles(); }
        catch (...) { std::lock_guard<std::mutex> lock(dataMutex); data.watching = false; data.error = ERROR_NOT_ENOUGH_MEMORY; }
        workerDone = true; SignalState();
    });
}
void Reread() {
    if (workerDone) { ResetEvent(rescanEvent); StartWorker(); }
    else SetEvent(rescanEvent);
}

bool IsCodexWindow(HWND candidate) {
    if (!candidate || candidate == window || !IsWindowVisible(candidate) || GetAncestor(candidate, GA_ROOT) != candidate) return false;
    if (GetWindow(candidate, GW_OWNER)) return false;
    DWORD pid = 0; GetWindowThreadProcessId(candidate, &pid);
    if(explicitHostPid)return pid==explicitHostPid;
    quota::Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process.valid()) return false;
    wchar_t path[32768]; DWORD length = static_cast<DWORD>(std::size(path));
    if (!QueryFullProcessImageNameW(process, 0, path, &length)) return false;
    auto lower = Lower(std::wstring(path, length));
    return lower.find(L"\\openai.codex_") != std::wstring::npos ||
        (lower.find(L"\\openai\\codex\\") != std::wstring::npos && EndsWith(lower, L"\\chatgpt.exe"));
}
void CALLBACK OnHostEvent(HWINEVENTHOOK, DWORD event, HWND candidate, LONG object, LONG child, DWORD, DWORD) {
    if (closing || !controller) return;
    if ((event == EVENT_SYSTEM_FOREGROUND || (candidate == host && object == OBJID_WINDOW && child == 0)) && !hostMessagePending) {
        hostMessagePending = true; PostMessageW(controller, HostChanged, 0, 0);
    }
}
void UnhookHost() {
    if (locationHook) UnhookWinEvent(locationHook); if (minimizeHook) UnhookWinEvent(minimizeHook); if (destroyHook) UnhookWinEvent(destroyHook);
    locationHook = minimizeHook = destroyHook = nullptr;
}
void AttachHost(HWND next) {
    if (next == host) return;
    UnhookHost(); host = next;
    // The hidden notification owner must survive Codex closing. Floating UI is foreground-only.
    if(window)SetWindowLongPtrW(window, GWLP_HWNDPARENT, 0);
    if (!host) return;
    DWORD pid = 0; GetWindowThreadProcessId(host, &pid);
    constexpr DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    locationHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, OnHostEvent, pid, 0, flags);
    minimizeHook = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr, OnHostEvent, pid, 0, flags);
    destroyHook = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr, OnHostEvent, pid, 0, flags);
}
BOOL CALLBACK FindHost(HWND candidate, LPARAM result) {
    if (IsCodexWindow(candidate)) { *reinterpret_cast<HWND*>(result) = candidate; return FALSE; } return TRUE;
}
void UpdateFonts(UINT nextDpi) {
    if (font && nextDpi == dpi) return;
    dpi = nextDpi;
    if (font) DeleteObject(font); if (smallFont) DeleteObject(smallFont);
    font = CreateFontW(-px(13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    smallFont = CreateFontW(-px(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
}
void PositionStrip() {
    if (headless) return;
    if(!window){
        window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,windowClass.c_str(),L"Codex 配额条 2.0",WS_POPUP,0,0,236,26,nullptr,nullptr,instance,nullptr);lastVisible=false;
        // 显示条重建时一并重建其提示框；所属主窗口销毁不会遗留提示窗口。
        if(window){
            tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,window,nullptr,instance,nullptr);
            SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,500);SendMessageW(tooltip,TTM_SETDELAYTIME,TTDT_INITIAL,450);
            TOOLINFOW info{sizeof(info)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=window;info.uId=reinterpret_cast<UINT_PTR>(window);info.lpszText=const_cast<wchar_t*>(L"等待用量数据");SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));
        }
    }
    if(!window)return;
    HWND foreground = GetAncestor(GetForegroundWindow(), GA_ROOT);
    if (foreground != window && foreground != host && IsCodexWindow(foreground)) AttachHost(foreground);
    if (!host || !IsWindow(host)) { HWND found = nullptr; EnumWindows(FindHost, reinterpret_cast<LPARAM>(&found)); AttachHost(found); }
    bool usableHost = host && IsWindowVisible(host) && !IsIconic(host);
    bool nowRunning=host&&IsWindow(host);
    if(!codexRunning.exchange(nowRunning)&&nowRunning)RequestCloud();
    bool show = showInCodex && usableHost && foreground == host;
    if (!show) {
        if (lastVisible || IsWindowVisible(window)) ShowWindow(window, SW_HIDE);
        if (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (GetWindow(window, GW_OWNER)) SetWindowLongPtrW(window, GWLP_HWNDPARENT, 0);
        lastVisible = false; AtomicStatus(GetData()); return;
    }
    // Ownership keeps the bar above this host without globally pinning it over other apps.
    bool ownerChanged = GetWindow(window, GW_OWNER) != host;
    if (ownerChanged) SetWindowLongPtrW(window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(host));
    UpdateFonts(GetDpiForWindow(usableHost ? host : window));
    bool weeklyOnly = quota::WeeklyOnly(GetData().snapshot);
    int width = px(leftSide ? 44 : weeklyOnly ? 116 : 236), height = px(leftSide ? weeklyOnly ? 56 : 110 : 26);
    int x = lastPlacement.left, y = lastPlacement.top;
    if (usableHost) {
        RECT rect{};
        if (FAILED(DwmGetWindowAttribute(host, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) GetWindowRect(host, &rect);
        int hostWidth = rect.right - rect.left, hostHeight = rect.bottom - rect.top;
        x = rect.left + std::min(std::max(px(320), (hostWidth - width) / 2), std::max(px(8), hostWidth - px(155) - width));
        y = rect.top + px(7);
        if (leftSide) { x = rect.left + px(5); y = rect.top + std::clamp((hostHeight - height) / 2, px(320), std::max(px(320), hostHeight - height - px(140))); }
        else if (hostWidth < px(630)) { x = rect.left + std::max(px(8), (hostWidth - width) / 2); y = rect.top + px(48); }
    } else if (lastPlacement.right <= lastPlacement.left) {
        MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromWindow(foreground, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
            x = monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2;
            y = monitor.rcWork.top + px(7);
        }
    }
    RECT placement{x,y,x+width,y+height};
    if (!lastVisible || ownerChanged || memcmp(&placement, &lastPlacement, sizeof(RECT)) != 0) {
        SetWindowRgn(window, CreateRoundRectRgn(0, 0, width + 1, height + 1, px(9), px(9)), FALSE);
        SetWindowPos(window, HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        lastPlacement = placement; InvalidateRect(window, nullptr, FALSE);
    } else SetWindowPos(window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    lastVisible = true; AtomicStatus(GetData());
}
COLORREF NumberColor(const quota::Window& w, bool stale) {
    if (!w.present || stale) return RGB(160,160,160);
    return 100 - w.used <= 20 ? RGB(246,183,92) : RGB(229,229,229);
}
void DrawTextAt(HDC dc, const std::wstring& text, RECT r, HFONT selected, COLORREF color, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    auto old = SelectObject(dc, selected); SetTextColor(dc, color); DrawTextW(dc, text.c_str(), -1, &r, flags); SelectObject(dc, old);
}
void PaintStrip(HDC target) {
    RECT r{}; GetClientRect(window, &r);
    HDC dc = CreateCompatibleDC(target); HBITMAP bitmap = CreateCompatibleBitmap(target, r.right, r.bottom); auto oldBmp = SelectObject(dc, bitmap);
    HBRUSH background = CreateSolidBrush(RGB(31,31,31)); FillRect(dc, &r, background); DeleteObject(background); SetBkMode(dc, TRANSPARENT);
    auto s = GetData(); auto now = quota::NowMs(); bool stale = IsStale(s,now);
    auto five = CurrentWindow(s.snapshot.fiveHour, now), week = CurrentWindow(s.snapshot.weekly, now);
    auto pen = CreatePen(PS_SOLID, px(1), RGB(65,65,65)); auto oldPen = SelectObject(dc, pen); auto oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, 0, 0, r.right, r.bottom, px(9), px(9));
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(pen);
    if (quota::WeeklyOnly(s.snapshot)) {
        if (leftSide) {
            DrawTextAt(dc, L"周", {0,px(5),r.right,px(25)}, smallFont, RGB(167,167,167), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawTextAt(dc, quota::Remaining(week), {0,px(27),r.right,px(51)}, smallFont, NumberColor(week, stale), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            DrawTextAt(dc, L"周", {px(12),0,px(40),r.bottom}, smallFont, RGB(167,167,167));
            DrawTextAt(dc, quota::Remaining(week), {px(45),0,px(106),r.bottom}, font, NumberColor(week, stale));
        }
    } else if (!leftSide) {
        DrawTextAt(dc, L"5小时", {px(11),0,px(53),r.bottom}, smallFont, RGB(167,167,167));
        DrawTextAt(dc, quota::Remaining(five), {px(60),0,px(117),r.bottom}, font, NumberColor(five, stale));
        DrawTextAt(dc, L"周", {px(133),0,px(153),r.bottom}, smallFont, RGB(167,167,167));
        DrawTextAt(dc, quota::Remaining(week), {px(164),0,px(224),r.bottom}, font, NumberColor(week, stale));
        auto line = CreatePen(PS_SOLID, 1, RGB(60,60,60)); auto p = SelectObject(dc, line); MoveToEx(dc, px(123), px(7), nullptr); LineTo(dc, px(123), r.bottom - px(7)); SelectObject(dc, p); DeleteObject(line);
    } else {
        UINT center = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
        DrawTextAt(dc, L"5h", {0,px(5),r.right,px(25)}, smallFont, RGB(167,167,167), center);
        DrawTextAt(dc, quota::Remaining(five), {0,px(25),r.right,px(50)}, smallFont, NumberColor(five, stale), center);
        DrawTextAt(dc, L"周", {0,px(58),r.right,px(78)}, smallFont, RGB(167,167,167), center);
        DrawTextAt(dc, quota::Remaining(week), {0,px(78),r.right,px(103)}, smallFont, NumberColor(week, stale), center);
    }
    BitBlt(target, 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp); DeleteObject(bitmap); DeleteDC(dc);
}
void UpdateTray(const Data& s, bool force = false) {
    if (headless) return;
    auto now = quota::NowMs();
    quota::Window values[]{CurrentWindow(s.snapshot.fiveHour, now),CurrentWindow(s.snapshot.weekly, now)};
    bool stale = IsStale(s,now);
    int size = std::max(32, GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem()) * 2);
    const wchar_t* captions[]{L"5h",L"W"};
    const wchar_t* names[]{L"5小时",L"周"};
    static auto hash = [] {
        wchar_t module[32768]{}; DWORD length = static_cast<DWORD>(std::size(module));
        // Store-app file redirection can make the loaded path differ from its logical launch path.
        if (!QueryFullProcessImageNameW(GetCurrentProcess(), 0, module, &length)) GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
        trayIdentity = quota::MetricIdentity(home, module); return trayIdentity;
    }();
    for (int i = 0; i < 2; ++i) {
        if (i == 0 && quota::WeeklyOnly(s.snapshot)) {
            if (trayAdded[i]) Shell_NotifyIconW(NIM_DELETE, &trays[i]);
            if (trays[i].hIcon) DestroyIcon(trays[i].hIcon);
            trays[i].hIcon = nullptr; trayAdded[i] = false; trayValues[i].clear(); trayRenderKeys[i].clear(); trayTips[i].clear();
            continue;
        }
        std::wstring full = quota::Remaining(values[i]), value = full;
        if (!value.empty() && value.back() == L'%') value.pop_back();
        std::wstring key = value + (stale ? L"/stale/" : L"/fresh/") + std::to_wstring(size);
        std::wstring tip = std::wstring(L"Codex ") + names[i] + L"剩余 " + full + L"\n最后事件：" + LocalTime(s.snapshot.observed);
        if (values[i].resets) tip += L"\n重置：" + LocalTime(values[i].resets * 1000);
        if (stale && values[i].present) tip += L"\n旧快照，等待本机新事件";
        if (!values[i].present) tip += L"\n服务未提供此窗口，或等待重置后的新事件";
        if (!force && trayAdded[i] && key == trayRenderKeys[i] && tip == trayTips[i]) continue;
        bool redraw = force || key != trayRenderKeys[i] || !trays[i].hIcon;
        COLORREF tint = !values[i].present || stale ? RGB(175,175,175) : 100 - values[i].used <= 20 ? RGB(246,183,92) : i == 0 ? RGB(100,190,245) : RGB(105,210,160);
        HICON image = redraw ? quota::MetricIcon(size, captions[i], value, tint) : trays[i].hIcon;
        if (!image) continue;
        auto next = trays[i]; next.cbSize = sizeof(next); next.hWnd = controller; next.uID = i + 1;
        next.guidItem = quota::MetricGuid(hash, next.uID); next.uCallbackMessage = TrayMessage; next.hIcon = image;
        next.uFlags = NIF_GUID | NIF_TIP | NIF_SHOWTIP;
        if (redraw) next.uFlags |= NIF_ICON;
        if (!trayAdded[i]) next.uFlags |= NIF_MESSAGE | NIF_ICON;
        wcsncpy_s(next.szTip, tip.c_str(), _TRUNCATE);
        bool added = !trayAdded[i];
        BOOL ok = Shell_NotifyIconW(added ? NIM_ADD : NIM_MODIFY, &next);
        if (!ok && !added) { added = true; next.uFlags |= NIF_MESSAGE | NIF_ICON; ok = Shell_NotifyIconW(NIM_ADD, &next); }
        if (ok) {
            if (added) { next.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &next); }
            if (redraw && trays[i].hIcon) DestroyIcon(trays[i].hIcon);
            trays[i] = next; trayAdded[i] = true; trayValues[i] = value; trayRenderKeys[i] = key; trayTips[i] = tip;
        } else if (redraw) DestroyIcon(image);
    }
}
void AddTray() {
    trayAdded[0] = trayAdded[1] = false; UpdateTray(GetData(), true);
}
void RemoveTray() {
    for (int i = 0; i < 2; ++i) {
        if (trayAdded[i]) Shell_NotifyIconW(NIM_DELETE, &trays[i]);
        if (trays[i].hIcon) DestroyIcon(trays[i].hIcon);
        trays[i].hIcon = nullptr; trayAdded[i] = false;
    }
}
void ScheduleNextWake(const Data& s) {
    KillTimer(controller, ExpiryTimer);
    auto now = quota::NowMs(); long long next = 0;
    auto consider = [&](long long at) { if (at > now && (!next || at < next)) next = at; };
    if (s.snapshot.fiveHour.present) consider(s.snapshot.fiveHour.resets * 1000);
    if (s.snapshot.weekly.present) consider(s.snapshot.weekly.resets * 1000);
    if (s.snapshot.observed) consider(s.snapshot.observed + GetPreferences().staleMinutes * 60000LL);
    nextWakeMs=next;expiryTimerId=0;expiryTimerError=0;
    if (next){SetLastError(0);expiryTimerId=SetTimer(controller,ExpiryTimer,static_cast<UINT>(std::clamp(next-now+25,100LL,2147483647LL)),nullptr);if(!expiryTimerId)expiryTimerError=GetLastError();}
}
void UpdateState() {
    auto s = GetData(); auto now = quota::NowMs();
    auto five = CurrentWindow(s.snapshot.fiveHour, now), week = CurrentWindow(s.snapshot.weekly, now);
    bool stale = IsStale(s,now);
    std::wstring text = quota::Remaining(five) + L"/" + quota::Remaining(week) + (stale ? L"/stale" : L"");
    bool displayChanged = text != paintedText;
    if (displayChanged) { paintedText = text; if(window)InvalidateRect(window, nullptr, FALSE); }
    tipText = quota::WeeklyOnly(s.snapshot) ? L"剩余配额：周 " + quota::Remaining(week) : L"剩余配额：5小时 " + quota::Remaining(five) + L"；周 " + quota::Remaining(week);
    tipText += L"\n数据时间：" + LocalTime(s.snapshot.observed);
    tipText += s.fromServer?L"\n来源：账户查询":L"\n来源：本机会话快照";
    if(!s.queryError.empty())tipText+=L"\n查询失败，保留旧值："+s.queryError;
    if (five.resets) tipText += L"\n5小时重置时间：" + LocalTime(five.resets * 1000);
    if (week.resets) tipText += L"\n周重置时间：" + LocalTime(week.resets * 1000);
    if (stale) tipText += L"\n灰色数值是旧快照，等待本机新事件。";
    if (s.error) tipText += L"\n本机日志监听不可用，错误码：" + std::to_wstring(s.error);
    tipText += L"\n右键可打开配置或立即刷新；双击立即刷新。";
    if (tooltip) {
        TOOLINFOW info{sizeof(info)}; info.hwnd = window; info.uId = reinterpret_cast<UINT_PTR>(window); info.lpszText = const_cast<wchar_t*>(tipText.c_str());
        SendMessageW(tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&info));
    }
    UpdateTray(s);
    PositionStrip();
    ScheduleNextWake(s); AtomicStatus(s);
    if(settingsWindow){
        std::wstring summary=L"更新 "+LocalTime(s.snapshot.observed)+(s.fromServer?L" · 账户查询":L" · 本地快照");
        if(s.refreshing)summary+=L" · 正在刷新";
        else if(!s.queryError.empty())summary=L"刷新失败 · "+s.queryError+L"，保留上次数据";
        else if(stale)summary+=L" · 等待新数据";
        bool changed=false;
        for(const auto& item:{std::pair<int,std::wstring>{206,summary},{211,quota::Remaining(five)},{212,quota::Remaining(week)}}){
            HWND label=GetDlgItem(settingsWindow,item.first);if(quota_ui::caption(label)!=item.second){SetWindowTextW(label,item.second.c_str());changed=true;}
        }
        if(changed)InvalidateRect(settingsWindow,nullptr,FALSE);
    }
}
// 所有查询都由同一个工作线程合并，按钮只发事件，不阻塞 UI。
void RequestCloud(bool manual){if(manual)manualRefresh=true;if(refreshEvent)SetEvent(refreshEvent);}
void RefreshNow(){Reread();RequestCloud(true);}
// 账户读取使用短生命周期 app-server，查询完成或取消后释放整棵子进程树。
void CloudLoop(){
    unsigned failures=0;
    while(WaitForSingleObject(stopEvent,0)!=WAIT_OBJECT_0){
        ResetEvent(refreshEvent);
        auto config=GetPreferences();
        // 默认仅在本机 Codex 存在时查询。关闭在线刷新后，不创建任何查询进程。
        bool manual=manualRefresh.exchange(false);
        bool enabled=config.online&&!headless&&(manual||!config.onlyWithCodex||codexRunning.load());
        if(enabled){
            auto generation=accountGeneration.load();
            {std::lock_guard<std::mutex> lock(dataMutex);data.refreshing=true;data.lastAttempt=quota::NowMs();}
            SignalState();
            auto result=quota::ReadAccountQuota(quota::FindCodexBinary(),home,stopEvent,15000);
            {std::lock_guard<std::mutex> lock(dataMutex);
                data.refreshing=false;
                if(generation==accountGeneration.load()){
                    if(result.ok){data.snapshot=result.snapshot;data.fromServer=true;data.lastSuccess=result.snapshot.observed;data.queryError.clear();failures=0;}
                    else {data.queryError=result.error;++failures;}
                }
            }
            SignalState();
        }
        config=GetPreferences();
        DWORD delay=config.online?static_cast<DWORD>(config.intervalSeconds*1000):INFINITE;
        // 失败自动退避到最多 15 分钟，手动刷新仍可以立即触发。
        if(config.online&&failures)delay=static_cast<DWORD>(std::min<unsigned long long>(900000,static_cast<unsigned long long>(delay)<<std::min(failures,3u)));
        HANDLE waits[]{stopEvent,refreshEvent};DWORD waited=WaitForMultipleObjects(2,waits,FALSE,delay);
        if(waited==WAIT_OBJECT_0||waited==WAIT_FAILED)break;
        if(waited==WAIT_OBJECT_0+1)ResetEvent(refreshEvent);
        // 合并快速重复点击；最多每 5 秒开始一次真实查询。
        auto s=GetData();if(s.lastAttempt&&quota::NowMs()-s.lastAttempt<5000){if(WaitForSingleObject(stopEvent,static_cast<DWORD>(5000-(quota::NowMs()-s.lastAttempt)))==WAIT_OBJECT_0)break;}
    }
}

// 主面板采用轻量 GDI 双缓冲；字体、画刷只在设置窗口存活期间保留。
void PaintSettings(HWND h,HDC target){
    using namespace quota_ui;RECT r{};GetClientRect(h,&r);if(r.right<=0||r.bottom<=0)return;
    HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,r.right,r.bottom);auto old=SelectObject(dc,bitmap);FillRect(dc,&r,theme.background);
    text(dc,L"Codex 配额",theme.rect(24,20,360,32),theme.title,Text);
    text(dc,L"刷新与显示设置",theme.rect(24,55,380,22),theme.smallFont,Muted);
    auto s=GetData();auto now=quota::NowMs();bool stale=IsStale(s,now);
    std::wstring badge=s.refreshing?L"正在刷新":!s.queryError.empty()?L"刷新失败":stale?L"等待更新":s.fromServer?L"已同步":L"本地快照";
    COLORREF badgeColor=!s.queryError.empty()?Error:stale?Warning:Accent;
    rounded(dc,theme.rect(480,27,116,27),Panel,Border,12);text(dc,badge,theme.rect(480,27,116,27),theme.smallFont,badgeColor,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    rounded(dc,theme.rect(24,94,572,142),Panel,Panel,12);
    text(dc,L"5 小时剩余",theme.rect(44,109,242,22),theme.smallFont,Muted);
    text(dc,L"本周剩余",theme.rect(330,109,242,22),theme.smallFont,Muted);
    HPEN pen=CreatePen(PS_SOLID,1,Border);auto oldPen=SelectObject(dc,pen);MoveToEx(dc,theme.px(308),theme.px(114),nullptr);LineTo(dc,theme.px(308),theme.px(196));MoveToEx(dc,theme.px(24),theme.px(489),nullptr);LineTo(dc,theme.px(596),theme.px(489));SelectObject(dc,oldPen);DeleteObject(pen);
    auto bar=[&](int x,const quota::Window& raw){auto w=CurrentWindow(raw,now);RECT track=theme.rect(x,190,244,4);rounded(dc,track,Border,Border,4);if(w.present){int width=static_cast<int>(std::clamp(100.0-w.used,0.0,100.0)*244/100.0);if(width>1)rounded(dc,theme.rect(x,190,width,4),stale?Muted:100-w.used<=20?Warning:Accent,stale?Muted:100-w.used<=20?Warning:Accent,4);}};
    bar(44,s.snapshot.fiveHour);bar(330,s.snapshot.weekly);
    text(dc,L"自动刷新",theme.rect(24,254,400,24),theme.medium,Text);
    text(dc,L"按设定间隔获取账户最新额度",theme.rect(24,324,530,20),theme.smallFont,Muted);
    text(dc,L"刷新间隔",theme.rect(24,343,274,22),theme.body,Text);
    text(dc,L"旧数据提示",theme.rect(322,343,274,22),theme.body,Text);
    rounded(dc,theme.rect(24,370,274,36),Field,GetFocus()==GetDlgItem(h,202)?Accent:Border);
    rounded(dc,theme.rect(322,370,274,36),Field,GetFocus()==GetDlgItem(h,205)?Accent:Border);
    text(dc,L"秒",theme.rect(248,371,36,34),theme.smallFont,Muted,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    text(dc,L"分钟",theme.rect(540,371,42,34),theme.smallFont,Muted,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    text(dc,L"30–3600 秒",theme.rect(24,411,274,20),theme.smallFont,Muted);
    text(dc,L"1–120 分钟",theme.rect(322,411,274,20),theme.smallFont,Muted);
    text(dc,L"显示方式",theme.rect(24,508,142,26),theme.body,Text);
    BitBlt(target,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
LRESULT CALLBACK SettingsProcedure(HWND h,UINT message,WPARAM wp,LPARAM lp){
    switch(message){
    case WM_CREATE:{
        quota_ui::theme.create(GetDpiForWindow(h));
        auto control=[&](const wchar_t* cls,const wchar_t* title,DWORD style,int id){HWND child=CreateWindowExW(0,cls,title,WS_CHILD|WS_VISIBLE|style,0,0,100,40,h,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);quota_ui::styleControl(child);return child;};
        auto config=GetPreferences();
        control(L"BUTTON",L"自动查询账户额度",BS_AUTOCHECKBOX|WS_TABSTOP,201);
        control(L"EDIT",std::to_wstring(config.intervalSeconds).c_str(),ES_NUMBER|WS_TABSTOP,202);
        control(L"EDIT",std::to_wstring(config.staleMinutes).c_str(),ES_NUMBER|WS_TABSTOP,205);
        control(L"BUTTON",L"仅在 Codex 运行时查询",BS_AUTOCHECKBOX|WS_TABSTOP,203);
        HWND combo=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_TABSTOP,204);
        for(auto text:{L"仅任务栏数字",L"任务栏 + Codex 顶部",L"任务栏 + Codex 左侧"})SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
        SendMessageW(combo,CB_SETCURSEL,config.showInCodex?(config.leftSide?2:1):0,0);
        SendMessageW(GetDlgItem(h,201),BM_SETCHECK,config.online?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(GetDlgItem(h,203),BM_SETCHECK,config.onlyWithCodex?BST_CHECKED:BST_UNCHECKED,0);
        control(L"STATIC",L"等待账户数据…",SS_LEFT|SS_NOPREFIX,206);
        control(L"STATIC",L"-",SS_LEFT|SS_NOPREFIX,211);control(L"STATIC",L"-",SS_LEFT|SS_NOPREFIX,212);
        control(L"STATIC",L"设置将在保存后生效",SS_LEFT|SS_NOPREFIX,207);
        control(L"BUTTON",L"立即刷新",BS_OWNERDRAW|WS_TABSTOP,301);
        control(L"BUTTON",L"保存并应用",BS_OWNERDRAW|WS_TABSTOP,302);
        control(L"BUTTON",L"关闭",BS_OWNERDRAW|WS_TABSTOP,303);
        quota_ui::layout(h);
        return 0;
    }
    case WM_PAINT:{PAINTSTRUCT p{};HDC dc=BeginPaint(h,&p);PaintSettings(h,dc);EndPaint(h,&p);return 0;}
    case WM_PRINTCLIENT:PaintSettings(h,reinterpret_cast<HDC>(wp));return 0;
    case WM_ERASEBKGND:return 1;
    case WM_DRAWITEM:if(quota_ui::drawItem(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)))return TRUE;break;
    case WM_MEASUREITEM:if(reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->CtlID==204){reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->itemHeight=quota_ui::theme.px(32);return TRUE;}break;
    case WM_CTLCOLORSTATIC:{
        using namespace quota_ui;HDC dc=reinterpret_cast<HDC>(wp);int id=GetDlgCtrlID(reinterpret_cast<HWND>(lp));SetBkMode(dc,TRANSPARENT);COLORREF color=Muted;
        if(id==211||id==212){auto s=GetData();auto w=CurrentWindow(id==211?s.snapshot.fiveHour:s.snapshot.weekly,quota::NowMs());color=!w.present||IsStale(s,quota::NowMs())?Muted:100-w.used<=20?Warning:Text;}
        if(id==207){auto messageText=caption(reinterpret_cast<HWND>(lp));color=messageText.find(L"须")!=std::wstring::npos||messageText.find(L"无法")!=std::wstring::npos?Error:messageText.find(L"已保存")!=std::wstring::npos?Accent:Muted;}
        bool onPanel=id==206||id==211||id==212;SetTextColor(dc,color);SetBkColor(dc,onPanel?Panel:Background);return reinterpret_cast<LRESULT>(onPanel?theme.panel:theme.background);
    }
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:SetTextColor(reinterpret_cast<HDC>(wp),quota_ui::Text);SetBkColor(reinterpret_cast<HDC>(wp),quota_ui::Field);return reinterpret_cast<LRESULT>(quota_ui::theme.field);
    case WM_DPICHANGED:{
        // 换屏时只重建设置窗口的字体与布局，不触碰用户输入或刷新配置。
        quota_ui::theme.create(HIWORD(wp));RECT* r=reinterpret_cast<RECT*>(lp);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);quota_ui::layout(h);InvalidateRect(h,nullptr,FALSE);return 0;
    }
    // 保持原生 Tab 顺序与 Enter 保存语义，外观自绘不改变键盘操作。
    case DM_GETDEFID:return MAKELRESULT(302,DC_HASDEFID);
    case WM_COMMAND:
        if(LOWORD(wp)==301){RefreshNow();return 0;}
        if(LOWORD(wp)==303){DestroyWindow(h);return 0;}
        if(LOWORD(wp)==302){
            auto config=GetPreferences();BOOL a=FALSE,b=FALSE;
            config.intervalSeconds=static_cast<int>(GetDlgItemInt(h,202,&a,FALSE));config.staleMinutes=static_cast<int>(GetDlgItemInt(h,205,&b,FALSE));
            if(!a||!b||!quota::ValidSettings(config)){SetWindowTextW(GetDlgItem(h,207),L"查询间隔须为 30–3600 秒；旧数据提示须为 1–120 分钟。");return 0;}
            config.online=SendMessageW(GetDlgItem(h,201),BM_GETCHECK,0,0)==BST_CHECKED;
            config.onlyWithCodex=SendMessageW(GetDlgItem(h,203),BM_GETCHECK,0,0)==BST_CHECKED;
            LRESULT selected=SendMessageW(GetDlgItem(h,204),CB_GETCURSEL,0,0);config.showInCodex=selected>0;config.leftSide=selected==2;
            if(!quota::SaveSettings(settingsPath,config)){SetWindowTextW(GetDlgItem(h,207),L"无法保存本机配置，请检查文件权限。");return 0;}
            {std::lock_guard<std::mutex> lock(preferencesMutex);preferences=config;}showInCodex=config.showInCodex;leftSide=config.leftSide;
            if(!config.online){std::lock_guard<std::mutex> lock(dataMutex);if(data.localSnapshot.observed){data.snapshot=data.localSnapshot;data.fromServer=false;}}
            lastVisible=false;PositionStrip();UpdateState();RequestCloud();SetWindowTextW(GetDlgItem(h,207),L"已保存并应用。");return 0;
        }
        break;
    case WM_CLOSE:DestroyWindow(h);return 0;
    case WM_DESTROY:settingsWindow=nullptr;quota_ui::theme.release();AtomicStatus(GetData());return 0;
    }
    return DefWindowProcW(h,message,wp,lp);
}
void OpenSettings(){
    if(headless)return;if(settingsWindow){ShowWindow(settingsWindow,SW_RESTORE);SetForegroundWindow(settingsWindow);return;}
    std::wstring name=L"QuotaSettings_"+windowClass;
    WNDCLASSEXW cls{sizeof(cls)};cls.hInstance=instance;cls.lpfnWndProc=SettingsProcedure;cls.lpszClassName=name.c_str();cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassExW(&cls);
    UINT d=GetDpiForSystem();RECT bounds{0,0,MulDiv(620,d,96),MulDiv(640,d,96)};AdjustWindowRectExForDpi(&bounds,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,0,d);
    settingsWindow=CreateWindowExW(0,name.c_str(),L"Codex 配额 — 配置",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);
    if(settingsWindow){
        BOOL dark=TRUE;DwmSetWindowAttribute(settingsWindow,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
        // 隐藏启动会覆盖第一次 ShowWindow；用户明确打开配置时强制显示该窗口。
        ShowWindow(settingsWindow,SW_SHOW);SetWindowPos(settingsWindow,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);SetForegroundWindow(settingsWindow);UpdateState();
    }
}
void Command(UINT choice) {
    if (choice == 1 || choice == 2) {
        showInCodex = true;
        leftSide = choice == 2; paintedText.clear(); lastVisible = false;
        WritePrivateProfileStringW(L"display", L"position", leftSide ? L"left" : L"top", settingsPath.c_str());
        WritePrivateProfileStringW(L"display", L"surface", L"taskbar-and-codex", settingsPath.c_str());
        PositionStrip(); UpdateState();
    } else if (choice == 3) RefreshNow();
    else if (choice == 4) PostMessageW(controller, WM_CLOSE, 0, 0);
    else if (choice == 7) OpenSettings();
    else if (choice == 5 || choice == 6) {
        showInCodex = choice == 6;
        WritePrivateProfileStringW(L"display", L"surface", showInCodex ? L"taskbar-and-codex" : L"taskbar", settingsPath.c_str());
        SetWindowLongPtrW(window, GWLP_HWNDPARENT, 0);
        lastVisible = false; PositionStrip();
    }
    // 菜单与配置窗口共用一份显示设置，避免后续保存覆盖菜单选择。
    if(choice==1||choice==2||choice==5||choice==6){std::lock_guard<std::mutex> lock(preferencesMutex);preferences.showInCodex=showInCodex;preferences.leftSide=leftSide;}
}
void Menu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (showInCodex && !leftSide ? MF_CHECKED : 0), 1, L"Codex 内显示在顶部");
    AppendMenuW(menu, MF_STRING | (showInCodex && leftSide ? MF_CHECKED : 0), 2, L"Codex 内显示在左侧");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (!showInCodex ? MF_CHECKED : 0), 5, L"仅任务栏数字");
    AppendMenuW(menu, MF_STRING | (showInCodex ? MF_CHECKED : 0), 6, L"任务栏 + Codex 内显示");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 7, L"配置…");
    AppendMenuW(menu, MF_STRING, 3, L"立即刷新账户 / 本机数据");
    AppendMenuW(menu, MF_STRING, 4, L"退出配额条");
    HWND returnFocus = GetForegroundWindow();
    POINT point{}; GetCursorPos(&point); SetForegroundWindow(controller);
    UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x, point.y, 0, controller, nullptr);
    DestroyMenu(menu); Command(choice);
    PostMessageW(controller, WM_NULL, 0, 0);
    if (choice!=7 && returnFocus && returnFocus != window && IsWindow(returnFocus)) SetForegroundWindow(returnFocus);
    PositionStrip();
}
LRESULT CALLBACK Procedure(HWND h, UINT message, WPARAM wp, LPARAM lp) {
    if (taskbarCreated && message == taskbarCreated) { AddTray(); UpdateState(); return 0; }
    switch (message) {
    case WM_PAINT: { PAINTSTRUCT p{}; HDC dc = BeginPaint(h, &p); if(h==window)PaintStrip(dc); EndPaint(h, &p); return 0; }
    case WM_PRINT:
    case WM_PRINTCLIENT: if(h==window)PaintStrip(reinterpret_cast<HDC>(wp)); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_LBUTTONDBLCLK: RefreshNow(); return 0;
    case WM_CONTEXTMENU: Menu(); return 0;
    case WM_COMMAND: Command(LOWORD(wp)); return 0;
    case StateChanged: UpdateState(); return 0;
    case HostChanged: hostMessagePending = false; PositionStrip(); return 0;
    case TrayMessage: if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU || LOWORD(lp) == NIN_SELECT || LOWORD(lp) == NIN_KEYSELECT) Menu(); return 0;
    case WM_TIMER:
        if(wp==ExpiryTimer){++timerFired;UpdateState();}
        if(wp==RecoveryTimer){PositionStrip();if(workerDone)Reread();UpdateState();}
        return 0;
    case WM_DPICHANGED: UpdateFonts(HIWORD(wp)); lastVisible = false; PositionStrip(); return 0;
    case WM_CLOSE: if(controller)DestroyWindow(controller);return 0;
    case WM_DESTROY:
        if(h!=controller){if(h==window){window=nullptr;tooltip=nullptr;lastVisible=false;if(!closing)PostMessageW(controller,HostChanged,0,0);}return 0;}
        closing = true; SetEvent(stopEvent); KillTimer(h, ExpiryTimer);KillTimer(h,RecoveryTimer);UnhookHost();
        if (foregroundHook) UnhookWinEvent(foregroundHook);
        if(settingsWindow)DestroyWindow(settingsWindow);
        if(window){SetWindowLongPtrW(window,GWLP_HWNDPARENT,0);DestroyWindow(window);}
        if (!headless) RemoveTray();
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, message, wp, lp);
}
}

int WINAPI wWinMain(HINSTANCE app, HINSTANCE, PWSTR, int) {
    instance = app;
    // 本程序只接收数字和列表选择；禁用本 UI 线程的 IME，避免输入法额外模块常驻。
    ImmDisableIME(0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t profile[32768]; DWORD n = GetEnvironmentVariableW(L"CODEX_HOME", profile, static_cast<DWORD>(std::size(profile)));
    if (n > 0 && n < std::size(profile)) home = profile;
    else { GetEnvironmentVariableW(L"USERPROFILE", profile, static_cast<DWORD>(std::size(profile))); home = std::wstring(profile) + L"\\.codex"; }
    int argc = 0; auto argv = CommandLineToArgvW(GetCommandLineW(), &argc); bool overridePosition = false, overrideVisibility = false, quit = false, openConfig=false,localOnly=false;
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--home" && i + 1 < argc) home = argv[++i];
        else if (arg == L"--status" && i + 1 < argc) statusPath = argv[++i];
        else if (arg == L"--headless") headless = true;
        else if (arg == L"--left" || arg == L"--top") { leftSide = arg == L"--left"; overridePosition = true; }
        else if (arg == L"--taskbar" || arg == L"--always-visible") { showInCodex = false; overrideVisibility = true; }
        else if (arg == L"--taskbar-and-codex" || arg == L"--codex-only") { showInCodex = true; overrideVisibility = true; }
        else if (arg == L"--quit") quit = true;
        else if (arg == L"--settings") openConfig=true;
        else if (arg == L"--local-only") localOnly=true;
        else if (arg == L"--host-pid" && i+1<argc) explicitHostPid=static_cast<DWORD>(_wtoi(argv[++i]));
    }
    LocalFree(argv);
    home = std::filesystem::path(home).lexically_normal().wstring(); sessions = home + L"\\sessions";
    auto hash = std::hash<std::wstring>{}(Lower(home)); windowClass = L"CodexQuotaStrip_" + std::to_wstring(hash) + (headless ? L"_headless" : L"");
    std::wstring controllerClass=L"QuotaController_"+std::to_wstring(hash)+(headless?L"_headless":L"");
    if (quit) { HWND existing=FindWindowW(controllerClass.c_str(),nullptr);if(!existing)existing=FindWindowW(windowClass.c_str(),nullptr);if(existing)PostMessageW(existing,WM_CLOSE,0,0);return 0; }
    quota::Handle singleton(CreateMutexW(nullptr, FALSE, (L"Local\\" + windowClass).c_str()));
    if (GetLastError() == ERROR_ALREADY_EXISTS) {if(openConfig)if(HWND existing=FindWindowW(controllerClass.c_str(),nullptr))PostMessageW(existing,WM_COMMAND,7,0);return 0;}
    GetEnvironmentVariableW(L"LOCALAPPDATA", profile, static_cast<DWORD>(std::size(profile)));
    auto settingsDir = std::wstring(profile) + L"\\CodexQuotaStrip";
    if (!headless) CreateDirectoryW(settingsDir.c_str(), nullptr);
    settingsPath = settingsDir + L"\\settings.ini";
    preferences=quota::LoadSettings(settingsPath);if(headless||localOnly)preferences.online=false;
    if (!headless && !overridePosition) { wchar_t saved[16]; GetPrivateProfileStringW(L"display", L"position", L"top", saved, 16, settingsPath.c_str()); leftSide = std::wstring(saved) == L"left"; }
    if (!headless && !overrideVisibility) { wchar_t saved[32]; GetPrivateProfileStringW(L"display", L"surface", L"taskbar", saved, 32, settingsPath.c_str()); showInCodex = std::wstring(saved) == L"taskbar-and-codex"; }
    preferences.showInCodex=showInCodex;preferences.leftSide=leftSide;
    quota::Handle stop(CreateEventW(nullptr, TRUE, FALSE, nullptr)), rescan(CreateEventW(nullptr, TRUE, FALSE, nullptr)),refresh(CreateEventW(nullptr,TRUE,FALSE,nullptr));stopEvent=stop;rescanEvent=rescan;refreshEvent=refresh;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES}; InitCommonControlsEx(&controls);
    WNDCLASSEXW cls{sizeof(cls)}; cls.hInstance = instance; cls.lpfnWndProc = Procedure; cls.lpszClassName = windowClass.c_str(); cls.style = CS_DBLCLKS; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&cls)) return 1;
    cls.lpszClassName=controllerClass.c_str();if(!RegisterClassExW(&cls))return 1;
    controller=CreateWindowExW(WS_EX_TOOLWINDOW,controllerClass.c_str(),L"Codex 配额控制器 2.0",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
    if(!controller)return 1;
    UpdateFonts(96);
    if (!headless) {
        taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated"); AddTray();
        PositionStrip();
        foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, OnHostEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        PositionStrip();
        // 15 秒只检查窗口和监听器状态，不抓屏、不查询网络、不重新读取全量日志。
        SetTimer(controller,RecoveryTimer,15000,nullptr);
    }
    UpdateState(); StartWorker();
    cloudWorker=std::thread([]{try{CloudLoop();}catch(...){std::lock_guard<std::mutex> lock(dataMutex);data.refreshing=false;data.queryError=L"账户刷新异常，请重启配额程序";}});
    if(openConfig)OpenSettings();
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { if(settingsWindow&&IsDialogMessageW(settingsWindow,&message))continue;TranslateMessage(&message);DispatchMessageW(&message); }
    SetEvent(stopEvent); if (worker.joinable()) worker.join();
    if(cloudWorker.joinable())cloudWorker.join();
    if (font) DeleteObject(font); if (smallFont) DeleteObject(smallFont);
    return 0;
}

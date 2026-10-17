#include "QuotaFiles.h"
#include "TrayIcons.h"
#include "QuotaSettings.h"
#include "QuotaProvider.h"
#include "SettingsTheme.h"
#include "Localization.h"
#include "QuotaHistory.h"
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
#include <shlobj.h>

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
HWND historyWindow = nullptr;
quota::HistoryStore historyStore;
quota::HistoryReadResult historyRows;
quota_ui::Theme historyTheme;
bool historyChart = false;
int settingsScroll = 0;
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
WIN32_FILE_ATTRIBUTE_DATA authMetadata{};
bool authMetadataKnown=false,authPresent=false;
DWORD authMetadataError=0;
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
void OpenHistory();
void TranslateHistory();
void RequestCloud(bool manual=false);
void UpdateState();
void PositionStrip();
quota::Settings GetPreferences(){std::lock_guard<std::mutex> lock(preferencesMutex);return preferences;}
const wchar_t* Ui(const wchar_t* value){return quota::Translate(value,GetPreferences().english);}
bool IsStale(const Data& s,long long now){return !s.snapshot.observed || now-s.snapshot.observed>=GetPreferences().staleMinutes*60000LL || (!s.watching&&!s.fromServer);}
int px(int x) { return MulDiv(x, static_cast<int>(dpi), 96); }
std::wstring Lower(std::wstring s) { for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c)); return s; }
bool EndsWith(const std::wstring& s, const wchar_t* suffix) { size_t n = wcslen(suffix); return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0; }
std::filesystem::path HistoryDirectory(const quota::Settings& config){return config.historyDirectory.empty()?std::filesystem::path(settingsPath).parent_path()/L"history":std::filesystem::path(config.historyDirectory);}
void ConfigureHistory(const quota::Settings& config){historyStore.configure(config.historyEnabled,HistoryDirectory(config));}
std::wstring HistoryError(const std::string& error){
    if(error.empty())return {};
    return Ui(error.find("read")!=std::string::npos?L"无法读取额度变化，请检查日志目录和权限。":L"无法记录额度变化，请检查日志目录和权限。");
}
// 调用者持有 dataMutex；只读取认证文件元数据，并与采纳快照共用同一代际边界。
unsigned long long ObserveAuthLocked(){
    WIN32_FILE_ATTRIBUTE_DATA current{};bool present=GetFileAttributesExW((home+L"\\auth.json").c_str(),GetFileExInfoStandard,&current)!=FALSE;
    DWORD error=present?ERROR_SUCCESS:GetLastError();
    bool changed=authMetadataKnown&&(present!=authPresent||error!=authMetadataError||
        (present&&(current.dwFileAttributes!=authMetadata.dwFileAttributes||current.nFileSizeHigh!=authMetadata.nFileSizeHigh||current.nFileSizeLow!=authMetadata.nFileSizeLow||
        CompareFileTime(&current.ftLastWriteTime,&authMetadata.ftLastWriteTime)!=0||CompareFileTime(&current.ftCreationTime,&authMetadata.ftCreationTime)!=0)));
    authMetadataKnown=true;authPresent=present;authMetadataError=error;authMetadata=current;
    if(changed){
        ++accountGeneration;historyStore.reset();data.snapshot={};data.localSnapshot={};data.error=0;data.fromServer=false;data.lastSuccess=0;data.queryError.clear();
    }
    return accountGeneration.load();
}
unsigned long long ObserveAuth(){std::lock_guard<std::mutex> guard(dataMutex);return ObserveAuthLocked();}

quota::Window CurrentWindow(quota::Window w, long long now) {
    // A reset is not evidence that 100% is available. Clear until a newer server snapshot arrives.
    if (w.resets > 0 && now >= w.resets * 1000) w.present = false;
    return w;
}
Data GetData() { std::lock_guard<std::mutex> lock(dataMutex); return data; }
void SignalState() { if (controller) PostMessageW(controller, StateChanged, 0, 0); }

std::wstring LocalTime(long long milliseconds) {
    if (milliseconds <= 0) return Ui(L"尚无数据");
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
    json += ",\"history_open\":"+std::string(historyWindow?"true":"false")+",\"history_enabled\":"+std::string(config.historyEnabled?"true":"false");
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
        if((!online||!data.lastSuccess)&&quota::Apply(snapshot,data.snapshot)){
            data.fromServer=false;
            // 只记录真正采纳的额度事件；UI 重绘、过期提示与文件重扫不产生新记录。
            historyStore.record(snapshot,"local-event");
        }
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
                ObserveAuth();
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
        window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,windowClass.c_str(),Ui(L"Codex 配额条 2.0"),WS_POPUP,0,0,236,26,nullptr,nullptr,instance,nullptr);lastVisible=false;
        // 显示条重建时一并重建其提示框；所属主窗口销毁不会遗留提示窗口。
        if(window){
            tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,window,nullptr,instance,nullptr);
            SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,500);SendMessageW(tooltip,TTM_SETDELAYTIME,TTDT_INITIAL,450);
            TOOLINFOW info{sizeof(info)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=window;info.uId=reinterpret_cast<UINT_PTR>(window);info.lpszText=const_cast<wchar_t*>(Ui(L"等待用量数据"));SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));
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
            DrawTextAt(dc, Ui(L"周"), {0,px(5),r.right,px(25)}, smallFont, RGB(167,167,167), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawTextAt(dc, quota::Remaining(week), {0,px(27),r.right,px(51)}, smallFont, NumberColor(week, stale), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            DrawTextAt(dc, Ui(L"周"), {px(12),0,px(40),r.bottom}, smallFont, RGB(167,167,167));
            DrawTextAt(dc, quota::Remaining(week), {px(45),0,px(106),r.bottom}, font, NumberColor(week, stale));
        }
    } else if (!leftSide) {
        DrawTextAt(dc, Ui(L"5小时"), {px(11),0,px(53),r.bottom}, smallFont, RGB(167,167,167));
        DrawTextAt(dc, quota::Remaining(five), {px(60),0,px(117),r.bottom}, font, NumberColor(five, stale));
        DrawTextAt(dc, Ui(L"周"), {px(133),0,px(153),r.bottom}, smallFont, RGB(167,167,167));
        DrawTextAt(dc, quota::Remaining(week), {px(164),0,px(224),r.bottom}, font, NumberColor(week, stale));
        auto line = CreatePen(PS_SOLID, 1, RGB(60,60,60)); auto p = SelectObject(dc, line); MoveToEx(dc, px(123), px(7), nullptr); LineTo(dc, px(123), r.bottom - px(7)); SelectObject(dc, p); DeleteObject(line);
    } else {
        UINT center = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
        DrawTextAt(dc, L"5h", {0,px(5),r.right,px(25)}, smallFont, RGB(167,167,167), center);
        DrawTextAt(dc, quota::Remaining(five), {0,px(25),r.right,px(50)}, smallFont, NumberColor(five, stale), center);
        DrawTextAt(dc, Ui(L"周"), {0,px(58),r.right,px(78)}, smallFont, RGB(167,167,167), center);
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
    const wchar_t* names[]{Ui(L"5小时"),Ui(L"周")};
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
        std::wstring tip = std::wstring(L"Codex ") + names[i] + Ui(L"剩余 ") + full + Ui(L"\n最后事件：") + LocalTime(s.snapshot.observed);
        if (values[i].resets) tip += Ui(L"\n重置：") + LocalTime(values[i].resets * 1000);
        if (stale && values[i].present) tip += Ui(L"\n旧快照，等待本机新事件");
        if (!values[i].present) tip += Ui(L"\n服务未提供此窗口，或等待重置后的新事件");
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
    tipText = quota::WeeklyOnly(s.snapshot) ? Ui(L"剩余配额：周 ") + quota::Remaining(week) : Ui(L"剩余配额：5小时 ") + quota::Remaining(five) + Ui(L"；周 ") + quota::Remaining(week);
    tipText += Ui(L"\n数据时间：") + LocalTime(s.snapshot.observed);
    tipText += s.fromServer?Ui(L"\n来源：账户查询"):Ui(L"\n来源：本机会话快照");
    if(!s.queryError.empty())tipText+=std::wstring(Ui(L"\n查询失败，保留旧值："))+Ui(s.queryError.c_str());
    if (five.resets) tipText += Ui(L"\n5小时重置时间：") + LocalTime(five.resets * 1000);
    if (week.resets) tipText += Ui(L"\n周重置时间：") + LocalTime(week.resets * 1000);
    if (stale) tipText += Ui(L"\n灰色数值是旧快照，等待本机新事件。");
    if (s.error) tipText += Ui(L"\n本机日志监听不可用，错误码：") + std::to_wstring(s.error);
    auto historyError=HistoryError(historyStore.error());if(!historyError.empty())tipText+=L"\n"+historyError;
    tipText += Ui(L"\n右键可打开配置或立即刷新；双击立即刷新。");
    if (tooltip) {
        TOOLINFOW info{sizeof(info)}; info.hwnd = window; info.uId = reinterpret_cast<UINT_PTR>(window); info.lpszText = const_cast<wchar_t*>(tipText.c_str());
        SendMessageW(tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&info));
    }
    UpdateTray(s);
    PositionStrip();
    ScheduleNextWake(s); AtomicStatus(s);
    if(settingsWindow){
        std::wstring summary=std::wstring(Ui(L"更新 "))+LocalTime(s.snapshot.observed)+(s.fromServer?Ui(L" · 账户查询"):Ui(L" · 本地快照"));
        if(s.refreshing)summary+=Ui(L" · 正在刷新");
        else if(!s.queryError.empty())summary=std::wstring(Ui(L"刷新失败 · "))+Ui(s.queryError.c_str())+Ui(L"，保留上次数据");
        else if(stale)summary+=Ui(L" · 等待新数据");
        bool changed=false;
        for(const auto& item:{std::pair<int,std::wstring>{206,summary},{211,quota::Remaining(five)},{212,quota::Remaining(week)}}){
            HWND label=GetDlgItem(settingsWindow,item.first);if(quota_ui::caption(label)!=item.second){SetWindowTextW(label,item.second.c_str());changed=true;}
        }
        if(changed)InvalidateRect(settingsWindow,nullptr,FALSE);
        SetWindowTextW(GetDlgItem(settingsWindow,213),historyError.c_str());
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
            // 即使 sessions 监听尚未建立，查询前也要隔离账号，不能仅依赖 watcher 通知。
            auto generation=ObserveAuth();
            {std::lock_guard<std::mutex> lock(dataMutex);data.refreshing=true;data.lastAttempt=quota::NowMs();}
            SignalState();
            auto result=quota::ReadAccountQuota(quota::FindCodexBinary(),home,stopEvent,15000);
            {std::lock_guard<std::mutex> lock(dataMutex);
                data.refreshing=false;
                // 元数据复核与结果采纳在同一锁下；切换期间返回的旧账号响应直接丢弃。
                if(generation==ObserveAuthLocked()){
                    if(result.ok){data.snapshot=result.snapshot;data.fromServer=true;data.lastSuccess=result.snapshot.observed;data.queryError.clear();failures=0;historyStore.record(result.snapshot,"account");}
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

// 查看器持有自己的字体与有界快照缓存；关闭窗口后释放，不增加轮询或网页运行时。
std::wstring HistoryValue(const quota::Window& value,const std::optional<double>& change){
    if(!value.present)return L"—";
    wchar_t text[100]{};
    if(change)swprintf_s(text,L"%.1f%% / %+.1f %ls",std::clamp(100.0-value.used,0.0,100.0),*change,Ui(L"百分点"));
    else swprintf_s(text,L"%.1f%% / —",std::clamp(100.0-value.used,0.0,100.0));
    return text;
}
std::wstring HistorySource(const quota::HistoryEntry& entry){
    auto source=Ui(entry.source=="account"?L"账户":L"本地事件");
    auto kind=Ui(entry.kind=="baseline"?L"基线":entry.kind=="reset"?L"重置":L"变化");
    return std::wstring(source)+L" / "+kind;
}
void PopulateHistory(){
    if(!historyWindow)return;
    HWND list=GetDlgItem(historyWindow,401);SendMessageW(list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(list);
    int row=0;
    for(auto it=historyRows.entries.rbegin();it!=historyRows.entries.rend();++it){
        std::wstring values[]{LocalTime(it->recorded),HistoryValue(it->snapshot.fiveHour,it->fiveChange),HistoryValue(it->snapshot.weekly,it->weeklyChange),HistorySource(*it)};
        LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=row;item.pszText=values[0].data();SendMessageW(list,LVM_INSERTITEMW,0,reinterpret_cast<LPARAM>(&item));
        for(int column=1;column<4;++column){item.iSubItem=column;item.pszText=values[column].data();SendMessageW(list,LVM_SETITEMTEXTW,row,reinterpret_cast<LPARAM>(&item));}
        ++row;
    }
    SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,TRUE);
    std::wstring summary;
    if(!historyRows.error.empty())summary=HistoryError(historyRows.error);
    else if(historyRows.entries.empty())summary=Ui(L"暂无额度变化记录");
    else summary=std::to_wstring(historyRows.entries.size())+L" "+Ui(L"条记录")+L" · "+Ui(historyRows.truncated?L"已截取最近记录":L"仅显示最近 30 日，最多 2000 条");
    if(!GetPreferences().historyEnabled)summary+=L" · "+std::wstring(Ui(L"已暂停记录，现有记录仍可查看"));
    SetWindowTextW(GetDlgItem(historyWindow,404),summary.c_str());
    SetWindowTextW(GetDlgItem(historyWindow,406),HistoryDirectory(GetPreferences()).c_str());
    InvalidateRect(GetDlgItem(historyWindow,405),nullptr,FALSE);
}
void RefreshHistory(){historyRows=historyStore.read();PopulateHistory();}
void TranslateHistory(){
    if(!historyWindow)return;
    SetWindowTextW(historyWindow,Ui(L"Codex 配额 — 额度变化"));
    SetWindowTextW(GetDlgItem(historyWindow,402),Ui(historyChart?L"列表":L"图表"));
    SetWindowTextW(GetDlgItem(historyWindow,403),Ui(L"刷新记录"));
    const wchar_t* labels[]{L"时间",L"5h 剩余 / 变化",L"周剩余 / 变化",L"来源 / 事件"};
    for(int column=0;column<4;++column){LVCOLUMNW value{};value.mask=LVCF_TEXT;value.pszText=const_cast<wchar_t*>(Ui(labels[column]));SendMessageW(GetDlgItem(historyWindow,401),LVM_SETCOLUMNW,column,reinterpret_cast<LPARAM>(&value));}
    PopulateHistory();
}
void LayoutHistory(HWND h){
    RECT bounds{};GetClientRect(h,&bounds);int margin=historyTheme.px(20),button=historyTheme.px(120),gap=historyTheme.px(12);
    auto move=[&](int id,int x,int y,int w,int height){SetWindowPos(GetDlgItem(h,id),nullptr,x,y,std::max(1,w),std::max(1,height),SWP_NOZORDER|SWP_NOACTIVATE);SendMessageW(GetDlgItem(h,id),WM_SETFONT,reinterpret_cast<WPARAM>(historyTheme.body),TRUE);};
    move(402,margin,margin,button,historyTheme.px(34));move(403,margin+button+gap,margin,button+historyTheme.px(30),historyTheme.px(34));
    move(406,margin,historyTheme.px(64),bounds.right-2*margin,historyTheme.px(24));
    for(int id:{401,405})move(id,margin,historyTheme.px(100),bounds.right-2*margin,bounds.bottom-historyTheme.px(160));
    move(404,margin,bounds.bottom-historyTheme.px(50),bounds.right-2*margin,historyTheme.px(40));
    int available=std::max<int>(historyTheme.px(720),bounds.right-2*margin-GetSystemMetrics(SM_CXVSCROLL)-4);
    for(int column=0;column<4;++column)ListView_SetColumnWidth(GetDlgItem(h,401),column,available*(column==0?24:column==3?22:27)/100);
}
void PaintHistoryChart(HWND h,HDC target){
    RECT bounds{};GetClientRect(h,&bounds);if(bounds.right<=0||bounds.bottom<=0)return;
    HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,bounds.right,bounds.bottom);auto oldBitmap=SelectObject(dc,bitmap);FillRect(dc,&bounds,historyTheme.background);
    using namespace quota_ui;
    int left=historyTheme.px(52),right=std::max<int>(left+1,bounds.right-historyTheme.px(20)),top=historyTheme.px(40),bottom=std::max<int>(top+1,bounds.bottom-historyTheme.px(52));
    auto label=[&](const std::wstring& value,RECT area,COLORREF color,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE){text(dc,value,area,historyTheme.smallFont,color,flags);};
    label(Ui(L"5 小时剩余"),{left,0,left+historyTheme.px(220),top},Accent);
    label(Ui(L"本周剩余"),{left+historyTheme.px(230),0,right,top},Warning);
    HPEN grid=CreatePen(PS_SOLID,1,Border);auto oldPen=SelectObject(dc,grid);
    for(int percent:{0,50,100}){int y=bottom-(bottom-top)*percent/100;MoveToEx(dc,left,y,nullptr);LineTo(dc,right,y);label(std::to_wstring(percent)+L"%",{0,y-historyTheme.px(10),left-historyTheme.px(8),y+historyTheme.px(10)},Muted,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);}
    SelectObject(dc,oldPen);DeleteObject(grid);
    if(historyRows.entries.empty())label(Ui(L"暂无额度变化记录"),{left,top,right,bottom},Muted,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    else{
        auto first=historyRows.entries.front().recorded,last=historyRows.entries.back().recorded,span=std::max(1LL,last-first);
        for(int series=0;series<2;++series){
            HPEN pen=CreatePen(PS_SOLID,std::max(1,historyTheme.px(2)),series?Warning:Accent);auto previousPen=SelectObject(dc,pen);bool connected=false;
            for(const auto& entry:historyRows.entries){
                const auto& value=series?entry.snapshot.weekly:entry.snapshot.fiveHour;const auto& change=series?entry.weeklyChange:entry.fiveChange;
                if(!value.present){connected=false;continue;}
                int x=left+static_cast<int>((right-left)*static_cast<double>(entry.recorded-first)/span);
                int y=bottom-static_cast<int>((bottom-top)*std::clamp(100.0-value.used,0.0,100.0)/100.0);
                // 基线、周期重置及缺失窗口都断开旧线段；不把周期恢复当作消耗。
                if(connected&&entry.kind=="change"&&change)LineTo(dc,x,y);else MoveToEx(dc,x,y,nullptr);
                SetPixelV(dc,x,y,series?Warning:Accent);connected=true;
            }
            SelectObject(dc,previousPen);DeleteObject(pen);
        }
        label(LocalTime(first),{left,bottom+historyTheme.px(8),right,bounds.bottom},Muted);
        label(LocalTime(last),{left,bottom+historyTheme.px(8),right,bounds.bottom},Muted,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    }
    BitBlt(target,0,0,bounds.right,bounds.bottom,dc,0,0,SRCCOPY);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);
}
LRESULT CALLBACK HistoryChartProcedure(HWND h,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_PAINT){PAINTSTRUCT paint{};HDC dc=BeginPaint(h,&paint);PaintHistoryChart(h,dc);EndPaint(h,&paint);return 0;}
    if(message==WM_PRINTCLIENT){PaintHistoryChart(h,reinterpret_cast<HDC>(wp));return 0;}
    if(message==WM_ERASEBKGND)return 1;
    return DefWindowProcW(h,message,wp,lp);
}
LRESULT CALLBACK HistoryProcedure(HWND h,UINT message,WPARAM wp,LPARAM lp){
    switch(message){
    case WM_CREATE:{
        historyTheme.create(GetDpiForWindow(h));
        auto control=[&](const wchar_t* type,const wchar_t* title,DWORD style,int id){return CreateWindowExW(0,type,title,WS_CHILD|WS_VISIBLE|style,0,0,100,40,h,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);};
        HWND list=control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP,401);
        ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
        ListView_SetBkColor(list,quota_ui::Background);ListView_SetTextBkColor(list,quota_ui::Background);ListView_SetTextColor(list,quota_ui::Text);
        SetWindowTheme(list,L"DarkMode_Explorer",nullptr);
        for(int column=0;column<4;++column){LVCOLUMNW value{};value.mask=LVCF_TEXT|LVCF_WIDTH;value.cx=200;value.pszText=const_cast<wchar_t*>(L"");SendMessageW(list,LVM_INSERTCOLUMNW,column,reinterpret_cast<LPARAM>(&value));}
        control(L"BUTTON",Ui(L"图表"),BS_OWNERDRAW|WS_TABSTOP,402);control(L"BUTTON",Ui(L"刷新记录"),BS_OWNERDRAW|WS_TABSTOP,403);
        control(L"STATIC",L"",SS_LEFT|SS_NOPREFIX,404);control(L"STATIC",L"",SS_LEFT|SS_NOPREFIX|SS_PATHELLIPSIS,406);
        control(L"QuotaHistoryChart",L"",0,405);ShowWindow(GetDlgItem(h,405),SW_HIDE);return 0;
    }
    case WM_SIZE:LayoutHistory(h);return 0;
    case WM_PAINT:{PAINTSTRUCT paint{};HDC dc=BeginPaint(h,&paint);RECT r{};GetClientRect(h,&r);FillRect(dc,&r,historyTheme.background);EndPaint(h,&paint);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_CTLCOLORSTATIC:SetTextColor(reinterpret_cast<HDC>(wp),quota_ui::Muted);SetBkColor(reinterpret_cast<HDC>(wp),quota_ui::Background);return reinterpret_cast<LRESULT>(historyTheme.background);
    case WM_DRAWITEM:{auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);FillRect(item.hDC,&item.rcItem,historyTheme.field);FrameRect(item.hDC,&item.rcItem,historyTheme.panel);quota_ui::text(item.hDC,quota_ui::caption(item.hwndItem),item.rcItem,historyTheme.body,quota_ui::Text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);return TRUE;}
    case WM_COMMAND:
        if(LOWORD(wp)==403){RefreshHistory();return 0;}
        if(LOWORD(wp)==402){historyChart=!historyChart;ShowWindow(GetDlgItem(h,401),historyChart?SW_HIDE:SW_SHOW);ShowWindow(GetDlgItem(h,405),historyChart?SW_SHOW:SW_HIDE);SetWindowTextW(GetDlgItem(h,402),Ui(historyChart?L"列表":L"图表"));return 0;}break;
    case WM_DPICHANGED:{historyTheme.create(HIWORD(wp));auto rect=reinterpret_cast<RECT*>(lp);SetWindowPos(h,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);LayoutHistory(h);return 0;}
    case WM_CLOSE:DestroyWindow(h);return 0;
    case WM_DESTROY:historyWindow=nullptr;historyRows={};historyTheme.release();historyChart=false;AtomicStatus(GetData());return 0;
    }
    return DefWindowProcW(h,message,wp,lp);
}
void OpenHistory(){
    if(headless)return;
    if(historyWindow){ShowWindow(historyWindow,SW_RESTORE);SetForegroundWindow(historyWindow);RefreshHistory();return;}
    WNDCLASSEXW chart{sizeof(chart)};chart.hInstance=instance;chart.lpfnWndProc=HistoryChartProcedure;chart.lpszClassName=L"QuotaHistoryChart";chart.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassExW(&chart);
    std::wstring name=L"QuotaHistory_"+windowClass;WNDCLASSEXW cls{sizeof(cls)};cls.hInstance=instance;cls.lpfnWndProc=HistoryProcedure;cls.lpszClassName=name.c_str();cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassExW(&cls);
    UINT d=GetDpiForSystem();RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    int width=std::min<int>(MulDiv(1040,d,96),work.right-work.left),height=std::min<int>(MulDiv(660,d,96),work.bottom-work.top);
    historyWindow=CreateWindowExW(0,name.c_str(),Ui(L"Codex 配额 — 额度变化"),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,nullptr,nullptr,instance,nullptr);
    if(historyWindow){BOOL dark=TRUE;DwmSetWindowAttribute(historyWindow,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));LayoutHistory(historyWindow);TranslateHistory();RefreshHistory();ShowWindow(historyWindow,SW_SHOW);SetWindowPos(historyWindow,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);SetForegroundWindow(historyWindow);AtomicStatus(GetData());}
}

void LayoutSettings(HWND h){
    quota_ui::layout(h);const auto& theme=quota_ui::theme;
    auto move=[&](int id,int x,int y,int width,int height){auto r=theme.rect(x,y,width,height);SetWindowPos(GetDlgItem(h,id),nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);SendMessageW(GetDlgItem(h,id),WM_SETFONT,reinterpret_cast<WPARAM>(theme.body),TRUE);};
    move(209,24,592,572,30);move(210,36,653,430,24);move(304,486,645,110,36);move(305,24,695,220,36);
    move(213,24,734,572,24);move(207,24,764,572,26);move(301,24,804,132,36);move(302,346,804,152,36);move(303,512,804,84,36);
    RECT bounds{};GetClientRect(h,&bounds);int total=theme.px(858);settingsScroll=std::clamp(settingsScroll,0,std::max<int>(0,total-bounds.bottom));
    SCROLLINFO scroll{sizeof(scroll)};scroll.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;scroll.nMax=total-1;scroll.nPage=static_cast<UINT>(bounds.bottom);scroll.nPos=settingsScroll;SetScrollInfo(h,SB_VERT,&scroll,TRUE);
    // 小屏幕或高 DPI 时滚动整个设置内容，让新增目录与保存按钮仍可到达。
    if(settingsScroll)for(HWND child=GetWindow(h,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
        RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,h,reinterpret_cast<POINT*>(&r),2);SetWindowPos(child,nullptr,r.left,r.top-settingsScroll,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
}
LRESULT CALLBACK HistoryToggleProcedure(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
    if(message==WM_PAINT||message==WM_PRINTCLIENT){
        PAINTSTRUCT paint{};HDC dc=message==WM_PAINT?BeginPaint(h,&paint):reinterpret_cast<HDC>(wp);const auto& theme=quota_ui::theme;
        RECT bounds{};GetClientRect(h,&bounds);FillRect(dc,&bounds,theme.background);bool checked=SendMessageW(h,BM_GETCHECK,0,0)==BST_CHECKED;
        RECT label=bounds;label.right-=theme.px(50);quota_ui::text(dc,quota_ui::caption(h),label,theme.body,quota_ui::Text);
        RECT track{bounds.right-theme.px(36),(bounds.bottom-theme.px(20))/2,bounds.right,(bounds.bottom+theme.px(20))/2};quota_ui::rounded(dc,track,checked?quota_ui::Accent:quota_ui::Border,quota_ui::Border,20);
        int x=checked?track.right-theme.px(17):track.left+theme.px(3);quota_ui::rounded(dc,{x,track.top+theme.px(3),x+theme.px(14),track.bottom-theme.px(3)},checked?quota_ui::AccentText:quota_ui::Text,checked?quota_ui::AccentText:quota_ui::Text,14);
        if(GetFocus()==h){InflateRect(&bounds,-1,-1);DrawFocusRect(dc,&bounds);}if(message==WM_PAINT)EndPaint(h,&paint);return 0;
    }
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_SETFOCUS||message==WM_KILLFOCUS||message==BM_SETCHECK)InvalidateRect(h,nullptr,FALSE);
    if(message==WM_NCDESTROY)RemoveWindowSubclass(h,HistoryToggleProcedure,id);
    return DefSubclassProc(h,message,wp,lp);
}
int CALLBACK HistoryFolderCallback(HWND h,UINT message,LPARAM,LPARAM directory){if(message==BFFM_INITIALIZED&&directory)SendMessageW(h,BFFM_SETSELECTIONW,TRUE,directory);return 0;}
void BrowseHistoryDirectory(HWND h){
    wchar_t directory[32768]{};GetDlgItemTextW(h,210,directory,32768);auto current=directory[0]?std::wstring(directory):HistoryDirectory(GetPreferences()).wstring();
    BROWSEINFOW browse{};browse.hwndOwner=h;browse.lpszTitle=Ui(L"选择额度日志目录");browse.ulFlags=BIF_RETURNONLYFSDIRS|BIF_EDITBOX|BIF_VALIDATE;browse.lpfn=HistoryFolderCallback;browse.lParam=reinterpret_cast<LPARAM>(current.c_str());
    auto item=SHBrowseForFolderW(&browse);if(!item)return;
    if(SHGetPathFromIDListEx(item,directory,32768,0))SetDlgItemTextW(h,210,directory);
    IMalloc* allocator=nullptr;if(SUCCEEDED(SHGetMalloc(&allocator))){allocator->Free(item);allocator->Release();}
}

// 主面板采用轻量 GDI 双缓冲；字体、画刷只在设置窗口存活期间保留。
void PaintSettings(HWND h,HDC target){
    using namespace quota_ui;RECT r{};GetClientRect(h,&r);if(r.right<=0||r.bottom<=0)return;
    HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,r.right,r.bottom);auto old=SelectObject(dc,bitmap);FillRect(dc,&r,theme.background);
    SetViewportOrgEx(dc,0,-settingsScroll,nullptr);
    text(dc,Ui(L"Codex 配额"),theme.rect(24,20,360,32),theme.title,Text);
    text(dc,Ui(L"刷新与显示设置"),theme.rect(24,55,380,22),theme.smallFont,Muted);
    auto s=GetData();auto now=quota::NowMs();bool stale=IsStale(s,now);
    std::wstring badge=s.refreshing?Ui(L"正在刷新"):!s.queryError.empty()?Ui(L"刷新失败"):stale?Ui(L"等待更新"):s.fromServer?Ui(L"已同步"):Ui(L"本地快照");
    COLORREF badgeColor=!s.queryError.empty()?Error:stale?Warning:Accent;
    rounded(dc,theme.rect(480,27,116,27),Panel,Border,12);text(dc,badge,theme.rect(480,27,116,27),theme.smallFont,badgeColor,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    rounded(dc,theme.rect(24,94,572,142),Panel,Panel,12);
    text(dc,Ui(L"5 小时剩余"),theme.rect(44,109,242,22),theme.smallFont,Muted);
    text(dc,Ui(L"本周剩余"),theme.rect(330,109,242,22),theme.smallFont,Muted);
    HPEN pen=CreatePen(PS_SOLID,1,Border);auto oldPen=SelectObject(dc,pen);MoveToEx(dc,theme.px(308),theme.px(114),nullptr);LineTo(dc,theme.px(308),theme.px(196));MoveToEx(dc,theme.px(24),theme.px(489),nullptr);LineTo(dc,theme.px(596),theme.px(489));SelectObject(dc,oldPen);DeleteObject(pen);
    auto bar=[&](int x,const quota::Window& raw){auto w=CurrentWindow(raw,now);RECT track=theme.rect(x,190,244,4);rounded(dc,track,Border,Border,4);if(w.present){int width=static_cast<int>(std::clamp(100.0-w.used,0.0,100.0)*244/100.0);if(width>1)rounded(dc,theme.rect(x,190,width,4),stale?Muted:100-w.used<=20?Warning:Accent,stale?Muted:100-w.used<=20?Warning:Accent,4);}};
    bar(44,s.snapshot.fiveHour);bar(330,s.snapshot.weekly);
    text(dc,Ui(L"自动刷新"),theme.rect(24,254,400,24),theme.medium,Text);
    text(dc,Ui(L"按设定间隔获取账户最新额度"),theme.rect(24,324,530,20),theme.smallFont,Muted);
    text(dc,Ui(L"刷新间隔"),theme.rect(24,343,274,22),theme.body,Text);
    text(dc,Ui(L"旧数据提示"),theme.rect(322,343,274,22),theme.body,Text);
    rounded(dc,theme.rect(24,370,274,36),Field,GetFocus()==GetDlgItem(h,202)?Accent:Border);
    rounded(dc,theme.rect(322,370,274,36),Field,GetFocus()==GetDlgItem(h,205)?Accent:Border);
    text(dc,Ui(L"秒"),theme.rect(248,371,36,34),theme.smallFont,Muted,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    text(dc,Ui(L"分钟"),theme.rect(540,371,42,34),theme.smallFont,Muted,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    text(dc,Ui(L"30–3600 秒"),theme.rect(24,411,274,20),theme.smallFont,Muted);
    text(dc,Ui(L"1–120 分钟"),theme.rect(322,411,274,20),theme.smallFont,Muted);
    text(dc,Ui(L"显示方式"),theme.rect(24,508,142,26),theme.body,Text);
    text(dc,Ui(L"界面语言"),theme.rect(24,552,142,26),theme.body,Text);
    text(dc,Ui(L"日志目录"),theme.rect(24,622,572,22),theme.smallFont,Muted);
    rounded(dc,theme.rect(24,645,450,36),Field,GetFocus()==GetDlgItem(h,210)?Accent:Border);
    text(dc,Ui(L"留空使用默认 history 目录"),theme.rect(260,697,336,32),theme.smallFont,Muted,DT_LEFT|DT_WORDBREAK);
    SetViewportOrgEx(dc,0,0,nullptr);
    BitBlt(target,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
// 只改固定文案和下拉选项，保留刷新间隔等尚未保存的输入。
void TranslateSettings(HWND h){
    SetWindowTextW(h,Ui(L"Codex 配额 — 配置"));
    for(const auto& item:{std::pair<int,const wchar_t*>{201,L"自动查询账户额度"},{203,L"仅在 Codex 运行时查询"},{209,L"记录额度变化"},{304,L"浏览…"},{305,L"查看额度变化"},{301,L"立即刷新"},{302,L"保存并应用"},{303,L"关闭"}})SetWindowTextW(GetDlgItem(h,item.first),Ui(item.second));
    HWND combo=GetDlgItem(h,204);LRESULT selected=SendMessageW(combo,CB_GETCURSEL,0,0);SendMessageW(combo,CB_RESETCONTENT,0,0);
    for(auto text:{L"仅任务栏数字",L"任务栏 + Codex 顶部",L"任务栏 + Codex 左侧"})SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(Ui(text)));
    SendMessageW(combo,CB_SETCURSEL,selected,0);InvalidateRect(h,nullptr,FALSE);
    paintedText.clear();if(window)InvalidateRect(window,nullptr,FALSE);
    TranslateHistory();
}
LRESULT CALLBACK SettingsProcedure(HWND h,UINT message,WPARAM wp,LPARAM lp){
    switch(message){
    case WM_CREATE:{
        quota_ui::theme.create(GetDpiForWindow(h));
        auto control=[&](const wchar_t* cls,const wchar_t* title,DWORD style,int id){HWND child=CreateWindowExW(0,cls,title,WS_CHILD|WS_VISIBLE|style,0,0,100,40,h,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);quota_ui::styleControl(child);return child;};
        auto config=GetPreferences();
        control(L"BUTTON",Ui(L"自动查询账户额度"),BS_AUTOCHECKBOX|WS_TABSTOP,201);
        control(L"EDIT",std::to_wstring(config.intervalSeconds).c_str(),ES_NUMBER|WS_TABSTOP,202);
        control(L"EDIT",std::to_wstring(config.staleMinutes).c_str(),ES_NUMBER|WS_TABSTOP,205);
        control(L"BUTTON",Ui(L"仅在 Codex 运行时查询"),BS_AUTOCHECKBOX|WS_TABSTOP,203);
        HWND combo=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_TABSTOP,204);
        for(auto text:{Ui(L"仅任务栏数字"),Ui(L"任务栏 + Codex 顶部"),Ui(L"任务栏 + Codex 左侧")})SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
        SendMessageW(combo,CB_SETCURSEL,config.showInCodex?(config.leftSide?2:1):0,0);
        HWND language=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_TABSTOP,208);
        for(auto text:{L"中文",L"English"})SendMessageW(language,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
        SendMessageW(language,CB_SETCURSEL,config.english?1:0,0);
        SendMessageW(GetDlgItem(h,201),BM_SETCHECK,config.online?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(GetDlgItem(h,203),BM_SETCHECK,config.onlyWithCodex?BST_CHECKED:BST_UNCHECKED,0);
        HWND enabled=control(L"BUTTON",Ui(L"记录额度变化"),BS_AUTOCHECKBOX|WS_TABSTOP,209);SetWindowSubclass(enabled,HistoryToggleProcedure,1,0);SendMessageW(enabled,BM_SETCHECK,config.historyEnabled?BST_CHECKED:BST_UNCHECKED,0);
        control(L"EDIT",config.historyDirectory.c_str(),ES_AUTOHSCROLL|WS_TABSTOP,210);
        control(L"BUTTON",Ui(L"浏览…"),BS_OWNERDRAW|WS_TABSTOP,304);control(L"BUTTON",Ui(L"查看额度变化"),BS_OWNERDRAW|WS_TABSTOP,305);
        control(L"STATIC",L"",SS_LEFT|SS_NOPREFIX,213);
        control(L"STATIC",Ui(L"等待账户数据…"),SS_LEFT|SS_NOPREFIX,206);
        control(L"STATIC",L"-",SS_LEFT|SS_NOPREFIX,211);control(L"STATIC",L"-",SS_LEFT|SS_NOPREFIX,212);
        control(L"STATIC",Ui(L"设置将在保存后生效"),SS_LEFT|SS_NOPREFIX,207);
        control(L"BUTTON",Ui(L"立即刷新"),BS_OWNERDRAW|WS_TABSTOP,301);
        control(L"BUTTON",Ui(L"保存并应用"),BS_OWNERDRAW|WS_TABSTOP,302);
        control(L"BUTTON",Ui(L"关闭"),BS_OWNERDRAW|WS_TABSTOP,303);
        settingsScroll=0;LayoutSettings(h);
        return 0;
    }
    case WM_PAINT:{PAINTSTRUCT p{};HDC dc=BeginPaint(h,&p);PaintSettings(h,dc);EndPaint(h,&p);return 0;}
    case WM_PRINTCLIENT:PaintSettings(h,reinterpret_cast<HDC>(wp));return 0;
    case WM_ERASEBKGND:return 1;
    case WM_SIZE:LayoutSettings(h);return 0;
    case WM_VSCROLL:{
        SCROLLINFO scroll{sizeof(scroll)};scroll.fMask=SIF_ALL;GetScrollInfo(h,SB_VERT,&scroll);int next=settingsScroll;
        switch(LOWORD(wp)){case SB_TOP:next=0;break;case SB_BOTTOM:next=scroll.nMax;break;case SB_LINEUP:next-=quota_ui::theme.px(30);break;case SB_LINEDOWN:next+=quota_ui::theme.px(30);break;case SB_PAGEUP:next-=scroll.nPage;break;case SB_PAGEDOWN:next+=scroll.nPage;break;case SB_THUMBTRACK:next=scroll.nTrackPos;break;}
        settingsScroll=next;LayoutSettings(h);InvalidateRect(h,nullptr,FALSE);return 0;
    }
    case WM_MOUSEWHEEL:settingsScroll-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*quota_ui::theme.px(60);LayoutSettings(h);InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_DRAWITEM:if(quota_ui::drawItem(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)))return TRUE;break;
    case WM_MEASUREITEM:if(reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->CtlID==204||reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->CtlID==208){reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->itemHeight=quota_ui::theme.px(32);return TRUE;}break;
    case WM_CTLCOLORSTATIC:{
        using namespace quota_ui;HDC dc=reinterpret_cast<HDC>(wp);int id=GetDlgCtrlID(reinterpret_cast<HWND>(lp));SetBkMode(dc,TRANSPARENT);COLORREF color=id==213?Error:Muted;
        if(id==211||id==212){auto s=GetData();auto w=CurrentWindow(id==211?s.snapshot.fiveHour:s.snapshot.weekly,quota::NowMs());color=!w.present||IsStale(s,quota::NowMs())?Muted:100-w.used<=20?Warning:Text;}
        if(id==207){auto messageText=caption(reinterpret_cast<HWND>(lp));color=messageText.find(L"须")!=std::wstring::npos||messageText.find(L"无法")!=std::wstring::npos||messageText.find(L"Use ")==0||messageText.find(L"Cannot")==0?Error:messageText.find(L"已保存")!=std::wstring::npos||messageText.find(L"Saved")==0?Accent:Muted;}
        bool onPanel=id==206||id==211||id==212;SetTextColor(dc,color);SetBkColor(dc,onPanel?Panel:Background);return reinterpret_cast<LRESULT>(onPanel?theme.panel:theme.background);
    }
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:SetTextColor(reinterpret_cast<HDC>(wp),quota_ui::Text);SetBkColor(reinterpret_cast<HDC>(wp),quota_ui::Field);return reinterpret_cast<LRESULT>(quota_ui::theme.field);
    case WM_DPICHANGED:{
        // 换屏时只重建设置窗口的字体与布局，不触碰用户输入或刷新配置。
        quota_ui::theme.create(HIWORD(wp));RECT* r=reinterpret_cast<RECT*>(lp);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);LayoutSettings(h);InvalidateRect(h,nullptr,FALSE);return 0;
    }
    // 保持原生 Tab 顺序与 Enter 保存语义，外观自绘不改变键盘操作。
    case DM_GETDEFID:return MAKELRESULT(302,DC_HASDEFID);
    case WM_COMMAND:
        if(LOWORD(wp)==301){RefreshNow();return 0;}
        if(LOWORD(wp)==303){DestroyWindow(h);return 0;}
        if(LOWORD(wp)==304){BrowseHistoryDirectory(h);return 0;}
        if(LOWORD(wp)==305){OpenHistory();return 0;}
        if(LOWORD(wp)==302){
            auto config=GetPreferences();BOOL a=FALSE,b=FALSE;
            config.intervalSeconds=static_cast<int>(GetDlgItemInt(h,202,&a,FALSE));config.staleMinutes=static_cast<int>(GetDlgItemInt(h,205,&b,FALSE));
            if(!a||!b||!quota::ValidSettings(config)){SetWindowTextW(GetDlgItem(h,207),Ui(L"查询间隔须为 30–3600 秒；旧数据提示须为 1–120 分钟。"));return 0;}
            config.online=SendMessageW(GetDlgItem(h,201),BM_GETCHECK,0,0)==BST_CHECKED;
            config.onlyWithCodex=SendMessageW(GetDlgItem(h,203),BM_GETCHECK,0,0)==BST_CHECKED;
            LRESULT selected=SendMessageW(GetDlgItem(h,204),CB_GETCURSEL,0,0);config.showInCodex=selected>0;config.leftSide=selected==2;
            config.english=SendMessageW(GetDlgItem(h,208),CB_GETCURSEL,0,0)==1;
            config.historyEnabled=SendMessageW(GetDlgItem(h,209),BM_GETCHECK,0,0)==BST_CHECKED;
            wchar_t directory[32768]{};GetDlgItemTextW(h,210,directory,32768);config.historyDirectory=directory;
            if(config.historyEnabled){std::string error;if(!HistoryDirectory(config).is_absolute()||!quota::HistoryStore::ValidateDirectory(HistoryDirectory(config),error)){SetWindowTextW(GetDlgItem(h,207),Ui(L"日志目录须为可写的绝对路径。"));return 0;}}
            if(!quota::SaveSettings(settingsPath,config)){SetWindowTextW(GetDlgItem(h,207),Ui(L"无法保存本机配置，请检查文件权限。"));return 0;}
            {std::lock_guard<std::mutex> lock(preferencesMutex);preferences=config;}showInCodex=config.showInCodex;leftSide=config.leftSide;
            ConfigureHistory(config);
            if(!config.online){std::lock_guard<std::mutex> lock(dataMutex);if(data.localSnapshot.observed){data.snapshot=data.localSnapshot;data.fromServer=false;}}
            TranslateSettings(h);lastVisible=false;PositionStrip();UpdateState();RequestCloud();SetWindowTextW(GetDlgItem(h,207),Ui(L"已保存并应用。"));return 0;
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
    UINT d=GetDpiForSystem();RECT bounds{0,0,MulDiv(620,d,96),MulDiv(858,d,96)};DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_VSCROLL;AdjustWindowRectExForDpi(&bounds,style,FALSE,0,d);
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    int width=bounds.right-bounds.left,height=std::min(bounds.bottom-bounds.top,work.bottom-work.top);
    settingsWindow=CreateWindowExW(0,name.c_str(),Ui(L"Codex 配额 — 配置"),style,work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,nullptr,nullptr,instance,nullptr);
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
    else if (choice == 8) OpenHistory();
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
    AppendMenuW(menu, MF_STRING | (showInCodex && !leftSide ? MF_CHECKED : 0), 1, Ui(L"Codex 内显示在顶部"));
    AppendMenuW(menu, MF_STRING | (showInCodex && leftSide ? MF_CHECKED : 0), 2, Ui(L"Codex 内显示在左侧"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (!showInCodex ? MF_CHECKED : 0), 5, Ui(L"仅任务栏数字"));
    AppendMenuW(menu, MF_STRING | (showInCodex ? MF_CHECKED : 0), 6, Ui(L"任务栏 + Codex 内显示"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 7, Ui(L"配置…"));
    AppendMenuW(menu, MF_STRING, 8, Ui(L"查看变化…"));
    AppendMenuW(menu, MF_STRING, 3, Ui(L"立即刷新账户 / 本机数据"));
    AppendMenuW(menu, MF_STRING, 4, Ui(L"退出配额条"));
    HWND returnFocus = GetForegroundWindow();
    POINT point{}; GetCursorPos(&point); SetForegroundWindow(controller);
    UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x, point.y, 0, controller, nullptr);
    DestroyMenu(menu); Command(choice);
    PostMessageW(controller, WM_NULL, 0, 0);
    if (choice!=7 && choice!=8 && returnFocus && returnFocus != window && IsWindow(returnFocus)) SetForegroundWindow(returnFocus);
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
        if(historyWindow)DestroyWindow(historyWindow);
        if(window){SetWindowLongPtrW(window,GWLP_HWNDPARENT,0);DestroyWindow(window);}
        if (!headless) RemoveTray();
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, message, wp, lp);
}
}

int WINAPI wWinMain(HINSTANCE app, HINSTANCE, PWSTR, int) {
    instance = app;
    // 日志目录允许中文路径；保留系统输入法，数字输入仍由 ES_NUMBER 约束。
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t profile[32768]; DWORD n = GetEnvironmentVariableW(L"CODEX_HOME", profile, static_cast<DWORD>(std::size(profile)));
    if (n > 0 && n < std::size(profile)) home = profile;
    else { GetEnvironmentVariableW(L"USERPROFILE", profile, static_cast<DWORD>(std::size(profile))); home = std::wstring(profile) + L"\\.codex"; }
    int argc = 0; auto argv = CommandLineToArgvW(GetCommandLineW(), &argc); bool overridePosition = false, overrideVisibility = false, quit = false, openConfig=false,openHistory=false,localOnly=false;
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
        else if (arg == L"--history") openHistory=true;
        else if (arg == L"--local-only") localOnly=true;
        else if (arg == L"--host-pid" && i+1<argc) explicitHostPid=static_cast<DWORD>(_wtoi(argv[++i]));
    }
    LocalFree(argv);
    home = std::filesystem::path(home).lexically_normal().wstring(); sessions = home + L"\\sessions";
    auto hash = std::hash<std::wstring>{}(Lower(home)); windowClass = L"CodexQuotaStrip_" + std::to_wstring(hash) + (headless ? L"_headless" : L"");
    std::wstring controllerClass=L"QuotaController_"+std::to_wstring(hash)+(headless?L"_headless":L"");
    if (quit) { HWND existing=FindWindowW(controllerClass.c_str(),nullptr);if(!existing)existing=FindWindowW(windowClass.c_str(),nullptr);if(existing)PostMessageW(existing,WM_CLOSE,0,0);return 0; }
    quota::Handle singleton(CreateMutexW(nullptr, FALSE, (L"Local\\" + windowClass).c_str()));
    if (GetLastError() == ERROR_ALREADY_EXISTS) {if(openConfig||openHistory)if(HWND existing=FindWindowW(controllerClass.c_str(),nullptr))PostMessageW(existing,WM_COMMAND,openHistory?8:7,0);return 0;}
    GetEnvironmentVariableW(L"LOCALAPPDATA", profile, static_cast<DWORD>(std::size(profile)));
    auto settingsDir = std::wstring(profile) + L"\\CodexQuotaStrip";
    if (!headless) CreateDirectoryW(settingsDir.c_str(), nullptr);
    settingsPath = settingsDir + L"\\settings.ini";
    preferences=quota::LoadSettings(settingsPath);if(headless||localOnly)preferences.online=false;
    ConfigureHistory(preferences);
    ObserveAuth();
    if (!headless && !overridePosition) { wchar_t saved[16]; GetPrivateProfileStringW(L"display", L"position", L"top", saved, 16, settingsPath.c_str()); leftSide = std::wstring(saved) == L"left"; }
    if (!headless && !overrideVisibility) { wchar_t saved[32]; GetPrivateProfileStringW(L"display", L"surface", L"taskbar", saved, 32, settingsPath.c_str()); showInCodex = std::wstring(saved) == L"taskbar-and-codex"; }
    preferences.showInCodex=showInCodex;preferences.leftSide=leftSide;
    quota::Handle stop(CreateEventW(nullptr, TRUE, FALSE, nullptr)), rescan(CreateEventW(nullptr, TRUE, FALSE, nullptr)),refresh(CreateEventW(nullptr,TRUE,FALSE,nullptr));stopEvent=stop;rescanEvent=rescan;refreshEvent=refresh;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES}; InitCommonControlsEx(&controls);
    WNDCLASSEXW cls{sizeof(cls)}; cls.hInstance = instance; cls.lpfnWndProc = Procedure; cls.lpszClassName = windowClass.c_str(); cls.style = CS_DBLCLKS; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&cls)) return 1;
    cls.lpszClassName=controllerClass.c_str();if(!RegisterClassExW(&cls))return 1;
    controller=CreateWindowExW(WS_EX_TOOLWINDOW,controllerClass.c_str(),Ui(L"Codex 配额控制器 2.0"),WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
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
    if(openHistory)OpenHistory();
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { if(settingsWindow&&IsDialogMessageW(settingsWindow,&message))continue;if(historyWindow&&IsDialogMessageW(historyWindow,&message))continue;TranslateMessage(&message);DispatchMessageW(&message); }
    SetEvent(stopEvent); if (worker.joinable()) worker.join();
    if(cloudWorker.joinable())cloudWorker.join();
    if (font) DeleteObject(font); if (smallFont) DeleteObject(smallFont);
    return 0;
}

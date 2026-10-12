#pragma once
#include <string>
#include <cstring>
namespace nativequota {
// 固定 UI 文案采用内置字典，用户路径与账号数据不送入翻译接口。
inline const char* Translate(const char* english, bool chinese) {
    if (!chinese) return english;
    struct Entry { const char* english; const char* chinese; };
    static const Entry entries[] = {
        {"Refresh now","立即刷新"},{"Settings…","设置…"},{"Quit","退出"},{"Save and apply","保存并应用"},{"Codex Quota — Settings","Codex 配额 — 设置"},
        {"Language","界面语言"},{"Query Codex account online","在线查询 Codex 账号额度"},
        {"Query only while the host app is open","仅在宿主应用打开时查询"},{"Query only while the host application is open","仅在宿主应用打开时查询"},
        {"Show window-following overlay (X11)","显示窗口跟随悬浮条（X11）"},{"Show window-following overlay","显示窗口跟随悬浮条"},
        {"Refresh interval (30–3600 s)","刷新间隔（30–3600 秒）"},{"Stale after (1–120 min)","旧数据提示（1–120 分钟）"},
        {"Overlay position","悬浮条位置"},{"Top","顶部"},{"Left","左侧"},{"Codex data directory","Codex 数据目录"},
        {"Codex executable (blank = auto)","Codex 路径（留空自动发现）"},{"Host window class contains","宿主窗口类包含"},{"Host application contains","宿主应用名包含"},
        {"Allow window following…","允许窗口跟随…"},{"Allow Accessibility…","允许辅助功能访问…"},
        {"Refreshing…","正在刷新…"},{"Online queries disabled","在线查询已关闭"},{"Account quota","账号额度"},
        {"\nAllow Accessibility access to follow Codex windows.","\n请授予辅助功能权限以跟随 Codex 窗口。"},
        {"\nWayland: overlay/host detection unavailable. Scheduling uses the interval.","\nWayland 不支持悬浮定位／宿主检测，查询按间隔运行。"},
        {"Cannot save: check absolute home path, values and permissions","无法保存：请检查数据目录绝对路径、数值和权限"},
        {"Cannot save: check absolute home path, values and permissions.","无法保存：请检查数据目录绝对路径、数值和权限。"},
        {"Enter valid whole-number intervals.","请输入有效的整数间隔。"},{"Codex five-hour remaining quota","Codex 5 小时剩余额度"},{"Codex weekly remaining quota","Codex 周剩余额度"},
        {"Query cancelled","查询已取消"},{"Codex executable not found; set its path in Settings","未找到 Codex，请在设置中指定路径"},
        {"Cannot start Codex app-server","无法启动 Codex app-server"},{"Codex account query failed","Codex 账号查询失败"},
        {"Invalid Codex proxy configuration; check config.toml","Codex 代理配置无效，请检查 config.toml"},
        {"Codex response exceeded size limit","Codex 响应超出大小限制"},{"Codex app-server exited","Codex app-server 已退出"},
        {"Codex query timed out","Codex 查询超时"},{"Cannot communicate with Codex","无法与 Codex 通信"},
        {"Cannot read Codex account quota","无法读取 Codex 账号额度"},{"Codex returned no supported quota windows","Codex 未返回支持的额度窗口"},
        {"Codex query failed","Codex 查询失败"},{"Account changed; waiting for new quota","账号已变化，等待新额度"},{"Account changed; refresh again","账号已变化，请重新刷新"}
    };
    for (const auto& entry : entries) if (std::strcmp(english,entry.english)==0) return entry.chinese;
    return english;
}
}

#pragma once
#include <string>
#include <cwchar>
namespace quota {
// 固定文案本地翻译，不请求网络翻译；未知错误只显示原文，路径及账号数据不做替换。
inline const wchar_t* Translate(const wchar_t* chinese, bool english) {
    if (!english) return chinese;
    struct Text { const wchar_t* chinese; const wchar_t* english; };
    static const Text entries[] = {
        {L"Codex 配额",L"Codex Quota"},{L"Codex 配额 — 配置",L"Codex Quota — Settings"},
        {L"刷新与显示设置",L"Refresh and display settings"},{L"5 小时剩余",L"Five-hour remaining"},{L"本周剩余",L"Weekly remaining"},
        {L"自动刷新",L"Automatic refresh"},{L"按设定间隔获取账户最新额度",L"Fetch current account quota at the selected interval"},
        {L"刷新间隔",L"Refresh interval"},{L"旧数据提示",L"Stale after"},{L"秒",L"s"},{L"分钟",L"min"},{L"30–3600 秒",L"30–3600 seconds"},{L"1–120 分钟",L"1–120 minutes"},
        {L"显示方式",L"Display"},{L"界面语言",L"Language"},{L"自动查询账户额度",L"Query account quota automatically"},{L"仅在 Codex 运行时查询",L"Query only while Codex is open"},
        {L"仅任务栏数字",L"Tray icons only"},{L"任务栏 + Codex 顶部",L"Tray + top strip"},{L"任务栏 + Codex 左侧",L"Tray + left strip"},
        {L"立即刷新",L"Refresh now"},{L"保存并应用",L"Save and apply"},{L"关闭",L"Close"},{L"配置…",L"Settings…"},
        {L"Codex 内显示在顶部",L"Show at the top of Codex"},{L"Codex 内显示在左侧",L"Show on the left of Codex"},{L"任务栏 + Codex 内显示",L"Tray + Codex strip"},
        {L"立即刷新账户 / 本机数据",L"Refresh account / local data"},{L"退出配额条",L"Quit Codex Quota"},
        {L"等待账户数据…",L"Waiting for account data…"},{L"等待用量数据",L"Waiting for quota data"},{L"尚无数据",L"No data yet"},
        {L"设置将在保存后生效",L"Changes take effect after saving"},{L"已保存并应用。",L"Saved and applied."},
        {L"查询间隔须为 30–3600 秒；旧数据提示须为 1–120 分钟。",L"Use 30–3600 seconds and 1–120 minutes."},
        {L"无法保存本机配置，请检查文件权限。",L"Cannot save settings. Check file permissions."},
        {L"正在刷新",L"Refreshing"},{L"刷新失败",L"Refresh failed"},{L"等待更新",L"Waiting for update"},{L"已同步",L"Synced"},{L"本地快照",L"Local snapshot"},
        {L"5小时",L"5h"},{L"周",L"W"},{L"剩余 ",L"remaining "},{L"\n最后事件：",L"\nUpdated: "},{L"\n重置：",L"\nResets: "},
        {L"\n旧快照，等待本机新事件",L"\nStale snapshot; waiting for updates"},{L"\n服务未提供此窗口，或等待重置后的新事件",L"\nWindow unavailable or awaiting data after reset"},
        {L"剩余配额：周 ",L"Remaining quota: W "},{L"剩余配额：5小时 ",L"Remaining quota: 5h "},{L"；周 ",L"; W "},
        {L"\n数据时间：",L"\nUpdated: "},{L"\n来源：账户查询",L"\nSource: account query"},{L"\n来源：本机会话快照",L"\nSource: local session"},
        {L"\n查询失败，保留旧值：",L"\nQuery failed; keeping last values: "},{L"\n5小时重置时间：",L"\n5h resets: "},{L"\n周重置时间：",L"\nWeekly resets: "},
        {L"\n灰色数值是旧快照，等待本机新事件。",L"\nGray values are stale; waiting for updates."},{L"\n本机日志监听不可用，错误码：",L"\nLocal watcher unavailable, error: "},
        {L"\n右键可打开配置或立即刷新；双击立即刷新。",L"\nRight-click for settings or refresh; double-click to refresh."},
        {L"更新 ",L"Updated "},{L" · 账户查询",L" · Account query"},{L" · 本地快照",L" · Local snapshot"},{L" · 正在刷新",L" · Refreshing"},
        {L"刷新失败 · ",L"Refresh failed · "},{L"，保留上次数据",L"; keeping previous data"},{L" · 等待新数据",L" · Waiting for new data"},
        {L"额度读取已取消",L"Query cancelled"},{L"获取 Codex 额度超时",L"Codex query timed out"},{L"Codex app-server 已退出",L"Codex app-server exited"},
        {L"Codex app-server 响应超出限制",L"Codex response exceeded size limit"},{L"Codex app-server 初始化失败",L"Codex initialization failed"},
        {L"读取 Codex 额度失败",L"Codex account query failed"},{L"Codex app-server 响应无效",L"Invalid Codex response"},{L"Codex app-server 配置不可用",L"Codex executable or data directory unavailable"},
        {L"无法创建 Codex app-server 通信通道",L"Cannot create Codex communication pipes"},{L"无法准备 Codex app-server 环境",L"Cannot prepare Codex environment"},
        {L"Codex 代理配置无效，请检查 config.toml",L"Invalid Codex proxy configuration; check config.toml"},
        {L"无法启动 Codex app-server",L"Cannot start Codex app-server"},{L"无法与 Codex app-server 通信",L"Cannot communicate with Codex"},
        {L"Codex app-server 未返回额度数据",L"Codex returned no quota data"},{L"读取 Codex 额度时发生错误",L"Codex query failed"},
        {L"账户刷新异常，请重启配额程序",L"Account refresh failed; restart the companion"}
        ,{L"记录额度变化",L"Record quota changes"},{L"额度变化",L"Quota changes"},{L"Codex 配额 — 额度变化",L"Codex Quota — History"}
        ,{L"日志目录",L"History directory"},{L"浏览…",L"Browse…"},{L"查看额度变化",L"View quota changes"},{L"查看变化…",L"Quota history…"}
        ,{L"留空使用默认 history 目录",L"Leave empty to use the default history directory"},{L"选择额度日志目录",L"Choose quota history directory"}
        ,{L"日志目录须为可写的绝对路径。",L"Use a writable absolute history directory."},{L"无法记录额度变化，请检查日志目录和权限。",L"Cannot write quota history; check directory and permissions."}
        ,{L"无法读取额度变化，请检查日志目录和权限。",L"Cannot read quota history; check directory and permissions."}
        ,{L"时间",L"Time"},{L"5h 剩余 / 变化",L"5h remaining / change"},{L"周剩余 / 变化",L"Weekly remaining / change"},{L"来源 / 事件",L"Source / event"}
        ,{L"图表",L"Chart"},{L"列表",L"List"},{L"刷新记录",L"Refresh history"},{L"暂无额度变化记录",L"No quota changes recorded"}
        ,{L"仅显示最近 30 日，最多 2000 条",L"Last 30 days, up to 2,000 entries"},{L"已截取最近记录",L"Showing recent entries only"}
        ,{L"条记录",L"entries"},{L"账户",L"Account"},{L"本地事件",L"Local event"},{L"基线",L"Baseline"},{L"变化",L"Change"},{L"重置",L"Reset"}
        ,{L"百分点",L"pp"},{L"已暂停记录，现有记录仍可查看",L"Recording paused; existing history is available"}
        ,{L"工作请求",L"Work requests"},{L"查看工作请求",L"View work requests"},{L"工作请求…",L"Work requests…"}
        ,{L"记录额度与工作请求",L"Record quota changes and work requests"}
        ,{L"无法读取工作请求，请检查日志目录和权限。",L"Cannot read work requests; check directory and permissions."}
        ,{L"无法记录工作请求，请检查日志目录和权限。",L"Cannot write work requests; check directory and permissions."}
        ,{L"Codex 配额 — 工作请求",L"Codex Quota — Work requests"},{L"暂无工作请求记录",L"No work requests recorded"}
        ,{L"模型",L"Model"},{L"状态",L"Status"},{L"实际 Token",L"Actual tokens"},{L"输入 / 缓存命中 / 缓存写入",L"Input / cached / cache write"}
        ,{L"输出 / 推理",L"Output / reasoning"},{L"账户快照变化 5h / 周",L"Account snapshot change 5h / weekly"},{L"实际金额",L"Actual amount"},{L"会话 / 请求",L"Thread / request"}
        ,{L"已完成",L"Completed"},{L"已中止",L"Aborted"},{L"已中断",L"Interrupted"},{L"失败",L"Failed"},{L"未完成",L"Incomplete"},{L"部分记录",L"Partial"}
        ,{L"每个工作请求已观察到的 Token",L"Observed tokens per work request"}
        ,{L"账户快照可能包含并发工作，基线可能早于请求；不归因于单个请求。金额 — 表示未提供。",L"Account snapshots may include concurrent work and predate this request; changes are not attributed to it. Amount — means unavailable."}
        ,{L"部分记录只包含可确认的 Token；缺失值保持 —。",L"Partial records contain confirmed tokens only; unavailable values remain —."}
    };
    for (const auto& text : entries) if (std::wcscmp(chinese,text.chinese)==0) return text.english;
    return chinese;
}
}

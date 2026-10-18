#pragma once
#include <string>
#include <cstring>
namespace nativequota {
// 固定 UI 文案采用内置字典，用户路径与账号数据不送入翻译接口。
inline const char* Translate(const char* english, bool chinese) {
    if (!chinese) return english;
    struct Entry { const char* english; const char* chinese; };
    static const Entry entries[] = {
        {"Quota history…","额度变化…"},{"Codex Quota — History","Codex 配额 — 变化历史"},
        {"Quota changes","额度变化"},{"Work requests","工作请求"},{"Record quota changes and request tokens","记录额度变化与请求 token"},
        {"Conversation","所属对话"},{"Unknown conversation","未知对话"},
        {"Model","模型"},{"Input tokens","输入 token"},{"Cached input tokens","缓存读取 token"},{"Cache write tokens","缓存写入 token"},
        {"Output tokens","输出 token"},{"Reasoning tokens","推理 token"},{"Total tokens","总 token"},
        {"5h quota (start → end)","5 小时额度（开始 → 结束）"},{"Weekly quota (start → end)","周额度（开始 → 结束）"},
        {"Actual cost","实际费用"},{"Status","状态"},{"Thread / turn","会话／请求"},{"Partial","部分数据"},{"Completed","已完成"},{"Aborted","已中断"},
        {"Interrupted","已中断"},{"Incomplete","未完整结束"},{"5h account change (pp)","5 小时账号快照变化（百分点）"},{"Weekly account change (pp)","周账号快照变化（百分点）"},
        {"No work requests recorded yet.","尚无工作请求记录。"},
        {"Account quota changes may include other work. Actual cost is unavailable.","账号额度变化可能包含其他工作；实际费用未知。"},
        {"Cached input is part of input; reasoning is part of output.","缓存读取包含在输入中，推理包含在输出中。"},
        {"Tokens per request: input (blue), output (green), cached input (orange)","每次请求 token：输入（蓝）、输出（绿）、缓存读取（橙）"},
        {"Cannot watch Codex sessions; check home directory and permissions","无法监听 Codex 会话，请检查数据目录与权限"},
        {"Cannot write request usage; check directory and permissions","无法写入请求用量，请检查目录与权限"},
        {"Cannot read request usage; check directory and permissions","无法读取请求用量，请检查目录与权限"},
        {"Record quota changes","记录额度数值变化"},{"History directory","历史日志目录"},{"Browse…","浏览…"},
        {"Choose history directory","选择历史日志目录"},{"Choose","选择"},{"Cancel","取消"},
        {"List","列表"},{"Chart","图表"},{"Refresh history","刷新历史"},
        {"Latest 30 UTC days; at most 2000 records. Refresh manually.","最近 30 个 UTC 日，最多 2000 条；请手动刷新。"},
        {"Showing newest records; older records were omitted.","正在显示最新记录，较早记录已省略。"},
        {"No quota changes recorded yet.","尚无额度变化记录。"},{"History recording is disabled.","历史记录已关闭。"},
        {"Solid: continuous records. Dashed: baseline/source/reset boundaries, reference only, not consumption. Missing values stay disconnected.","实线：连续记录。虚线：重新记录、来源切换或额度重置，仅作参考连接，不表示消耗。缺失值不连线。"},
        {"Time (local)","时间（本地）"},{"5h remaining","5 小时剩余"},{"Weekly remaining","周剩余"},
        {"5h change (pp)","5 小时变化（百分点）"},{"Weekly change (pp)","周变化（百分点）"},
        {"Source / event","来源／事件"},{"Baseline","基线"},{"Change","变化"},{"Reset","重置"},
        {"Account query","账号查询"},{"Local event","本地事件"},
        {"5h (blue) / Weekly (green) — remaining %","5 小时（蓝）／周（绿）— 剩余 %"},
        {"Cannot use history directory; choose an absolute writable path.","无法使用历史目录，请选择可写的绝对路径。"},
        {"Cannot write quota history; check directory and permissions","无法写入额度历史，请检查目录与权限"},
        {"Cannot read quota history; check directory and permissions","无法读取额度历史，请检查目录与权限"},
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

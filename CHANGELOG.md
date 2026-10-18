# 变更记录 / Changelog

## Unreleased

## v3.0.0-preview.6 — 2026-10-18

- 工作请求新增前置“所属对话”列：本地标题、子代理父对话、ID／未知回退及重命名刷新；标题不写入 CSV。尾读通过 rollout 文件名保留会话 ID，旧记录通过有界只读历史 ID 索引补认，重复来源保持未知。 / Add a leading Conversation column with local titles, subagent parents, ID/unknown fallbacks and rename refresh without CSV title storage. Preserve source UUIDs from rollout filenames and resolve legacy rows through a bounded read-only history ID index, retaining unknown values on conflicts.
- 修复 Windows 商店包缓存路径被误识别为 Codex 主程序、导致配额条附着到插件配置／历史窗口的问题；排除自身进程并核对主程序文件名。 / Prevent Windows Store cache paths from misidentifying companion settings/history as Codex; exclude the companion process and verify the host executable name.
- 三平台额度图连接有效点：连续变化使用实线，基线／来源／重置边界使用虚线参考连接并显示说明；缺失值保持断线。 / Connect valid quota points on all platforms using solid continuous segments and labelled dashed baseline/source/reset references, preserving missing-value gaps.
- 修复从 Codex 切回已打开的配置／历史窗口时配额条未及时隐藏、遮挡输入的问题；复用前台事件，不新增轮询。 / Hide the quota strip immediately when returning to existing settings/history windows, preventing obstructed input without adding polling.
- 新增每次工作请求的真实 Token 明细、参考账户额度起止快照、请求状态及原生中英文列表／图表，实际金额未知时显示“—”。 / Add real per-request token breakdowns, reference account snapshots, statuses and bilingual native list/charts; unknown actual charges show “—”.
- 三平台文件事件监听与每日请求 CSV 沿用日志目录及开关；有界尾读、部分记录标记、重放去重、认证边界隔离，不增加模型请求或查询轮询。 / Event-driven request CSV logging on all platforms shares history preferences, with bounded tails, partial markers, replay deduplication and authentication boundaries, without additional model requests or query polling.
- 新增自选目录的每日 CSV 额度变化日志及记录开关。 / Record quota changes in daily CSV files in a selected directory, with an enable switch.
- Windows／Linux／macOS 新增原生中英文列表／图表查看、按需有界读取及独立日志错误。 / Native bilingual list/chart viewers, bounded on-demand reads and separate logging errors.
- 缺失值、重置周期和账号切换保留基线，数值不变时不写盘。 / Preserve missing values and reset/account baselines; avoid writes for unchanged quotas.

## v3.0.0-preview.5 — 2026-10-13

- 新增 Windows 当前用户中英安装向导，开始菜单与可选桌面快捷方式；升级／卸载保留设置和自建文件，不改自启钩子。 / Added a per-user Chinese/English Windows setup wizard and shortcuts; updates/removal preserve settings and extra files without changing startup hooks.
- 新增 macOS 通用 DMG／PKG，固定 PKG 安装到 Applications；保留 APP ZIP。 / Added universal macOS DMG/PKG with fixed Applications installation, retaining APP ZIP.
- 新增 Linux RPM 和 TGZ 用户安装／卸载入口，DEB／RPM 依赖按实际二进制生成，保留现有 DEB／TGZ。 / Added Linux RPM and TGZ user install/remove scripts, with binary-derived package dependencies and existing DEB/TGZ retained.
- 增加隔离安装生命周期和多发行版容器检查；中英安装指南与 Release 说明按系统／架构列出选包和兼容范围。 / Added isolated installer lifecycle and multi-distribution container checks; bilingual guides and release notes map OS/architecture to downloads and compatibility.

## v3.0.0-preview.4 — 2026-10-12

- 三平台账号查询自动读取所选 Codex `config.toml` 的代理环境变量和文件选定的 profile，下一次查询应用修改。 / All platforms reuse proxy environment variables and the file-selected profile from the chosen Codex config.toml; changes apply on the next query.
- 代理仅作用到查询子进程，未配置项保留启动环境；支持大小写别名及空值清除，不注入其他配置密钥。 / Proxies affect query children only; unconfigured entries retain launch variables. Supports case aliases and explicit clearing, excluding unrelated configured secrets.
- 新增真实子进程环境、配置变更、profile、无效配置和大小限制回归测试。 / Added regression coverage for real child environments, config updates, profiles, invalid config and size limits.
- 内置 MIT 许可 toml++ v3.4.0 解析器，发布包附第三方许可，不增加常驻进程或刷新定时器。 / Includes the MIT-licensed toml++ v3.4.0 parser and third-party notices, without additional resident processes or refresh timers.

## v3.0.0-preview.3 — 2026-10-10

- 修复 Windows 语言下拉列表高度为零的问题，中文和 English 选项均可完整显示和点击。 / Fixed a zero-height Windows language dropdown so both Chinese and English are visible and selectable.
- 修复显示位置下拉列表的选项裁切，并统一语言选择框的深色样式。 / Fixed clipped display-position options and applied the dark style to the language selector.
- 回归测试实际展开列表、检查选项可见范围、点击 English 并验证即时翻译和保存。 / Regression tests open the real dropdown, check visible option bounds, click English, and verify live translation and persistence.
- 三平台产物基于同一源码提交重新构建；Linux／macOS 应用行为与 preview.2 保持一致。 / All platform packages are rebuilt from the same source commit; Linux/macOS application behavior is unchanged from preview.2.

## v3.0.0-preview.2 — 2026-10-10

- Windows／Linux／macOS 配置窗口加入中英文语言选择；保存后即时更新菜单、界面和状态提示，并持久化选择。 / Added persisted Chinese/English selection with live menu, UI and status translation on all three platforms.
- 翻译使用内置字典，不增加网络请求或常驻翻译进程。 / Uses built-in translations without network calls or a translation process.
- 中文 `README.md` 与英文 `README.en.md` 保持独立内容和相互切换链接。 / Keeps independent Chinese and English README files with language-switch links.

## v3.0.0-preview.1 — 2026-10-10

- 添加 Linux x64 托盘／X11 悬浮条和 macOS 通用菜单栏／辅助功能悬浮条原生预览版。 / Added native Linux x64 tray/X11 overlay and universal macOS menu bar/Accessibility overlay previews.
- 明确只提供周额度的账号自动隐藏 5 小时区域；恢复窗口后自动还原。 / Accounts with confirmed weekly-only quotas hide the five-hour region and restore it when available.
- 认证元数据事件清空旧账号快照，稳定用户级实例锁防止账号目录变化后的重复启动。 / Auth metadata notifications invalidate old snapshots; stable per-user locks prevent duplicate instances after account-home changes.
- 原生核心、真实进程协议、配置／账号变化、Linux X11 UI 和 macOS UI smoke 纳入云端验证。 / Hosted validation covers native core/process protocol, settings/account changes, Linux X11 UI and macOS UI smoke.
- 原生版暂不提供 Windows 的会话日志回退；Wayland 仅托盘；macOS 未 Developer ID 签名／公证，真实用户 AX 跟随尚待设备验证。 / Native previews omit Windows session-log fallback; Wayland is tray-only; macOS has no Developer ID/notarization and real-user AX following remains unverified.

## 初始公开源码与文档 / Initial public source and documentation

### 2026-10-10

- 修复构建脚本误选不完整 Windows SDK 的问题，并加入目录完整性回归测试。 / Fixed selection of incomplete Windows SDK installations and added a directory-completeness regression test.
- 中文与英文 README 拆分为独立文件，提供语言切换链接。 / Split Chinese and English READMEs into separate files with language-switch links.
- 添加 MIT 许可证、贡献指南和安全说明。 / Added the MIT license, contribution guide and security notes.
- 补充 Linux／macOS 发起远程 Windows 构建的步骤与命令。 / Added commands to build Windows artifacts remotely from Linux/macOS.
- 添加手动触发的 Windows 构建工作流，执行默认测试并打包许可证、源码提交标识和校验清单。 / Added a manually triggered Windows build workflow with default tests, license, source commit identifier and checksums.
- README 提供中英说明、多环境配置、刷新设置、启动与排错命令。 / README now provides bilingual setup, environment configuration, refresh settings, startup and troubleshooting commands.

### 初始公开源码 / Initial public source

- Win32 数字托盘图标和可选 Codex 顶部／左侧配额条。 / Win32 numeric tray icons and optional top/left Codex strip.
- 原生深色配置窗口、账号查询、本地事件回退与窗口重建。 / Native dark settings window, account queries, local-event fallback and host-window recovery.
- MSVC 构建脚本，以及解析、设置、图标和集成测试。 / MSVC build scripts and parsing, settings, icon and integration tests.

旧记录日期仅标识源码／文档变更。 / Dates in older entries identify source/documentation changes.

# 变更记录 / Changelog

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

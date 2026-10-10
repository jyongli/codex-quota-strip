# 变更记录 / Changelog

## 未发布 / Unreleased

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

日期是文档／源码记录，不代表已发布同名 Release 或版本标签。 / Dates identify source/documentation changes, not published releases or version tags.

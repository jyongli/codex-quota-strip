# 贡献指南 / Contributing

欢迎提交范围明确的 Issue 和 Pull Request；中文和英文均可。请先查看 [README](README.md)、[构建环境说明](docs/building-on-linux-macos.md) 和现有问题。

Focused issues and pull requests are welcome in Chinese or English. Start with the [README](README.md), [build-environment guide](docs/building-on-linux-macos.md) and existing issues.

## 问题报告 / Bug reports

请提供提交版本、操作系统／桌面会话类型、Codex 安装方式、复现步骤、预期与实际结果，以及相关的刷新／显示设置。截图只保留问题区域，删除账号信息。安全问题请按 [SECURITY.md](SECURITY.md) 处理。

Include the commit, OS/desktop session type, Codex installation method, reproduction steps, expected/actual behavior, and relevant refresh/display settings. Crop screenshots and remove account details. Follow [SECURITY.md](SECURITY.md) for security issues.

## 开发与验证 / Development and validation

从 Fork 或功能分支修改，保持改动聚焦。不要提交 `auth.json`、会话内容、账号令牌、本机设置、诊断日志或编译中间文件。

Work in a fork or feature branch and keep the change focused. Do not commit authentication files, session content, tokens, local settings, diagnostic logs or compiler intermediates.

在仓库根目录使用 Windows PowerShell / From the repository root in Windows PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Build.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Test.ps1
```

窗口、可见性、设置交互或文件监听行为改变时，在交互式 Windows 桌面验证集成测试：

For changes to windows, visibility, settings interactions or file notifications, run integration tests in an interactive Windows desktop session:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Test.ps1 -Integration
```

PowerShell 7 可将 `powershell.exe` 替换为 `pwsh.exe`。纯文档改动检查命令、链接和 Markdown 渲染即可；不要宣称未执行的测试通过。自动构建仅执行默认测试，不验证真实 Codex 窗口和在线账号查询。

Use `pwsh.exe` for PowerShell 7. For documentation-only changes, check commands, links and Markdown rendering. Do not report unexecuted checks as passing. The hosted build runs default tests only, without validating real Codex windows or online account queries.

Linux／macOS 使用 [原生构建步骤](docs/native-desktop.zh-CN.md) 和 CTest；涉及平台 UI 的改动需运行相应 smoke 测试。CI 包含 X11 合成窗口跟随和 macOS 基础 UI 验证，不表示真实账号或所有桌面环境已通过。

For Linux/macOS, follow the [native build guide](docs/native-desktop.en.md) and CTest, plus platform UI smoke tests for relevant changes. CI verifies synthetic X11 following and basic macOS UI, not live accounts or every desktop environment.

## 设计约束 / Design constraints

- 保持资源占用低，优先事件通知和有界读取；避免高频轮询、录屏和常驻查询进程。 / Keep resource usage low with event notifications and bounded reads; avoid frequent polling, screen capture and persistent query processes.
- 缺失配额显示 `-`，不推算未知数据；旧快照、查询失败和最新数据需可区分。 / Display `-` for missing quotas without guessing; distinguish stale snapshots, query failures and current data.
- 添加简洁中文注释解释关键约束；沿用现有代码风格。 / Use concise Chinese comments for important constraints and follow existing code style.
- 提交说明包含改动原因、实际验证结果及限制；行为改变时同步中英说明。 / Explain the change, actual verification and limitations; update both languages for behavior changes.

## 许可 / Licensing

贡献的原创代码和文档须可按本仓库的 [MIT License](LICENSE) 发布。引入第三方材料时保留其许可与版权声明，并说明来源。

Original contributed code and documentation must be available under this repository's [MIT License](LICENSE). Preserve third-party license/copyright notices and identify the source when adding third-party material.

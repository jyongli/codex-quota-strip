# 原生 Linux / macOS 配额程序

## 目标与范围

交付可下载的 Linux x64 程序和 macOS 通用 `.app`，显示两种剩余配额，提供刷新间隔、旧快照阈值、在线查询和顶部/左侧悬浮条设置。保留 Windows 实现。

- macOS：AppKit 菜单栏、非激活悬浮窗口；通过用户授权的 Accessibility 获取前台 Codex 窗口，失去前台后隐藏。
- Linux：GTK3 与发行版 Ayatana 托盘库；X11 事件驱动跟随窗口。Wayland 托盘可用，通用跨应用悬浮定位不支持。
- 在线查询使用短生命周期 `codex app-server`，不直接读取认证文件或发起推理。
- 单实例、设置持久化、查询超时/取消/退出清理和有界 JSON 输入。
- 没有轮询截图、浏览器运行时或常驻 app-server。刷新工作线程在查询之间阻塞等待。

## 文件与顺序

1. `source/QuotaCore.h`：保留 Windows UTC 转换，补充 POSIX 转换；新增原生核心回归测试。
2. `native/NativeCore.*`：跨 POSIX 设置、实例锁、配额状态、进程查询、刷新线程和悬浮几何规则。
3. `native/NativeTests.cpp`、`FakeServer.cpp`：真实管道协议、错误/超时/取消清理、配置与几何测试；使用合成数据。
4. `native/LinuxApp.cpp`：托盘、配置窗、X11 前台/窗口事件、被动悬浮条和无 GUI smoke 测试入口。
5. `native/MacApp.mm`、`Info.plist.in`：原生菜单栏、配置窗、AX 观察器与权限提示、悬浮条和 smoke 测试。
6. `CMakeLists.txt` 与 GitHub Actions：原生系统编译测试，生成 `.deb`/`.tar.gz` 和 macOS `.zip`，附 MIT 许可、提交标识和校验清单。
7. 中英文 README、平台文档、安全说明与变更记录：更新真实功能和限制，保留独立语言入口。

## 验收

- 原生编译、解析/UTC、设置、几何、真实进程协议测试通过；Windows 原有测试继续通过。
- Linux 的测试 X11 会话验证前台隐藏、移动定位和退出；macOS 无账号 smoke 测试确认 AppKit 启动和退出。
- 下载产物，核对格式/架构、许可和校验；报告未在真实用户系统/账号上验证的部分。
- macOS 未签名/未公证，用户须按系统提供的方式批准打开；辅助功能权限由用户自行授予。
- Linux Wayland 悬浮条不列为已实现；配置界面和文档说明限制。

当前分支：`feature/native-desktop`。仅在验证后整合到 `main`。无必须新增的模型账号/API Key。

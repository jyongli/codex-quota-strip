# 安全说明 / Security

## 维护范围 / Maintenance scope

问题报告以当前 `main` 分支为基准。历史快照和自行修改的程序应先在当前源码上核实问题；本项目没有承诺响应时限。

Reports are evaluated against current `main`. Confirm issues in historical or modified builds against current source where possible. This project does not promise a response deadline.

## 报告方式 / Reporting

当前仓库未启用 GitHub 私密漏洞报告。请先提交不含利用细节或敏感数据的 [Issue](https://github.com/jyongli/codex-quota-strip/issues)，概述受影响版本和问题类型，请求与维护者确定私密沟通方式。

GitHub private vulnerability reporting is currently not enabled. First open an [issue](https://github.com/jyongli/codex-quota-strip/issues) containing only the affected version and a high-level problem category, and ask the maintainer to arrange a private channel.

不要在公开 Issue、PR 或日志中附上账号令牌、`auth.json`、完整会话、个人路径或可直接利用的细节。复现数据使用虚构账号和隔离目录。

Do not include tokens, `auth.json`, complete sessions, personal paths or directly exploitable details in public issues, pull requests or logs. Use synthetic accounts/data and isolated directories for reproduction.

## 数据边界 / Data boundaries

Windows 版读取 Codex 会话日志提取额度事件，原生预览版使用在线账号查询和内存快照；两者不直接解析认证文件内容。账号查询交由本机 Codex app-server 处理，其进程在查询完成、超时或退出时清理。Windows 的 `--status` 会在明确指定的路径写入诊断状态；这些文件不应提交到仓库。macOS 窗口跟随需用户授予辅助功能权限，不请求录屏权限。

Windows reads session logs for quota events; native previews use online queries and in-memory snapshots. Neither directly parses authentication-file contents. Local Codex app-server handles queries, with cleanup on completion, timeout or exit. Windows `--status` writes diagnostic state to an explicit path; do not commit those files. macOS window following requires user-granted Accessibility access, without Screen Recording permission.

## 构建产物 / Build artifacts

账号查询会从所选 Codex `config.toml` 解析代理，只将四项代理变量注入查询子进程；不会注入其他配置密钥。配置错误提示不回显代理 URL／密码，配置和代理凭据不写入公开构建信息。 / Queries parse proxies from the selected Codex config.toml and inject only the four proxy variables into the child, excluding other configured secrets. Errors do not echo proxy URLs/passwords; configurations and proxy credentials are excluded from public build information.

GitHub Actions 生成 Windows、Linux x64 和 macOS 通用产物，包含许可证和 SHA-256 清单。Windows／Linux 无发布者签名；macOS 只有 ad-hoc 完整性签名，没有 Apple Developer ID 或公证。校验清单用于检测下载或传输差异，不等于独立的发布者签名验证。

GitHub Actions produces Windows, Linux x64 and universal macOS artifacts with license/checksums. Windows/Linux lack publisher signatures; macOS has only an ad-hoc integrity signature, without Apple Developer ID or notarization. Checksums detect transfer differences but do not independently verify publisher identity.

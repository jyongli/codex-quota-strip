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

本程序读取 Codex 会话日志提取额度事件；不直接解析认证文件内容。账号查询交由本机 Codex app-server 处理，其进程在查询完成、超时或退出时清理。启用 `--status` 会在明确指定的路径写入诊断状态；这些文件不应提交到仓库。

The utility reads Codex session logs to extract quota events, without directly parsing authentication-file contents. Local Codex app-server handles account queries, with cleanup on completion, timeout or exit. `--status` writes diagnostic state to the explicitly selected path; do not commit those files.

## 构建产物 / Build artifacts

GitHub Actions 产物来自对应提交，当前没有代码签名或原生 Linux／macOS 版本。产物包含许可证和 SHA-256 清单；校验清单用于检测下载或传输差异，不等于独立的签名验证。

GitHub Actions artifacts correspond to their recorded source commit. They are currently unsigned and are Windows builds, without native Linux/macOS versions. Artifacts include a license and SHA-256 manifest; checksums detect transfer differences but are not an independent signature.

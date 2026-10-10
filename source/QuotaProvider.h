#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include "QuotaCore.h"

namespace quota {

// 单次 app-server 查询的结果；error 仅包含固定的通用提示，不携带子进程文本。
struct ProviderResult {
    bool ok = false;
    Snapshot snapshot;
    std::wstring error;
    DWORD childPid = 0;
    unsigned long long elapsedMs = 0;
};

// 优先复用正在运行的 Codex 二进制，否则选择最新安装的桌面版二进制。
std::wstring FindCodexBinary();

// 通过隐藏的短生命周期 app-server 子进程读取当前账号额度。
ProviderResult ReadAccountQuota(const std::wstring& exe, const std::wstring& home,
                                HANDLE cancel, DWORD timeoutMs = 15000);

}

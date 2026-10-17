#pragma once
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

// 隔离测试子进程的设置和 history，保持测试主进程及用户环境不变。
inline BOOL CreateQuotaFixtureProcess(std::wstring& command, const std::filesystem::path& root, STARTUPINFOW& start, PROCESS_INFORMATION& process) {
    auto environment = GetEnvironmentStringsW(); if (!environment) return FALSE;
    std::vector<std::wstring> variables;
    for (auto entry = environment; *entry; entry += wcslen(entry) + 1) if (_wcsnicmp(entry, L"LOCALAPPDATA=", 13) != 0) variables.emplace_back(entry);
    FreeEnvironmentStringsW(environment);
    variables.push_back(L"LOCALAPPDATA=" + root.wstring());
    std::sort(variables.begin(), variables.end(), [](const auto& a, const auto& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    std::vector<wchar_t> block;
    for (const auto& value : variables) { block.insert(block.end(), value.begin(), value.end()); block.push_back(L'\0'); }
    block.push_back(L'\0');
    return CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, block.data(), nullptr, &start, &process);
}

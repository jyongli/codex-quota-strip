#include <windows.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <cstdio>
#include "QuotaProvider.h"

namespace {

std::wstring ProbeHome() {
    std::wstring configured;
    DWORD size = GetEnvironmentVariableW(L"CODEX_HOME", nullptr, 0);
    if (size) {
        configured.resize(size);
        DWORD written = GetEnvironmentVariableW(L"CODEX_HOME", configured.data(), size);
        if (written && written < size) { configured.resize(written); return configured; }
    }
    DWORD profileSize = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
    if (!profileSize) return {};
    std::wstring profile(profileSize, L'\0');
    DWORD written = GetEnvironmentVariableW(L"USERPROFILE", profile.data(), profileSize);
    if (!written || written >= profileSize) return {};
    profile.resize(written);
    return (std::filesystem::path(profile) / L".codex").wstring();
}

bool ChildExited(DWORD pid) {
    if (!pid) return true;
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return GetLastError() == ERROR_INVALID_PARAMETER;
    DWORD state = WaitForSingleObject(process, 0);
    CloseHandle(process);
    return state == WAIT_OBJECT_0;
}

void PrintResult(const wchar_t* mode, const quota::ProviderResult& result) {
    const double five = result.snapshot.fiveHour.present ? 100.0 - result.snapshot.fiveHour.used : -1.0;
    const double weekly = result.snapshot.weekly.present ? 100.0 - result.snapshot.weekly.used : -1.0;
    std::wprintf(L"mode=%ls ok=%d five_hour_remaining=%.1f weekly_remaining=%.1f elapsed_ms=%llu spawned=%d child_exited=%d\n",
        mode, result.ok ? 1 : 0, five, weekly, result.elapsedMs,
        result.childPid ? 1 : 0, ChildExited(result.childPid) ? 1 : 0);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const wchar_t* mode = argc > 1 ? argv[1] : L"success";
    std::wstring exe = quota::FindCodexBinary();
    std::wstring home = ProbeHome();
    if (exe.empty() || home.empty()) {
        std::wprintf(L"mode=%ls ok=0 five_hour_remaining=-1 weekly_remaining=-1 elapsed_ms=0 spawned=0 child_exited=1\n", mode);
        return 2;
    }

    DWORD timeout = 15000;
    HANDLE cancellation = nullptr;
    HANDLE rawCancel = nullptr;
    std::thread cancellationThread;
    if (_wcsicmp(mode, L"timeout") == 0) {
        timeout = 100;
    } else if (_wcsicmp(mode, L"cancel") == 0) {
        rawCancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!rawCancel) return 3;
        cancellation = rawCancel;
        cancellationThread = std::thread([rawCancel]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            SetEvent(rawCancel);
        });
    } else {
        mode = L"success";
    }

    quota::ProviderResult result = quota::ReadAccountQuota(exe, home, cancellation, timeout);
    if (cancellationThread.joinable()) cancellationThread.join();
    PrintResult(mode, result);
    if (rawCancel) CloseHandle(rawCancel);
    return result.ok ? 0 : 1;
}

#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cwctype>
#include <cwchar>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "QuotaFiles.h"
#include "QuotaProvider.h"

namespace quota {
namespace {

constexpr size_t kMaxRpcLineBytes = 256 * 1024 - 1;
constexpr size_t kMaxRpcBytes = 1024 * 1024;
constexpr unsigned kMaxRpcLines = 4096;

class ScopedHandle {
    HANDLE value_ = INVALID_HANDLE_VALUE;
public:
    explicit ScopedHandle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value) {}
    ~ScopedHandle() { reset(); }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    HANDLE get() const { return value_; }
    bool valid() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    void reset(HANDLE value = INVALID_HANDLE_VALUE) {
        if (valid()) CloseHandle(value_);
        value_ = value;
    }
};

class AttributeList {
    std::vector<std::max_align_t> storage_;
    LPPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
public:
    bool initialize() {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (!bytes) return false;
        storage_.resize((bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
        list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if (!InitializeProcThreadAttributeList(list_, 1, 0, &bytes)) {
            list_ = nullptr;
            return false;
        }
        return true;
    }
    ~AttributeList() { if (list_) DeleteProcThreadAttributeList(list_); }
    LPPROC_THREAD_ATTRIBUTE_LIST get() const { return list_; }
};

// 子进程放入独立 job；返回或异常展开时都结束其进程树。
class ProcessTreeGuard {
    HANDLE job_ = nullptr;
    HANDLE process_ = nullptr;
public:
    ProcessTreeGuard(HANDLE job, HANDLE process) : job_(job), process_(process) {}
    ~ProcessTreeGuard() { stop(); }
    void stop() {
        if (!job_) return;
        // 即使主进程已退出，job 中残留的后代也会一起结束。
        TerminateJobObject(job_, ERROR_CANCELLED);
        if (process_) WaitForSingleObject(process_, 2000);
        job_ = nullptr;
    }
};

std::wstring EnvironmentValue(const wchar_t* key) {
    DWORD size = GetEnvironmentVariableW(key, nullptr, 0);
    if (!size) return {};
    std::wstring value(size, L'\0');
    DWORD written = GetEnvironmentVariableW(key, value.data(), size);
    if (!written || written >= size) return {};
    value.resize(written);
    return value;
}

bool IsHomeKey(const std::wstring& entry) {
    size_t equal = entry.find(L'=');
    return equal != std::wstring::npos && equal > 0 &&
        _wcsicmp(entry.substr(0, equal).c_str(), L"CODEX_HOME") == 0;
}

// 只覆写本次子进程的 CODEX_HOME，其他变量（包括代理配置）沿用父进程。
bool MakeEnvironment(const std::wstring& home, std::vector<wchar_t>& block) {
    LPWCH inherited = GetEnvironmentStringsW();
    if (!inherited) return false;
    std::vector<std::wstring> entries;
    for (const wchar_t* item = inherited; *item; item += std::wcslen(item) + 1) {
        std::wstring entry(item);
        if (!IsHomeKey(entry)) entries.push_back(std::move(entry));
    }
    FreeEnvironmentStringsW(inherited);

    entries.push_back(L"CODEX_HOME=" + home);
    std::sort(entries.begin(), entries.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });
    size_t total = 1;
    for (const auto& entry : entries) total += entry.size() + 1;
    block.reserve(total);
    for (const auto& entry : entries) {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return true;
}

// 按 Windows 命令行规则引用路径，避免空格或反斜杠改变参数边界。
std::wstring QuoteArgument(const std::wstring& argument) {
    std::wstring quoted(1, L'"');
    size_t slashes = 0;
    for (wchar_t ch : argument) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'"') {
            quoted.append(slashes * 2 + 1, L'\\');
            quoted.push_back(ch);
            slashes = 0;
            continue;
        }
        quoted.append(slashes, L'\\');
        slashes = 0;
        quoted.push_back(ch);
    }
    quoted.append(slashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

enum class ReceiveState { Pending, Result, RemoteError, Limit, PipeError, Cancelled, TimedOut, Exited };

// 只拆 JSON-RPC 外层信封；initialize 的 result 可能较大，不必把能力描述交给额度解析器。
class RpcEnvelope {
    std::string_view text_;
    size_t pos_ = 0;
    void spaces() { while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\r' || text_[pos_] == '\n' || text_[pos_] == '\t')) ++pos_; }
    bool string(std::string_view& raw) {
        if (pos_ >= text_.size() || text_[pos_++] != '"') return false;
        size_t start = pos_;
        while (pos_ < text_.size()) {
            unsigned char ch = static_cast<unsigned char>(text_[pos_++]);
            if (ch == '"') { raw = text_.substr(start, pos_ - start - 1); return true; }
            if (ch < 32) return false;
            if (ch == '\\') {
                if (pos_ >= text_.size()) return false;
                char escaped = text_[pos_++];
                if (escaped == 'u') {
                    for (int i = 0; i < 4; ++i) {
                        if (pos_ >= text_.size()) return false;
                        char hex = text_[pos_++];
                        if (!((hex >= '0' && hex <= '9') || (hex >= 'a' && hex <= 'f') || (hex >= 'A' && hex <= 'F'))) return false;
                    }
                } else if (escaped != '"' && escaped != '\\' && escaped != '/' && escaped != 'b' &&
                           escaped != 'f' && escaped != 'n' && escaped != 'r' && escaped != 't') return false;
            }
        }
        return false;
    }
    bool value(std::string_view& raw) {
        spaces();
        if (pos_ >= text_.size()) return false;
        size_t start = pos_;
        if (text_[pos_] == '"') {
            std::string_view ignored;
            if (!string(ignored)) return false;
        } else if (text_[pos_] == '{' || text_[pos_] == '[') {
            char stack[32];
            size_t depth = 0;
            stack[depth++] = text_[pos_++] == '{' ? '}' : ']';
            while (pos_ < text_.size() && depth) {
                char ch = text_[pos_];
                if (ch == '"') {
                    std::string_view ignored;
                    if (!string(ignored)) return false;
                } else if (ch == '{' || ch == '[') {
                    if (depth == sizeof(stack)) return false;
                    stack[depth++] = ch == '{' ? '}' : ']';
                    ++pos_;
                } else if (ch == '}' || ch == ']') {
                    if (stack[depth - 1] != ch) return false;
                    --depth;
                    ++pos_;
                } else {
                    ++pos_;
                }
            }
            if (depth) return false;
        } else {
            while (pos_ < text_.size() && text_[pos_] != ',' && text_[pos_] != '}' && text_[pos_] != ']' &&
                   text_[pos_] != ' ' && text_[pos_] != '\r' && text_[pos_] != '\n' && text_[pos_] != '\t') ++pos_;
            if (pos_ == start) return false;
        }
        raw = text_.substr(start, pos_ - start);
        return true;
    }
public:
    explicit RpcEnvelope(std::string_view text) : text_(text) {}
    bool extract(int expectedId, std::string_view& result, bool& hasResult, bool& hasError) {
        spaces();
        if (pos_ >= text_.size() || text_[pos_++] != '{') return false;
        std::string_view idValue;
        bool hasId = false, sawError = false, duplicate = false;
        for (;;) {
            spaces();
            if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; break; }
            std::string_view key, field;
            if (!string(key)) return false;
            spaces();
            if (pos_ >= text_.size() || text_[pos_++] != ':') return false;
            if (!value(field)) return false;
            if (key == "id") { if (hasId) duplicate = true; hasId = true; idValue = field; }
            else if (key == "result") { if (hasResult) duplicate = true; hasResult = true; result = field; }
            else if (key == "error") {
                if (sawError) duplicate = true;
                sawError = true;
                hasError = field != "null";
            }
            spaces();
            if (pos_ < text_.size() && text_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; break; }
            return false;
        }
        spaces();
        if (pos_ != text_.size() || duplicate || !hasId) return false;
        try {
            Json id = JsonParser(std::string(idValue)).parse();
            return id.kind == Json::Number && id.num == expectedId;
        } catch (...) { return false; }
    }
};

class RpcReader {
    std::string line_;
    bool discard_ = false;
    size_t bytes_ = 0;
    unsigned lines_ = 0;
public:
    ReceiveState feed(const char* bytes, size_t length, int expectedId, bool parseResult, Json& result) {
        if (bytes_ + length > kMaxRpcBytes) return ReceiveState::Limit;
        bytes_ += length;
        for (size_t i = 0; i < length; ++i) {
            const char ch = bytes[i];
            if (ch == '\n') {
                if (++lines_ > kMaxRpcLines) return ReceiveState::Limit;
                if (!discard_ && !line_.empty()) {
                    // 先清理帧缓冲，避免 initialize 与下一条额度响应拼成一行。
                    std::string complete=std::move(line_);line_.clear();
                    std::string_view payload;
                    bool hasResult = false, hasError = false;
                    if (RpcEnvelope(complete).extract(expectedId, payload, hasResult, hasError)) {
                        if (hasError) return ReceiveState::RemoteError;
                        if (!hasResult) return ReceiveState::PipeError;
                        if (parseResult) {
                            if (payload.size() > 32768) return ReceiveState::Limit;
                            try { result = JsonParser(std::string(payload)).parse(); }
                            catch (...) { return ReceiveState::PipeError; }
                        }
                        return ReceiveState::Result;
                    }
                }
                line_.clear();
                discard_ = false;
            } else if (!discard_) {
                if (line_.size() >= kMaxRpcLineBytes) {
                    line_.clear();
                    discard_ = true;
                } else {
                    line_.push_back(ch);
                }
            }
        }
        return ReceiveState::Pending;
    }
};

bool WriteRequest(HANDLE input, const char* request, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        DWORD written = 0;
        DWORD wanted = static_cast<DWORD>(std::min<size_t>(length - offset, MAXDWORD));
        if (!WriteFile(input, request + offset, wanted, &written, nullptr) || !written) return false;
        offset += written;
    }
    return true;
}

ReceiveState WaitForResponse(HANDLE output, HANDLE process, HANDLE cancel,
                             ULONGLONG deadline, RpcReader& reader, int id, bool parseResult, Json& result) {
    char bytes[4096];
    for (;;) {
        if (cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) return ReceiveState::Cancelled;
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) return ReceiveState::Exited;
        ULONGLONG now = GetTickCount64();
        if (now >= deadline) return ReceiveState::TimedOut;

        DWORD available = 0;
        if (!PeekNamedPipe(output, nullptr, 0, nullptr, &available, nullptr)) return ReceiveState::PipeError;
        if (available) {
            DWORD count = 0;
            DWORD wanted = std::min<DWORD>(available, static_cast<DWORD>(sizeof(bytes)));
            if (!ReadFile(output, bytes, wanted, &count, nullptr) || !count) return ReceiveState::PipeError;
            ReceiveState state = reader.feed(bytes, count, id, parseResult, result);
            if (state != ReceiveState::Pending) return state;
            continue;
        }

        DWORD waitMs = static_cast<DWORD>(std::min<ULONGLONG>(20, deadline - now));
        if (cancel) {
            HANDLE waits[] = { cancel, process };
            DWORD waited = WaitForMultipleObjects(2, waits, FALSE, waitMs);
            if (waited == WAIT_OBJECT_0) return ReceiveState::Cancelled;
            if (waited == WAIT_OBJECT_0 + 1) return ReceiveState::Exited;
        } else if (WaitForSingleObject(process, waitMs) == WAIT_OBJECT_0) {
            return ReceiveState::Exited;
        }
    }
}

std::wstring LowerPath(std::wstring path) {
    std::replace(path.begin(), path.end(), L'/', L'\\');
    std::transform(path.begin(), path.end(), path.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return path;
}

unsigned long long FileModified(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    ULARGE_INTEGER value{};
    value.HighPart = data.ftLastWriteTime.dwHighDateTime;
    value.LowPart = data.ftLastWriteTime.dwLowDateTime;
    return value.QuadPart;
}

int BinaryRank(const std::wstring& path, const std::wstring& appBin) {
    std::wstring lower = LowerPath(path);
    std::wstring appRoot = LowerPath(appBin);
    if (lower.size() > appRoot.size() && lower.compare(0, appRoot.size(), appRoot) == 0 && lower[appRoot.size()] == L'\\')
        return 3;
    if (lower.find(L"\\plugins\\app-server\\") != std::wstring::npos ||
        lower.find(L"\\plugins\\appserver\\") != std::wstring::npos) return 2;
    return 1;
}

std::wstring RunningBinary(const std::wstring& appBin) {
    ScopedHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) return {};
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) return {};

    std::wstring selected;
    int selectedRank = 0;
    unsigned long long selectedTime = 0;
    do {
        if (_wcsicmp(entry.szExeFile, L"codex.exe") != 0) continue;
        ScopedHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
        if (!process.valid()) continue;
        std::vector<wchar_t> path(32768);
        DWORD length = static_cast<DWORD>(path.size());
        if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &length) || length == 0) continue;
        std::wstring candidate(path.data(), length);
        int rank = BinaryRank(candidate, appBin);
        unsigned long long modified = FileModified(candidate);
        if (rank > selectedRank || (rank == selectedRank && modified > selectedTime)) {
            selected = std::move(candidate);
            selectedRank = rank;
            selectedTime = modified;
        }
    } while (Process32NextW(snapshot.get(), &entry));
    return selected;
}

std::wstring LatestInstalledBinary(const std::wstring& appBin) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path base(appBin);
    std::wstring selected;
    unsigned long long selectedTime = 0;
    auto consider = [&](const fs::path& candidate) {
        std::error_code existsError;
        if (!fs::is_regular_file(candidate, existsError) || existsError) return;
        std::wstring path = candidate.wstring();
        unsigned long long modified = FileModified(path);
        if (modified > selectedTime) {
            selected = std::move(path);
            selectedTime = modified;
        }
    };
    consider(base / L"codex.exe");
    fs::directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code entryError;
        if (it->is_directory(entryError) && !entryError) consider(it->path() / L"codex.exe");
    }
    return selected;
}

const wchar_t* ErrorText(ReceiveState state, bool initialize) {
    if (state == ReceiveState::Cancelled) return L"额度读取已取消";
    if (state == ReceiveState::TimedOut) return L"获取 Codex 额度超时";
    if (state == ReceiveState::Exited) return L"Codex app-server 已退出";
    if (state == ReceiveState::Limit) return L"Codex app-server 响应超出限制";
    if (state == ReceiveState::RemoteError) return initialize ? L"Codex app-server 初始化失败" : L"读取 Codex 额度失败";
    return L"Codex app-server 响应无效";
}

} // namespace

std::wstring FindCodexBinary() {
    const std::wstring local = EnvironmentValue(L"LOCALAPPDATA");
    if (local.empty()) return {};
    const std::wstring appBin = (std::filesystem::path(local) / L"OpenAI" / L"Codex" / L"bin").wstring();
    std::wstring running = RunningBinary(appBin);
    return running.empty() ? LatestInstalledBinary(appBin) : running;
}

ProviderResult ReadAccountQuota(const std::wstring& exe, const std::wstring& home,
                                HANDLE cancel, DWORD timeoutMs) {
    const ULONGLONG started = GetTickCount64();
    ProviderResult result;
    auto finish = [&](const wchar_t* error = nullptr) {
        result.ok = error == nullptr;
        if (error) result.error = error;
        result.elapsedMs = GetTickCount64() - started;
        return result;
    };

    try {
        if (exe.empty() || home.empty()) return finish(L"Codex app-server 配置不可用");
        if (cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) return finish(L"额度读取已取消");
        const ULONGLONG deadline = started + timeoutMs;

        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;
        HANDLE rawInputRead = nullptr, rawInputWrite = nullptr;
        if (!CreatePipe(&rawInputRead, &rawInputWrite, &security, 0)) return finish(L"无法创建 Codex app-server 通信通道");
        ScopedHandle inputRead(rawInputRead), inputWrite(rawInputWrite);
        HANDLE rawOutputRead = nullptr, rawOutputWrite = nullptr;
        if (!CreatePipe(&rawOutputRead, &rawOutputWrite, &security, 0)) return finish(L"无法创建 Codex app-server 通信通道");
        ScopedHandle outputRead(rawOutputRead), outputWrite(rawOutputWrite);
        if (!SetHandleInformation(inputWrite.get(), HANDLE_FLAG_INHERIT, 0) ||
            !SetHandleInformation(outputRead.get(), HANDLE_FLAG_INHERIT, 0))
            return finish(L"无法创建 Codex app-server 通信通道");
        ScopedHandle nullError(CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!nullError.valid()) return finish(L"无法创建 Codex app-server 通信通道");

        std::vector<wchar_t> environment;
        if (!MakeEnvironment(home, environment)) return finish(L"无法准备 Codex app-server 环境");
        AttributeList attributes;
        if (!attributes.initialize()) return finish(L"无法准备 Codex app-server 环境");
        HANDLE inheritedHandles[] = { inputRead.get(), outputWrite.get(), nullError.get() };
        if (!UpdateProcThreadAttribute(attributes.get(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inheritedHandles, sizeof(inheritedHandles), nullptr, nullptr))
            return finish(L"无法准备 Codex app-server 环境");

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.StartupInfo.wShowWindow = SW_HIDE;
        startup.StartupInfo.hStdInput = inputRead.get();
        startup.StartupInfo.hStdOutput = outputWrite.get();
        startup.StartupInfo.hStdError = nullError.get();
        startup.lpAttributeList = attributes.get();
        std::wstring commandLine = QuoteArgument(exe) + L" app-server --stdio";
        std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
        mutableCommand.push_back(L'\0');

        ScopedHandle job(CreateJobObjectW(nullptr, nullptr));
        if (!job.valid()) return finish(L"无法启动 Codex app-server");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo{};
        jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &jobInfo, sizeof(jobInfo)))
            return finish(L"无法启动 Codex app-server");

        PROCESS_INFORMATION processInfo{};
        DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT;
        if (!CreateProcessW(exe.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE, flags,
            environment.data(), nullptr, &startup.StartupInfo, &processInfo))
            return finish(L"无法启动 Codex app-server");
        ScopedHandle process(processInfo.hProcess), thread(processInfo.hThread);
        result.childPid = processInfo.dwProcessId;
        inputRead.reset();
        outputWrite.reset();
        nullError.reset();

        if (!AssignProcessToJobObject(job.get(), process.get())) {
            TerminateProcess(process.get(), ERROR_CANCELLED);
            WaitForSingleObject(process.get(), 2000);
            return finish(L"无法启动 Codex app-server");
        }
        ProcessTreeGuard tree(job.get(), process.get());
        if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
            tree.stop();
            return finish(L"无法启动 Codex app-server");
        }
        thread.reset();

        static constexpr char initialize[] =
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"clientInfo\":{\"name\":\"codex_quota_strip\",\"title\":\"Codex Quota Strip\",\"version\":\"2.0.0\"}}}\n";
        static constexpr char initialized[] = "{\"jsonrpc\":\"2.0\",\"method\":\"initialized\"}\n";
        static constexpr char readLimits[] = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"account/rateLimits/read\"}\n";
        if (!WriteRequest(inputWrite.get(), initialize, sizeof(initialize) - 1)) {
            tree.stop();
            return finish(L"无法与 Codex app-server 通信");
        }

        RpcReader reader;
        Json response;
        ReceiveState state = WaitForResponse(outputRead.get(), process.get(), cancel, deadline, reader, 1, false, response);
        if (state != ReceiveState::Result) {
            tree.stop();
            return finish(ErrorText(state, true));
        }
        if (!WriteRequest(inputWrite.get(), initialized, sizeof(initialized) - 1) ||
            !WriteRequest(inputWrite.get(), readLimits, sizeof(readLimits) - 1)) {
            tree.stop();
            return finish(L"无法与 Codex app-server 通信");
        }

        state = WaitForResponse(outputRead.get(), process.get(), cancel, deadline, reader, 2, true, response);
        if (state != ReceiveState::Result) {
            tree.stop();
            return finish(ErrorText(state, false));
        }
        if (!ReadLimits(response, result.snapshot)) {
            tree.stop();
            return finish(L"Codex app-server 未返回额度数据");
        }
        result.snapshot.observed = NowMs();
        tree.stop();
        return finish();
    } catch (...) {
        return finish(L"读取 Codex 额度时发生错误");
    }
}

} // namespace quota

#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "LoginStartup.h"
#include <algorithm>
#include <memory>
#include <sddl.h>
#include <type_traits>
#include <vector>

#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"shell32.lib")

namespace quota_guard {
enum class State { Off, On, Other, Error };
inline constexpr wchar_t ServiceName[]=L"CodexQuotaStripGuard";

namespace detail {
inline constexpr wchar_t PipeName[]=L"\\\\.\\pipe\\CodexQuotaStripGuard";
inline constexpr wchar_t DirectoryName[]=L"CodexQuotaStripGuard";
inline constexpr wchar_t ImageName[]=L"CodexQuotaGuard.exe";
inline constexpr wchar_t ServiceSwitch[]=L"--service";
inline constexpr unsigned RetryLimit=3;

inline bool Same(const std::wstring& a,const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_EQUAL;
}

// 只接受本地驱动器上的普通 Win32 路径，排除设备路径、备用数据流和路径别名。
inline bool LocalPath(const std::wstring& path) {
    if(path.size()<3 || path.size()>=32768 || !((path[0]>=L'A' && path[0]<=L'Z') ||
        (path[0]>=L'a' && path[0]<=L'z')) || path[1]!=L':' || path[2]!=L'\\' ||
        path.find_first_of(L":\"<>|?*/",2)!=std::wstring::npos)return false;
    size_t start=3;
    while(start<path.size()) {
        const size_t separator=path.find(L'\\',start);
        const size_t end=separator==std::wstring::npos?path.size():separator;
        if(end==start || path[end-1]==L'.' || path[end-1]==L' ')return false;
        for(size_t i=start;i<end;++i)if(path[i]<L' ')return false;
        start=end+1;
    }
    return true;
}
inline bool OwnsBinding(const std::wstring& storedSid,const std::wstring& storedExe,
    const std::wstring& sid,const std::wstring& exe) {
    return !sid.empty() && LocalPath(exe) && Same(storedSid,sid) && Same(storedExe,exe);
}
inline bool CanRestart(DWORD exitCode,bool sessionActive,bool stopping,unsigned attempts) {
    return exitCode!=0 && sessionActive && !stopping && attempts<RetryLimit;
}

struct HandleCloser {
    void operator()(void* handle)const {if(handle && handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
};
using Handle=std::unique_ptr<void,HandleCloser>;
inline Handle FileHandle(HANDLE value){return Handle(value==INVALID_HANDLE_VALUE?nullptr:value);}
inline bool RunningImage(HANDLE file) {
    if(!file || file==INVALID_HANDLE_VALUE)return false;
    using Query=LONG(NTAPI*)(HANDLE,ULONG,void*,ULONG,ULONG*);
    const auto module=GetModuleHandleW(L"ntdll.dll");
    const auto query=module?reinterpret_cast<Query>(GetProcAddress(module,"NtQueryInformationProcess")):nullptr;
    // Native 信息类 44 按文件对象比对当前映像，避免路径在提权后被替换。
    // 接口不可用时拒绝安装，不退化为仅比较路径或字节。
    // 定义与语义：https://github.com/m417z/ntdoc/blob/main/descriptions/processinfoclass.md#processimagefilemapping-44
    return query && query(GetCurrentProcess(),44,&file,sizeof(file),nullptr)==0;
}
inline Handle OpenNotificationPipe(const wchar_t* name) {
    return FileHandle(CreateFileW(name,FILE_READ_DATA|SYNCHRONIZE,0,nullptr,OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_ANONYMOUS,nullptr));
}
inline std::wstring NotificationSecurity(const std::wstring& sid) {
    // CreateFile 还会读取管道属性。此掩码只允许读取，避开与创建实例重叠的写入权限。
    return L"D:P(A;;FA;;;SY)(A;;0x100081;;;"+sid+L")";
}
struct ServiceCloser {
    void operator()(SC_HANDLE handle)const {if(handle)CloseServiceHandle(handle);}
};
using Service=std::unique_ptr<std::remove_pointer_t<SC_HANDLE>,ServiceCloser>;
struct LocalCloser {
    void operator()(void* value)const {if(value)LocalFree(value);}
};
using Local=std::unique_ptr<void,LocalCloser>;

inline std::wstring TokenSid(HANDLE token) {
    DWORD bytes=0;
    GetTokenInformation(token,TokenUser,nullptr,0,&bytes);
    if(!bytes)return {};
    std::vector<BYTE> buffer(bytes);
    if(!GetTokenInformation(token,TokenUser,buffer.data(),bytes,&bytes))return {};
    wchar_t* sid=nullptr;
    if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,&sid))return {};
    Local storage(sid);
    return sid;
}
inline bool CanonicalSid(const std::wstring& value) {
    PSID sid=nullptr;
    if(!ConvertStringSidToSidW(value.c_str(),&sid))return false;
    Local storage(sid);
    wchar_t* roundTrip=nullptr;
    if(!ConvertSidToStringSidW(sid,&roundTrip))return false;
    Local name(roundTrip);
    return Same(value,roundTrip);
}
inline std::wstring ProgramFiles() {
    // 只取 64 位机器注册表的 Program Files 路径，不使用启动方环境变量。
    wchar_t path[32768]{};
    DWORD bytes=sizeof(path);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion",
        L"ProgramFilesDir",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,path,&bytes)!=ERROR_SUCCESS ||
        !LocalPath(path))return {};
    std::wstring result(path);
    if(result.size()>3 && result.back()==L'\\')result.pop_back();
    if(result.size()==3)return {};
    return result;
}
}

inline std::wstring InstalledGuardPath() {
    const auto base=detail::ProgramFiles();
    return base.empty()?L"":base+L"\\"+detail::DirectoryName+L"\\"+detail::ImageName;
}
inline std::wstring CurrentUserSid() {
    HANDLE token=nullptr;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return {};
    detail::Handle storage(token);
    return detail::TokenSid(token);
}

namespace detail {
struct Binding {
    std::wstring sid;
    std::wstring exe;
    std::wstring home;
};

// 只有 SCM 指向固定守护程序、使用指定账户和参数格式时，才承认该配置属于本程序。
inline State Query(SC_HANDLE service,Binding& binding) {
    DWORD bytes=0;
    if(QueryServiceConfigW(service,nullptr,0,&bytes) || GetLastError()!=ERROR_INSUFFICIENT_BUFFER)return State::Error;
    std::vector<BYTE> storage(bytes);
    auto config=reinterpret_cast<QUERY_SERVICE_CONFIGW*>(storage.data());
    if(!QueryServiceConfigW(service,config,bytes,&bytes))return State::Error;
    if(config->dwServiceType!=SERVICE_WIN32_OWN_PROCESS || !config->lpServiceStartName ||
        !Same(config->lpServiceStartName,L"LocalSystem") || !config->lpBinaryPathName)return State::Other;
    int argc=0;
    auto argv=CommandLineToArgvW(config->lpBinaryPathName,&argc);
    Local arguments(argv);
    const auto image=InstalledGuardPath();
    if(!argv || argc!=5 || image.empty() || !Same(argv[0],image) || std::wstring(argv[1])!=ServiceSwitch ||
        !CanonicalSid(argv[2]) || !LocalPath(argv[3]) || !LocalPath(argv[4]))return State::Other;
    binding={argv[2],argv[3],argv[4]};
    return State::On;
}
inline State ReadBinding(const std::wstring& sid,const std::wstring& exe,Binding& binding) {
    if(!CanonicalSid(sid) || !LocalPath(exe))return State::Error;
    Service manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));
    if(!manager)return State::Error;
    Service service(OpenServiceW(manager.get(),ServiceName,SERVICE_QUERY_CONFIG));
    if(!service)return GetLastError()==ERROR_SERVICE_DOES_NOT_EXIST?State::Off:State::Error;
    const auto state=Query(service.get(),binding);
    if(state!=State::On)return state;
    return OwnsBinding(binding.sid,binding.exe,sid,exe)?State::On:State::Other;
}
}

inline State Read(const std::wstring& sid,const std::wstring& exe) {
    detail::Binding binding;
    return detail::ReadBinding(sid,exe,binding);
}

namespace detail {
inline void CALLBACK NotifyWorker(PTP_CALLBACK_INSTANCE,void* context) {
    std::unique_ptr<std::wstring> home(static_cast<std::wstring*>(context));
    Binding binding;
    if(ReadBinding(CurrentUserSid(),quota_startup::CurrentExe(),binding)!=State::On || !Same(binding.home,*home))return;
    // 连接本身就是通知。短暂等待守护端确认，使它能在连接关闭前读取真实客户端 PID。
    // 匿名 SQOS 避免伪造的管道端点取得调用方的身份。
    const ULONGLONG deadline=GetTickCount64()+2000;
    Handle pipe;
    do {
        pipe=OpenNotificationPipe(PipeName);
        if(pipe)break;
        const DWORD error=GetLastError();
        if(error!=ERROR_PIPE_BUSY && error!=ERROR_FILE_NOT_FOUND)return;
        Sleep(50);
    }while(GetTickCount64()<deadline);
    if(!pipe)return;
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    if(!event)return;
    OVERLAPPED pending{};
    pending.hEvent=event.get();
    BYTE ack=0;
    DWORD read=0;
    if(!ReadFile(pipe.get(),&ack,1,&read,&pending) && GetLastError()==ERROR_IO_PENDING) {
        if(WaitForSingleObject(event.get(),1000)!=WAIT_OBJECT_0)CancelIoEx(pipe.get(),&pending);
        GetOverlappedResult(pipe.get(),&pending,&read,TRUE);
    }
}
}

// 读取 SCM 和等待管道均在后台进行，窗口线程只排队一次。
inline void Notify(const std::wstring& home) {
    if(!detail::LocalPath(home))return;
    auto argument=std::make_unique<std::wstring>(home);
    auto raw=argument.release();
    if(!TrySubmitThreadpoolCallback(detail::NotifyWorker,raw,nullptr))delete raw;
}
}

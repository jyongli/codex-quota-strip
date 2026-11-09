#include "QuotaGuard.h"
#include <aclapi.h>
#include <array>
#include <mutex>
#include <unordered_map>
#include <userenv.h>
#include <wtsapi32.h>

#pragma comment(lib,"userenv.lib")
#pragma comment(lib,"wtsapi32.lib")

namespace {
using quota_guard::ServiceName;
using quota_guard::State;
using namespace quota_guard::detail;

constexpr DWORD ShareReadWrite=FILE_SHARE_READ|FILE_SHARE_WRITE;
constexpr DWORD ServiceTimeout=12000;
constexpr wchar_t TrustedInstaller[]=L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464";
constexpr wchar_t InstalledSecurity[]=L"O:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x1200a9;;;BU)";
constexpr wchar_t ManagementMutex[]=L"Global\\CodexQuotaStripGuard.Management";

struct Security {
    Local descriptor;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,FALSE};
    explicit Security(const std::wstring& sddl) {
        PSECURITY_DESCRIPTOR parsed=nullptr;
        if(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&parsed,nullptr)) {
            descriptor.reset(parsed);
            attributes.lpSecurityDescriptor=parsed;
        }
    }
};

bool PrivilegedSid(PSID sid,PSID installer) {
    return IsWellKnownSid(sid,WinLocalSystemSid) || IsWellKnownSid(sid,WinBuiltinAdministratorsSid) ||
        (installer && EqualSid(sid,installer));
}

bool IsElevatedAdministrator() {
    BYTE sid[SECURITY_MAX_SID_SIZE]{};
    DWORD bytes=sizeof(sid);
    BOOL administrator=FALSE;
    return CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,sid,&bytes) &&
        CheckTokenMembership(nullptr,sid,&administrator) && administrator;
}

struct UnlockMutex {
    void operator()(void* mutex)const {ReleaseMutex(mutex);}
};

// 逐级核对目录和文件的实际对象；祖先目录不得允许普通用户替换路径。
bool ProtectedObject(HANDLE file,bool directory,bool installed,PSID installer) {
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file,&info) || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) ||
        directory!=((info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0) ||
        (!directory && info.nNumberOfLinks!=1))return false;
    PSID owner=nullptr;
    PACL acl=nullptr;
    PSECURITY_DESCRIPTOR raw=nullptr;
    const auto result=GetSecurityInfo(file,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
        &owner,nullptr,&acl,nullptr,&raw);
    Local descriptor(raw);
    if(result!=ERROR_SUCCESS || !owner || !acl || !PrivilegedSid(owner,installer))return false;
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision=0;
    if(installed && (!GetSecurityDescriptorControl(raw,&control,&revision) || !(control&SE_DACL_PROTECTED)))return false;

    const DWORD unsafe=DELETE|WRITE_DAC|WRITE_OWNER|FILE_DELETE_CHILD|FILE_WRITE_ATTRIBUTES|FILE_WRITE_EA|
        (installed?(FILE_WRITE_DATA|FILE_APPEND_DATA):0);
    for(DWORD index=0;index<acl->AceCount;++index) {
        void* entry=nullptr;
        if(!GetAce(acl,index,&entry))return false;
        const auto header=static_cast<ACE_HEADER*>(entry);
        if(header->AceFlags&INHERIT_ONLY_ACE)continue;
        if(header->AceType==ACCESS_DENIED_ACE_TYPE)continue;
        if(header->AceType!=ACCESS_ALLOWED_ACE_TYPE)return false;
        auto ace=static_cast<ACCESS_ALLOWED_ACE*>(entry);
        DWORD mask=ace->Mask;
        GENERIC_MAPPING mapping{FILE_GENERIC_READ,FILE_GENERIC_WRITE,FILE_GENERIC_EXECUTE,FILE_ALL_ACCESS};
        MapGenericMask(&mask,&mapping);
        if((mask&unsafe) && !PrivilegedSid(&ace->SidStart,installer))return false;
    }
    return true;
}

Handle ProtectedOpen(const std::wstring& path,bool directory,bool installed,PSID installer=nullptr) {
    auto file=FileHandle(CreateFileW(path.c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,ShareReadWrite,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr));
    if(file && !ProtectedObject(file.get(),directory,installed,installer)) {
        file.reset();
        SetLastError(ERROR_ACCESS_DENIED);
    }
    return file;
}

bool Missing(DWORD error){return error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND;}

bool PinProgramFiles(const std::wstring& base,std::vector<Handle>& pins) {
    if(base.empty() || GetDriveTypeW(base.substr(0,3).c_str())!=DRIVE_FIXED)return false;
    PSID trustedInstaller=nullptr;
    if(!ConvertStringSidToSidW(TrustedInstaller,&trustedInstaller))return false;
    Local sid(trustedInstaller);
    size_t end=3;
    for(;;) {
        auto directory=ProtectedOpen(base.substr(0,end),true,false,trustedInstaller);
        if(!directory)return false;
        pins.push_back(std::move(directory));
        if(end==base.size())return true;
        const auto next=base.find(L'\\',end==3?end:end+1);
        end=next==std::wstring::npos?base.size():next;
    }
}

DWORD CopyGuard(const std::wstring& image,bool& created) {
    const auto source=quota_startup::CurrentExe();
    if(!LocalPath(source))return ERROR_INVALID_NAME;
    auto input=FileHandle(CreateFileW(source.c_str(),GENERIC_READ|FILE_EXECUTE|SYNCHRONIZE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!input)return GetLastError();
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(input.get(),&info) ||
        (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) ||
        !RunningImage(input.get()))return ERROR_ACCESS_DENIED;

    Security security(InstalledSecurity);
    if(!security.descriptor)return GetLastError();
    // CREATE_NEW 保证提权路径不覆盖现有文件，目的文件使用自己的受保护 ACL。
    auto output=FileHandle(CreateFileW(image.c_str(),GENERIC_WRITE,0,&security.attributes,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(!output)return GetLastError();
    created=true;
    std::array<BYTE,16384> buffer{};
    for(;;) {
        DWORD read=0,written=0;
        if(!ReadFile(input.get(),buffer.data(),static_cast<DWORD>(buffer.size()),&read,nullptr))return GetLastError();
        if(!read)return FlushFileBuffers(output.get())?ERROR_SUCCESS:GetLastError();
        if(!WriteFile(output.get(),buffer.data(),read,&written,nullptr))return GetLastError();
        if(written!=read)return ERROR_WRITE_FAULT;
    }
}

DWORD ServiceStatus(SC_HANDLE service,SERVICE_STATUS_PROCESS& status) {
    DWORD bytes=0;
    if(!QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&bytes))
        return GetLastError();
    return ERROR_SUCCESS;
}
DWORD WaitForService(SC_HANDLE service,DWORD expected) {
    const ULONGLONG until=GetTickCount64()+ServiceTimeout;
    do {
        SERVICE_STATUS_PROCESS status{};
        const DWORD error=ServiceStatus(service,status);
        if(error!=ERROR_SUCCESS)return error;
        if(status.dwCurrentState==expected)return ERROR_SUCCESS;
        if(expected==SERVICE_RUNNING && status.dwCurrentState==SERVICE_STOPPED)
            return status.dwWin32ExitCode?status.dwWin32ExitCode:ERROR_SERVICE_NOT_ACTIVE;
        Sleep(100);
    }while(GetTickCount64()<until);
    return ERROR_TIMEOUT;
}
DWORD StartGuard(SC_HANDLE service) {
    if(!StartServiceW(service,0,nullptr) && GetLastError()!=ERROR_SERVICE_ALREADY_RUNNING)return GetLastError();
    return WaitForService(service,SERVICE_RUNNING);
}
DWORD StopGuard(SC_HANDLE service) {
    SERVICE_STATUS_PROCESS status{};
    const DWORD error=ServiceStatus(service,status);
    if(error!=ERROR_SUCCESS)return error;
    if(status.dwCurrentState==SERVICE_STOPPED)return ERROR_SUCCESS;
    Handle process;
    if(status.dwProcessId)process.reset(OpenProcess(SYNCHRONIZE,FALSE,status.dwProcessId));
    SERVICE_STATUS stopped{};
    if(status.dwCurrentState!=SERVICE_STOP_PENDING && !ControlService(service,SERVICE_CONTROL_STOP,&stopped)
        && GetLastError()!=ERROR_SERVICE_NOT_ACTIVE)return GetLastError();
    const DWORD result=WaitForService(service,SERVICE_STOPPED);
    if(result==ERROR_SUCCESS && process && WaitForSingleObject(process.get(),ServiceTimeout)!=WAIT_OBJECT_0)
        return ERROR_TIMEOUT;
    return result;
}

DWORD ManageGuard(bool enable,const Binding& requested) {
    if(!CanonicalSid(requested.sid) || !LocalPath(requested.exe) || requested.exe.back()==L'\\' ||
        (enable && !LocalPath(requested.home)))return ERROR_INVALID_PARAMETER;
    // UAC 只能由同一账号同意。使用其他管理员凭据启动不能替另一账号注册服务。
    if(!Same(quota_guard::CurrentUserSid(),requested.sid) || !IsElevatedAdministrator())return ERROR_ACCESS_DENIED;
    // 所有管理动作串行化，避免重复提权时同时启动和移除同一个服务。
    Security managementSecurity(L"O:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)");
    if(!managementSecurity.descriptor)return GetLastError();
    const Handle mutex(CreateMutexExW(&managementSecurity.attributes,ManagementMutex,0,SYNCHRONIZE|MUTEX_MODIFY_STATE));
    if(!mutex)return GetLastError();
    const DWORD acquired=WaitForSingleObject(mutex.get(),ServiceTimeout);
    if(acquired!=WAIT_OBJECT_0 && acquired!=WAIT_ABANDONED)
        return acquired==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError();
    const std::unique_ptr<void,UnlockMutex> unlock(mutex.get());
    const auto base=ProgramFiles();
    if(base.empty())return ERROR_PATH_NOT_FOUND;
    const auto directory=base+L"\\"+DirectoryName;
    const auto image=directory+L"\\"+ImageName;
    Service manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT|(enable?SC_MANAGER_CREATE_SERVICE:0)));
    if(!manager)return GetLastError();
    constexpr DWORD rights=SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|SERVICE_START|SERVICE_STOP|DELETE;
    Service service(OpenServiceW(manager.get(),ServiceName,rights));
    if(!service) {
        const DWORD error=GetLastError();
        if(error!=ERROR_SERVICE_DOES_NOT_EXIST)return error;
        if(!enable)return ERROR_SUCCESS;
    }else{
        Binding existing;
        const auto state=Query(service.get(),existing);
        if(state==State::Error)return ERROR_ACCESS_DENIED;
        if(state!=State::On || !OwnsBinding(existing.sid,existing.exe,requested.sid,requested.exe) ||
            (enable && !Same(existing.home,requested.home)))return ERROR_ALREADY_EXISTS;
    }
    std::vector<Handle> pins;
    if(!PinProgramFiles(base,pins))return ERROR_ACCESS_DENIED;

    if(service) {
        auto folder=ProtectedOpen(directory,true,true);
        if(!folder && (enable || !Missing(GetLastError())))return GetLastError();
        auto installed=folder?ProtectedOpen(image,false,true):Handle{};
        if(folder && !installed && (enable || !Missing(GetLastError())))return GetLastError();
        if(enable)return StartGuard(service.get());
        const DWORD stopped=StopGuard(service.get());
        if(stopped!=ERROR_SUCCESS)return stopped;
        // 保留 SCM 归属记录直到清理成功，遇到占用时允许用户重试移除。
        if(installed) {
            installed.reset();
            if(!DeleteFileW(image.c_str()) && !Missing(GetLastError()))return GetLastError();
        }
        if(folder) {
            folder.reset();
            if(!RemoveDirectoryW(directory.c_str()) && !Missing(GetLastError()))return GetLastError();
        }
        return DeleteService(service.get())?ERROR_SUCCESS:GetLastError();
    }

    Security security(InstalledSecurity);
    if(!security.descriptor)return GetLastError();
    // 上次 SCM 创建失败可能只留下空目录；仅允许复用通过 ACL 校验的空目录。
    if(!CreateDirectoryW(directory.c_str(),&security.attributes) && GetLastError()!=ERROR_ALREADY_EXISTS)return GetLastError();
    auto folder=ProtectedOpen(directory,true,true);
    if(!folder)return GetLastError();
    std::error_code emptyError;
    if(!std::filesystem::is_empty(directory,emptyError) || emptyError)return ERROR_ALREADY_EXISTS;

    // 先登记归属，再创建映像。清理失败时保留 SCM 记录，使下次可以继续移除。
    using quota_startup::Quote;
    const auto command=Quote(image)+L" "+ServiceSwitch+L" "+Quote(requested.sid)+L" "+
        Quote(requested.exe)+L" "+Quote(requested.home);
    service.reset(CreateServiceW(manager.get(),ServiceName,L"Codex Quota Strip Guard",rights,
        SERVICE_WIN32_OWN_PROCESS,SERVICE_AUTO_START,SERVICE_ERROR_NORMAL,command.c_str(),
        nullptr,nullptr,nullptr,nullptr,nullptr));
    if(!service) {
        const DWORD error=GetLastError();
        folder.reset();
        if(!RemoveDirectoryW(directory.c_str()) && !Missing(GetLastError()))return GetLastError();
        return error;
    }
    bool createdImage=false;
    DWORD result=CopyGuard(image,createdImage);
    if(result==ERROR_SUCCESS) {
        auto installed=ProtectedOpen(image,false,true);
        if(!installed)result=GetLastError();
        else result=StartGuard(service.get());
    }
    if(result!=ERROR_SUCCESS) {
        // 逐步确认回收结果；占用或停止失败时留下可重试的归属记录。
        const DWORD stopped=StopGuard(service.get());
        if(stopped!=ERROR_SUCCESS)return stopped;
        if(createdImage && !DeleteFileW(image.c_str()) && !Missing(GetLastError()))return GetLastError();
        folder.reset();
        if(!RemoveDirectoryW(directory.c_str()) && !Missing(GetLastError()))return GetLastError();
        if(!DeleteService(service.get()))return GetLastError();
    }
    return result;
}

struct Watch {
    Handle process;
    LUID login{};
    unsigned attempts=0;
    ULONGLONG restartAt=0;
};
struct GuardRuntime {
    Binding binding;
    Handle stop;
    SERVICE_STATUS_HANDLE service=nullptr;
    std::mutex sessions;
    // 只保留受监视会话的注销标记，避免服务随无关会话的登录次数累积状态。
    std::unordered_map<DWORD,bool> loggedOff;
    std::unordered_map<DWORD,Watch> watches;
};

void Report(GuardRuntime& runtime,DWORD state,DWORD error=ERROR_SUCCESS) {
    SERVICE_STATUS status{};
    status.dwServiceType=SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState=state;
    status.dwWin32ExitCode=error;
    if(state==SERVICE_RUNNING)status.dwControlsAccepted=SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN|SERVICE_ACCEPT_SESSIONCHANGE;
    if(state==SERVICE_START_PENDING || state==SERVICE_STOP_PENDING) {
        status.dwCheckPoint=1;
        status.dwWaitHint=ServiceTimeout;
    }
    SetServiceStatus(runtime.service,&status);
}
DWORD WINAPI Control(DWORD control,DWORD event,void* data,void* context) {
    auto& runtime=*static_cast<GuardRuntime*>(context);
    if(control==SERVICE_CONTROL_STOP || control==SERVICE_CONTROL_SHUTDOWN) {
        std::lock_guard<std::mutex> lock(runtime.sessions);
        SetEvent(runtime.stop.get());
        return ERROR_SUCCESS;
    }
    if(control==SERVICE_CONTROL_SESSIONCHANGE && data) {
        const auto session=static_cast<WTSSESSION_NOTIFICATION*>(data)->dwSessionId;
        std::lock_guard<std::mutex> lock(runtime.sessions);
        const auto tracked=runtime.loggedOff.find(session);
        if(tracked!=runtime.loggedOff.end()) {
            if(event==WTS_SESSION_LOGOFF)tracked->second=true;
            if(event==WTS_SESSION_LOGON)tracked->second=false;
        }
        return ERROR_SUCCESS;
    }
    return control==SERVICE_CONTROL_INTERROGATE?ERROR_SUCCESS:ERROR_CALL_NOT_IMPLEMENTED;
}
bool Stopping(const GuardRuntime& runtime) {
    return WaitForSingleObject(runtime.stop.get(),0)!=WAIT_TIMEOUT;
}
bool LoginId(HANDLE token,LUID& id) {
    TOKEN_STATISTICS info{};
    DWORD bytes=0;
    if(!GetTokenInformation(token,TokenStatistics,&info,sizeof(info),&bytes))return false;
    id=info.AuthenticationId;
    return true;
}
bool SameLogin(const LUID& a,const LUID& b) {return a.HighPart==b.HighPart && a.LowPart==b.LowPart;}

bool LoggedOff(const GuardRuntime& runtime,DWORD session) {
    const auto found=runtime.loggedOff.find(session);
    return found==runtime.loggedOff.end() || found->second;
}

void ForgetSession(GuardRuntime& runtime,DWORD session) {
    std::lock_guard<std::mutex> lock(runtime.sessions);
    runtime.loggedOff.erase(session);
}

Handle UserToken(GuardRuntime& runtime,DWORD session,const LUID& expected) {
    if(session==0 || Stopping(runtime))return {};
    {
        std::lock_guard<std::mutex> lock(runtime.sessions);
        if(LoggedOff(runtime,session))return {};
    }
    HANDLE raw=nullptr;
    if(!WTSQueryUserToken(session,&raw))return {};
    Handle token(raw);
    DWORD actualSession=0,bytes=0;
    TOKEN_ELEVATION_TYPE elevation{};
    if(!GetTokenInformation(token.get(),TokenElevationType,&elevation,sizeof(elevation),&bytes))return {};
    // 有分离令牌的管理员只使用登录实例的普通权限令牌恢复窗口。
    if(elevation==TokenElevationTypeFull) {
        TOKEN_LINKED_TOKEN linked{};
        if(!GetTokenInformation(token.get(),TokenLinkedToken,&linked,sizeof(linked),&bytes))return {};
        token.reset(linked.LinkedToken);
        if(!GetTokenInformation(token.get(),TokenElevationType,&elevation,sizeof(elevation),&bytes) ||
            elevation!=TokenElevationTypeLimited)return {};
    }
    LUID login{};
    if(!GetTokenInformation(token.get(),TokenSessionId,&actualSession,sizeof(actualSession),&bytes) || actualSession!=session ||
        !Same(TokenSid(token.get()),runtime.binding.sid) || !LoginId(token.get(),login) || !SameLogin(login,expected))return {};
    return token;
}

bool WatchClient(GuardRuntime& runtime,HANDLE pipe) {
    ULONG pid=0,clientSession=0;
    if(!GetNamedPipeClientProcessId(pipe,&pid) || !pid ||
        !GetNamedPipeClientSessionId(pipe,&clientSession) || !clientSession)return false;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));
    if(!process || WaitForSingleObject(process.get(),0)!=WAIT_TIMEOUT)return false;
    HANDLE raw=nullptr;
    if(!OpenProcessToken(process.get(),TOKEN_QUERY,&raw))return false;
    Handle token(raw);
    DWORD session=0,bytes=0;
    LUID login{};
    if(!GetTokenInformation(raw,TokenSessionId,&session,sizeof(session),&bytes) || session!=clientSession ||
        !Same(TokenSid(raw),runtime.binding.sid) || !LoginId(raw,login))return false;
    wchar_t image[32768]{};
    DWORD length=static_cast<DWORD>(std::size(image));
    if(!QueryFullProcessImageNameW(process.get(),0,image,&length) || !Same(image,runtime.binding.exe))return false;
    // 每个登录会话只跟踪一个窗口进程。恢复子进程重复连接时保留原重试次数。
    auto found=runtime.watches.find(session);
    if(found!=runtime.watches.end() && found->second.process &&
        WaitForSingleObject(found->second.process.get(),0)==WAIT_TIMEOUT)
        return GetProcessId(found->second.process.get())==pid;
    if(found==runtime.watches.end()) {
        if(runtime.watches.size()>=32)return false;
        std::lock_guard<std::mutex> lock(runtime.sessions);
        runtime.loggedOff.try_emplace(session,false);
    }
    if(!UserToken(runtime,session,login)) {
        if(found==runtime.watches.end())ForgetSession(runtime,session);
        return false;
    }
    runtime.watches.insert_or_assign(session,Watch{std::move(process),login});
    return true;
}

struct EnvironmentCloser {
    void operator()(void* environment)const {if(environment)DestroyEnvironmentBlock(environment);}
};
Handle Restart(GuardRuntime& runtime,DWORD session,const LUID& login) {
    auto token=UserToken(runtime,session,login);
    if(!token)return {};
    void* raw=nullptr;
    if(!CreateEnvironmentBlock(&raw,token.get(),FALSE))return {};
    std::unique_ptr<void,EnvironmentCloser> environment(raw);
    using quota_startup::Quote;
    auto command=Quote(runtime.binding.exe)+L" --home "+Quote(runtime.binding.home);
    const auto directory=runtime.binding.exe.substr(0,runtime.binding.exe.find_last_of(L'\\')+1);
    wchar_t desktop[]=L"winsta0\\default";
    STARTUPINFOW startup{};
    startup.cb=sizeof(startup);
    startup.lpDesktop=desktop;
    PROCESS_INFORMATION child{};
    // 先以登录用户身份访问路径并创建挂起的进程，再确认该登录仍有效。
    if(Stopping(runtime) || !ImpersonateLoggedOnUser(token.get()))return {};
    const BOOL created=CreateProcessAsUserW(token.get(),runtime.binding.exe.c_str(),command.data(),nullptr,nullptr,FALSE,
        CREATE_UNICODE_ENVIRONMENT|CREATE_SUSPENDED,environment.get(),directory.c_str(),&startup,&child);
    if(!RevertToSelf())ExitProcess(ERROR_ACCESS_DENIED);
    if(!created)return {};
    Handle process(child.hProcess),thread(child.hThread);
    const bool currentLogin=static_cast<bool>(UserToken(runtime,session,login));
    {
        std::lock_guard<std::mutex> lock(runtime.sessions);
        if(currentLogin && !Stopping(runtime) && !LoggedOff(runtime,session) && ResumeThread(thread.get())!=DWORD(-1))
            return process;
    }
    TerminateProcess(process.get(),0);
    return {};
}

void UpdateWatches(GuardRuntime& runtime) {
    const ULONGLONG now=GetTickCount64();
    for(auto it=runtime.watches.begin();it!=runtime.watches.end();) {
        const auto session=it->first;
        auto& watch=it->second;
        bool remove=false;
        if(watch.process && WaitForSingleObject(watch.process.get(),0)==WAIT_OBJECT_0) {
            DWORD exitCode=0;
            remove=!GetExitCodeProcess(watch.process.get(),&exitCode) ||
                !CanRestart(exitCode,static_cast<bool>(UserToken(runtime,session,watch.login)),Stopping(runtime),watch.attempts);
            watch.process.reset();
            watch.restartAt=now+(500ULL<<watch.attempts);
        }
        if(!remove && !watch.process && now>=watch.restartAt) {
            ++watch.attempts;
            watch.process=Restart(runtime,session,watch.login);
            remove=!watch.process && (watch.attempts>=RetryLimit || !UserToken(runtime,session,watch.login));
            watch.restartAt=now+(500ULL<<watch.attempts);
        }
        if(remove) {
            ForgetSession(runtime,session);
            it=runtime.watches.erase(it);
        }
        else ++it;
    }
}

void Ack(HANDLE pipe) {
    const Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    if(!event)return;
    OVERLAPPED write{};
    write.hEvent=event.get();
    const BYTE value=1;
    DWORD bytes=0;
    if(!WriteFile(pipe,&value,1,&bytes,&write) && GetLastError()==ERROR_IO_PENDING) {
        CancelIoEx(pipe,&write);
        GetOverlappedResult(pipe,&write,&bytes,TRUE);
    }
}

DWORD Listen(GuardRuntime& runtime) {
    // 精确授权绑定用户和 SYSTEM。使用具体读权限，避免授予普通用户创建服务端实例的权限。
    Security security(NotificationSecurity(runtime.binding.sid));
    if(!security.descriptor)return GetLastError();
    auto pipe=FileHandle(CreateNamedPipeW(PipeName,PIPE_ACCESS_OUTBOUND|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_REJECT_REMOTE_CLIENTS,1,128,0,0,&security.attributes));
    if(!pipe)return GetLastError();
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    if(!event)return GetLastError();
    Report(runtime,SERVICE_RUNNING);
    while(!Stopping(runtime)) {
        ResetEvent(event.get());
        OVERLAPPED connect{};
        connect.hEvent=event.get();
        const BOOL immediate=ConnectNamedPipe(pipe.get(),&connect);
        const DWORD error=immediate?ERROR_SUCCESS:GetLastError();
        if(error==ERROR_IO_PENDING) {
            for(;;) {
                // 进程退出直接唤醒等待，不用定时器轮询；只有失败重试需要到期时间。
                std::vector<HANDLE> events{runtime.stop.get(),event.get()};
                DWORD timeout=INFINITE;
                const auto now=GetTickCount64();
                for(const auto& entry:runtime.watches) {
                    const auto& watch=entry.second;
                    if(watch.process)events.push_back(watch.process.get());
                    else timeout=std::min<DWORD>(timeout,watch.restartAt<=now?0:
                        static_cast<DWORD>(std::min<ULONGLONG>(watch.restartAt-now,MAXDWORD-1)));
                }
                const DWORD wait=WaitForMultipleObjects(static_cast<DWORD>(events.size()),events.data(),FALSE,timeout);
                if(wait==WAIT_OBJECT_0+1)break;
                if(wait!=WAIT_TIMEOUT && !(wait>=WAIT_OBJECT_0+2 && wait<WAIT_OBJECT_0+events.size())) {
                    CancelIoEx(pipe.get(),&connect);
                    DWORD bytes=0;
                    GetOverlappedResult(pipe.get(),&connect,&bytes,TRUE);
                    return wait==WAIT_OBJECT_0?ERROR_SUCCESS:ERROR_GEN_FAILURE;
                }
                UpdateWatches(runtime);
            }
            DWORD bytes=0;
            if(GetOverlappedResult(pipe.get(),&connect,&bytes,FALSE) && !Stopping(runtime) && WatchClient(runtime,pipe.get()))
                Ack(pipe.get());
        }else if(error==ERROR_SUCCESS || error==ERROR_PIPE_CONNECTED) {
            if(!Stopping(runtime) && WatchClient(runtime,pipe.get()))Ack(pipe.get());
        }else if(error!=ERROR_NO_DATA)return error;
        DisconnectNamedPipe(pipe.get());
        UpdateWatches(runtime);
    }
    return ERROR_SUCCESS;
}

void WINAPI RunService(DWORD,wchar_t**) {
    GuardRuntime runtime;
    runtime.stop.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    if(!runtime.stop)return;
    runtime.service=RegisterServiceCtrlHandlerExW(ServiceName,Control,&runtime);
    if(!runtime.service)return;
    Report(runtime,SERVICE_START_PENDING);
    const auto protectedImage=quota_guard::InstalledGuardPath();
    DWORD result=ERROR_ACCESS_DENIED;
    // 配置只从 SCM 读取。命令行和 IPC 不得提供任何恢复目标。
    if(Same(quota_guard::CurrentUserSid(),L"S-1-5-18") && !protectedImage.empty() &&
        Same(quota_startup::CurrentExe(),protectedImage)) {
        Service manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));
        Service service(manager?OpenServiceW(manager.get(),ServiceName,SERVICE_QUERY_CONFIG):nullptr);
        if(service && Query(service.get(),runtime.binding)==State::On)result=Listen(runtime);
    }
    Report(runtime,SERVICE_STOP_PENDING);
    Report(runtime,SERVICE_STOPPED,result);
}
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    int argc=0;
    auto args=CommandLineToArgvW(GetCommandLineW(),&argc);
    Local storage(args);
    if(!args || argc<2)return ERROR_INVALID_PARAMETER;
    const std::wstring command=args[1];
    if(command==ServiceSwitch && argc==5) {
        SERVICE_TABLE_ENTRYW dispatch[]={{const_cast<wchar_t*>(ServiceName),RunService},{nullptr,nullptr}};
        return StartServiceCtrlDispatcherW(dispatch)?ERROR_SUCCESS:static_cast<int>(GetLastError());
    }
    if(command==L"--register" && argc==5)return static_cast<int>(ManageGuard(true,{args[2],args[3],args[4]}));
    if(command==L"--unregister" && argc==4)return static_cast<int>(ManageGuard(false,{args[2],args[3],{}}));
    if(command==L"--check" && argc==4) {
        switch(quota_guard::Read(args[2],args[3])) {
        case State::On:return ERROR_SUCCESS;
        case State::Off:return ERROR_SERVICE_DOES_NOT_EXIST;
        case State::Other:return ERROR_ALREADY_EXISTS;
        default:return ERROR_ACCESS_DENIED;
        }
    }
    return ERROR_INVALID_PARAMETER;
}

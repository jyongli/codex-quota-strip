#include "QuotaGuard.h"
#include <iostream>

static bool SourceImageIdentity() {
    using namespace quota_guard::detail;
    const auto self=quota_startup::CurrentExe();
    auto image=FileHandle(CreateFileW(self.c_str(),GENERIC_READ|FILE_EXECUTE|SYNCHRONIZE,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!image || !RunningImage(image.get()))return false;
    // 即使另一个文件的字节与自身完全相同，也不能把它当成正在运行的映像对象。
    wchar_t temp[32768]{};
    if(!GetTempPathW(static_cast<DWORD>(std::size(temp)),temp))return false;
    const auto copy=std::wstring(temp)+L"quota-guard-image-"+std::to_wstring(GetCurrentProcessId())+
        L"-"+std::to_wstring(GetTickCount64())+L".exe";
    if(!CopyFileW(self.c_str(),copy.c_str(),TRUE))return false;
    auto foreign=FileHandle(CreateFileW(copy.c_str(),GENERIC_READ|FILE_EXECUTE|SYNCHRONIZE,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    const bool rejected=foreign && !RunningImage(foreign.get());
    foreign.reset();
    const bool cleaned=DeleteFileW(copy.c_str())!=FALSE;
    return rejected && cleaned;
}

static bool LocalNotification() {
    using namespace quota_guard::detail;
    const auto failure=[](const char* stage) {
        std::cerr<<stage<<" failed with "<<GetLastError()<<'\n';
        return false;
    };
    // 使用唯一的本机测试管道，实际核对生产客户端访问权限和服务端可见的进程身份。
    const auto name=L"\\\\.\\pipe\\CodexQuotaStripGuard.Tests."+std::to_wstring(GetCurrentProcessId())+
        L"."+std::to_wstring(GetTickCount64());
    PSECURITY_DESCRIPTOR raw=nullptr;
    const auto sid=quota_guard::CurrentUserSid();
    const auto sddl=NotificationSecurity(sid);
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&raw,nullptr))return failure("pipe security");
    Local descriptor(raw);
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),raw,FALSE};
    auto server=FileHandle(CreateNamedPipeW(name.c_str(),PIPE_ACCESS_OUTBOUND|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_REJECT_REMOTE_CLIENTS,2,128,0,0,&security));
    if(!server)return failure("create fixture pipe");
    auto client=OpenNotificationPipe(name.c_str());
    if(!client || ConnectNamedPipe(server.get(),nullptr) || GetLastError()!=ERROR_PIPE_CONNECTED)return failure("connect fixture pipe");
    ULONG pid=0,session=0;
    DWORD expectedSession=0;
    if(!GetNamedPipeClientProcessId(server.get(),&pid) || pid!=GetCurrentProcessId() ||
        !GetNamedPipeClientSessionId(server.get(),&session) ||
        !ProcessIdToSessionId(pid,&expectedSession) || session!=expectedSession)return failure("read fixture client identity");
    if(Same(sid,L"S-1-5-18"))return true; // SYSTEM 是服务端，拥有创建实例的权限。
    auto extraServer=FileHandle(CreateNamedPipeW(name.c_str(),PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_BYTE|PIPE_REJECT_REMOTE_CLIENTS,2,128,0,0,&security));
    return (!extraServer && GetLastError()==ERROR_ACCESS_DENIED) || failure("check pipe instance access");
}

int main() {
    using namespace quota_guard::detail;
    const std::wstring sid=L"S-1-5-21-100-200-300-1001";
    const std::wstring exe=L"C:\\Example Dir\\CodexQuotaStrip.exe";
    const auto check=[](bool ok,const char* message) {
        if(!ok)std::cerr<<"FAIL: "<<message<<'\n';
        return ok;
    };
    bool ok=check(OwnsBinding(sid,exe,sid,L"c:\\example dir\\codexquotastrip.exe"),"same SID and executable must identify the installation");
    ok=check(!OwnsBinding(sid,exe,L"S-1-5-21-100-200-300-1002",exe)
        && !OwnsBinding(sid,exe,sid,L"C:\\Other\\CodexQuotaStrip.exe"),"other users and installations must be excluded") && ok;
    ok=check(!LocalPath(L"\\\\server\\share\\app.exe") && !LocalPath(L"C:\\Folder\\..\\app.exe")
        && !LocalPath(L"C:\\Folder\\app.exe:stream"),"remote and ambiguous paths must be excluded") && ok;
    ok=check(CanRestart(1,true,false,0),"an abnormal exit in the active session must be eligible for recovery") && ok;
    ok=check(!CanRestart(0,true,false,0) && !CanRestart(1,false,false,0)
        && !CanRestart(1,true,true,0) && !CanRestart(1,true,false,3),
        "normal exit, session end, service stop, and retry limit must block recovery") && ok;
    ok=check(LocalNotification(),"local notification must expose PID and session and disallow creating another pipe server") && ok;
    ok=check(SourceImageIdentity(),"an identical copied file must not pass running-image object verification") && ok;
    if(ok)std::cout<<"PASS: guard binding, restart policy, real local pipe notification and running-image identity\n";
    return ok?0:1;
}

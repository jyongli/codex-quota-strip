#pragma once
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <iterator>
#include <string>

namespace quota_startup {
enum class State { Off, On, Other, Error };
inline constexpr wchar_t RunKey[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t RunValue[]=L"CodexQuotaStrip.Login";

// 直接生成 Windows 参数，不经过命令解释器；结束引号前的反斜杠也需转义。
inline std::wstring Quote(const std::wstring& argument){
    std::wstring result=L"\"";
    size_t slashes=0;
    for(wchar_t c:argument){
        if(c==L'\\'){++slashes;continue;}
        result.append(c==L'"'?slashes*2+1:slashes,L'\\');
        result+=c;slashes=0;
    }
    result.append(slashes*2,L'\\');return result+L'"';
}
inline std::wstring CurrentExe(){
    wchar_t path[32768]{};DWORD size=GetModuleFileNameW(nullptr,path,static_cast<DWORD>(std::size(path)));
    return size&&size<std::size(path)?std::wstring(path,size):std::wstring();
}
inline bool AbsolutePath(const std::wstring& value){
    return value.find(L'"')==std::wstring::npos&&std::filesystem::path(value).is_absolute();
}
inline State ReadValue(HKEY root,const wchar_t* key,const std::wstring& exe){
    wchar_t command[261]{};DWORD bytes=sizeof(command);
    const auto status=RegGetValueW(root,key,RunValue,RRF_RT_REG_SZ|RRF_NOEXPAND,nullptr,command,&bytes);
    if(status==ERROR_FILE_NOT_FOUND||status==ERROR_PATH_NOT_FOUND)return State::Off;
    if(status==ERROR_MORE_DATA||status==ERROR_UNSUPPORTED_TYPE)return State::Other;
    if(status!=ERROR_SUCCESS||exe.empty())return State::Error;
    int count=0;auto args=CommandLineToArgvW(command,&count);
    if(!args)return State::Error;
    // 只管理带本程序启动标记、路径也匹配的值；同名自定义命令不碰。
    const bool owned=(count==2||count==4)&&std::wstring(args[1])==L"--login-start"&&
        (count==2||(std::wstring(args[2])==L"--home"&&AbsolutePath(args[3])))&&
        CompareStringOrdinal(args[0],-1,exe.c_str(),-1,TRUE)==CSTR_EQUAL;
    LocalFree(args);return owned?State::On:State::Other;
}
inline State Read(HKEY root,const std::wstring& exe){return ReadValue(root,RunKey,exe);}
inline LSTATUS Set(HKEY root,const std::wstring& exe,const std::wstring& home,bool enabled){
    if(!AbsolutePath(exe)||(enabled&&!home.empty()&&!AbsolutePath(home)))return ERROR_INVALID_PARAMETER;
    std::wstring command;
    if(enabled){
        command=Quote(exe)+L" --login-start";
        if(!home.empty())command+=L" --home "+Quote(home);
        // Windows Run 的命令行最多 260 字符，过长时明确失败。
        if(command.size()>260)return ERROR_FILENAME_EXCED_RANGE;
    }
    HKEY key{};
    auto status=enabled?RegCreateKeyExW(root,RunKey,0,nullptr,0,KEY_SET_VALUE|KEY_QUERY_VALUE,nullptr,&key,nullptr):
        RegOpenKeyExW(root,RunKey,0,KEY_SET_VALUE|KEY_QUERY_VALUE,&key);
    if(!enabled&&(status==ERROR_FILE_NOT_FOUND||status==ERROR_PATH_NOT_FOUND))return ERROR_SUCCESS;
    if(status!=ERROR_SUCCESS)return status;
    const auto current=ReadValue(key,nullptr,exe);
    if(current==State::Other)status=ERROR_ALREADY_EXISTS;
    else if(current==State::Error)status=ERROR_ACCESS_DENIED;
    else if(enabled)status=RegSetValueExW(key,RunValue,0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));
    else if(current==State::On)status=RegDeleteValueW(key,RunValue);
    RegCloseKey(key);return status;
}
}

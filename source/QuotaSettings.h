#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include "QuotaHistory.h"
#include <atomic>
#include <cstring>
namespace quota {
// 查询间隔以秒计；旧快照阈值以分钟计，均有上下界以避免意外高频请求。
struct Settings {
    bool online = true, onlyWithCodex = true, showInCodex = false, leftSide = false, english = false;
    int intervalSeconds = 120, staleMinutes = 5;
    // 空目录使用 settings.ini 旁的 history；关闭记录不删除已写入的 CSV。
    bool historyEnabled = true;
    std::wstring historyDirectory;
};
inline bool ValidSettings(const Settings& s) { return s.intervalSeconds >= 30 && s.intervalSeconds <= 3600 && s.staleMinutes >= 1 && s.staleMinutes <= 120; }
inline Settings LoadSettings(const std::wstring& path) {
    Settings s; wchar_t text[32]{};
    s.online = GetPrivateProfileIntW(L"refresh",L"online",1,path.c_str()) != 0;
    s.onlyWithCodex = GetPrivateProfileIntW(L"refresh",L"only_with_codex",1,path.c_str()) != 0;
    s.intervalSeconds = static_cast<int>(GetPrivateProfileIntW(L"refresh",L"seconds",120,path.c_str()));
    s.staleMinutes = static_cast<int>(GetPrivateProfileIntW(L"refresh",L"stale_minutes",5,path.c_str()));
    if (!ValidSettings(s)) { s.intervalSeconds=120; s.staleMinutes=5; }
    GetPrivateProfileStringW(L"display",L"surface",L"taskbar",text,32,path.c_str()); s.showInCodex=std::wstring(text)==L"taskbar-and-codex";
    GetPrivateProfileStringW(L"display",L"position",L"top",text,32,path.c_str()); s.leftSide=std::wstring(text)==L"left";
    GetPrivateProfileStringW(L"display",L"language",L"zh-CN",text,32,path.c_str()); s.english=std::wstring(text)==L"en";
    s.historyEnabled=GetPrivateProfileIntW(L"history",L"enabled",1,path.c_str())!=0;
    wchar_t directory[32768]{};
    GetPrivateProfileStringW(L"history",L"directory",L"",directory,static_cast<DWORD>(std::size(directory)),path.c_str());
    s.historyDirectory=directory;
    return s;
}
namespace settings_detail {
// INI API 对无 BOM 的文件使用系统 ANSI 页；先完整解码旧文件，保留未知节和键。
inline bool ReadUnicode(const std::wstring& path,std::wstring& text){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){DWORD error=GetLastError();return error==ERROR_FILE_NOT_FOUND;}
    LARGE_INTEGER size{};if(!GetFileSizeEx(file,&size)||size.QuadPart<0||size.QuadPart>2*1024*1024){CloseHandle(file);return false;}
    std::vector<unsigned char> bytes(static_cast<size_t>(size.QuadPart));DWORD read=0;
    bool ok=bytes.empty()||(ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)&&read==bytes.size());CloseHandle(file);if(!ok)return false;
    if(bytes.size()>=2&&((bytes[0]==0xff&&bytes[1]==0xfe)||(bytes[0]==0xfe&&bytes[1]==0xff))){
        if((bytes.size()-2)%2)return false;text.resize((bytes.size()-2)/2);
        for(size_t i=0;i<text.size();++i)text[i]=static_cast<wchar_t>(bytes[0]==0xff?bytes[2+2*i]|(bytes[3+2*i]<<8):(bytes[2+2*i]<<8)|bytes[3+2*i]);
        return true;
    }
    size_t offset=bytes.size()>=3&&bytes[0]==0xef&&bytes[1]==0xbb&&bytes[2]==0xbf?3:0;if(bytes.size()==offset)return true;
    UINT codepage=offset?CP_UTF8:CP_ACP;DWORD flags=offset?MB_ERR_INVALID_CHARS:0;auto source=reinterpret_cast<const char*>(bytes.data()+offset);int length=static_cast<int>(bytes.size()-offset);
    int count=MultiByteToWideChar(codepage,flags,source,length,nullptr,0);if(!count)return false;text.resize(static_cast<size_t>(count));
    return MultiByteToWideChar(codepage,flags,source,length,text.data(),count)==count;
}
inline bool CreateUnicode(const std::wstring& path,const std::wstring& text){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
    const unsigned char bom[]{0xff,0xfe};DWORD written=0;bool ok=WriteFile(file,bom,2,&written,nullptr)&&written==2;
    DWORD length=static_cast<DWORD>(text.size()*sizeof(wchar_t));if(ok&&length)ok=WriteFile(file,text.data(),length,&written,nullptr)&&written==length;
    CloseHandle(file);if(!ok)DeleteFileW(path.c_str());return ok;
}
}
// 无效输入不会写盘；调用者在成功后再应用到当前运行中的程序。
inline bool SaveSettings(const std::wstring& path,const Settings& s) {
    if(!ValidSettings(s)) return false;
    // 保存前验证自选目录，失败时不得部分覆盖原设置；实际日志 I/O 由 HistoryStore 承担。
    if(s.historyEnabled&&!s.historyDirectory.empty()){
        std::string error;
        if(!std::filesystem::path(s.historyDirectory).is_absolute()||!HistoryStore::ValidateDirectory(s.historyDirectory,error))return false;
    }
    // 所有改动先写同目录 UTF-16 临时文件；任一步失败都不覆盖原设置。
    static std::mutex saveMutex;std::lock_guard<std::mutex> guard(saveMutex);static std::atomic<unsigned long long> sequence{0};
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,path.c_str());
    std::wstring text;if(!settings_detail::ReadUnicode(path,text))return false;
    auto temporary=path+L".quota-save-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++sequence)+L".tmp";
    if(!settings_detail::CreateUnicode(temporary,text))return false;
    auto write=[&](const wchar_t* section,const wchar_t* key,const std::wstring& value){return WritePrivateProfileStringW(section,key,value.c_str(),temporary.c_str())!=FALSE;};
    bool written=write(L"refresh",L"online",s.online?L"1":L"0") && write(L"refresh",L"only_with_codex",s.onlyWithCodex?L"1":L"0") &&
        write(L"refresh",L"seconds",std::to_wstring(s.intervalSeconds)) && write(L"refresh",L"stale_minutes",std::to_wstring(s.staleMinutes)) &&
        write(L"display",L"surface",s.showInCodex?L"taskbar-and-codex":L"taskbar") && write(L"display",L"position",s.leftSide?L"left":L"top") && write(L"display",L"language",s.english?L"en":L"zh-CN") &&
        write(L"history",L"enabled",s.historyEnabled?L"1":L"0") && write(L"history",L"directory",s.historyDirectory);
    // 全空参数是缓存刷新请求；Windows 不为此操作提供常规写入成功返回值。
    if(written)WritePrivateProfileStringW(nullptr,nullptr,nullptr,temporary.c_str());
    if(written){HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)written=false;else{written=FlushFileBuffers(file)!=FALSE;CloseHandle(file);}}
    bool saved=written&&MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!saved)DeleteFileW(temporary.c_str());else WritePrivateProfileStringW(nullptr,nullptr,nullptr,path.c_str());
    return saved;
}
}

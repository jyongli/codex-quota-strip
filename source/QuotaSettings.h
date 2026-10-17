#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include "QuotaHistory.h"
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
// 无效输入不会写盘；调用者在成功后再应用到当前运行中的程序。
inline bool SaveSettings(const std::wstring& path,const Settings& s) {
    if(!ValidSettings(s)) return false;
    // 保存前验证自选目录，失败时不得部分覆盖原设置；实际日志 I/O 由 HistoryStore 承担。
    if(s.historyEnabled&&!s.historyDirectory.empty()){
        std::string error;
        if(!std::filesystem::path(s.historyDirectory).is_absolute()||!HistoryStore::ValidateDirectory(s.historyDirectory,error))return false;
    }
    auto write=[&](const wchar_t* section,const wchar_t* key,const std::wstring& value){return WritePrivateProfileStringW(section,key,value.c_str(),path.c_str())!=FALSE;};
    return write(L"refresh",L"online",s.online?L"1":L"0") && write(L"refresh",L"only_with_codex",s.onlyWithCodex?L"1":L"0") &&
        write(L"refresh",L"seconds",std::to_wstring(s.intervalSeconds)) && write(L"refresh",L"stale_minutes",std::to_wstring(s.staleMinutes)) &&
        write(L"display",L"surface",s.showInCodex?L"taskbar-and-codex":L"taskbar") && write(L"display",L"position",s.leftSide?L"left":L"top") && write(L"display",L"language",s.english?L"en":L"zh-CN") &&
        write(L"history",L"enabled",s.historyEnabled?L"1":L"0") && write(L"history",L"directory",s.historyDirectory);
}
}

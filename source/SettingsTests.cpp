#include "QuotaSettings.h"
#include <filesystem>
#include <iostream>
int main(){
    wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);auto file=std::filesystem::path(temp)/(L"quota-settings-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    quota::Settings s;s.intervalSeconds=45;s.staleMinutes=2;s.onlyWithCodex=false;s.showInCodex=true;s.leftSide=true;s.english=true;
    if(!quota::SaveSettings(file.wstring(),s)){std::cerr<<"FAIL: valid settings must save\n";return 1;}
    if(GetPrivateProfileIntW(L"history",L"enabled",-1,file.c_str())!=1){std::cerr<<"FAIL: saved settings must explicitly preserve default enabled history\n";return 1;}
    auto loaded=quota::LoadSettings(file.wstring());
    if(loaded.intervalSeconds!=45||loaded.staleMinutes!=2||loaded.onlyWithCodex||!loaded.showInCodex||!loaded.leftSide||!loaded.english){std::cerr<<"FAIL: restart must preserve settings and UI language\n";return 1;}
    auto history=std::filesystem::path(temp)/(L"quota-history-settings-中文-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    s.historyDirectory=history.wstring();
    if(!quota::SaveSettings(file.wstring(),s)){std::cerr<<"FAIL: writable absolute Unicode history directory must save\n";return 1;}
    loaded=quota::LoadSettings(file.wstring());
    if(!loaded.historyEnabled||loaded.historyDirectory!=history.wstring()){std::cerr<<"FAIL: restart must preserve the history toggle and selected directory\n";return 1;}
    auto relative=s;s.historyDirectory=L"relative-history";
    if(quota::SaveSettings(file.wstring(),s)||quota::LoadSettings(file.wstring()).historyDirectory!=history.wstring()){std::cerr<<"FAIL: a relative history directory must not overwrite saved settings\n";return 1;}
    s=relative;s.historyEnabled=false;
    if(!quota::SaveSettings(file.wstring(),s)||quota::LoadSettings(file.wstring()).historyEnabled){std::cerr<<"FAIL: disabled history must persist without clearing the selected directory\n";return 1;}
    s.intervalSeconds=0;if(quota::SaveSettings(file.wstring(),s)||quota::LoadSettings(file.wstring()).intervalSeconds!=45){std::cerr<<"FAIL: invalid interval must not overwrite saved settings\n";return 1;}
    DeleteFileW(file.c_str());std::filesystem::remove(history);std::cout<<"PASS: settings round trip, history enable/path persistence, Unicode directory and invalid input protection\n";return 0;
}

#include "QuotaSettings.h"
#include <filesystem>
#include <iostream>
#include <fstream>
int main(){
    wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);auto file=std::filesystem::path(temp)/(L"quota-settings-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    {std::ofstream original(file,std::ios::binary);original<<"[custom]\nunknown_key=preserve-legacy-ANSI-value\n";}
    quota::Settings s;s.intervalSeconds=45;s.staleMinutes=2;s.onlyWithCodex=false;s.showInCodex=true;s.leftSide=true;s.english=true;
    if(!quota::SaveSettings(file.wstring(),s)){std::cerr<<"FAIL: valid settings must save\n";return 1;}
    if(GetPrivateProfileIntW(L"history",L"enabled",-1,file.c_str())!=1){std::cerr<<"FAIL: saved settings must explicitly preserve default enabled history\n";return 1;}
    {std::ifstream encoded(file,std::ios::binary);unsigned char bom[2]{};encoded.read(reinterpret_cast<char*>(bom),2);if(bom[0]!=0xff||bom[1]!=0xfe){std::cerr<<"FAIL: settings must migrate legacy ANSI to UTF-16LE before storing Unicode paths\n";return 1;}}
    wchar_t unknown[128]{};GetPrivateProfileStringW(L"custom",L"unknown_key",L"",unknown,128,file.c_str());
    if(std::wstring(unknown)!=L"preserve-legacy-ANSI-value"){std::cerr<<"FAIL: encoding migration must preserve unknown sections and values\n";return 1;}
    auto loaded=quota::LoadSettings(file.wstring());
    if(loaded.intervalSeconds!=45||loaded.staleMinutes!=2||loaded.onlyWithCodex||!loaded.showInCodex||!loaded.leftSide||!loaded.english){std::cerr<<"FAIL: restart must preserve settings and UI language\n";return 1;}
    auto history=std::filesystem::path(temp)/(L"quota-history-settings-中文-\U0001F9EA-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    s.historyDirectory=history.wstring();
    if(!quota::SaveSettings(file.wstring(),s)){std::cerr<<"FAIL: writable absolute Unicode history directory must save\n";return 1;}
    loaded=quota::LoadSettings(file.wstring());
    if(!loaded.historyEnabled||loaded.historyDirectory!=history.wstring()){std::cerr<<"FAIL: restart must preserve the history toggle and selected directory\n";return 1;}
    auto relative=s;s.historyDirectory=L"relative-history";
    if(quota::SaveSettings(file.wstring(),s)||quota::LoadSettings(file.wstring()).historyDirectory!=history.wstring()){std::cerr<<"FAIL: a relative history directory must not overwrite saved settings\n";return 1;}
    s=relative;s.historyEnabled=false;
    if(!quota::SaveSettings(file.wstring(),s)||quota::LoadSettings(file.wstring()).historyEnabled){std::cerr<<"FAIL: disabled history must persist without clearing the selected directory\n";return 1;}
    HANDLE held=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(held==INVALID_HANDLE_VALUE){std::cerr<<"FAIL: locked settings fixture must open\n";return 1;}
    auto blockedSave=s;blockedSave.intervalSeconds=60;bool changed=quota::SaveSettings(file.wstring(),blockedSave);CloseHandle(held);
    if(changed||quota::LoadSettings(file.wstring()).intervalSeconds!=45){std::cerr<<"FAIL: failed atomic replacement must preserve the original settings\n";return 1;}
    s.intervalSeconds=0;if(quota::SaveSettings(file.wstring(),s)||quota::LoadSettings(file.wstring()).intervalSeconds!=45){std::cerr<<"FAIL: invalid interval must not overwrite saved settings\n";return 1;}
    DeleteFileW(file.c_str());std::filesystem::remove(history);std::cout<<"PASS: settings round trip, history enable/path persistence, Unicode directory and invalid input protection\n";return 0;
}

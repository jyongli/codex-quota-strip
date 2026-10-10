#include "QuotaSettings.h"
#include <filesystem>
#include <iostream>
int main(){
    wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);auto file=std::filesystem::path(temp)/(L"quota-settings-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    quota::Settings s;s.intervalSeconds=45;s.staleMinutes=2;s.onlyWithCodex=false;s.showInCodex=true;s.leftSide=true;s.english=true;
    if(!quota::SaveSettings(file.wstring(),s)){std::cerr<<"FAIL: valid settings must save\n";return 1;}
    auto loaded=quota::LoadSettings(file.wstring());
    if(loaded.intervalSeconds!=45||loaded.staleMinutes!=2||loaded.onlyWithCodex||!loaded.showInCodex||!loaded.leftSide||!loaded.english){std::cerr<<"FAIL: restart must preserve settings and UI language\n";return 1;}
    s.intervalSeconds=0;if(quota::SaveSettings(file.wstring(),s)||quota::LoadSettings(file.wstring()).intervalSeconds!=45){std::cerr<<"FAIL: invalid interval must not overwrite saved settings\n";return 1;}
    DeleteFileW(file.c_str());std::cout<<"PASS: settings round trip and invalid interval protection\n";return 0;
}

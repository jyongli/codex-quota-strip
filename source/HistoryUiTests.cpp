#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include "QuotaCore.h"
#include "QuotaHistory.h"
#include "RequestUsage.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <chrono>

namespace {
struct Target { DWORD pid=0; HWND control=nullptr,settings=nullptr,history=nullptr; };
BOOL CALLBACK Find(HWND h,LPARAM pointer){
    auto& target=*reinterpret_cast<Target*>(pointer);DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid!=target.pid)return TRUE;
    wchar_t name[200]{};GetClassNameW(h,name,200);std::wstring type=name;
    if(type.find(L"QuotaController_")==0)target.control=h;
    if(type.find(L"QuotaSettings_")==0)target.settings=h;
    if(type.find(L"QuotaHistory_")==0)target.history=h;
    return TRUE;
}
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool Wait(const std::function<bool()>& ready){auto deadline=GetTickCount64()+5000;do{if(ready())return true;Sleep(20);}while(GetTickCount64()<deadline);return false;}
bool WaitCloud(const std::function<bool()>& ready){auto deadline=GetTickCount64()+12000;do{if(ready())return true;Sleep(20);}while(GetTickCount64()<deadline);return false;}
std::wstring Caption(HWND h){wchar_t text[1024]{};GetWindowTextW(h,text,1024);return text;}
quota::Json State(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);std::string text((std::istreambuf_iterator<char>(in)),{});try{return quota::JsonParser(text).parse();}catch(...){return {};}}
void Append(const std::filesystem::path& file,int fiveUsed,int weeklyUsed,long long observed);
std::wstring ListCell(HWND list,int row,int column){
    // 只读取本测试拥有的应用列表控件；跨进程 LVM_GETITEMTEXT 需要目标进程内存。
    DWORD pid=0;GetWindowThreadProcessId(list,&pid);HANDLE process=OpenProcess(PROCESS_VM_OPERATION|PROCESS_VM_READ|PROCESS_VM_WRITE,FALSE,pid);Check(process!=nullptr,"Owned list process must open");
    void* memory=VirtualAllocEx(process,nullptr,sizeof(LVITEMW)+2048,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!memory){CloseHandle(process);Check(false,"Owned list buffer must allocate");}
    LVITEMW item{};item.iSubItem=column;item.cchTextMax=1024;item.pszText=reinterpret_cast<wchar_t*>(static_cast<unsigned char*>(memory)+sizeof(item));
    bool ok=WriteProcessMemory(process,memory,&item,sizeof(item),nullptr)!=FALSE;
    if(ok)ok=SendMessageTimeoutW(list,LVM_GETITEMTEXTW,row,reinterpret_cast<LPARAM>(memory),SMTO_ABORTIFHUNG,2000,nullptr)!=0;
    wchar_t text[1024]{};if(ok)ok=ReadProcessMemory(process,item.pszText,text,sizeof(text),nullptr)!=FALSE;VirtualFreeEx(process,memory,0,MEM_RELEASE);CloseHandle(process);Check(ok,"Owned list text must be readable");return text;
}
std::string Iso(long long milliseconds){
    auto seconds=static_cast<time_t>(milliseconds/1000);std::tm utc{};gmtime_s(&utc,&seconds);char text[64]{};
    sprintf_s(text,"%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",utc.tm_year+1900,utc.tm_mon+1,utc.tm_mday,utc.tm_hour,utc.tm_min,utc.tm_sec,milliseconds%1000);return text;
}
void RolloutRequest(const std::filesystem::path& file,long long stamp,const std::string& turn,bool known=true){
    std::ofstream out(file,std::ios::app|std::ios::binary);
    auto event=[&](int offset,const char* type,const std::string& payload){out<<"{\"timestamp\":\""<<Iso(stamp+offset)<<"\",\"type\":\""<<type<<"\",\"payload\":"<<payload<<"}\n";};
    auto tokens=[](int input,int cached,int write,int output,int reasoning){return "{\"input_tokens\":"+std::to_string(input)+",\"cached_input_tokens\":"+std::to_string(cached)+",\"cache_write_input_tokens\":"+std::to_string(write)+",\"output_tokens\":"+std::to_string(output)+",\"reasoning_output_tokens\":"+std::to_string(reasoning)+",\"total_tokens\":"+std::to_string(input+output)+"}";};
    auto usage=[&](int offset,int input,int cached,int write,int output,int reasoning,int five,int week){event(offset,"event_msg","{\"type\":\"token_count\",\"info\":{\"total_token_usage\":"+tokens(input,cached,write,output,reasoning)+"},\"rate_limits\":{\"primary\":{\"used_percent\":"+std::to_string(five)+",\"window_minutes\":300,\"resets_at\":2100000000},\"secondary\":{\"used_percent\":"+std::to_string(week)+",\"window_minutes\":10080,\"resets_at\":2100000000}}}");};
    event(0,"session_meta","{\"id\":\"thread-"+turn+"\"}");if(known)usage(1,100,20,4,10,2,20,40);
    event(2,"event_msg","{\"type\":\"task_started\",\"turn_id\":\""+turn+"\"}");
    event(3,"turn_context","{\"turn_id\":\""+turn+"\",\"model\":\"synthetic-model\"}");
    event(4,"response_item","{\"role\":\"user\",\"content\":\"SYNTHETIC-PROMPT-DO-NOT-STORE\"}");
    if(known){usage(5,160,30,7,30,8,22,41);usage(6,220,60,10,60,12,24,43);}
    event(7,"event_msg","{\"type\":\"task_complete\",\"turn_id\":\""+turn+"\"}");
}
void CheckRequestChart(HWND chart){
    RECT size{};GetClientRect(chart,&size);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=size.right;info.bmiHeader.biHeight=-size.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);Check(bitmap&&bits,"Request chart memory surface must allocate");auto old=SelectObject(dc,bitmap);PrintWindow(chart,dc,PW_CLIENTONLY);GdiFlush();auto pixels=static_cast<DWORD*>(bits);
    int bars=0,gap=0;for(int y=50;y<size.bottom-56;++y)for(int x=70;x<size.right-20;++x)if((pixels[y*size.right+x]&0xffffff)==0x93d5b9){++bars;if(x>size.right/2-8&&x<size.right/2+8)++gap;}
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);Check(bars>20&&gap==0,"Request token chart must draw observed totals while leaving the unavailable request empty");
}
void CheckWorkRequests(const std::filesystem::path& executable){
    wchar_t temporary[MAX_PATH]{};GetTempPathW(MAX_PATH,temporary);auto root=std::filesystem::path(temporary)/(L"quota-request-ui-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    auto settings=root/L"CodexQuotaStrip"/L"settings.ini",status=root/L"status.json",sessions=root/L"sessions";
    std::filesystem::create_directories(settings.parent_path());std::filesystem::create_directories(sessions);{std::ofstream out(settings);out<<"[refresh]\nonline=0\n[display]\nlanguage=en\n";}
    PROCESS_INFORMATION process{};Target target{};
    auto cleanup=[&]{if(process.hProcess){if(target.control)SendMessageTimeoutW(target.control,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,2000,nullptr);if(WaitForSingleObject(process.hProcess,5000)!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,1);CloseHandle(process.hThread);CloseHandle(process.hProcess);process={};}};
    try{
        wchar_t previous[32768]{};GetEnvironmentVariableW(L"LOCALAPPDATA",previous,32768);SetEnvironmentVariableW(L"LOCALAPPDATA",root.c_str());
        auto command=L"\""+executable.wstring()+L"\" --local-only --home \""+root.wstring()+L"\" --status \""+status.wstring()+L"\"";STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        BOOL created=CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process);SetEnvironmentVariableW(L"LOCALAPPDATA",previous);Check(created!=FALSE,"Request fixture must start");target.pid=process.dwProcessId;
        Check(Wait([&]{EnumWindows(Find,reinterpret_cast<LPARAM>(&target));return target.control&&State(status).get("watching").num==1;}),"Request fixture must watch synthetic rollout files");
        SendMessageW(target.control,WM_COMMAND,9,0);EnumWindows(Find,reinterpret_cast<LPARAM>(&target));Check(target.history&&SendMessageW(GetDlgItem(target.history,407),CB_GETCURSEL,0,0)==1,"Request tray entry must open the work-request page");
        HWND list=GetDlgItem(target.history,401);Check(SendMessageW(ListView_GetHeader(list),HDM_GETITEMCOUNT,0,0)==10,"Request list must expose a visible conversation column alongside model/status/token/account/amount columns");
        Check(Caption(GetDlgItem(target.history,408)).find(L"not attributed")!=std::wstring::npos,"Account-wide quota changes must not be presented as request consumption");
        quota::RequestStore store;store.configure(false,settings.parent_path()/L"history");auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        {std::ofstream out(root/L"session_index.jsonl");out<<"{\"id\":\"thread-turn-one\",\"thread_name\":\"Source conversation\",\"updated_at\":\"2026-10-18T01:00:00Z\"}\n";}
        RolloutRequest(sessions/L"one.jsonl",stamp,"turn-one");Check(Wait([&]{return store.read().entries.size()==1;}),"A completed real rollout request must be persisted by the native watcher");
        Check(SendMessageW(list,LVM_GETITEMCOUNT,0,0)==0,"Open request view must not poll after a completed request");SendMessageW(target.history,WM_COMMAND,403,0);
        Check(ListCell(list,0,1)==L"Source conversation","Conversation title must appear beside time without scrolling to the raw IDs");
        Check(ListCell(list,0,2)==L"synthetic-model"&&ListCell(list,0,3)==L"Completed","Request model and completion status must come from rollout metadata");
        Check(ListCell(list,0,4)==L"170"&&ListCell(list,0,5)==L"120 / 40 / 6"&&ListCell(list,0,6)==L"50 / 10","Multiple responses must aggregate actual token deltas without double counting cached or reasoning tokens");
        Check(ListCell(list,0,7)==L"80% → 76% (-4.0 pp) / 60% → 57% (-3.0 pp)"&&ListCell(list,0,8)==L"—","Account snapshot before/after values must accompany deltas, while absent money remains an em dash");
        {std::ofstream out(root/L"session_index.jsonl",std::ios::app);out<<"{\"id\":\"thread-turn-one\",\"thread_name\":\"\\u5bf9\\u8bdd Renamed\"}\n";}
        SendMessageW(target.history,WM_COMMAND,403,0);Check(ListCell(list,0,1)==L"对话 Renamed"&&ListCell(list,0,9)==L"thread-turn-one / turn-one","Refresh must pick up renamed Unicode titles and preserve raw identities");
        RolloutRequest(sessions/L"unknown.jsonl",stamp+20,"turn-unknown",false);Check(Wait([&]{return store.read().entries.size()==2;}),"A request without token metadata must remain visible");SendMessageW(target.history,WM_COMMAND,403,0);
        Check(ListCell(list,0,1)==L"thread-turn-unknown","Missing title must fall back to the actual thread ID");
        Check(ListCell(list,0,4)==L"—"&&ListCell(list,0,5)==L"—"&&ListCell(list,0,6)==L"—"&&ListCell(list,0,8)==L"—","Unavailable token counts and actual money must not be fabricated");
        RolloutRequest(sessions/L"three.jsonl",stamp+40,"turn-three");Check(Wait([&]{return store.read().entries.size()==3;}),"A second measured request must persist");SendMessageW(target.history,WM_COMMAND,403,0);SendMessageW(target.history,WM_COMMAND,402,0);CheckRequestChart(GetDlgItem(target.history,405));
        // 已开始的请求跨越账号边界时不能归给新账号，重放旧开头也不得复活它。
        auto boundary=sessions/L"boundary.jsonl";
        {std::ofstream out(boundary);out<<"{\"timestamp\":\""<<Iso(stamp+50)<<"\",\"type\":\"session_meta\",\"payload\":{\"id\":\"thread-boundary\"}}\n{\"timestamp\":\""<<Iso(stamp+51)<<"\",\"type\":\"event_msg\",\"payload\":{\"type\":\"task_started\",\"turn_id\":\"turn-boundary\"}}\n";}
        Append(boundary,25,44,stamp+52);Check(Wait([&]{return State(status).get("observed_ms").num>=stamp+52;}),"Pre-auth request must reach the watcher");
        {std::ofstream auth(root/L"auth.json");auth<<"synthetic account boundary";}
        Check(Wait([&]{return State(status).get("observed_ms").num==0;}),"Auth change must clear the previous quota");
        auto afterAuth=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+10;
        {std::ofstream out(boundary,std::ios::app);out<<"{\"timestamp\":\""<<Iso(afterAuth)<<"\",\"type\":\"event_msg\",\"payload\":{\"type\":\"task_complete\",\"turn_id\":\"turn-boundary\"}}\n";}
        Append(boundary,26,45,afterAuth+1);Check(Wait([&]{return State(status).get("observed_ms").num>=afterAuth+1;}),"Fresh post-auth quota must still update");Check(store.read().entries.size()==3,"A request crossing the auth boundary must not be attributed to the new account");
        // 有界尾部只看见结束事件时，保留一条所有用量未知的部分记录。
        auto tailStamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+10;
        {std::ofstream tail(sessions/L"end-only.jsonl");tail<<"{\"timestamp\":\""<<Iso(tailStamp)<<"\",\"type\":\"event_msg\",\"payload\":{\"type\":\"task_complete\",\"turn_id\":\"turn-tail-only\"}}\n";}
        Check(Wait([&]{return store.read().entries.size()==4;}),"Known completion with an unavailable beginning must remain visible after the auth boundary");SendMessageW(target.history,WM_COMMAND,403,0);
        Check(ListCell(list,0,3).find(L"Partial")!=std::wstring::npos&&ListCell(list,0,4)==L"—"&&ListCell(list,0,7)==L"— / —"&&ListCell(list,0,8)==L"—","Unknown-start request must not inherit token, quota or money values");
        Check(ListCell(list,0,1)==L"Unknown conversation", "Unrecoverable source must be labelled instead of guessed");
        SendMessageW(target.control,WM_COMMAND,7,0);EnumWindows(Find,reinterpret_cast<LPARAM>(&target));Check(GetDlgItem(target.settings,306)!=nullptr,"Settings must link directly to work requests");
        SendMessageW(GetDlgItem(target.settings,208),CB_SETCURSEL,0,0);SendMessageW(target.settings,WM_COMMAND,302,0);Check(Caption(target.history)==L"Codex 配额 — 工作请求","Open request view must translate live with settings");
        SendMessageW(target.settings,WM_COMMAND,305,0);Check(SendMessageW(GetDlgItem(target.history,407),CB_GETCURSEL,0,0)==0,"Existing quota history must remain accessible");SendMessageW(target.settings,WM_COMMAND,306,0);Check(SendMessageW(GetDlgItem(target.history,407),CB_GETCURSEL,0,0)==1,"Settings request link must select work requests");
        auto disabledStamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+20;
        SendMessageW(GetDlgItem(target.settings,209),BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(target.settings,WM_COMMAND,302,0);RolloutRequest(sessions/L"disabled.jsonl",disabledStamp,"turn-disabled");Check(Wait([&]{return State(status).get("observed_ms").num>=disabledStamp+6;}),"Disabling request recording must preserve normal quota updates");Check(store.read().entries.size()==4,"Disabled recording must not append completed requests");
        // 只有请求事件、没有额度更新时，写入失败也必须独立通知 UI。
        auto blocked=root/L"blocked-requests";SendMessageW(GetDlgItem(target.settings,210),WM_SETTEXT,0,reinterpret_cast<LPARAM>(blocked.c_str()));SendMessageW(GetDlgItem(target.settings,209),BM_SETCHECK,BST_CHECKED,0);SendMessageW(target.settings,WM_COMMAND,302,0);
        std::filesystem::remove(blocked);{std::ofstream out(blocked);out<<"synthetic blocked request directory";}
        RolloutRequest(sessions/L"request-error.jsonl",disabledStamp+20,"turn-error",false);Check(Wait([&]{return Caption(GetDlgItem(target.settings,213)).find(L"工作请求")!=std::wstring::npos;}),"Request-only write failure must be surfaced independently of quota updates");
        for(const auto& entry:std::filesystem::directory_iterator(settings.parent_path()/L"history"))if(entry.path().filename().wstring().find(L"request-usage-")==0){std::ifstream input(entry.path());std::string bytes((std::istreambuf_iterator<char>(input)),{});Check(bytes.find("SYNTHETIC-PROMPT")==std::string::npos,"Request persistence must not copy conversation text");}
        cleanup();std::cout<<"PASS: real work-request rollout aggregation, unknown values, amount em dash, account-change labelling, chart gaps, language and disabled capture\n";
    }catch(...){cleanup();throw;}
}
void CheckChart(HWND chart){
    // PrintWindow 将本测试图表渲染到内存位图，不读取屏幕或用户桌面。
    RECT size{};GetClientRect(chart,&size);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=size.right;info.bmiHeader.biHeight=-size.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);Check(bitmap&&bits,"Chart memory surface must allocate");auto previous=SelectObject(dc,bitmap);
    PrintWindow(chart,dc,PW_CLIENTONLY);GdiFlush();int fivePixels=0,weeklyPixels=0;auto pixels=static_cast<DWORD*>(bits);
    for(int y=50;y<size.bottom-52;++y)for(int x=52;x<size.right-20;++x){DWORD value=pixels[y*size.right+x]&0xffffff;if(value==0x93d5b9)++fivePixels;if(value==0xefbb79)++weeklyPixels;}
    std::cout<<"Chart "<<size.right<<"x"<<size.bottom<<" series pixels="<<fivePixels<<","<<weeklyPixels<<" sample="<<std::hex<<pixels[(size.bottom/2)*size.right+size.right/2]<<std::dec<<"\n";
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);Check(fivePixels>10&&weeklyPixels>10,"GDI history chart must draw both actual quota series");
}
void CheckQuotaConnections(HWND history,HWND settings,const std::filesystem::path& originalDirectory){
    // 真实查看器读取合成 CSV；检查两点之间的像素，而非只数端点，避免散点图也被判为通过。
    auto directory=originalDirectory.parent_path()/L"connected-chart";std::filesystem::create_directories(directory);
    auto now=quota::history_detail::Now();std::vector<quota::HistoryEntry> entries(6);
    for(size_t i=0;i<entries.size();++i){auto& row=entries[i];row.recorded=now-6000+static_cast<long long>(i)*1000;row.snapshot.observed=row.recorded;row.source="account";row.kind=i==0||i==1?"baseline":i==3?"reset":"change";row.snapshot.fiveHour={true,i<3?20.0+i*10:5.0+(i-3)*5,static_cast<long long>(i<3?2000000000:2000000300)};row.snapshot.weekly={true,i<3?40.0+i*10:10.0+(i-3)*10,static_cast<long long>(i<3?2000000000:2000000300)};if(row.kind=="change"){row.fiveChange=-10;row.weeklyChange=-10;}}
    entries[4].snapshot.fiveHour={};entries[4].fiveChange.reset();entries[5].fiveChange.reset();
    {std::ofstream out(directory/("quota-history-"+quota::history_detail::Day(now)+".csv"),std::ios::binary);out<<quota::history_detail::Header();for(const auto& row:entries)out<<quota::history_detail::Serialize(row);}
    SendMessageW(GetDlgItem(settings,210),WM_SETTEXT,0,reinterpret_cast<LPARAM>(directory.c_str()));SendMessageW(settings,WM_COMMAND,302,0);SendMessageW(history,WM_COMMAND,403,0);
    HWND chart=GetDlgItem(history,405);RECT bounds{};GetClientRect(chart,&bounds);auto px=[&](int n){return MulDiv(n,GetDpiForWindow(chart),96);};
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=bounds.right;info.bmiHeader.biHeight=-bounds.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);Check(bitmap&&bits,"Connected chart fixture surface must allocate");auto old=SelectObject(dc,bitmap);PrintWindow(chart,dc,PW_CLIENTONLY);GdiFlush();auto pixels=static_cast<DWORD*>(bits);
    auto segment=[&](size_t first,size_t last,bool weekly){
        int left=px(52),right=bounds.right-px(20),top=px(40),bottom=bounds.bottom-px(52);auto point=[&](size_t i){const auto& w=weekly?entries[i].snapshot.weekly:entries[i].snapshot.fiveHour;return POINT{left+static_cast<int>((right-left)*static_cast<double>(i)/5),bottom-static_cast<int>((bottom-top)*(100-w.used)/100)};};
        auto a=point(first),b=point(last);int hit=0,total=0;DWORD tint=weekly?0xefbb79:0x93d5b9;
        for(int x=a.x+px(8);x<b.x-px(8);++x){int y=a.y+static_cast<int>((b.y-a.y)*static_cast<double>(x-a.x)/(b.x-a.x));bool found=false;for(int offset=-px(2);offset<=px(2);++offset)if(y+offset>=0&&y+offset<bounds.bottom&&(pixels[(y+offset)*bounds.right+x]&0xffffff)==tint)found=true;hit+=found;++total;}
        return std::pair<int,int>{hit,total};
    };
    auto baseline=segment(0,1,true),continuous=segment(1,2,true),reset=segment(2,3,true),missing=segment(3,5,false);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    Check(baseline.first>baseline.second/5&&baseline.first<baseline.second*19/20,"Quota points across new baselines must be connected with a dashed reference line");
    Check(continuous.first>continuous.second*4/5,"Continuous quota observations must remain joined by a solid line");
    Check(reset.first>reset.second/5&&reset.first<reset.second*19/20,"Quota cycle resets must be joined with a dashed reference line");
    Check(missing.first==0,"Unavailable quota must keep a real gap rather than interpolate a fabricated value");
    SendMessageW(GetDlgItem(settings,210),WM_SETTEXT,0,reinterpret_cast<LPARAM>(originalDirectory.c_str()));SendMessageW(settings,WM_COMMAND,302,0);SendMessageW(history,WM_COMMAND,403,0);
    std::cout<<"PASS: quota chart joins valid observations, uses dashed boundaries and preserves missing-value gaps\n";
}
// 只产生真实 token_count 结构的合成额度，不写入认证或会话正文。
void Append(const std::filesystem::path& file,int fiveUsed,int weeklyUsed,long long observed){
    auto seconds=static_cast<time_t>(observed/1000);static const long long resetBase=seconds;std::tm utc{};gmtime_s(&utc,&seconds);
    char timestamp[64]{};sprintf_s(timestamp,"%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",utc.tm_year+1900,utc.tm_mon+1,utc.tm_mday,utc.tm_hour,utc.tm_min,utc.tm_sec,observed%1000);
    std::ofstream out(file,std::ios::app|std::ios::binary);
    out<<"{\"timestamp\":\""<<timestamp<<"\",\"type\":\"event_msg\",\"payload\":{\"type\":\"token_count\",\"rate_limits\":{\"primary\":{\"used_percent\":"<<fiveUsed<<",\"window_minutes\":300,\"resets_at\":"<<resetBase+3600<<"},\"secondary\":{\"used_percent\":"<<weeklyUsed<<",\"window_minutes\":10080,\"resets_at\":"<<resetBase+604800<<"}}}}\n";
}
// 假服务只读测试专用的额度数字，不读取 auth.json；经过真实 app-server 管道返回。
int FakeHistoryProvider(){
    wchar_t home[32768]{};GetEnvironmentVariableW(L"CODEX_HOME",home,32768);auto root=std::filesystem::path(home);
    std::string line;while(std::getline(std::cin,line)){
        if(line.find("\"initialize\"")!=std::string::npos)std::cout<<"{\"id\":1,\"result\":{}}\n"<<std::flush;
        else if(line.find("account/rateLimits/read")!=std::string::npos){
            int used=0;{std::ifstream input(root/L"provider-values.txt");input>>used;}
            {std::ofstream started(root/L"query-started");started<<"synthetic request started";}
            while(std::filesystem::exists(root/L"delay-response"))Sleep(10);
            std::cout<<"{\"id\":2,\"result\":{\"rateLimits\":{\"primary\":{\"usedPercent\":"<<used<<",\"windowDurationMins\":300,\"resetsAt\":2100000000}}}}\n"<<std::flush;
        }
    }
    return 0;
}
void CheckAccountIsolation(const std::filesystem::path& executable,const std::filesystem::path& self){
    wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);auto root=std::filesystem::path(temp)/(L"quota-history-account-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    auto profile=root/L"profile",home=root/L"home",status=root/L"status.json",settings=profile/L"CodexQuotaStrip"/L"settings.ini",binary=profile/L"OpenAI"/L"Codex"/L"bin"/L"codex.exe";
    std::filesystem::create_directories(settings.parent_path());std::filesystem::create_directories(binary.parent_path());std::filesystem::create_directories(home);
    {std::ofstream out(settings);out<<"[refresh]\nonline=1\nonly_with_codex=0\nseconds=120\n";}
    std::filesystem::copy_file(self,binary);
    auto write=[&](const wchar_t* name,const std::string& value){std::ofstream out(home/name,std::ios::binary);out<<value;};
    write(L"auth.json","synthetic account A metadata");write(L"provider-values.txt","20");
    PROCESS_INFORMATION app{},provider{};Target target{};
    auto cleanup=[&]{
        if(app.hProcess){if(target.control)SendMessageTimeoutW(target.control,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,2000,nullptr);if(WaitForSingleObject(app.hProcess,5000)!=WAIT_OBJECT_0)TerminateProcess(app.hProcess,1);CloseHandle(app.hThread);CloseHandle(app.hProcess);app={};}
        if(provider.hProcess){TerminateProcess(provider.hProcess,0);WaitForSingleObject(provider.hProcess,5000);CloseHandle(provider.hThread);CloseHandle(provider.hProcess);provider={};}
    };
    try{
        auto start=[&](const std::filesystem::path& path,const std::wstring& arguments,PROCESS_INFORMATION& process){
            wchar_t previous[32768]{};GetEnvironmentVariableW(L"LOCALAPPDATA",previous,32768);SetEnvironmentVariableW(L"LOCALAPPDATA",profile.c_str());
            auto command=L"\""+path.wstring()+L"\" "+arguments;STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
            BOOL created=CreateProcessW(path.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process);SetEnvironmentVariableW(L"LOCALAPPDATA",previous);Check(created!=FALSE,"Account history fixture must start");
        };
        // 先运行隔离 appBin 内的合成 codex.exe，使真实发现逻辑优先选它，避免调用用户的 Codex。
        start(binary,L"--provider-host",provider);
        start(executable,L"--home \""+home.wstring()+L"\" --status \""+status.wstring()+L"\"",app);target.pid=app.dwProcessId;
        Check(WaitCloud([&]{EnumWindows(Find,reinterpret_cast<LPARAM>(&target));return target.control&&State(status).get("five_hour_remaining").num==80;}),"Real provider path must adopt synthetic account A without a sessions directory");
        quota::HistoryStore history;history.configure(false,settings.parent_path()/L"history");Check(history.read().entries.size()==1,"Account A must produce one history baseline");
        write(L"auth.json","synthetic changed account B metadata with a different length");write(L"provider-values.txt","55");SendMessageW(target.control,WM_COMMAND,3,0);
        Check(WaitCloud([&]{return State(status).get("five_hour_remaining").num==45;}),"Manual query must adopt synthetic account B");
        auto rows=history.read().entries;Check(rows.size()==2&&rows.back().kind=="baseline"&&!rows.back().fiveChange,"Changing auth without sessions must reset the history baseline instead of subtracting accounts");
        // 暂停服务响应，在查询中途切换账号，迟到结果不能落盘或恢复旧账号额度。
        std::filesystem::remove(home/L"query-started");write(L"provider-values.txt","56");write(L"delay-response","hold synthetic response");SendMessageW(target.control,WM_COMMAND,3,0);
        Check(WaitCloud([&]{return std::filesystem::exists(home/L"query-started")&&State(status).get("refreshing").num==1;}),"Delayed synthetic query must reach the provider");
        write(L"auth.json","synthetic account C");write(L"provider-values.txt","70");std::filesystem::remove(home/L"delay-response");
        Check(WaitCloud([&]{auto state=State(status);return state.get("refreshing").num==0&&state.get("observed_ms").num==0;}),"Auth metadata changing during query must discard the late response");
        Check(history.read().entries.size()==2,"Discarded account response must not write history");
        SendMessageW(target.control,WM_COMMAND,3,0);Check(WaitCloud([&]{return State(status).get("five_hour_remaining").num==30;}),"A fresh account C query must succeed");
        rows=history.read().entries;Check(rows.size()==3&&rows.back().kind=="baseline"&&!rows.back().fiveChange,"Fresh account C must start a separate history baseline");
        cleanup();std::cout<<"PASS: real provider auth metadata checks with missing sessions, account baselines and late-response rejection\n";
    }catch(...){cleanup();throw;}
}
}
int wmain(int argc,wchar_t** argv){
    if(argc>1&&std::wstring(argv[1])==L"app-server")return FakeHistoryProvider();
    if(argc>1&&std::wstring(argv[1])==L"--provider-host"){for(;;)Sleep(INFINITE);}
    if(argc<2)return 2;
    wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);
    auto root=std::filesystem::path(temp)/(L"quota-history-ui-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    auto settings=root/L"CodexQuotaStrip"/L"settings.ini",status=root/L"status.json",log=root/L"sessions"/L"synthetic.jsonl";
    std::filesystem::create_directories(settings.parent_path());std::filesystem::create_directories(log.parent_path());
    {std::ofstream out(settings);out<<"[refresh]\nonline=0\n[history]\nenabled=1\n";}
    PROCESS_INFORMATION process{};Target target{};HWND original=GetForegroundWindow();
    try{
        wchar_t previous[32768]{};GetEnvironmentVariableW(L"LOCALAPPDATA",previous,32768);SetEnvironmentVariableW(L"LOCALAPPDATA",root.c_str());
        auto command=L"\""+std::filesystem::absolute(argv[1]).wstring()+L"\" --local-only --home \""+root.wstring()+L"\" --status \""+status.wstring()+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        BOOL created=CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process);SetEnvironmentVariableW(L"LOCALAPPDATA",previous);Check(created!=FALSE,"History fixture must start");target.pid=process.dwProcessId;
        Check(Wait([&]{EnumWindows(Find,reinterpret_cast<LPARAM>(&target));return target.control&&State(status).get("watching").num==1;}),"History fixture watcher must initialize");
        SendMessageTimeoutW(target.control,WM_COMMAND,8,0,SMTO_ABORTIFHUNG,2000,nullptr);
        Check(Wait([&]{EnumWindows(Find,reinterpret_cast<LPARAM>(&target));return target.history&&IsWindowVisible(target.history);}),"History command must open a real native history window");
        Check(GetDlgItem(target.history,401)&&GetDlgItem(target.history,402)&&GetDlgItem(target.history,403),"History must expose list, chart toggle and refresh controls");
        Check(GetDlgItem(target.history,407)&&SendMessageW(GetDlgItem(target.history,407),CB_GETCOUNT,0,0)==2,"History viewer must offer quota and work-request pages");
        std::cout<<"PASS: native history entry point\n";
        SendMessageW(target.control,WM_COMMAND,7,0);EnumWindows(Find,reinterpret_cast<LPARAM>(&target));Check(target.settings!=nullptr,"Settings must open alongside history");
        Check(GetDlgItem(target.settings,209)&&GetDlgItem(target.settings,210)&&GetDlgItem(target.settings,304)&&GetDlgItem(target.settings,305),"History settings must expose enable, directory, browse and viewer");
        RECT originalSettings{};GetWindowRect(target.settings,&originalSettings);SetWindowPos(target.settings,nullptr,0,0,originalSettings.right-originalSettings.left,520,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        SendMessageW(target.settings,WM_VSCROLL,SB_BOTTOM,0);RECT viewport{},saveButton{};GetClientRect(target.settings,&viewport);GetWindowRect(GetDlgItem(target.settings,302),&saveButton);MapWindowPoints(nullptr,target.settings,reinterpret_cast<POINT*>(&saveButton),2);
        Check(saveButton.top>=0&&saveButton.bottom<=viewport.bottom,"Scrolling must expose the entire Save button on a small or high-DPI screen");
        SendMessageW(target.settings,WM_VSCROLL,SB_TOP,0);
        SetWindowPos(target.settings,nullptr,0,0,originalSettings.right-originalSettings.left,originalSettings.bottom-originalSettings.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        // 保存相对目录应失败，不能让后续写入落入不可预测的工作目录。
        SendMessageW(GetDlgItem(target.settings,210),WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"relative-history"));SendMessageW(target.settings,WM_COMMAND,302,0);
        auto validation=Caption(GetDlgItem(target.settings,207));char validationUtf8[4096]{};WideCharToMultiByte(CP_UTF8,0,validation.c_str(),-1,validationUtf8,4096,nullptr,nullptr);
        std::cout<<"Directory validation message: "<<validationUtf8<<"; enabled="<<SendMessageW(GetDlgItem(target.settings,209),BM_GETCHECK,0,0)<<"; interval length="<<Caption(GetDlgItem(target.settings,202)).size()<<"\n";
        Check(Caption(GetDlgItem(target.settings,207)).find(L"目录")!=std::wstring::npos,"Relative history directory must be rejected with a specific error");
        auto directory=root/L"custom-history";SendMessageW(GetDlgItem(target.settings,210),WM_SETTEXT,0,reinterpret_cast<LPARAM>(directory.c_str()));
        SendMessageW(GetDlgItem(target.settings,208),CB_SETCURSEL,1,0);SendMessageW(target.settings,WM_COMMAND,302,0);
        Check(Caption(GetDlgItem(target.settings,305))==L"View quota changes","History controls must translate live to English");
        Check(Caption(target.history)==L"Codex Quota — History","An already open history viewer must translate with settings");
        wchar_t saved[32768]{};GetPrivateProfileStringW(L"history",L"directory",L"",saved,32768,settings.c_str());Check(saved==directory.wstring(),"Chosen history directory must persist");
        auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();Append(log,20,40,stamp);Check(Wait([&]{return State(status).get("weekly_remaining").num==60;}),"Local baseline must reach the displayed quota");
        SendMessageW(target.history,WM_COMMAND,403,0);
        HWND list=GetDlgItem(target.history,401);Check(SendMessageW(list,LVM_GETITEMCOUNT,0,0)==1,"History list must include accepted baseline");
        // 绘制、手动刷新和无变化新事件均不应重复生成 CSV 行。
        Append(log,20,40,stamp+10);Check(Wait([&]{return State(status).get("observed_ms").num>=stamp+10;}),"Unchanged new snapshot must be accepted");
        SendMessageW(target.history,WM_COMMAND,403,0);Check(SendMessageW(list,LVM_GETITEMCOUNT,0,0)==1,"Unchanged quota and refresh must not add history rows");
        Append(log,24,43,stamp+20);Check(Wait([&]{return State(status).get("weekly_remaining").num==57;}),"Changed quota must reach display");
        // 查看器只在打开或手动刷新时读盘，后台额度事件不能刷新列表。
        Check(SendMessageW(list,LVM_GETITEMCOUNT,0,0)==1,"Open history must not poll or refresh on quota events");
        SendMessageW(target.history,WM_COMMAND,403,0);Check(SendMessageW(list,LVM_GETITEMCOUNT,0,0)==2,"Manual history refresh must read the new change");
        SendMessageW(target.history,WM_COMMAND,402,0);Check(IsWindowVisible(GetDlgItem(target.history,405))&&!IsWindowVisible(list),"Chart mode must expose the native GDI chart");
        CheckChart(GetDlgItem(target.history,405));
        CheckQuotaConnections(target.history,target.settings,directory);
        SendMessageW(target.history,WM_COMMAND,402,0);Check(IsWindowVisible(list),"List mode must remain available");
        SendMessageW(GetDlgItem(target.settings,209),BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(target.settings,WM_COMMAND,302,0);
        Append(log,30,50,stamp+30);Check(Wait([&]{return State(status).get("weekly_remaining").num==50;}),"Disabled logging must not stop quota updates");
        SendMessageW(target.history,WM_COMMAND,403,0);Check(SendMessageW(list,LVM_GETITEMCOUNT,0,0)==2,"Disabled logging must preserve existing rows without writing new ones");
        // 保存后让测试目录变成普通文件，模拟磁盘失败：只提示日志错误，额度照常更新。
        auto blocked=root/L"blocked-history";SendMessageW(GetDlgItem(target.settings,210),WM_SETTEXT,0,reinterpret_cast<LPARAM>(blocked.c_str()));
        SendMessageW(GetDlgItem(target.settings,209),BM_SETCHECK,BST_CHECKED,0);SendMessageW(target.settings,WM_COMMAND,302,0);
        std::filesystem::remove(blocked);{std::ofstream out(blocked);out<<"Synthetic blocked history directory";}
        Append(log,33,51,stamp+40);Check(Wait([&]{return State(status).get("weekly_remaining").num==49&&!Caption(GetDlgItem(target.settings,213)).empty();}),"History write failure must be reported independently while quota keeps updating");
        SendMessageW(target.settings,WM_CLOSE,0,0);SendMessageW(target.history,WM_COMMAND,402,0);Check(IsWindow(target.history),"Closing settings must preserve history resources");
        SendMessageW(target.history,WM_CLOSE,0,0);Check(WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT,"Closing history must preserve the quota companion");
        SendMessageTimeoutW(target.control,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,2000,nullptr);Check(WaitForSingleObject(process.hProcess,5000)==WAIT_OBJECT_0,"History fixture must exit cleanly");
        CloseHandle(process.hThread);CloseHandle(process.hProcess);process={};target={};if(IsWindow(original))SetForegroundWindow(original);
        CheckWorkRequests(std::filesystem::absolute(argv[1]));
        CheckAccountIsolation(std::filesystem::absolute(argv[1]),std::filesystem::absolute(argv[0]));
        std::wcout<<L"PASS: history persistence, deduplication, manual list/chart, language, disabled capture and window lifetimes; fixture "<<root.wstring()<<L"\n";return 0;
    }catch(const std::exception& error){
        std::cerr<<"FAIL: "<<error.what()<<"\n";
        if(target.control)SendMessageTimeoutW(target.control,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,2000,nullptr);
        if(process.hProcess){if(WaitForSingleObject(process.hProcess,5000)!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,1);CloseHandle(process.hThread);CloseHandle(process.hProcess);}if(IsWindow(original))SetForegroundWindow(original);return 1;
    }
}

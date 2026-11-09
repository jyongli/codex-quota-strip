#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "QuotaCore.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <functional>
#include "SettingsTheme.h"
#pragma comment(lib,"uxtheme.lib")
struct InputEvents{int clicks=0,wheels=0;WPARAM wheelKeys=0;LPARAM wheelPoint=0;};
LRESULT CALLBACK observeInput(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR context){
    auto& events=*reinterpret_cast<InputEvents*>(context);
    if(message==WM_COMMAND&&HIWORD(wp)==BN_CLICKED)++events.clicks;
    if(message==WM_MOUSEWHEEL){++events.wheels;events.wheelKeys=wp;events.wheelPoint=lp;return 0;}
    return DefSubclassProc(h,message,wp,lp);
}
void checkSwitchHitArea(){
    for(UINT dpi:{96u,144u,192u}){
        quota_ui::theme.create(dpi);
        HWND parent=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Input fixture",WS_POPUP|WS_VISIBLE,
            20,20,quota_ui::theme.px(400),quota_ui::theme.px(100),nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        InputEvents events;SetWindowSubclass(parent,observeInput,3,reinterpret_cast<DWORD_PTR>(&events));
        bool correct=true;
        for(int id:{201,203,209,214}){
            const int width=quota_ui::theme.px(360),height=quota_ui::theme.px(28);
            HWND toggle=CreateWindowExW(0,L"BUTTON",L"Switch label",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|WS_TABSTOP,
                10,10,width,height,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
            quota_ui::styleControl(toggle);
            const auto slider=MAKELPARAM(width-quota_ui::theme.px(18),height/2);
            const auto label=MAKELPARAM(quota_ui::theme.px(10),height/2);
            const auto blank=MAKELPARAM(width/2,height/2);
            const auto above=MAKELPARAM(width-quota_ui::theme.px(18),0);
            auto click=[&](LPARAM down,LPARAM up){
                SendMessageW(toggle,BM_SETCHECK,BST_UNCHECKED,0);events.clicks=0;
                SendMessageW(toggle,WM_LBUTTONDOWN,MK_LBUTTON,down);SendMessageW(toggle,WM_LBUTTONUP,0,up);
            };
            // 标签、行内空白及滑块上方均不是鼠标开关目标。
            for(auto point:{label,blank,above}){click(point,point);correct=correct&&SendMessageW(toggle,BM_GETCHECK,0,0)==BST_UNCHECKED&&events.clicks==0;}
            click(slider,slider);correct=correct&&SendMessageW(toggle,BM_GETCHECK,0,0)==BST_CHECKED&&events.clicks==1;
            click(slider,label);correct=correct&&SendMessageW(toggle,BM_GETCHECK,0,0)==BST_UNCHECKED&&events.clicks==0&&GetCapture()!=toggle;
            click(label,slider);correct=correct&&SendMessageW(toggle,BM_GETCHECK,0,0)==BST_UNCHECKED&&events.clicks==0;
            events.clicks=0;SendMessageW(toggle,WM_KEYDOWN,VK_SPACE,0);SendMessageW(toggle,WM_KEYUP,VK_SPACE,0);
            correct=correct&&SendMessageW(toggle,BM_GETCHECK,0,0)==BST_CHECKED&&events.clicks==1;
            DestroyWindow(toggle);
        }
        RemoveWindowSubclass(parent,observeInput,3);DestroyWindow(parent);quota_ui::theme.release();
        if(!correct)throw std::runtime_error("Only the right-hand slider may toggle on mouse click; labels, blank space and cancelled drags must not activate it");
    }
    std::cout<<"PASS: slider-only mouse activation, drag cancellation and keyboard access at 100%, 150% and 200% DPI\n";
}
void checkComboWheel(){
    quota_ui::theme.create(96);
    HWND parent=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Combo wheel fixture",WS_POPUP|WS_VISIBLE,
        20,20,500,240,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    InputEvents events;SetWindowSubclass(parent,observeInput,3,reinterpret_cast<DWORD_PTR>(&events));
    bool correct=true;
    for(int id:{204,208}){
        HWND combo=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_TABSTOP,
            10,10,350,180,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        quota_ui::styleControl(combo);
        for(auto option:{L"First",L"Second",L"Third"})SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(option));
        SendMessageW(combo,CB_SETCURSEL,1,0);SetFocus(combo);
        // 使用真实控件消息验证：收起时滚轮交给页面，原坐标和修饰键不能丢失。
        for(WORD modifiers:{static_cast<WORD>(0),static_cast<WORD>(MK_CONTROL)})for(short delta:{static_cast<short>(-WHEEL_DELTA),static_cast<short>(WHEEL_DELTA)}){
            events.wheels=0;const WPARAM wheel=MAKEWPARAM(modifiers,static_cast<WORD>(delta));const LPARAM point=MAKELPARAM(90,60);
            SendMessageW(combo,WM_MOUSEWHEEL,wheel,point);
            correct=correct&&SendMessageW(combo,CB_GETCURSEL,0,0)==1&&events.wheels==1&&events.wheelKeys==wheel&&events.wheelPoint==point;
        }
        SendMessageW(combo,CB_SHOWDROPDOWN,TRUE,0);events.wheels=0;
        const bool opened=SendMessageW(combo,CB_GETDROPPEDSTATE,0,0)!=FALSE;
        SendMessageW(combo,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),MAKELPARAM(90,60));
        correct=correct&&opened&&events.wheels==0;
        SendMessageW(combo,CB_SHOWDROPDOWN,FALSE,0);SendMessageW(combo,CB_SETCURSEL,1,0);
        SendMessageW(combo,WM_KEYDOWN,VK_DOWN,0);correct=correct&&SendMessageW(combo,CB_GETCURSEL,0,0)==2;
        DestroyWindow(combo);
    }
    RemoveWindowSubclass(parent,observeInput,3);DestroyWindow(parent);quota_ui::theme.release();
    if(!correct)throw std::runtime_error("Closed combo wheel input must scroll its parent without changing the selection; opened lists and keyboard selection must remain usable");
    std::cout<<"PASS: closed combo wheel forwarding, stable selection, opened-list and keyboard behavior\n";
}
// 只在隔离窗口里改变勾选状态，不触发真实登录启动项的写入。
void checkSwitchRepaint(){
    quota_ui::theme.create(96);
    HWND parent=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Switch repaint fixture",WS_POPUP|WS_VISIBLE,
        20,20,400,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND toggle=CreateWindowExW(0,L"BUTTON",L"",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,10,10,360,28,
        parent,reinterpret_cast<HMENU>(214),GetModuleHandleW(nullptr),nullptr);
    quota_ui::styleControl(toggle);
    RedrawWindow(toggle,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
    ValidateRect(toggle,nullptr);
    SendMessageW(toggle,BM_SETCHECK,BST_CHECKED,0);
    RECT dirty{},bounds{};GetClientRect(toggle,&bounds);
    const bool repaints=GetUpdateRect(toggle,&dirty,FALSE)&&EqualRect(&dirty,&bounds);
    const bool checked=SendMessageW(toggle,BM_GETCHECK,0,0)==BST_CHECKED;
    SendMessageW(toggle,WM_KEYDOWN,VK_SPACE,0);SendMessageW(toggle,WM_KEYUP,VK_SPACE,0);
    const bool keyboard=SendMessageW(toggle,BM_GETCHECK,0,0)==BST_UNCHECKED;
    DestroyWindow(parent);quota_ui::theme.release();
    if(!checked||!repaints||!keyboard)throw std::runtime_error("Switch state changes must repaint the whole label and right-hand track, without a stale native checkbox");
    std::cout<<"PASS: native switch state invalidates its entire custom-painted surface\n";
}
LRESULT CALLBACK countMoves(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR context){
    if(message==WM_WINDOWPOSCHANGED&&!(reinterpret_cast<WINDOWPOS*>(lp)->flags&SWP_NOMOVE))
        ++*reinterpret_cast<int*>(context);
    return DefSubclassProc(h,message,wp,lp);
}
void checkScrollLayout(){
    // 记录真实控件位置事件，确保一次滚动不会先跳回原位再二次移动。
    for(UINT dpi:{96u,144u,192u}){
        quota_ui::theme.create(dpi);
        HWND parent=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,1300,1300,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        HWND controls[3]{};int moves[3]{};const int ids[]={201,214,307};
        for(int i=0;i<3;++i)controls[i]=CreateWindowExW(0,L"BUTTON",L"",WS_CHILD,0,0,100,28,parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ids[i])),GetModuleHandleW(nullptr),nullptr);
        quota_ui::layout(parent);
        RECT before[3]{};
        for(int i=0;i<3;++i){GetWindowRect(controls[i],&before[i]);SetWindowSubclass(controls[i],countMoves,2,reinterpret_cast<DWORD_PTR>(&moves[i]));}
        const int offset=quota_ui::theme.px(120);quota_ui::layout(parent,offset);
        bool stable=true;
        for(int i=0;i<3;++i){RECT after{};GetWindowRect(controls[i],&after);
            stable=stable&&moves[i]==1&&after.top==before[i].top-offset&&after.left==before[i].left&&
                after.bottom-after.top==before[i].bottom-before[i].top;
            RemoveWindowSubclass(controls[i],countMoves,2);
        }
        DestroyWindow(parent);quota_ui::theme.release();
        if(!stable)throw std::runtime_error("Scrolling must move each control directly once, preserving layout at each DPI");
    }
    std::cout<<"PASS: one control movement per scroll at 100%, 150% and 200% DPI\n";
}
struct Target{DWORD pid;HWND control=nullptr,settings=nullptr;};
BOOL CALLBACK find(HWND h,LPARAM p){auto& t=*reinterpret_cast<Target*>(p);DWORD id=0;GetWindowThreadProcessId(h,&id);if(id!=t.pid)return TRUE;wchar_t cls[200]{};GetClassNameW(h,cls,200);std::wstring s=cls;if(s.find(L"QuotaController_")==0)t.control=h;else if(!t.control&&s.find(L"CodexQuotaStrip_")==0)t.control=h;if(s.find(L"QuotaSettings_")==0)t.settings=h;return TRUE;}
void check(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
bool wait(const std::function<bool()>& f){auto started=GetTickCount64();do{if(f())return true;Sleep(20);}while(GetTickCount64()-started<2500);return false;}
quota::Json state(const std::filesystem::path& f){std::ifstream in(f,std::ios::binary);std::string s((std::istreambuf_iterator<char>(in)),{});try{return quota::JsonParser(s).parse();}catch(...){return {};}}
int wmain(int argc,wchar_t** argv){
    if(argc<2)return 2;wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);auto root=std::filesystem::path(temp)/(L"quota-config-ui-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(root/L"sessions");std::filesystem::create_directories(root/L"CodexQuotaStrip");
    {std::ofstream out(root/L"CodexQuotaStrip"/L"settings.ini");out<<"[refresh]\nonline=0\nseconds=120\n";}
    PROCESS_INFORMATION process{};Target t{};HWND original=GetForegroundWindow();
    try{
        checkSwitchHitArea();
        checkComboWheel();
        checkSwitchRepaint();
        checkScrollLayout();
        wchar_t previous[32768]{};GetEnvironmentVariableW(L"LOCALAPPDATA",previous,32768);SetEnvironmentVariableW(L"LOCALAPPDATA",root.c_str());
        std::wstring command=L"\""+std::filesystem::absolute(argv[1]).wstring()+L"\" --home \""+root.wstring()+L"\" --status \""+(root/L"status.json").wstring()+L"\"";
        STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESHOWWINDOW;start.wShowWindow=SW_HIDE;BOOL ok=CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&process);SetEnvironmentVariableW(L"LOCALAPPDATA",previous);check(ok!=FALSE,"Config fixture must start");t.pid=process.dwProcessId;
        check(wait([&]{EnumWindows(find,reinterpret_cast<LPARAM>(&t));return t.control!=nullptr;}),"Fixture control window must exist");
        SendMessageTimeoutW(t.control,WM_COMMAND,7,0,SMTO_ABORTIFHUNG,2000,nullptr);
        check(wait([&]{EnumWindows(find,reinterpret_cast<LPARAM>(&t));return t.settings&&IsWindowVisible(t.settings);}),"Settings command must open a real native configuration window");
        check(GetClassLongPtrW(t.settings,GCLP_HICON)&&GetClassLongPtrW(t.settings,GCLP_HICONSM),"Settings window must expose the custom large and small application icons");
        // 测试仅验证控件接入；真实自启读写由隔离注册表测试覆盖，不点击宿主机启动开关。
        HWND startup=GetDlgItem(t.settings,214),guard=GetDlgItem(t.settings,307);
        check(startup&&(GetWindowLongPtrW(startup,GWL_STYLE)&BS_TYPEMASK)==BS_AUTOCHECKBOX,"Settings must expose a real login startup checkbox");
        check(guard&&!IsWindowEnabled(guard),"Diagnostic instances must not register a system service");
        // 新增控件位于长配置页底部，实际滚动后验证整个按钮仍可操作。
        SendMessageW(t.settings,WM_VSCROLL,SB_BOTTOM,0);
        RECT viewportSettings{},guardBounds{};GetClientRect(t.settings,&viewportSettings);GetWindowRect(guard,&guardBounds);
        MapWindowPoints(nullptr,t.settings,reinterpret_cast<POINT*>(&guardBounds),2);
        check(guardBounds.top>=0&&guardBounds.bottom<=viewportSettings.bottom,"Scrolling settings must expose the whole watchdog button");
        SendMessageW(t.settings,WM_VSCROLL,SB_TOP,0);
        HWND toggle=GetDlgItem(t.settings,201);LRESULT before=SendMessageW(toggle,BM_GETCHECK,0,0);
        SendMessageW(toggle,WM_KEYDOWN,VK_SPACE,0);SendMessageW(toggle,WM_KEYUP,VK_SPACE,0);
        check(SendMessageW(toggle,BM_GETCHECK,0,0)!=before,"Styled switches must retain native Space-key behavior");SendMessageW(toggle,BM_SETCHECK,before,0);
        // 紧凑页在正常屏幕无需滚动；缩短真实窗口以继续覆盖滚轮转交与小屏布局。
        RECT normalSettings{};GetWindowRect(t.settings,&normalSettings);
        SetWindowPos(t.settings,nullptr,0,0,normalSettings.right-normalSettings.left,
            MulDiv(500,GetDpiForWindow(t.settings),96),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        for(int id:{204,208}){
            SendMessageW(t.settings,WM_VSCROLL,SB_TOP,0);HWND selector=GetDlgItem(t.settings,id);
            const auto selection=SendMessageW(selector,CB_GETCURSEL,0,0);
            SendMessageW(selector,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),MAKELPARAM(100,100));
            SCROLLINFO page{sizeof(page)};page.fMask=SIF_POS;GetScrollInfo(t.settings,SB_VERT,&page);
            check(page.nPos>0&&SendMessageW(selector,CB_GETCURSEL,0,0)==selection,"Wheel over a closed settings selector must scroll the real page without changing its selection");
        }
        SetWindowPos(t.settings,nullptr,0,0,normalSettings.right-normalSettings.left,
            normalSettings.bottom-normalSettings.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        SendMessageW(t.settings,WM_VSCROLL,SB_TOP,0);
        HWND combo=GetDlgItem(t.settings,204);SendMessageW(combo,CB_SHOWDROPDOWN,TRUE,0);check(SendMessageW(combo,CB_GETDROPPEDSTATE,0,0)!=0,"Styled display selector must still open its native dropdown");
        COMBOBOXINFO displayInfo{};displayInfo.cbSize=sizeof(displayInfo);RECT displayViewport{},lastDisplayRow{};
        check(GetComboBoxInfo(combo,&displayInfo)!=FALSE,"Display dropdown must expose its list");GetClientRect(displayInfo.hwndList,&displayViewport);
        check(SendMessageW(displayInfo.hwndList,LB_GETITEMRECT,2,reinterpret_cast<LPARAM>(&lastDisplayRow))!=LB_ERR&&lastDisplayRow.bottom<=displayViewport.bottom,"Expanded display dropdown must show all three positions without clipping");
        SendMessageW(combo,CB_SHOWDROPDOWN,FALSE,0);
        SendMessageW(GetDlgItem(t.settings,202),WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"45"));SendMessageW(GetDlgItem(t.settings,205),WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"2"));
        SendMessageW(GetDlgItem(t.settings,204),CB_SETCURSEL,1,0);SendMessageW(GetDlgItem(t.settings,203),BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(t.settings,WM_COMMAND,302,0);
        check(wait([&]{auto s=state(root/L"status.json");return s.get("refresh_seconds").num==45&&s.get("stale_minutes").num==2&&s.get("show_in_codex").num==1;}),"Apply must immediately change interval, stale threshold and display mode");
        check(GetPrivateProfileIntW(L"refresh",L"seconds",0,(root/L"CodexQuotaStrip"/L"settings.ini").c_str())==45,"Settings must persist for restart");
        HWND language=GetDlgItem(t.settings,208);check(language!=nullptr,"Settings must provide a Chinese/English language selector");
        // 实际展开列表并点击 English；仅设置选中索引无法发现选项被裁切的问题。
        check(SendMessageW(language,CB_GETCOUNT,0,0)==2,"Language selector must contain Chinese and English");
        SendMessageW(language,CB_SHOWDROPDOWN,TRUE,0);
        COMBOBOXINFO info{};info.cbSize=sizeof(info);
        check(GetComboBoxInfo(language,&info)&&IsWindowVisible(info.hwndList),"Language dropdown must be visible");
        RECT viewport{},englishRow{};GetClientRect(info.hwndList,&viewport);
        check(SendMessageW(info.hwndList,LB_GETITEMRECT,1,reinterpret_cast<LPARAM>(&englishRow))!=LB_ERR,"English row must exist");
        std::cout<<"Language dropdown viewport="<<viewport.bottom-viewport.top<<"; English row="<<englishRow.top<<".."<<englishRow.bottom<<"\n";
        check(englishRow.top>=viewport.top&&englishRow.bottom<=viewport.bottom,"Expanded language dropdown must show English without clipping");
        LPARAM point=MAKELPARAM((englishRow.left+englishRow.right)/2,(englishRow.top+englishRow.bottom)/2);
        SendMessageW(info.hwndList,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(info.hwndList,WM_LBUTTONUP,0,point);
        check(SendMessageW(language,CB_GETCURSEL,0,0)==1&&!SendMessageW(language,CB_GETDROPPEDSTATE,0,0),"Clicking the visible English row must select it and close the dropdown");
        SendMessageW(t.settings,WM_COMMAND,302,0);
        wchar_t caption[180]{};GetWindowTextW(GetDlgItem(t.settings,302),caption,180);
        check(std::wstring(caption)==L"Save and apply","Applying English must translate the live configuration window");
        GetWindowTextW(startup,caption,180);check(std::wstring(caption)==L"Start at Windows login (applies immediately)","Login startup must use the selected language");
        GetPrivateProfileStringW(L"display",L"language",L"",caption,180,(root/L"CodexQuotaStrip"/L"settings.ini").c_str());
        check(std::wstring(caption)==L"en","Language choice must persist for restart");
        SendMessageW(language,CB_SETCURSEL,0,0);SendMessageW(t.settings,WM_COMMAND,302,0);
        GetWindowTextW(GetDlgItem(t.settings,302),caption,180);check(std::wstring(caption)==L"保存并应用","Switching back must restore Chinese without restarting");
        SendMessageW(GetDlgItem(t.settings,202),WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"0"));SendMessageW(t.settings,WM_COMMAND,302,0);Sleep(50);check(state(root/L"status.json").get("refresh_seconds").num==45,"Invalid intervals must leave the active configuration unchanged");
        SendMessageW(t.settings,WM_COMMAND,303,0);check(!IsWindow(t.settings),"Close button must release the configuration window");check(WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT,"Closing settings must preserve the quota process");
        std::cout<<"PASS: native configuration apply, persistence, validation and close lifecycle\n";
        SendMessageTimeoutW(t.control,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,2000,nullptr);check(WaitForSingleObject(process.hProcess,3000)==WAIT_OBJECT_0,"Fixture must exit");CloseHandle(process.hThread);CloseHandle(process.hProcess);if(IsWindow(original))SetForegroundWindow(original);return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";if(t.control)SendMessageTimeoutW(t.control,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,1000,nullptr);if(process.hProcess){WaitForSingleObject(process.hProcess,3000);CloseHandle(process.hThread);CloseHandle(process.hProcess);}if(IsWindow(original))SetForegroundWindow(original);return 1;}
}

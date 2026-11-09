#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <string>

namespace quota_ui {
// 统一的原生深色样式；只在打开设置时创建字体和画刷，不增加动画或刷新定时器。
constexpr COLORREF Background=RGB(24,25,27), Panel=RGB(35,36,39), Field=RGB(30,31,34);
constexpr COLORREF Text=RGB(239,240,242), Muted=RGB(157,163,172), Border=RGB(63,66,72);
constexpr COLORREF Accent=RGB(147,213,185), AccentText=RGB(19,37,29), Warning=RGB(239,187,121), Error=RGB(244,161,161);
// 紧凑页保持 14 像素正文与可点击控件高度，优先压缩重复说明和区块留白。
constexpr int SettingsWidth=620,SettingsHeight=734;
struct Theme {
    UINT dpi=96;
    HFONT body=nullptr,smallFont=nullptr,title=nullptr,value=nullptr,medium=nullptr;
    HBRUSH background=nullptr,panel=nullptr,field=nullptr;
    int px(int n)const{return MulDiv(n,static_cast<int>(dpi),96);}
    RECT rect(int x,int y,int w,int h)const{return {px(x),px(y),px(x+w),px(y+h)};}
    void release(){for(auto f:{body,smallFont,title,value,medium})if(f)DeleteObject(f);for(auto b:{background,panel,field})if(b)DeleteObject(b);body=smallFont=title=value=medium=nullptr;background=panel=field=nullptr;}
    void create(UINT next){
        release();dpi=next;
        auto font=[&](int size,int weight){return CreateFontW(-px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");};
        body=font(14,FW_NORMAL);smallFont=font(12,FW_NORMAL);title=font(23,FW_SEMIBOLD);medium=font(14,FW_SEMIBOLD);value=font(36,FW_SEMIBOLD);
        background=CreateSolidBrush(Background);panel=CreateSolidBrush(Panel);field=CreateSolidBrush(Field);
    }
};
inline Theme theme;
inline void rounded(HDC dc,RECT r,COLORREF fill,COLORREF line,int radius=7){
    HBRUSH brush=CreateSolidBrush(fill);HPEN pen=CreatePen(PS_SOLID,1,line);auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,pen);
    RoundRect(dc,r.left,r.top,r.right,r.bottom,theme.px(radius),theme.px(radius));SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(brush);DeleteObject(pen);
}
inline void text(HDC dc,const std::wstring& value,RECT r,HFONT font,COLORREF color,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE){
    auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,value.c_str(),-1,&r,flags);SelectObject(dc,old);
}
inline std::wstring caption(HWND h){wchar_t value[256]{};GetWindowTextW(h,value,256);return value;}
inline bool isSwitch(int id){return id==201||id==203||id==209||id==214;}
inline RECT switchTrack(const RECT& bounds){
    return {bounds.right-theme.px(36),(bounds.bottom-theme.px(20))/2,
        bounds.right,(bounds.bottom+theme.px(20))/2};
}

// 保留原生控件的点击、空格键、勾选和下拉行为，仅替换外观。
inline void paintControl(HWND h,HDC dc){
    RECT r{};GetClientRect(h,&r);int id=GetDlgCtrlID(h);bool focus=GetFocus()==h;
    FillRect(dc,&r,theme.background);
    if(isSwitch(id)){
        bool checked=SendMessageW(h,BM_GETCHECK,0,0)==BST_CHECKED;
        RECT label=r;label.right-=theme.px(62);text(dc,caption(h),label,theme.body,IsWindowEnabled(h)?Text:Muted);
        RECT track=switchTrack(r);
        rounded(dc,track,checked?Accent:Border,checked?Accent:Border,20);
        int x=checked?track.right-theme.px(17):track.left+theme.px(3);
        RECT thumb{x,track.top+theme.px(3),x+theme.px(14),track.bottom-theme.px(3)};
        rounded(dc,thumb,checked?AccentText:Text,checked?AccentText:Text,14);
        if(focus){RECT ring=r;InflateRect(&ring,-1,-1);DrawFocusRect(dc,&ring);}
    }else if(id==204||id==208){
        rounded(dc,r,Field,focus?Accent:Border);
        wchar_t label[180]{};LRESULT selected=SendMessageW(h,CB_GETCURSEL,0,0);if(selected>=0)SendMessageW(h,CB_GETLBTEXT,selected,reinterpret_cast<LPARAM>(label));
        RECT value=r;value.left+=theme.px(12);value.right-=theme.px(34);text(dc,label,value,theme.body,Text);
        HPEN pen=CreatePen(PS_SOLID,theme.px(1),Muted);auto old=SelectObject(dc,pen);int x=r.right-theme.px(18),y=r.bottom/2-theme.px(2);
        MoveToEx(dc,x-theme.px(4),y,nullptr);LineTo(dc,x,y+theme.px(4));LineTo(dc,x+theme.px(4),y);SelectObject(dc,old);DeleteObject(pen);
    }
}
inline LRESULT CALLBACK ControlProcedure(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
    const int controlId=GetDlgCtrlID(h);
    if(msg==WM_MOUSEWHEEL&&(controlId==204||controlId==208)&&!SendMessageW(h,CB_GETDROPPEDSTATE,0,0)){
        // 收起的选择框不消费滚轮，交给配置页滚动；展开时仍沿用原生列表操作。
        if(HWND parent=GetParent(h))SendMessageW(parent,msg,wp,lp);
        return 0;
    }
    if(isSwitch(controlId)&&(msg==WM_LBUTTONDOWN||msg==WM_LBUTTONUP||msg==WM_LBUTTONDBLCLK)){
        RECT bounds{};GetClientRect(h,&bounds);const auto track=switchTrack(bounds);
        const POINT point{static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))};
        if(!PtInRect(&track,point)){
            // 原生复选框横跨整行；鼠标只允许命中画出的滑块。拖出后松开视为取消。
            if(msg==WM_LBUTTONUP&&GetCapture()==h){
                DefSubclassProc(h,WM_CANCELMODE,0,0);
                if(GetCapture()==h)ReleaseCapture();
                SendMessageW(h,BM_SETSTATE,FALSE,0);InvalidateRect(h,nullptr,FALSE);
            }
            return 0;
        }
    }
    switch(msg){
    case WM_PAINT:{PAINTSTRUCT p{};HDC dc=BeginPaint(h,&p);paintControl(h,dc);EndPaint(h,&p);return 0;}
    case WM_PRINTCLIENT:paintControl(h,reinterpret_cast<HDC>(wp));return 0;
    case WM_ERASEBKGND:return 1;
    case BM_SETCHECK:case BM_SETSTATE:case WM_SETFOCUS:case WM_KILLFOCUS:
    case WM_ENABLE:case WM_SETTEXT:case WM_SETFONT:case WM_UPDATEUISTATE:
    case WM_LBUTTONDOWN:case WM_LBUTTONUP:case WM_LBUTTONDBLCLK:case WM_KEYDOWN:case WM_KEYUP:{
        // 原生按钮会直接重绘左侧勾选框。先完成原生状态更新，再重绘整个自定义开关。
        const auto result=DefSubclassProc(h,msg,wp,lp);
        InvalidateRect(h,nullptr,FALSE);return result;
    }
    case WM_NCDESTROY:RemoveWindowSubclass(h,ControlProcedure,id);break;
    }
    return DefSubclassProc(h,msg,wp,lp);
}
inline void styleControl(HWND h){SetWindowTheme(h,L"",L"");int id=GetDlgCtrlID(h);if(isSwitch(id)||id==204||id==208)SetWindowSubclass(h,ControlProcedure,1,0);}

inline bool drawItem(const DRAWITEMSTRUCT& item){
    RECT r=item.rcItem;int id=static_cast<int>(item.CtlID);bool selected=(item.itemState&ODS_SELECTED)!=0,focus=(item.itemState&ODS_FOCUS)!=0;
    if(item.CtlType==ODT_BUTTON){
        // 先清除原生按钮底色，圆角之外保持面板背景，避免残留白色角点。
        FillRect(item.hDC,&r,theme.background);
        bool primary=id==302;COLORREF fill=primary?(selected?RGB(119,186,157):Accent):(selected?RGB(48,51,56):Field);
        rounded(item.hDC,r,fill,primary?fill:focus?Accent:Border);
        text(item.hDC,caption(item.hwndItem),r,theme.medium,primary?AccentText:Text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(focus){InflateRect(&r,-theme.px(4),-theme.px(4));DrawFocusRect(item.hDC,&r);}return true;
    }
    if(item.CtlType==ODT_COMBOBOX){
        HBRUSH brush=CreateSolidBrush(selected?RGB(45,63,55):Field);FillRect(item.hDC,&r,brush);DeleteObject(brush);
        wchar_t label[180]{};if(item.itemID!=static_cast<UINT>(-1))SendMessageW(item.hwndItem,CB_GETLBTEXT,item.itemID,reinterpret_cast<LPARAM>(label));
        r.left+=theme.px(12);text(item.hDC,label,r,theme.body,selected?Accent:Text);return true;
    }
    return false;
}
// 位置以 96 DPI 为基准，输入框与下拉框沿用既有控件 ID 和语义。
inline void layout(HWND h,int scrollOffset=0){
    struct Placement{int id,x,y,width,height;};
    // 坐标直接减去滚动量，避免每次先回到未滚动位置、再移动第二次。
    const Placement controls[]={
        {211,36,80,252,38},{212,330,80,254,38},{206,36,132,548,18},
        {201,20,194,580,24},{203,20,310,580,24},{202,32,259,218,22},{205,332,259,218,22},
        // 下拉列表高度包含展开内容，收起高度由选中项高度控制。
        {204,20,382,280,32+3*32+4},{208,320,382,280,32+2*32+4},
        {209,20,430,580,26},{210,166,469,314,22},{304,504,464,96,32},
        {305,20,504,280,32},{306,320,504,280,32},{213,20,540,580,18},
        {214,20,562,580,24},{215,20,590,580,18},{307,20,616,248,32},{216,280,616,320,36},
        {207,20,658,580,18},{301,20,686,132,32},{302,348,686,152,32},{303,516,686,84,32}
    };
    HDWP batch=BeginDeferWindowPos(static_cast<int>(sizeof(controls)/sizeof(controls[0])));
    for(const auto& c:controls){
        HWND child=GetDlgItem(h,c.id);if(!child)continue;
        HFONT font=c.id==211||c.id==212?theme.value:
            c.id==206||c.id==207||c.id==213||c.id==215||c.id==216?theme.smallFont:theme.body;
        // 字体只在首次布局或 DPI 改变时更新，滚动不触发无意义的重绘。
        if(reinterpret_cast<HFONT>(SendMessageW(child,WM_GETFONT,0,0))!=font)
            SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        if(batch){RECT r=theme.rect(c.x,c.y,c.width,c.height);
            batch=DeferWindowPos(batch,child,nullptr,r.left,r.top-scrollOffset,r.right-r.left,r.bottom-r.top,
                SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS);}
    }
    // 整批提交子控件位置；分配失败时仍保证布局可用。
    if(!batch||!EndDeferWindowPos(batch))for(const auto& c:controls){
        HWND child=GetDlgItem(h,c.id);if(!child)continue;RECT r=theme.rect(c.x,c.y,c.width,c.height);
        SetWindowPos(child,nullptr,r.left,r.top-scrollOffset,r.right-r.left,r.bottom-r.top,
            SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS);
    }
    for(int id:{204,208})for(int index:{-1,0}){
        const int height=theme.px(index==-1?26:32);HWND child=GetDlgItem(h,id);
        if(child&&SendMessageW(child,CB_GETITEMHEIGHT,index,0)!=height)SendMessageW(child,CB_SETITEMHEIGHT,index,height);
    }
}
}

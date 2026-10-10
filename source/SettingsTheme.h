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

// 保留原生控件的点击、空格键、勾选和下拉行为，仅替换外观。
inline void paintControl(HWND h,HDC dc){
    RECT r{};GetClientRect(h,&r);int id=GetDlgCtrlID(h);bool focus=GetFocus()==h;
    FillRect(dc,&r,theme.background);
    if(id==201||id==203){
        bool checked=SendMessageW(h,BM_GETCHECK,0,0)==BST_CHECKED;
        RECT label=r;label.right-=theme.px(62);text(dc,caption(h),label,theme.body,Text);
        RECT track{r.right-theme.px(36),(r.bottom-theme.px(20))/2,r.right,(r.bottom+theme.px(20))/2};
        rounded(dc,track,checked?Accent:Border,checked?Accent:Border,20);
        int x=checked?track.right-theme.px(17):track.left+theme.px(3);
        RECT thumb{x,track.top+theme.px(3),x+theme.px(14),track.bottom-theme.px(3)};
        rounded(dc,thumb,checked?AccentText:Text,checked?AccentText:Text,14);
        if(focus){RECT ring=r;InflateRect(&ring,-1,-1);DrawFocusRect(dc,&ring);}
    }else if(id==204){
        rounded(dc,r,Field,focus?Accent:Border);
        wchar_t label[180]{};LRESULT selected=SendMessageW(h,CB_GETCURSEL,0,0);if(selected>=0)SendMessageW(h,CB_GETLBTEXT,selected,reinterpret_cast<LPARAM>(label));
        RECT value=r;value.left+=theme.px(12);value.right-=theme.px(34);text(dc,label,value,theme.body,Text);
        HPEN pen=CreatePen(PS_SOLID,theme.px(1),Muted);auto old=SelectObject(dc,pen);int x=r.right-theme.px(18),y=r.bottom/2-theme.px(2);
        MoveToEx(dc,x-theme.px(4),y,nullptr);LineTo(dc,x,y+theme.px(4));LineTo(dc,x+theme.px(4),y);SelectObject(dc,old);DeleteObject(pen);
    }
}
inline LRESULT CALLBACK ControlProcedure(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
    switch(msg){
    case WM_PAINT:{PAINTSTRUCT p{};HDC dc=BeginPaint(h,&p);paintControl(h,dc);EndPaint(h,&p);return 0;}
    case WM_PRINTCLIENT:paintControl(h,reinterpret_cast<HDC>(wp));return 0;
    case WM_ERASEBKGND:return 1;
    case WM_SETFOCUS:case WM_KILLFOCUS:InvalidateRect(h,nullptr,FALSE);InvalidateRect(GetParent(h),nullptr,FALSE);break;
    case WM_NCDESTROY:RemoveWindowSubclass(h,ControlProcedure,id);break;
    }
    return DefSubclassProc(h,msg,wp,lp);
}
inline void styleControl(HWND h){SetWindowTheme(h,L"",L"");int id=GetDlgCtrlID(h);if(id==201||id==203||id==204)SetWindowSubclass(h,ControlProcedure,1,0);}

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
inline void layout(HWND h){
    auto move=[&](int id,int x,int y,int w,int height){RECT r=theme.rect(x,y,w,height);SetWindowPos(GetDlgItem(h,id),nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);};
    move(211,44,138,244,46);move(212,330,138,244,46);move(206,44,208,530,20);
    move(201,24,293,572,28);move(203,24,446,572,28);
    move(202,36,378,204,22);move(205,334,378,204,22);move(204,176,504,420,36);
    move(207,24,550,572,26);move(301,24,588,132,36);move(302,346,588,152,36);move(303,512,588,84,36);
    for(int id:{201,202,203,204,205,207,301,302,303})SendMessageW(GetDlgItem(h,id),WM_SETFONT,reinterpret_cast<WPARAM>(theme.body),TRUE);
    for(int id:{211,212})SendMessageW(GetDlgItem(h,id),WM_SETFONT,reinterpret_cast<WPARAM>(theme.value),TRUE);
    SendMessageW(GetDlgItem(h,206),WM_SETFONT,reinterpret_cast<WPARAM>(theme.smallFont),TRUE);
    SendMessageW(GetDlgItem(h,204),CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),theme.px(30));SendMessageW(GetDlgItem(h,204),CB_SETITEMHEIGHT,0,theme.px(32));
}
}

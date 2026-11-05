#pragma once
#include "win_theme.h"
#include <algorithm>
#include <string>

namespace ui {
inline COLORREF colorRef(Gdiplus::Color color){return color.ToCOLORREF();}

inline RECT ownedWindowRect(HWND owner,int width,int height,bool alignRight=false){
    RECT anchor{};
    GetWindowRect(owner,&anchor);
    MONITORINFO monitor{sizeof(monitor)};
    if(!GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor))
        return {anchor.left,anchor.top,anchor.left+width,anchor.top+height};
    const auto& work=monitor.rcWork;
    const LONG x=alignRight?anchor.right-width:(anchor.left+anchor.right-width)/2;
    const LONG y=(anchor.top+anchor.bottom-height)/2;
    const LONG left=std::clamp(x,work.left,std::max(work.left,work.right-width));
    const LONG top=std::clamp(y,work.top,std::max(work.top,work.bottom-height));
    return {left,top,left+width,top+height};
}

struct DialogColors {
    const appearance::Palette& palette=appearance::palette();
    HBRUSH panel=CreateSolidBrush(colorRef(palette.panel));
    HBRUSH field=CreateSolidBrush(colorRef(palette.raised));
    ~DialogColors(){DeleteObject(panel);DeleteObject(field);}

    LRESULT control(UINT message,HDC dc) const {
        const bool input=message==WM_CTLCOLOREDIT||message==WM_CTLCOLORLISTBOX;
        SetTextColor(dc,colorRef(palette.text));
        SetBkColor(dc,colorRef(input?palette.raised:palette.panel));
        return reinterpret_cast<LRESULT>(input?field:panel);
    }
    void draw(const DRAWITEMSTRUCT& item,bool primary=false) const {
        const bool button=item.CtlType==ODT_BUTTON;
        const bool disabled=(item.itemState&ODS_DISABLED)!=0;
        const bool active=!disabled&&(item.itemState&(ODS_SELECTED|ODS_HOTLIGHT));
        const bool accent=!disabled&&(primary||(!button&&active));
        SetDCBrushColor(item.hDC,colorRef(accent?palette.accent:active?palette.hover:button?palette.panel:palette.raised));
        FillRect(item.hDC,&item.rcItem,(HBRUSH)GetStockObject(DC_BRUSH));
        if(button){
            SetDCBrushColor(item.hDC,colorRef(palette.edge));
            FrameRect(item.hDC,&item.rcItem,(HBRUSH)GetStockObject(DC_BRUSH));
        }
        std::wstring text;
        if(button){
            text.resize(GetWindowTextLengthW(item.hwndItem)+1);
            GetWindowTextW(item.hwndItem,text.data(),int(text.size()));
        }else if(item.itemID!=UINT(-1)){
            auto length=SendMessageW(item.hwndItem,CB_GETLBTEXTLEN,item.itemID,0);
            if(length!=CB_ERR){
                text.resize(length+1);
                SendMessageW(item.hwndItem,CB_GETLBTEXT,item.itemID,reinterpret_cast<LPARAM>(text.data()));
            }
        }
        SetTextColor(item.hDC,colorRef(disabled?palette.muted:accent?palette.onAccent:palette.text));
        SetBkMode(item.hDC,TRANSPARENT);
        RECT rect=item.rcItem;
        if(!button){rect.left+=6;rect.right-=4;}
        DrawTextW(item.hDC,text.c_str(),-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX|(button?DT_CENTER:DT_LEFT));
        if(item.itemState&ODS_FOCUS){rect=item.rcItem;InflateRect(&rect,-3,-3);DrawFocusRect(item.hDC,&rect);}
    }
};
}

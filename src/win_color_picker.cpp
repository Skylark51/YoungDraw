#include "win_dialogs.h"
#include "win_dialog_style.h"
#include "win_ui.h"
#include <commctrl.h>
#include <windowsx.h>
#include <array>
#include <cmath>
#include <cwchar>

namespace ui {
namespace {
constexpr int firstSwatch=100,firstCustom=200,hexInput=300,saveCustom=301;
constexpr RECT spectrum{282,16,510,178},hueBar{520,16,542,178};
constexpr std::array<COLORREF,24> swatches={
    RGB(247,184,191), RGB(250,205,176), RGB(250,230,166), RGB(205,231,171),
    RGB(174,226,209), RGB(177,214,240), RGB(198,191,239), RGB(234,189,225),
    RGB(176,64,83), RGB(202,102,44), RGB(177,133,23), RGB(85,138,48),
    RGB(28,134,113), RGB(36,111,169), RGB(104,79,171), RGB(156,67,137),
    RGB(0,0,0), RGB(49,53,64), RGB(93,98,111), RGB(136,142,155),
    RGB(175,182,192), RGB(208,214,222), RGB(234,238,243), RGB(255,255,255)
};
std::array<std::optional<COLORREF>,16> customColors{};
size_t nextCustom=0;

struct ColorPicker {
    DialogColors colors;
    HFONT font=nullptr;
    COLORREF current;
    double hue=0,saturation=0,brightness=0;
    bool syncing=false,done=false,accepted=false;
    int drag=0;
    double spectrumHue=-1;
    std::vector<DWORD> spectrumPixels;

    explicit ColorPicker(COLORREF initial):current(initial){channels(initial);}
    ~ColorPicker(){if(font)DeleteObject(font);}

    void channels(COLORREF color){
        const double r=GetRValue(color)/255.,g=GetGValue(color)/255.,b=GetBValue(color)/255.;
        brightness=std::max({r,g,b});
        const double range=brightness-std::min({r,g,b});
        saturation=brightness>0?range/brightness:0;
        if(range>0){
            if(brightness==r)hue=(g-b)/range;
            else if(brightness==g)hue=2+(b-r)/range;
            else hue=4+(r-g)/range;
            if(hue<0)hue+=6;
        }
    }
    COLORREF colorAt(double h,double s,double v) const {
        auto channel=[&](double offset){
            const double k=std::fmod(offset+h,6.);
            return BYTE(std::lround(255*v*(1-s*std::max(0.,std::min({k,4-k,1.})))));
        };
        return RGB(channel(5),channel(3),channel(1));
    }
    void showHex(HWND window){
        wchar_t value[8]{};
        swprintf_s(value,L"#%02X%02X%02X",GetRValue(current),GetGValue(current),GetBValue(current));
        syncing=true;
        SetDlgItemTextW(window,hexInput,value);
        syncing=false;
        EnableWindow(GetDlgItem(window,IDOK),TRUE);
        EnableWindow(GetDlgItem(window,saveCustom),TRUE);
        InvalidateRect(window,nullptr,FALSE);
    }
    void select(HWND window,COLORREF color){
        current=color;channels(color);showHex(window);
    }
    void readHex(HWND window){
        if(syncing)return;
        wchar_t input[10]{};
        GetDlgItemTextW(window,hexInput,input,int(std::size(input)));
        const wchar_t* value=input+(*input==L'#');
        const bool valid=wcslen(value)==6&&wcsspn(value,L"0123456789abcdefABCDEF")==6;
        EnableWindow(GetDlgItem(window,IDOK),valid);
        EnableWindow(GetDlgItem(window,saveCustom),valid);
        if(!valid)return;
        const auto rgb=wcstoul(value,nullptr,16);
        current=RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255);
        channels(current);
        InvalidateRect(window,nullptr,FALSE);
    }
    void dragTo(HWND window,POINT cursor){
        if(drag==1){
            saturation=std::clamp(double(cursor.x-spectrum.left)/(spectrum.right-spectrum.left-1),0.,1.);
            brightness=1-std::clamp(double(cursor.y-spectrum.top)/(spectrum.bottom-spectrum.top-1),0.,1.);
        }else{
            hue=std::clamp(6.*(cursor.y-hueBar.top)/(hueBar.bottom-hueBar.top-1),0.,5.999999);
        }
        current=colorAt(hue,saturation,brightness);
        showHex(window);
    }
    void paint(HWND window,HDC dc) {
        RECT bounds{};
        GetClientRect(window,&bounds);
        FillRect(dc,&bounds,colors.panel);
        const int width=spectrum.right-spectrum.left,height=spectrum.bottom-spectrum.top;
        if(spectrumHue!=hue){
            spectrumHue=hue;
            spectrumPixels.resize(size_t(width)*height);
            for(int y=0;y<height;y++)for(int x=0;x<width;x++){
                const auto rgb=colorAt(hue,double(x)/(width-1),1-double(y)/(height-1));
                spectrumPixels[size_t(y)*width+x]=(GetRValue(rgb)<<16)|(GetGValue(rgb)<<8)|GetBValue(rgb);
            }
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
        info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        SetDIBitsToDevice(dc,spectrum.left,spectrum.top,width,height,0,0,0,height,spectrumPixels.data(),&info,DIB_RGB_COLORS);
        auto brush=static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
        for(LONG y=hueBar.top;y<hueBar.bottom;y++){
            const RECT row{hueBar.left,y,hueBar.right,y+1};
            SetDCBrushColor(dc,colorAt(6.*(y-hueBar.top)/(height-1),1,1));
            FillRect(dc,&row,brush);
        }
        const int x=spectrum.left+int(std::lround(saturation*(width-1)));
        const int y=spectrum.top+int(std::lround((1-brightness)*(height-1)));
        const int h=hueBar.top+int(std::lround(hue*(height-1)/6));
        SetDCBrushColor(dc,RGB(255,255,255));
        const RECT outside{x-5,y-5,x+6,y+6},indicator{hueBar.left-2,h-3,hueBar.right+2,h+4};
        const RECT inside{x-4,y-4,x+5,y+5};
        FrameRect(dc,&outside,brush);
        FrameRect(dc,&indicator,brush);
        SetDCBrushColor(dc,RGB(0,0,0));
        FrameRect(dc,&inside,brush);
        const RECT chip{282,197,367,238};
        SetDCBrushColor(dc,current);
        FillRect(dc,&chip,brush);
    }
};

LRESULT CALLBACK colorProc(HWND window,UINT msg,WPARAM wp,LPARAM lp){
    auto* p=reinterpret_cast<ColorPicker*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(msg==WM_CREATE){
        p=static_cast<ColorPicker*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));
        p->font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,interfaceFont());
        auto control=[&](const wchar_t* type,const wchar_t* title,int id,int x,int y,int w,int h,DWORD style=0){
            auto child=CreateWindowW(type,title,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,
                reinterpret_cast<HMENU>(INT_PTR(id)),nullptr,nullptr);
            SendMessageW(child,WM_SETFONT,WPARAM(p->font),TRUE);
            return child;
        };
        constexpr DWORD button=WS_TABSTOP|BS_OWNERDRAW;
        control(L"STATIC",L"기본 색",0,16,15,246,20);
        for(int i=0;i<int(swatches.size());i++){
            const auto color=swatches[i];
            wchar_t name[8]{};
            swprintf_s(name,L"#%02X%02X%02X",GetRValue(color),GetGValue(color),GetBValue(color));
            control(L"BUTTON",name,firstSwatch+i,16+i%8*31,39+i/8*29,26,24,button);
        }
        control(L"STATIC",L"사용자 지정 색",0,16,135,246,20);
        for(int i=0;i<int(customColors.size());i++)
            control(L"BUTTON",(L"사용자 지정 색 "+std::to_wstring(i+1)).c_str(),firstCustom+i,16+i%8*31,159+i/8*29,26,24,button);
        control(L"BUTTON",L"사용자 지정 색에 추가",saveCustom,16,222,240,30,button);
        control(L"STATIC",L"HEX",0,383,188,156,19);
        auto input=control(L"EDIT",L"",hexInput,383,208,159,28,WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL);
        SendMessageW(input,EM_SETLIMITTEXT,7,0);
        SendMessageW(input,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(6,6));
        control(L"BUTTON",L"확인",IDOK,338,258,98,31,button);
        control(L"BUTTON",L"취소",IDCANCEL,444,258,98,31,button);
        p->showHex(window);
        return 0;
    }
    if(!p)return DefWindowProcW(window,msg,wp,lp);
    switch(msg){
    case WM_ERASEBKGND:{RECT bounds{};GetClientRect(window,&bounds);FillRect(reinterpret_cast<HDC>(wp),&bounds,p->colors.panel);return 1;}
    case WM_PAINT:{PAINTSTRUCT ps{};p->paint(window,BeginPaint(window,&ps));EndPaint(window,&ps);return 0;}
    case WM_PRINTCLIENT:p->paint(window,reinterpret_cast<HDC>(wp));return 0;
    case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:return p->colors.control(msg,reinterpret_cast<HDC>(wp));
    case DM_GETDEFID:return MAKELRESULT(IDOK,DC_HASDEFID);
    case WM_DRAWITEM:{
        const auto& item=*reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
        std::optional<COLORREF> color;
        if(item.CtlID>=firstSwatch&&item.CtlID<firstSwatch+swatches.size())color=swatches[item.CtlID-firstSwatch];
        else if(item.CtlID>=firstCustom&&item.CtlID<firstCustom+customColors.size())color=customColors[item.CtlID-firstCustom];
        else{p->colors.draw(item,item.CtlID==IDOK);return TRUE;}
        auto brush=static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
        SetDCBrushColor(item.hDC,color.value_or(p->colors.palette.raised.ToCOLORREF()));
        FillRect(item.hDC,&item.rcItem,brush);
        SetDCBrushColor(item.hDC,p->colors.palette.edge.ToCOLORREF());
        FrameRect(item.hDC,&item.rcItem,brush);
        if(item.itemState&ODS_FOCUS){
            RECT focus=item.rcItem;
            InflateRect(&focus,-3,-3);
            DrawFocusRect(item.hDC,&focus);
        }
        return TRUE;
    }
    case WM_LBUTTONDOWN:{
        const POINT cursor{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        p->drag=PtInRect(&spectrum,cursor)?1:PtInRect(&hueBar,cursor)?2:0;
        if(p->drag){SetCapture(window);p->dragTo(window,cursor);}
        return 0;
    }
    case WM_MOUSEMOVE:if(p->drag)p->dragTo(window,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;
    case WM_LBUTTONUP:if(p->drag){p->drag=0;ReleaseCapture();}return 0;
    case WM_CAPTURECHANGED:p->drag=0;return 0;
    case WM_COMMAND:{
        const int id=LOWORD(wp);
        if(id==hexInput&&HIWORD(wp)==EN_CHANGE){p->readHex(window);return 0;}
        if(id>=firstSwatch&&id<firstSwatch+int(swatches.size())){p->select(window,swatches[id-firstSwatch]);return 0;}
        if(id>=firstCustom&&id<firstCustom+int(customColors.size())){
            if(auto color=customColors[id-firstCustom])p->select(window,*color);
            return 0;
        }
        if(id==saveCustom&&IsWindowEnabled(GetDlgItem(window,saveCustom))){
            const size_t slot=nextCustom++%customColors.size();
            customColors[slot]=p->current;
            InvalidateRect(GetDlgItem(window,firstCustom+int(slot)),nullptr,FALSE);
            return 0;
        }
        if(id==IDOK&&!IsWindowEnabled(GetDlgItem(window,IDOK)))return 0;
        if(id==IDOK||id==IDCANCEL){p->accepted=id==IDOK;p->done=true;PostMessageW(window,WM_NULL,0,0);return 0;}
        break;
    }
    case WM_DESTROY:case WM_CLOSE:p->done=true;PostMessageW(window,WM_NULL,0,0);return 0;
    }
    return DefWindowProcW(window,msg,wp,lp);
}
}

std::optional<COLORREF> chooseColor(HWND parent,COLORREF initial){
    ColorPicker picker(initial);
    WNDCLASSW wc{};wc.lpfnWndProc=colorProc;wc.hInstance=GetModuleHandleW(nullptr);
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=L"YoungDrawColorPicker";
    RegisterClassW(&wc);
    if(auto* host=inlineDialogHost()){
        auto panel=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"색 선택",WS_CHILD|WS_CLIPCHILDREN,
            0,0,560,306,host->window,nullptr,wc.hInstance,&picker);
        if(!panel)return {};
        appearance::nativeControl(GetDlgItem(panel,hexInput));
        runInlineDialog(panel,picker.done,GetDlgItem(panel,hexInput));
        return picker.accepted?std::optional<COLORREF>{picker.current}:std::nullopt;
    }
    constexpr DWORD style=WS_CAPTION|WS_SYSMENU|WS_POPUP|WS_CLIPCHILDREN;
    RECT size{0,0,560,306};
    AdjustWindowRect(&size,style,FALSE);
    const auto box=ownedWindowRect(parent,size.right-size.left,size.bottom-size.top);
    auto window=CreateWindowW(wc.lpszClassName,L"색 선택",style,box.left,box.top,box.right-box.left,
        box.bottom-box.top,parent,nullptr,wc.hInstance,&picker);
    if(!window)return {};
    appearance::titlebar(window);
    const auto focus=GetFocus();
    const bool enabled=IsWindowEnabled(parent)!=FALSE;
    if(enabled)EnableWindow(parent,FALSE);
    ShowWindow(window,SW_SHOW);
    SetFocus(GetDlgItem(window,hexInput));
    MSG msg{};
    BOOL result=1;
    while(!picker.done&&(result=GetMessageW(&msg,nullptr,0,0))>0){
        if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    if(enabled&&IsWindow(parent))EnableWindow(parent,TRUE);
    if(IsWindow(window))DestroyWindow(window);
    if(enabled&&IsWindow(parent)){SetActiveWindow(parent);if(IsWindow(focus))SetFocus(focus);}
    if(result==0)PostQuitMessage(int(msg.wParam));
    return picker.accepted?std::optional<COLORREF>{picker.current}:std::nullopt;
}
}


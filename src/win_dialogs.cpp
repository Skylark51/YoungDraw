#include "win_button_help.h"
#include "win_dialogs.h"
#include "win_ui.h"
#include "win_dialog_style.h"
#include "win_scripts.h"
#include "mechanism_data.h"
#include "win_editor_state.h"
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <uxtheme.h>
namespace ui {
namespace {
struct GraphPicker {
    std::vector<yd::Reaction> mechanisms;
    int count() const {return mechanisms.empty()?5:int(mechanisms.size());}
    GraphChoice choice;DialogColors colors;HFONT font=nullptr;bool done=false,ok=false,editStyle=false;std::function<void(int)> preview;
};
const wchar_t* graphNames[]={L"부드러운 곡선",L"직선",L"준위 · 점선 연결",L"준위 · 실선 연결",L"독립 에너지 준위"};
const wchar_t* graphDetails[]={L"봉우리와 중간체를 부드럽게 연결",L"상태 사이를 직선으로 연결",L"평평한 준위를 점선으로 연결",L"평평한 준위를 실선으로 연결",L"스핀 다중도 등 개별 상태 비교"};
constexpr const wchar_t* mechanismNames[]={L"직선",L"가로 → 아래 → 순환",L"가로 → 아래 → 분기·합류",L"촉매 순환"};
void drawMechanismPath(Gdiplus::Graphics& g,const Gdiplus::RectF& card,const yd::Reaction& reaction){
    using namespace Gdiplus;
    using yd::Vec;
    constexpr double nodeRadius=5.5;
    std::vector<Vec> nodes;
    for(size_t i=0;i<reaction.slots.size();++i)nodes.push_back(reactionSlot(reaction,i));
    Vec lo=nodes.front(),hi=lo;
    for(auto p:nodes){
        lo={std::min(lo.x,p.x),std::min(lo.y,p.y)};
        hi={std::max(hi.x,p.x),std::max(hi.y,p.y)};
    }
    const Vec center{card.X+card.Width/2,card.Y+card.Height/2},mid=(lo+hi)*.5;
    const double scale=std::min((card.Width-42)/std::max(1.,hi.x-lo.x),(card.Height-40)/std::max(1.,hi.y-lo.y));
    for(auto& p:nodes)p=center+(p-mid)*scale;

    const Color ink(255,35,32,49),surface(255,250,250,250);
    Pen line(ink,2.6f);line.SetStartCap(LineCapRound);line.SetEndCap(LineCapRound);
    SolidBrush fill(ink),empty(surface);
    auto point=[](Vec p){return PointF(float(p.x),float(p.y));};
    auto unit=[](Vec p){return p*(1/p.length());};
    for(const auto& link:reaction.links){
        const auto from=nodes[link.from],to=nodes[link.to],delta=to-from;
        const double bend=reaction.cycleStages.empty()?link.bend:-.72;
        const auto control=(from+to)*.5+Vec{-delta.y,delta.x}*bend;
        const auto start=from+unit(control-from)*(nodeRadius+2);
        const auto direction=unit(to-control);
        const auto tip=to-direction*(nodeRadius+2);
        const auto base=tip-direction*7;
        const Vec normal{-direction.y,direction.x};
        g.DrawBezier(&line,point(start),point(start+(control-start)*(2./3)),point(base+(control-base)*(2./3)),point(base));
        const PointF head[]={point(tip),point(base+normal*3.7),point(base-normal*3.7)};
        g.FillPolygon(&fill,head,3);
    }
    for(size_t i=0;i<nodes.size();++i){
        const auto p=nodes[i];
        const RectF dot(float(p.x-nodeRadius),float(p.y-nodeRadius),float(nodeRadius*2),float(nodeRadius*2));
        g.FillEllipse(i==0?&fill:&empty,dot);
        g.DrawEllipse(&line,dot);
    }
}
void drawMechanismCard(const DRAWITEMSTRUCT& item,const GraphPicker& picker,int mode){
    using namespace Gdiplus;
    const auto& palette=picker.colors.palette;
    const bool selected=picker.choice.mode==mode;
    RECT bounds{};GetClientRect(item.hwndItem,&bounds);
    FillRect(item.hDC,&bounds,picker.colors.panel);
    Graphics g(item.hDC);g.SetSmoothingMode(SmoothingModeAntiAlias);
    RectF box(0.f,0.f,float(bounds.right),float(bounds.bottom));
    RectF square(box.X+8,box.Y+2,144,144);
    appearance::rounded(g,square,13,selected?palette.accent:palette.raised);
    square.Inflate(-4,-4);
    appearance::rounded(g,square,10,Color(255,250,250,250));
    drawMechanismPath(g,square,picker.mechanisms[mode]);
    if(selected){
        SolidBrush dot(palette.accent);g.FillEllipse(&dot,square.GetRight()-14,square.Y+6,8.f,8.f);
    }
    const bool focused=(item.itemState&ODS_FOCUS)&&!(item.itemState&ODS_NOFOCUSRECT);
    ui::drawLabel(g,mechanismNames[mode],{box.X,box.Y+151,box.Width,30},12.f,
        selected||focused?palette.accent:palette.text,selected,true);
}
void drawGraphCard(const DRAWITEMSTRUCT& item,GraphPicker& picker,int mode){
    using namespace Gdiplus;
    Graphics g(item.hDC);g.SetSmoothingMode(SmoothingModeAntiAlias);
    const auto& palette=picker.colors.palette;const bool selected=picker.choice.mode==mode;
    RectF box(float(item.rcItem.left),float(item.rcItem.top),float(item.rcItem.right-item.rcItem.left),float(item.rcItem.bottom-item.rcItem.top));
    SolidBrush fill(selected?palette.hover:palette.raised);g.FillRectangle(&fill,box);
    Pen border(selected?palette.accent:palette.edge,selected?2.f:1.f);g.DrawRectangle(&border,box.X+1,box.Y+1,box.Width-2,box.Height-2);
    if(item.itemState&ODS_FOCUS){RECT focus=item.rcItem;InflateRect(&focus,-4,-4);DrawFocusRect(item.hDC,&focus);}
    const float x=box.X+22,y=box.Y+20,w=box.Width-44,h=60;
    Pen axis(palette.muted,1);g.DrawLine(&axis,x,y+h,x+w,y+h);g.DrawLine(&axis,x,y+h,x,y);
    Pen path(palette.text,2.1f);PointF pts[]={{x+10,y+46},{x+w*.5f,y+9},{x+w-10,y+50}};
    if(mode==0){g.DrawBezier(&path,pts[0],PointF(x+w*.3f,y+46),PointF(x+w*.3f,y+9),pts[1]);g.DrawBezier(&path,pts[1],PointF(x+w*.7f,y+9),PointF(x+w*.7f,y+50),pts[2]);}
    else if(mode==1)g.DrawLines(&path,pts,3);
    else{
        const float half=13;
        for(int i=0;i<3;++i){
            if(i&&mode!=4){Pen join(palette.muted,1.4f);if(mode==2)join.SetDashStyle(DashStyleDash);g.DrawLine(&join,pts[i-1].X+half,pts[i-1].Y,pts[i].X-half,pts[i].Y);}
            g.DrawLine(&path,pts[i].X-half,pts[i].Y,pts[i].X+half,pts[i].Y);
        }
    }
    SolidBrush dot(palette.accent);for(auto pt:pts)g.FillEllipse(&dot,pt.X-2.8f,pt.Y-2.8f,5.6f,5.6f);
    ui::drawLabel(g,graphNames[mode],{box.X+12,box.Y+91,box.Width-24,25},14,palette.text,true,true);
    ui::drawLabel(g,graphDetails[mode],{box.X+10,box.Y+118,box.Width-20,26},10.5f,palette.muted,false,true);
    if(selected){SolidBrush chosen(palette.accent);g.FillEllipse(&chosen,box.X+box.Width-19.f,box.Y+9.f,8.f,8.f);}
}
LRESULT CALLBACK graphPickerProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){
    auto* picker=reinterpret_cast<GraphPicker*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_CREATE){ui::enableButtonHelp();
        picker=static_cast<GraphPicker*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(picker));
        picker->font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,ui::interfaceFont());
        auto control=[&](const wchar_t* kind,const wchar_t* label,DWORD style,int id,int x,int y,int w,int h){auto c=CreateWindowW(kind,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,hwnd,reinterpret_cast<HMENU>(INT_PTR(id)),nullptr,nullptr);SendMessageW(c,WM_SETFONT,WPARAM(picker->font),TRUE);return c;};
        if(!picker->mechanisms.empty()){
            control(L"STATIC",L"경로 예시를 보고 양식을 선택하세요.",0,0,22,16,640,28);
            for(int i=0;i<picker->count();++i){
                auto card=control(L"BUTTON",mechanismNames[i],WS_TABSTOP|BS_OWNERDRAW,100+i,16+i*164,50,160,182);
                SetWindowTheme(card,L"",L"");
            }
            control(L"BUTTON",L"만들기",WS_TABSTOP|BS_OWNERDRAW,IDOK,450,248,100,34);
            control(L"BUTTON",L"취소",WS_TABSTOP|BS_OWNERDRAW,IDCANCEL,562,248,100,34);
            return 0;
        }
        control(L"STATIC",picker->editStyle?L"현재 그래프의 스타일을 선택하세요":L"그래프 형태를 선택하세요",0,0,22,16,680,30);
        for(int i=0;i<5;++i)control(L"BUTTON",graphNames[i],WS_TABSTOP|BS_OWNERDRAW,100+i,22+(i%3)*228,53+(i/3)*169,216,156);
        if(!picker->editStyle){
            control(L"STATIC",L"에너지 표기",0,0,22,402,170,25);
            control(L"BUTTON",L"ΔE · 에너지",WS_TABSTOP|BS_OWNERDRAW,200,22,435,216,43);
            control(L"BUTTON",L"ΔG · 자유 에너지",WS_TABSTOP|BS_OWNERDRAW,201,250,435,216,43);
        }
        const int hintY=picker->editStyle?402:491,buttonY=picker->editStyle?457:546;
        control(L"STATIC",picker->editStyle?L"카드를 누르면 현재 그래프에 미리보기로 반영됩니다.\n데이터·색상·구조·주석은 유지하며, 취소하면 원래 스타일로 돌아갑니다.":L"형태와 표기는 만든 뒤에도 바꿀 수 있습니다.\n에너지 갭과 구간 생략은 필요할 때 추가하세요.",0,0,22,hintY,680,45);
        control(L"BUTTON",picker->editStyle?L"적용":L"만들기",WS_TABSTOP|BS_OWNERDRAW,IDOK,476,buttonY,100,34);
        control(L"BUTTON",L"취소",WS_TABSTOP|BS_OWNERDRAW,IDCANCEL,588,buttonY,100,34);
        return 0;
    }
    if(!picker)return DefWindowProcW(hwnd,message,wp,lp);
    if(message==WM_ERASEBKGND){RECT r{};GetClientRect(hwnd,&r);FillRect(HDC(wp),&r,picker->colors.panel);return 1;}
    if(message==WM_CTLCOLORSTATIC)return picker->colors.control(message,HDC(wp));
    if(message==WM_DRAWITEM){const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if(item.CtlID>=100&&item.CtlID<100+UINT(picker->count())){
            const int mode=int(item.CtlID)-100;
            if(picker->mechanisms.empty())drawGraphCard(item,*picker,mode);
            else drawMechanismCard(item,*picker,mode);
        }
        else picker->colors.draw(item,item.CtlID==IDOK||(item.CtlID==200&&!picker->choice.freeEnergy)||(item.CtlID==201&&picker->choice.freeEnergy));
        return TRUE;
    }
    if(message==DM_GETDEFID)return MAKELRESULT(IDOK,DC_HASDEFID);
    if(message==WM_COMMAND){int id=LOWORD(wp);
        if(id>=100&&id<100+picker->count()){picker->choice.mode=id-100;if(picker->preview)picker->preview(picker->choice.mode);for(int i=100;i<100+picker->count();++i)InvalidateRect(GetDlgItem(hwnd,i),nullptr,TRUE);return 0;}
        if(!picker->editStyle&&(id==200||id==201)){picker->choice.freeEnergy=id==201;for(int i=200;i<=201;++i)InvalidateRect(GetDlgItem(hwnd,i),nullptr,TRUE);return 0;}
        if(id==IDOK||id==IDCANCEL){picker->done=true;picker->ok=id==IDOK;PostMessageW(hwnd,WM_NULL,0,0);return 0;}
    }
    if(message==WM_CLOSE){picker->done=true;PostMessageW(hwnd,WM_NULL,0,0);return 0;}
    return DefWindowProcW(hwnd,message,wp,lp);
}
std::optional<GraphChoice> showPathPicker(HWND owner,GraphPicker& picker,const wchar_t* title,int width,int height,const wchar_t* className){
    const auto instance=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};wc.lpfnWndProc=graphPickerProc;wc.hInstance=instance;wc.lpszClassName=className;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    constexpr DWORD style=WS_CAPTION|WS_SYSMENU|WS_POPUP|WS_CLIPCHILDREN;
    RECT area{0,0,width,height};AdjustWindowRectEx(&area,style,FALSE,0);
    const int w=area.right-area.left,h=area.bottom-area.top;const auto r=ownedWindowRect(owner,w,h,picker.editStyle);
    auto hwnd=CreateWindowExW(0,className,title,style,r.left,r.top,w,h,owner,nullptr,instance,&picker);
    if(!hwnd)return {};
    const auto previousFocus=GetFocus();
    appearance::titlebar(hwnd);attachWindowHelp(hwnd);EnableWindow(owner,FALSE);ShowWindow(hwnd,SW_SHOW);SetFocus(GetDlgItem(hwnd,100+picker.choice.mode));
    MSG msg{};BOOL result=1;
    while(!picker.done&&(result=GetMessageW(&msg,nullptr,0,0))>0)if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    EnableWindow(owner,TRUE);DestroyWindow(hwnd);SetActiveWindow(owner);DeleteObject(picker.font);
    if(previousFocus&&(previousFocus==owner||IsChild(owner,previousFocus)))SetFocus(previousFocus);
    if(result==0)PostQuitMessage(int(msg.wParam));
    if(picker.ok)return picker.choice;
    return {};
}
}
std::optional<GraphChoice> graphChoice(HWND owner,int initialMode,bool editStyle,const std::function<void(int)>& preview){
    GraphPicker picker;picker.choice.mode=std::clamp(initialMode,0,4);picker.editStyle=editStyle;picker.preview=preview;
    return showPathPicker(owner,picker,editStyle?L"그래프 스타일 변경":L"새 에너지 그래프",710,editStyle?509:598,L"YoungDrawGraphPicker");
}
std::optional<yd::MechanismTemplate> mechanismChoice(HWND owner){
    GraphPicker picker;
    for(auto layout:{yd::MechanismTemplate::Linear,yd::MechanismTemplate::FeedIntoCycle,
        yd::MechanismTemplate::SplitAndMerge,yd::MechanismTemplate::CatalyticCycle}){
        auto path=yd::mechanismTemplate(layout);
        yd::makeMechanismGraph(path);
        picker.mechanisms.push_back(std::move(path));
    }
    auto chosen=showPathPicker(owner,picker,L"반응 메커니즘 만들기",688,298,L"YoungDrawMechanismPicker");
    if(chosen)return static_cast<yd::MechanismTemplate>(chosen->mode);
    return {};
}
static HWND editSurface(HWND control){COMBOBOXINFO info{sizeof(info)};return GetComboBoxInfo(control,&info)&&info.hwndItem?info.hwndItem:control;}
std::vector<std::wstring> fontFamilies(HWND window){
    std::vector<std::wstring> fonts;LOGFONTW query{};query.lfCharSet=DEFAULT_CHARSET;
    HDC dc=GetDC(window);
    EnumFontFamiliesExW(dc,&query,[](const LOGFONTW* font,const TEXTMETRICW*,DWORD,LPARAM data)->int{
        auto& list=*reinterpret_cast<std::vector<std::wstring>*>(data);
        if(font->lfFaceName[0]!=L'@')list.emplace_back(font->lfFaceName);
        return 1;
    },reinterpret_cast<LPARAM>(&fonts),0);
    ReleaseDC(window,dc);
    std::sort(fonts.begin(),fonts.end());fonts.erase(std::unique(fonts.begin(),fonts.end()),fonts.end());
    return fonts;
}
Field numericField(std::wstring label,double value,double minimum,double maximum,double increment){
    Field field{std::move(label),yd::editor::number(value)};
    field.stepValue=[=](const std::string& text,int steps){
        double current=value;
        try{current=yd::editor::numeric(text);}catch(const std::exception&){}
        return yd::editor::number(std::clamp(current+steps*increment,minimum,maximum));
    };
    return field;
}
static void stepEdit(HWND hwnd,const Field& field,int steps){
    const int length=GetWindowTextLengthW(hwnd);
    std::wstring current(length+1,L'\0');
    current.resize(GetWindowTextW(hwnd,current.data(),length+1));
    const auto next=wide(field.stepValue(utf8(current),steps));
    if(next==current)return;
    SetWindowTextW(hwnd,next.c_str());
    SendMessageW(hwnd,EM_SETSEL,next.size(),next.size());
}
static LRESULT CALLBACK scriptEditProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR ref){
    const auto& field=*reinterpret_cast<const Field*>(ref);
    if(multilineShortcut(hwnd,msg,wp)||(field.scripts&&scriptShortcut(hwnd,msg,wp)))return 0;
    if(field.stepWithSigns&&field.stepValue&&msg==WM_CHAR&&(wp==L'+'||wp==L'-')){
        stepEdit(hwnd,field,wp==L'+'?1:-1);
        return 0;
    }
    if(field.stepValue&&msg==WM_KEYDOWN&&(wp==VK_UP||wp==VK_DOWN)){
        stepEdit(hwnd,field,wp==VK_UP?1:-1);
        return 0;
    }
    if(msg==WM_GETDLGCODE)return DefSubclassProc(hwnd,msg,wp,lp)|DLGC_WANTCHARS|DLGC_WANTARROWS;
    if(msg==WM_CHAR&&wp==1)return 0;
    if(msg==WM_KEYDOWN&&wp=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(hwnd,EM_SETSEL,0,-1);return 0;}
    return DefSubclassProc(hwnd,msg,wp,lp);
}
static int inputHeight(const Field& field){return field.multiline?22*field.rows:29;}
static int labelHeight(const Field& field){
    const int lines=1+int(std::count(field.label.begin(),field.label.end(),L'\n'));
    if(lines>1)return 22*lines;
    return field.multiline?std::min(72,inputHeight(field)):34;
}
static int rowHeight(const Field& field){
    constexpr int gap=12,labelOffset=4;
    return std::max(inputHeight(field)+gap,labelOffset+labelHeight(field)+4);
}
using FormTabs=std::vector<std::pair<std::wstring,std::vector<size_t>>>;
struct Form {
    FormTabs tabs;
    HWND tab=nullptr;
    HWND wheelInput=nullptr;
    int wheelDelta=0;
    std::vector<std::vector<HWND>> rows;
    int contentBottom=16;
    RECT picture{};
    std::function<void(HDC,const RECT&,const std::vector<std::string>&)> illustration;
    DialogColors colors;
    std::vector<Field> fields;std::vector<HWND> edits;
    std::vector<std::string> result;Preview preview;
    bool done=false,ok=false;HFONT font=nullptr;HWND error=nullptr,apply=nullptr;
};
static std::vector<std::string> read(Form& f){
    std::vector<std::string> values;
    for(size_t i=0;i<f.edits.size();i++){
        if(!f.fields[i].choices.empty()&&!f.fields[i].editableChoices)values.push_back(std::to_string(SendMessageW(f.edits[i],CB_GETCURSEL,0,0)));
        else if(f.fields[i].editableChoices&&SendMessageW(f.edits[i],CB_GETCURSEL,0,0)!=CB_ERR){
            auto index=SendMessageW(f.edits[i],CB_GETCURSEL,0,0);int n=int(SendMessageW(f.edits[i],CB_GETLBTEXTLEN,index,0));std::wstring w(n+1,0);SendMessageW(f.edits[i],CB_GETLBTEXT,index,(LPARAM)w.data());w.resize(n);values.push_back(utf8(w));
        }
        else{int n=GetWindowTextLengthW(f.edits[i]);std::wstring w(n+1,0);GetWindowTextW(f.edits[i],w.data(),n+1);w.resize(n);values.push_back(utf8(w));}
    }return values;
}
static bool preview(Form& f){
    if(!f.preview)return true;std::wstring error;
    try{error=f.preview(read(f));}catch(const std::exception& e){error=wide(e.what());}
    SetWindowTextW(f.error,error.empty()?L"미리보기가 갱신되었습니다. 취소하면 원래 그림으로 복원합니다.":error.c_str());
    EnableWindow(f.apply,error.empty());return error.empty();
}
static void layoutFormTabs(Form& f){
    const int active=TabCtrl_GetCurSel(f.tab);
    int y=52;
    for(size_t i=0;i<f.rows.size();++i)for(HWND control:f.rows[i])ShowWindow(control,SW_HIDE);
    for(size_t i:f.tabs.at(active).second){
        for(size_t j=0;j<f.rows[i].size();++j){
            auto control=f.rows[i][j];
            RECT r{};GetWindowRect(control,&r);
            POINT pos{r.left,r.top};ScreenToClient(GetParent(control),&pos);
            SetWindowPos(control,HWND_TOP,pos.x,y+(j==0?4:0),0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        }
        y+=rowHeight(f.fields[i]);
    }
}
static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    auto* f=reinterpret_cast<Form*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_CREATE){ui::enableButtonHelp();
        f=static_cast<Form*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(f));
        f->font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,ui::interfaceFont());int y=16;
        if(!f->tabs.empty()){
            f->tab=CreateWindowW(WC_TABCONTROLW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP,16,10,644,35,hwnd,nullptr,nullptr,nullptr);
            SendMessageW(f->tab,WM_SETFONT,WPARAM(f->font),TRUE);
            for(auto& group:f->tabs){
                TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=group.first.data();
                TabCtrl_InsertItem(f->tab,TabCtrl_GetItemCount(f->tab),&item);
            }
            TabCtrl_SetCurSel(f->tab,0);y=52;std::vector<ui::HelpRegion> help;for(size_t i=0;i<f->tabs.size();i++){RECT rect{};if(TabCtrl_GetItemRect(f->tab,int(i),&rect))help.push_back({UINT_PTR(i+1),rect,f->tabs[i].first+L" 설정 항목을 표시합니다. 다른 탭의 입력값은 유지됩니다."});}ui::setRegionHelp(f->tab,help);
        }
        for(size_t i=0;i<f->fields.size();i++){
            auto& field=f->fields[i];HWND label=CreateWindowW(L"STATIC",field.label.c_str(),WS_CHILD|WS_VISIBLE,16,y+4,210,labelHeight(field),hwnd,nullptr,nullptr,nullptr);SendMessageW(label,WM_SETFONT,(WPARAM)f->font,TRUE);
            int height=inputHeight(field);HWND edit;
            if(!field.choices.empty()){
                edit=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|(field.editableChoices?CBS_DROPDOWN:CBS_DROPDOWNLIST)|WS_VSCROLL|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,230,y,field.scripts?330:430,200,hwnd,(HMENU)(INT_PTR)(100+i),nullptr,nullptr);
                for(auto& c:field.choices)SendMessageW(edit,CB_ADDSTRING,0,(LPARAM)c.c_str());
                if(field.editableChoices)SetWindowTextW(edit,wide(field.value).c_str());else SendMessageW(edit,CB_SETCURSEL,std::stoi(field.value),0);
            }else edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",wide(field.value).c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|(field.multiline?ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL:ES_AUTOHSCROLL),230,y,field.scripts?330:field.color?385:430,height,hwnd,(HMENU)(INT_PTR)(100+i),nullptr,nullptr);
            SendMessageW(edit,WM_SETFONT,(WPARAM)f->font,TRUE);SendMessageW(edit,EM_SETLIMITTEXT,100000,0);f->edits.push_back(edit);f->rows.push_back({label,edit});
            if(field.color){HWND b=CreateWindowW(L"BUTTON",L"...",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,621,y,39,29,hwnd,(HMENU)(INT_PTR)(500+i),nullptr,nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)f->font,TRUE);f->rows.back().push_back(b);}
            if(field.choices.empty()||field.editableChoices)SetWindowSubclass(editSurface(edit),scriptEditProc,1,reinterpret_cast<DWORD_PTR>(&field));
            if(field.scripts)for(int k=0;k<2;k++){HWND b=CreateWindowW(L"BUTTON",k?L"x₂":L"x²",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,565+k*48,y,44,29,hwnd,(HMENU)(INT_PTR)((k?800:700)+i),nullptr,nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)f->font,TRUE);f->rows.back().push_back(b);}
            if(f->tabs.empty())y+=rowHeight(field);
        }
        if(f->tab){layoutFormTabs(*f);y=f->contentBottom;}
        if(f->illustration){f->picture={16,y,660,y+124};y+=132;}
        if(f->preview){f->error=CreateWindowW(L"STATIC",L"올바른 입력은 그림에 즉시 반영됩니다.",WS_CHILD|WS_VISIBLE,16,y,644,40,hwnd,nullptr,nullptr,nullptr);SendMessageW(f->error,WM_SETFONT,(WPARAM)f->font,TRUE);y+=43;}
        for(int i=0;i<2;i++){HWND b=CreateWindowW(L"BUTTON",i?L"취소":L"적용",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,450+i*110,y+4,100,32,hwnd,(HMENU)(INT_PTR)(i?IDCANCEL:IDOK),nullptr,nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)f->font,TRUE);if(!i)f->apply=b;}
        return 0;
    }
    if(!f)return DefWindowProcW(hwnd,msg,wp,lp);
    if(msg==WM_MOUSEWHEEL){
        const POINT cursor{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        for(size_t i=0;i<f->edits.size();++i){
            auto input=f->edits[i];RECT bounds{};
            if(!f->fields[i].stepValue||!IsWindowVisible(input)||!GetWindowRect(input,&bounds)||!PtInRect(&bounds,cursor))continue;
            if(f->wheelInput!=input){f->wheelInput=input;f->wheelDelta=0;}
            f->wheelDelta+=GET_WHEEL_DELTA_WPARAM(wp);
            const int steps=f->wheelDelta/WHEEL_DELTA;f->wheelDelta%=WHEEL_DELTA;
            if(steps)stepEdit(input,f->fields[i],steps);
            return 1;
        }
        f->wheelInput=nullptr;
        return 0;
    }
    if(msg==WM_PAINT&&f->illustration){
        PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);
        try{f->illustration(dc,f->picture,read(*f));}catch(const std::exception&){}
        EndPaint(hwnd,&ps);return 0;
    }
    if(msg==WM_NOTIFY&&f->tab&&reinterpret_cast<NMHDR*>(lp)->hwndFrom==f->tab&&
        reinterpret_cast<NMHDR*>(lp)->code==TCN_SELCHANGE){layoutFormTabs(*f);return 0;}
    if(msg==DM_GETDEFID)return MAKELRESULT(IDOK,DC_HASDEFID);
    if(msg==WM_ERASEBKGND){RECT r;GetClientRect(hwnd,&r);FillRect((HDC)wp,&r,f->colors.panel);return 1;}
    if(msg==WM_CTLCOLORSTATIC||msg==WM_CTLCOLOREDIT||msg==WM_CTLCOLORLISTBOX)return f->colors.control(msg,(HDC)wp);
    if(msg==WM_MEASUREITEM){reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->itemHeight=25;return TRUE;}
    if(msg==WM_DRAWITEM){const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);f->colors.draw(item,item.CtlID==IDOK);return TRUE;}
    if(msg==WM_COMMAND){int id=LOWORD(wp);
        if((id>=700&&id<int(700+f->fields.size()))||(id>=800&&id<int(800+f->fields.size()))){size_t i=size_t(id-(id>=800?800:700));toggleScript(editSurface(f->edits[i]),id<800);return 0;}
        if(id>=500&&id<int(500+f->fields.size())){
            size_t i=size_t(id-500);static COLORREF custom[16]={};CHOOSECOLORW c{};c.lStructSize=sizeof(c);c.hwndOwner=hwnd;c.lpCustColors=custom;c.Flags=CC_FULLOPEN|CC_RGBINIT;
            try{auto value=read(*f)[i];unsigned rgb=std::stoul(value.substr(1),nullptr,16);c.rgbResult=RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255);}catch(...){}
            if(ChooseColorW(&c)){char value[8];sprintf_s(value,"#%02x%02x%02x",GetRValue(c.rgbResult),GetGValue(c.rgbResult),GetBValue(c.rgbResult));SetWindowTextW(f->edits[i],wide(value).c_str());}return 0;
        }
        if(id==IDOK||id==IDCANCEL){if(id==IDOK){if(!preview(*f))return 0;f->result=read(*f);f->ok=true;}f->done=true;PostMessageW(hwnd,WM_NULL,0,0);return 0;}
        if(f->apply&&f->edits.size()==f->fields.size()&&(HIWORD(wp)==EN_CHANGE||HIWORD(wp)==CBN_SELCHANGE||HIWORD(wp)==CBN_EDITCHANGE)){
            preview(*f);
            if(f->illustration)InvalidateRect(hwnd,&f->picture,TRUE);
        }
    }
    if(msg==WM_CLOSE){f->done=true;PostMessageW(hwnd,WM_NULL,0,0);return 0;}return DefWindowProcW(hwnd,msg,wp,lp);
}
std::optional<std::vector<std::string>> form(HWND parent,const std::wstring& title,const std::vector<Field>& fields,const Preview& callback,const FormTabs& tabs,
    const std::function<void(HDC,const RECT&,const std::vector<std::string>&)>& illustration){
    static bool registered=false;if(!registered){WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawForm";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&wc);registered=true;}
    Form f;f.fields=fields;f.preview=callback;f.tabs=tabs;f.illustration=illustration;
    if(tabs.empty())for(auto& v:fields)f.contentBottom+=rowHeight(v);
    else{
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TAB_CLASSES};InitCommonControlsEx(&controls);
        f.contentBottom=52;
        for(const auto& group:tabs){
            int y=52;for(size_t i:group.second)y+=rowHeight(fields.at(i));
            f.contentBottom=std::max(f.contentBottom,y);
        }
    }
    const int height=92+f.contentBottom+(callback?43:0)+(illustration?132:0);
    const RECT r=ownedWindowRect(parent,700,height,bool(callback));
    HWND hwnd=CreateWindowExW(0,L"YoungDrawForm",title.c_str(),WS_CAPTION|WS_SYSMENU|WS_POPUP|WS_CLIPCHILDREN,r.left,r.top,700,height,parent,nullptr,GetModuleHandleW(nullptr),&f);
    if(!hwnd)return {};
    const auto previousFocus=GetFocus();
    appearance::titlebar(hwnd);EnableWindow(parent,FALSE);ShowWindow(hwnd,SW_SHOW);
    SetFocus(f.tab?f.tab:f.edits.empty()?f.apply:f.edits.front());MSG msg{};while(!f.done&&GetMessageW(&msg,nullptr,0,0)>0){const bool editKey=msg.message==WM_KEYDOWN&&
            (msg.wParam==VK_HOME||msg.wParam==VK_END||(msg.wParam=='A'&&(GetKeyState(VK_CONTROL)&0x8000)))&&
            GetWindowSubclass(msg.hwnd,scriptEditProc,1,nullptr);
        if(msg.message==WM_MOUSEWHEEL&&SendMessageW(hwnd,msg.message,msg.wParam,msg.lParam))continue;
        if(editKey||!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    EnableWindow(parent,TRUE);DestroyWindow(hwnd);if(f.font)DeleteObject(f.font);SetActiveWindow(parent);
    if(previousFocus&&(previousFocus==parent||IsChild(parent,previousFocus)))SetFocus(previousFocus);
    if(f.ok)return f.result;return {};
}
namespace {
struct SearchDialog {
    const std::vector<SearchItem>& entries;
    const wchar_t* action;
    bool showDetails;
    HWND query=nullptr,list=nullptr,details=nullptr;
    std::vector<size_t> matches;
    std::optional<size_t> result;
    bool done=false;

    void selection(HWND hwnd){
        const auto row=SendMessageW(list,LB_GETCURSEL,0,0);
        const bool valid=row!=LB_ERR;
        EnableWindow(GetDlgItem(hwnd,IDOK),valid);
        if(details)SetWindowTextW(details,valid?entries[matches[row]].detail.c_str():L"");
    }
    void filter(HWND hwnd){
        const int length=GetWindowTextLengthW(query);
        std::wstring input(length+1,L'\0');
        input.resize(GetWindowTextW(query,input.data(),length+1));
        CharLowerBuffW(input.data(),DWORD(input.size()));
        matches.clear();SendMessageW(list,LB_RESETCONTENT,0,0);
        for(size_t i=0;i<entries.size();++i){
            auto value=entries[i].title+L" "+entries[i].detail;
            CharLowerBuffW(value.data(),DWORD(value.size()));
            if(value.find(input)==std::wstring::npos)continue;
            matches.push_back(i);
            SendMessageW(list,LB_ADDSTRING,0,LPARAM(entries[i].title.c_str()));
        }
        SendMessageW(list,LB_SETCURSEL,0,0);selection(hwnd);
    }
    static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
        auto* s=reinterpret_cast<SearchDialog*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(msg==WM_CREATE){ui::enableButtonHelp();
            s=static_cast<SearchDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));
            auto control=[&](const wchar_t* cls,const wchar_t* text,int id,int x,int y,int w,int h,DWORD flags,DWORD ex=0){
                auto c=CreateWindowExW(ex,cls,text,WS_CHILD|WS_VISIBLE|flags,x,y,w,h,hwnd,HMENU(INT_PTR(id)),nullptr,nullptr);
                SendMessageW(c,WM_SETFONT,WPARAM(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
                return c;
            };
            s->query=control(L"EDIT",L"",10,14,14,608,27,WS_TABSTOP|ES_AUTOHSCROLL,WS_EX_CLIENTEDGE);
            SendMessageW(s->query,EM_SETCUEBANNER,TRUE,LPARAM(L"검색어를 입력하세요"));
            s->list=control(L"LISTBOX",L"",11,14,50,608,s->showDetails?170:252,
                WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,WS_EX_CLIENTEDGE);
            SendMessageW(s->list,LB_SETITEMHEIGHT,0,22);
            if(s->showDetails)s->details=control(L"EDIT",L"",12,14,227,608,75,
                WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY,WS_EX_CLIENTEDGE);
            control(L"BUTTON",s->action,IDOK,414,312,100,29,WS_TABSTOP|BS_OWNERDRAW);
            control(L"BUTTON",L"닫기",IDCANCEL,522,312,100,29,WS_TABSTOP|BS_OWNERDRAW);
            s->filter(hwnd);return 0;
        }
        if(!s)return DefWindowProcW(hwnd,msg,wp,lp);
        if(msg==DM_GETDEFID)return MAKELRESULT(IDOK,DC_HASDEFID);
        if(msg==WM_CLOSE){s->done=true;return 0;}
        if(msg==WM_COMMAND){
            const auto id=LOWORD(wp),code=HIWORD(wp);
            if(id==10&&code==EN_CHANGE)s->filter(hwnd);
            if(id==11&&code==LBN_SELCHANGE)s->selection(hwnd);
            if(id==IDOK||(id==11&&code==LBN_DBLCLK)){
                auto row=SendMessageW(s->list,LB_GETCURSEL,0,0);
                if(row!=LB_ERR){s->result=s->matches[row];s->done=true;}
            }
            if(id==IDCANCEL)s->done=true;
            return 0;
        }
        return DefWindowProcW(hwnd,msg,wp,lp);
    }
};
}

std::optional<size_t> search(HWND owner,const std::wstring& title,const std::vector<SearchItem>& entries,
    const wchar_t* action,bool showDetails){
    static const auto registered=[]{
        WNDCLASSW wc{};wc.lpfnWndProc=SearchDialog::proc;wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpszClassName=L"YoungDrawSearch";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.hbrBackground=HBRUSH(COLOR_BTNFACE+1);return RegisterClassW(&wc);
    }();
    if(!registered)return {};
    SearchDialog state{entries,action,showDetails};
    const RECT rect=ownedWindowRect(owner,652,393);
    auto hwnd=CreateWindowExW(0,L"YoungDrawSearch",title.c_str(),
        WS_CAPTION|WS_SYSMENU|WS_POPUP|WS_CLIPCHILDREN,rect.left,rect.top,652,393,owner,nullptr,nullptr,&state);
    if(!hwnd)return {};
    appearance::titlebar(hwnd);EnableWindow(owner,FALSE);ShowWindow(hwnd,SW_SHOW);SetFocus(state.query);
    MSG msg{};BOOL got=1;
    while(!state.done&&(got=GetMessageW(&msg,nullptr,0,0))>0){
        if(msg.message==WM_KEYDOWN&&msg.hwnd==state.query&&msg.wParam==VK_DOWN)SetFocus(state.list);
        else if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE)state.done=true;
        else if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    EnableWindow(owner,TRUE);DestroyWindow(hwnd);SetActiveWindow(owner);
    if(!got)PostQuitMessage(int(msg.wParam));
    return state.result;
}

}

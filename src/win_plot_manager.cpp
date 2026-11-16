#include "win_button_help.h"
#include "win_plot_manager.h"
#include "win_editor_state.h"
#include "win_editor_commands.h"
#include "win_editor_plot.h"
#include "win_editor_chrome.h"
#include "win_editor_render.h"
#include "win_editor_structure.h"
#include "win_dialogs.h"
#include "win_dialog_style.h"
#include "win_ui.h"
#include "cdxml.h"
#include <commctrl.h>

namespace yd::editor {
HWND plotManagerWindow=nullptr;
namespace {
constexpr int tabFirst=4000,slotControl=4200,sideControl=4201;
constexpr int editStructure=4300,pasteStructure=4301,fileStructure=4302,sizeStructure=4303,removeStructure=4304;
struct Card{int action,icon;const wchar_t* label;const wchar_t* detail;};
const std::vector<std::vector<Card>> pages={
    {{131,131,L"데이터 표",L"상태·에너지 입력"},{132,132,L"경로 추가",L"비교 경로 만들기"},{133,133,L"경로 스타일",L"이름·색·선·표식"},{changePlotStyleCommand,130,L"그래프 형태",L"곡선·직선·준위"},{142,142,L"구간별 색상",L"준위·연결선 색"},{135,135,L"양쪽 분기",L"좌우 경로 구성"}},
    {{137,137,L"제목·축 설정",L"표시·범위·단위·눈금"},{plotAxisLengthsCommand,239,L"축 길이",L"가로·세로 길이"},{plotFontCommand,253,L"글꼴·스타일",L"글꼴·굵게·기울임"},{240,240,L"상태 이름 크기",L"축 아래 이름"},{241,241,L"데이터 값 크기",L"에너지 숫자"},{plotLegendSizeCommand,249,L"범례 크기",L"경로 이름 글자"},{246,246,L"Y축 제목 간격",L"제목과 축 사이 여백"}},
    {{139,139,L"반응 이름·화살표",L"범위·글자·화살촉"},{136,136,L"화살표 자석 배치",L"캔버스에서 두 점 선택"},{plotAnnotationsCommand,236,L"TS·이름 위치",L"생략·갭·라벨 위치"},{addEnergyGapCommand,237,L"에너지 갭 추가",L"캔버스에 지시선 배치"},{151,151,L"스핀 교차점",L"교차 에너지·라벨"},{138,138,L"출판용 정돈",L"비율·배치 자동 정리"}},
    {{editStructure,186,L"구조 독립 편집",L"선택 상태의 구조만"},{pasteStructure,123,L"구조 붙여넣기",L"클립보드에서 삽입"},{fileStructure,124,L"구조 파일 삽입",L"CDXML·이미지 파일"},{sizeStructure,165,L"구조 크기",L"선택 구조 확대·축소"},{removeStructure,144,L"구조 제거",L"선택 영역만 비우기"}},
    {{242,242,L"X축 굵기",L"가로축 획"},{243,243,L"Y축 굵기",L"세로축 획"},{244,244,L"곡선 굵기",L"경로 선 굵기"},{245,245,L"그림 선 굵기",L"삽입 구조의 획"},{plotWaveWidthCommand,250,L"물결 굵기",L"생략 표시 획"},{plotWaveGapCommand,251,L"물결·곡선 간격",L"본선 절단 여백"}}
};
struct Manager{
    HWND window=nullptr,tooltip=nullptr;int owner=0,page=0;bool busy=false;
    HFONT font=nullptr;ui::DialogColors colors;std::vector<HWND> cards;RectF preview;
    ~Manager(){if(font)DeleteObject(font);}
    EnergyPlot* graph(){return plot(owner);}
    HWND button(int id,const wchar_t* label){auto h=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,(HMENU)INT_PTR(id),GetModuleHandleW(nullptr),nullptr);SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);return h;}
    void targets(){
        auto combo=GetDlgItem(window,slotControl);int selected=int(SendMessageW(combo,CB_GETCURSEL,0,0));SendMessageW(combo,CB_RESETCONTENT,0,0);
        if(auto* p=graph())for(size_t i=0;i<p->states.size();i++){auto label=std::to_wstring(i+1)+L" · "+ui::wide(p->states[i].label);SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)label.c_str());}
        auto* p=graph();int count=p?int(p->states.size()):0;SendMessageW(combo,CB_SETCURSEL,count?std::clamp(selected,0,count-1):-1,0);
    }
    void layout(){
        RECT client{};GetClientRect(window,&client);const int width=client.right,height=client.bottom;
        const int panel=std::clamp(width/2,390,520),gap=10,cardWidth=(panel-40-2*gap)/3;
        const int tabWidth=(width-40-4*6)/5;
        for(int i=0;i<5;i++)MoveWindow(GetDlgItem(window,tabFirst+i),20+i*(tabWidth+6),72,tabWidth,34,TRUE);
        MoveWindow(GetDlgItem(window,slotControl),20,122,panel-182,28,TRUE);MoveWindow(GetDlgItem(window,sideControl),panel-152,122,132,28,TRUE);
        ShowWindow(GetDlgItem(window,slotControl),page==3?SW_SHOW:SW_HIDE);ShowWindow(GetDlgItem(window,sideControl),page==3?SW_SHOW:SW_HIDE);
        const int start=page==3?164:132;
        for(size_t i=0;i<cards.size();i++)MoveWindow(cards[i],20+int(i%3)*(cardWidth+gap),start+int(i/3)*106,cardWidth,96,TRUE);
        preview={float(panel+12),142.f,float(std::max(120,width-panel-32)),float(std::max(100,height-218))};
        MoveWindow(GetDlgItem(window,104),20,height-52,104,32,TRUE);MoveWindow(GetDlgItem(window,105),132,height-52,104,32,TRUE);MoveWindow(GetDlgItem(window,IDCANCEL),width-124,height-52,104,32,TRUE);
        InvalidateRect(window,nullptr,FALSE);
    }
    void selectPage(int selected){
        page=selected;for(auto h:cards)DestroyWindow(h);cards.clear();
        if(tooltip)DestroyWindow(tooltip);
        tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,window,nullptr,GetModuleHandleW(nullptr),nullptr);SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,340);
        for(const auto& card:pages[size_t(page)]){auto h=button(card.action,card.label);cards.push_back(h);TOOLINFOW tip{sizeof(tip)};tip.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tip.hwnd=window;tip.uId=UINT_PTR(h);tip.lpszText=const_cast<wchar_t*>(card.detail);SendMessageW(tooltip,TTM_ADDTOOLW,0,(LPARAM)&tip);}
        targets();layout();ui::attachWindowHelp(window);for(int i=0;i<5;i++)InvalidateRect(GetDlgItem(window,tabFirst+i),nullptr,FALSE);
    }
    std::wstring value(int id){auto* p=graph();if(!p)return L"그래프 없음";
        if(id==plotFontCommand)return ui::wide(p->fontFamily);
        double n=0;bool numeric=true,percent=false;
        switch(id){case 240:n=p->stateFontSize;break;case 241:n=p->valueFontSize;break;case 242:n=p->xAxisWidth;break;case 243:n=p->yAxisWidth;break;case 244:n=p->curveWidth;break;case 245:n=p->structureInkScale*100;percent=true;break;case 246:n=p->yTitleGap;break;case plotLegendSizeCommand:n=p->legendFontSize;break;case plotWaveWidthCommand:n=p->waveWidth;break;case plotWaveGapCommand:n=p->waveGap;break;default:numeric=false;}
        if(!numeric)return {};if((id==plotWaveGapCommand&&n<0)||(id!=plotWaveGapCommand&&n==0))return L"자동";
        return ui::wide(number(n))+(percent?L"%":L" pt");
    }
    void draw(const DRAWITEMSTRUCT& item){
        Graphics g(item.hDC);g.SetSmoothingMode(SmoothingModeAntiAlias);const auto& palette=appearance::palette();
        RectF box(float(item.rcItem.left),float(item.rcItem.top),float(item.rcItem.right-item.rcItem.left),float(item.rcItem.bottom-item.rcItem.top));
        const bool tab=item.CtlID>=tabFirst&&item.CtlID<tabFirst+5;const bool active=tab&&int(item.CtlID)-tabFirst==page;
        appearance::rounded(g,box,8,active?palette.accent:item.itemState&ODS_SELECTED?palette.hover:palette.raised);
        const auto color=active?palette.onAccent:palette.text;
        const Card* card=nullptr;for(auto& entry:pages[size_t(page)])if(entry.action==int(item.CtlID))card=&entry;
        if(card){icon(g,card->icon,{box.X+(box.Width-32)/2,box.Y+10,32,32},color);ui::drawLabel(g,card->label,{box.X+4,box.Y+49,box.Width-8,22},13.f,color,true,true);auto current=value(card->action);ui::drawLabel(g,current.empty()?card->detail:current,{box.X+4,box.Y+73,box.Width-8,17},11.f,palette.muted,false,true);}
        else{wchar_t label[128]{};GetWindowTextW(item.hwndItem,label,128);ui::drawLabel(g,label,box,13.f,color,active,true);}
        if(item.itemState&ODS_FOCUS){RECT focus=item.rcItem;InflateRect(&focus,-4,-4);DrawFocusRect(item.hDC,&focus);}
    }
    void paint(HDC dc){
        Graphics g(dc);g.SetSmoothingMode(SmoothingModeAntiAlias);const auto& palette=appearance::palette();RECT client{};GetClientRect(window,&client);SolidBrush background(palette.panel);g.FillRectangle(&background,0,0,client.right,client.bottom);
        text(g,L"그래프 관리",20,16,21,palette.text,true);text(g,L"아이콘을 눌러 편집 · 변경 즉시 적용 · 실행 취소로 복원",20,46,12,palette.muted);
        text(g,L"선택 그래프 미리보기",preview.X,116,12,palette.muted,true);appearance::rounded(g,preview,8,Color(255,255,255,255));
        if(graph())try{auto sc=makeScene(doc.subset({owner}),measure);auto b=sc.bounds;double scale=std::min((preview.Width-32)/std::max(1.,b.w),(preview.Height-32)/std::max(1.,b.h));auto saved=g.Save();g.SetClip(preview);g.TranslateTransform(preview.X+(preview.Width-float(b.w*scale))/2-float(b.x*scale),preview.Y+(preview.Height-float(b.h*scale))/2-float(b.y*scale));g.ScaleTransform(float(scale),float(scale));render(g,sc);g.Restore(saved);}catch(const std::exception&){text(g,L"미리보기를 표시할 수 없습니다.",preview.X+16,preview.Y+24,12,palette.muted);}
        else text(g,L"그래프가 삭제되었습니다. 창을 닫고 다시 선택하세요.",preview.X+12,preview.Y+24,12,palette.muted);
        text(g,page==3?L"상태·위/아래 영역 선택 → 구조를 별도로 편집합니다.":L"구조 편집과 캔버스 배치는 각 도구의 안내를 따릅니다.",20,float(client.bottom-80),11,palette.muted);
    }
    void act(int action){
        if(action==104||action==105){command(action);targets();InvalidateRect(window,nullptr,FALSE);for(auto h:cards)InvalidateRect(h,nullptr,FALSE);return;}
        if(!graph())throw std::runtime_error("편집할 그래프가 없습니다.");if(doc.isLocked(owner))throw std::runtime_error("그래프 잠금을 해제한 뒤 편집하세요.");
        selection={owner};
        if(action>=editStructure&&action<=removeStructure){
            const int index=int(SendDlgItemMessageW(window,slotControl,CB_GETCURSEL,0,0));if(index<0||size_t(index)>=graph()->states.size())throw std::runtime_error("구조를 넣을 상태를 먼저 선택하세요.");
            targetContainer=owner;targetSlot=size_t(index);targetAbove=SendDlgItemMessageW(window,sideControl,CB_GETCURSEL,0,0)==0;
            if(action==editStructure)editTargetStructure();else if(action==pasteStructure)fillTarget(false);else if(action==fileStructure)fillTarget(true);else if(action==sizeStructure)resizeSelected();
            else{Document before=doc,candidate=doc;Document empty;replaceSlotStructure(candidate,empty,owner,targetSlot,targetAbove);if(toNative(candidate)!=toNative(before)){history.checkpoint(before);doc=std::move(candidate);changed();}}
        }else command(action);
        targets();InvalidateRect(window,nullptr,FALSE);for(auto h:cards)InvalidateRect(h,nullptr,FALSE);
    }
};
LRESULT CALLBACK managerProc(HWND h,UINT message,WPARAM wp,LPARAM lp){
    auto* self=reinterpret_cast<Manager*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Manager*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(h,message,wp,lp);
    switch(message){
    case WM_CREATE:{ui::enableButtonHelp();self->font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,ui::interfaceFont());const wchar_t* tabs[]={L"데이터·경로",L"축·글자",L"반응·주석",L"구조",L"굵기·물결"};for(int i=0;i<5;i++)self->button(tabFirst+i,tabs[i]);self->button(104,L"실행 취소");self->button(105,L"다시 실행");self->button(IDCANCEL,L"닫기");
        auto slot=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,0,0,100,300,h,(HMENU)slotControl,GetModuleHandleW(nullptr),nullptr);auto side=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST,0,0,100,140,h,(HMENU)sideControl,GetModuleHandleW(nullptr),nullptr);SendMessageW(slot,WM_SETFONT,(WPARAM)self->font,TRUE);SendMessageW(side,WM_SETFONT,(WPARAM)self->font,TRUE);SendMessageW(side,CB_ADDSTRING,0,(LPARAM)L"상태 위 구조");SendMessageW(side,CB_ADDSTRING,0,(LPARAM)L"상태 아래 구조");SendMessageW(side,CB_SETCURSEL,0,0);self->selectPage(0);appearance::titlebar(h);return 0;}
    case WM_SIZE:self->layout();return 0;
    case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={900,640};return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PRINTCLIENT:self->paint(reinterpret_cast<HDC>(wp));return 0;
    case WM_PAINT:{PAINTSTRUCT paint;auto dc=BeginPaint(h,&paint);self->paint(dc);EndPaint(h,&paint);return 0;}
    case WM_DRAWITEM:self->draw(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:return self->colors.control(message,reinterpret_cast<HDC>(wp));
    case WM_COMMAND:{int action=LOWORD(wp);if(action==IDCANCEL){DestroyWindow(h);return 0;}if(action>=tabFirst&&action<tabFirst+5){self->selectPage(action-tabFirst);return 0;}if(action==slotControl||action==sideControl)return 0;if(self->busy)return 0;
        self->busy=true;EnableWindow(h,FALSE);try{self->act(action);}catch(const std::exception& error){MessageBoxW(h,ui::wide(error.what()).c_str(),L"그래프 관리",MB_OK|MB_ICONINFORMATION);}self->busy=false;EnableWindow(h,TRUE);
        if(action==136||action==addEnergyGapCommand){DestroyWindow(h);SetForegroundWindow(win);SetFocus(win);}else if(IsWindowVisible(h)){SetForegroundWindow(h);SetFocus(h);}return 0;}
    case WM_CLOSE:DestroyWindow(h);return 0;
    case WM_NCDESTROY:if(plotManagerWindow==h)plotManagerWindow=nullptr;SetWindowLongPtrW(h,GWLP_USERDATA,0);delete self;return DefWindowProcW(h,message,wp,lp);
    }
    return DefWindowProcW(h,message,wp,lp);
}
}
void managePlot(int id,bool visible){
    if(!plot(id))throw std::runtime_error("관리할 그래프를 선택하세요.");
    if(plotManagerWindow){auto* manager=reinterpret_cast<Manager*>(GetWindowLongPtrW(plotManagerWindow,GWLP_USERDATA));manager->owner=id;manager->targets();InvalidateRect(plotManagerWindow,nullptr,FALSE);if(visible){ShowWindow(plotManagerWindow,SW_SHOWNORMAL);SetForegroundWindow(plotManagerWindow);}return;}
    WNDCLASSW wc{};wc.lpfnWndProc=managerProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawPlotManager";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    auto* state=new Manager;state->owner=id;auto rect=ui::ownedWindowRect(win,1120,780);
    auto h=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"그래프 관리",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,win,nullptr,wc.hInstance,state);
    if(!h){delete state;throw std::runtime_error("그래프 관리 창을 열 수 없습니다.");}plotManagerWindow=h;if(visible){ShowWindow(h,SW_SHOW);UpdateWindow(h);}
}
void closePlotManager(){if(plotManagerWindow)DestroyWindow(plotManagerWindow);}
void refreshPlotManager(){if(plotManagerWindow)InvalidateRect(plotManagerWindow,nullptr,FALSE);}
bool handlePlotManagerMessage(MSG& message){if(!plotManagerWindow||!IsWindowEnabled(plotManagerWindow)||(message.hwnd!=plotManagerWindow&&!IsChild(plotManagerWindow,message.hwnd)))return false;if(message.message==WM_KEYDOWN&&(GetKeyState(VK_CONTROL)&0x8000)&&message.wParam=='Z'){SendMessageW(plotManagerWindow,WM_COMMAND,(GetKeyState(VK_SHIFT)&0x8000)?105:104,0);return true;}return IsDialogMessageW(plotManagerWindow,&message)!=FALSE;}
}

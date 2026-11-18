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
#include "plot_data.h"
#include <commctrl.h>
#include <windowsx.h>
#include <uxtheme.h>

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
    {{242,242,L"X축 표시·굵기",L"숨김 또는 획 조절"},{243,243,L"Y축 표시·굵기",L"숨김 또는 획 조절"},{244,244,L"곡선 굵기",L"경로 선 굵기"},{245,245,L"그림 선 굵기",L"선택 후 + / - 조절"},{plotWaveWidthCommand,250,L"물결 굵기",L"선택 후 + / - 조절"},{plotWaveGapCommand,251,L"물결·곡선 간격",L"본선 절단 여백"}}
};
struct Manager{
    HWND window=nullptr;int owner=0,page=0;bool busy=false;
    HWND grid=nullptr;bool dataOpen=false;int weightAction=0;ULONGLONG transitionStart=0;int transitionOffset=0;int wheelAction=0,wheelDelta=0;std::optional<Scene> cachedScene;
    HFONT font=nullptr;ui::DialogColors colors;std::vector<HWND> cards;RectF preview;
    ~Manager(){if(font)DeleteObject(font);}
    EnergyPlot* graph(){return plot(owner);}
    HWND button(int id,const wchar_t* label){auto h=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,(HMENU)INT_PTR(id),GetModuleHandleW(nullptr),nullptr);SetWindowTheme(h,L"",L"");SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);return h;}
    void targets(){
        auto combo=GetDlgItem(window,slotControl);int selected=targetContainer==owner?int(targetSlot):int(SendMessageW(combo,CB_GETCURSEL,0,0));SendMessageW(combo,CB_RESETCONTENT,0,0);
        if(auto* p=graph())for(size_t i=0;i<p->states.size();i++){auto label=std::to_wstring(i+1)+L" · "+ui::wide(p->states[i].label);SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)label.c_str());}
        auto* p=graph();int count=p?int(p->states.size()):0;SendMessageW(combo,CB_SETCURSEL,count?std::clamp(selected,0,count-1):-1,0);
        if(targetContainer==owner)SendDlgItemMessageW(window,sideControl,CB_SETCURSEL,targetAbove?0:1,0);
    }
    bool flushGrid(){return !grid||ui::commitEmbeddedTable(grid);}
    void openData(){
        if(grid){dataOpen=true;layout();SetFocus(GetDlgItem(grid,10));return;}
        auto* p=graph();if(!p)return;EnergyPlot original=*p;
        if(original.series.empty()){EnergySeries path;path.name="Path 1";for(auto& state:original.states)path.values.push_back(state.energy);original.series.push_back(path);}
        std::vector<std::wstring> columns={L"단계명"};for(auto& path:original.series)columns.push_back(ui::wide(path.name));
        ui::Table rows;for(size_t i=0;i<original.states.size();i++){std::vector<std::string> row={original.states[i].label};for(auto& path:original.series)row.push_back(path.hasValue(i)?number(path.values[i]):"");rows.push_back(row);}
        grid=ui::embeddedTable(window,columns,rows,[this,last=rows](const ui::Table& data) mutable ->std::wstring{
            if(data==last)return {};
            try{auto* current=graph();if(!current)return L"그래프가 없습니다.";if(doc.isLocked(owner))return L"그래프 잠금을 해제하세요.";
                auto revised=energyGridData(*current,data);Document before=doc;auto old=*current;relocatePlot(old,revised);*graph()=std::move(revised);
                if(toNative(before)!=toNative(doc)){history.checkpoint(before);changed();}last=data;return {};
            }catch(const std::exception& e){return ui::wide(e.what());}
        },[](size_t column,const std::string& value)->std::wstring{return column==0||validEnergyGridValue(value)?L"":L"에너지를 숫자로 입력하세요. 빈칸은 생략됩니다.";},[this](HWND parent,size_t column)->std::optional<std::wstring>{
            if(!column||!graph())return {};auto* p=graph();if(column>p->series.size())return {};
            auto result=ui::form(window,L"경로 이름 변경",{{L"경로 이름",p->series[column-1].name,false,{},false,4,true}});if(!result)return {};
            if(!graph()||doc.isLocked(owner))return {};Document before=doc;graph()->series[column-1].name=result->front();if(toNative(before)!=toNative(doc)){history.checkpoint(before);changed();}return ui::wide(result->front());
        });
        dataOpen=true;layout();SetFocus(GetDlgItem(grid,10));ui::attachWindowHelp(window);
    }
    void layout(){
        RECT client{};GetClientRect(window,&client);const int width=client.right,height=client.bottom;
        const int panelTop=std::max(250,height-352),gap=10;
        preview={20.f,88.f,float(width-40),float(panelTop-110)};
        const int tabWidth=(width-40-4*6)/5;
        auto batch=BeginDeferWindowPos(48);
        auto place=[&](HWND h,int x,int y,int w,int ht,bool visible=true){batch=DeferWindowPos(batch,h,nullptr,x,y,w,ht,SWP_NOZORDER|SWP_NOACTIVATE|(visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));};
        for(int i=0;i<5;i++)place(GetDlgItem(window,tabFirst+i),20+i*(tabWidth+6),panelTop,tabWidth,34);
        place(GetDlgItem(window,slotControl),20,panelTop+46,width-192,28,page==3);place(GetDlgItem(window,sideControl),width-162,panelTop+46,142,28,page==3);
        const int start=panelTop+(page==3?84:46)+transitionOffset,columns=4,cardWidth=(width-40-(columns-1)*gap)/columns;
        for(auto& category:pages)for(auto& card:category){auto h=GetDlgItem(window,card.action);auto found=std::find_if(pages[size_t(page)].begin(),pages[size_t(page)].end(),[&](const Card& c){return c.action==card.action;});bool show=found!=pages[size_t(page)].end()&&!(dataOpen&&page==0);int i=show?int(found-pages[size_t(page)].begin()):0;place(h,20+(i%columns)*(cardWidth+gap),start+(i/columns)*100,cardWidth,90,show);}
        if(grid)place(grid,20,panelTop+44,width-40,height-panelTop-124,dataOpen&&page==0);
        place(GetDlgItem(window,104),20,height-46,104,30);place(GetDlgItem(window,105),132,height-46,104,30);
        place(GetDlgItem(window,4400),width-244,height-46,112,30,dataOpen&&page==0);
        place(GetDlgItem(window,IDCANCEL),width-124,height-46,104,30);
        EndDeferWindowPos(batch);InvalidateRect(window,nullptr,FALSE);
    }
    void selectPage(int selected){
        if(!flushGrid())return;
        if(page!=selected){BOOL effects=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&effects,0);transitionStart=GetTickCount64();transitionOffset=effects?8:0;if(effects)SetTimer(window,1,16,nullptr);}
        page=selected;targets();layout();for(int i=0;i<5;i++)InvalidateRect(GetDlgItem(window,tabFirst+i),nullptr,FALSE);
    }
    void initializeCards(){
        for(const auto& category:pages)for(const auto& card:category)cards.push_back(button(card.action,card.label));
        selectPage(0);ui::attachWindowHelp(window);
    }
    void stepWeight(bool increase){
        if(page==4)stepPlotAppearance(owner,weightAction,increase?1:-1);
    }
    void wheel(POINT cursor,int delta){
        if(busy||(dataOpen&&page==0))return;
        for(const auto& card:pages[size_t(page)]){
            RECT bounds{};GetWindowRect(GetDlgItem(window,card.action),&bounds);
            if(!PtInRect(&bounds,cursor))continue;
            if(wheelAction!=card.action){wheelAction=card.action;wheelDelta=0;}
            wheelDelta+=delta;
            const int steps=wheelDelta/WHEEL_DELTA;wheelDelta%=WHEEL_DELTA;
            stepPlotAppearance(owner,card.action,steps);
            return;
        }
        wheelAction=0;
    }
    std::wstring value(int id){auto* p=graph();if(!p)return L"그래프 없음";
        if(id==plotFontCommand)return ui::wide(p->fontFamily);
        double n=0;bool numeric=true,percent=false;
        switch(id){case 240:n=p->stateFontSize;break;case 241:n=p->valueFontSize;break;case 242:if(!p->showXAxis)return L"숨김";n=p->xAxisWidth;break;case 243:if(!p->showYAxis)return L"숨김";n=p->yAxisWidth;break;case 244:n=p->curveWidth;break;case 245:n=p->structureInkScale*100;percent=true;break;case 246:n=p->yTitleGap;break;case plotLegendSizeCommand:n=p->legendFontSize;break;case plotWaveWidthCommand:n=p->waveWidth;break;case plotWaveGapCommand:n=p->waveGap;break;default:numeric=false;}
        if(!numeric)return {};if((id==plotWaveGapCommand&&n<0)||(id!=plotWaveGapCommand&&n==0))return L"자동";
        return ui::wide(number(n))+(percent?L"%":L" pt");
    }
    void draw(const DRAWITEMSTRUCT& item){
        const auto& palette=appearance::palette();
        RECT client{};GetClientRect(item.hwndItem,&client);
        SetDCBrushColor(item.hDC,palette.panel.ToCOLORREF());
        FillRect(item.hDC,&client,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        Graphics g(item.hDC);g.SetSmoothingMode(SmoothingModeAntiAlias);
        RectF box(0.f,0.f,float(client.right),float(client.bottom));
        const bool tab=item.CtlID>=tabFirst&&item.CtlID<tabFirst+5;
        const bool active=(tab&&int(item.CtlID)-tabFirst==page)||(!tab&&int(item.CtlID)==weightAction);
        const bool keyboardFocus=(item.itemState&ODS_FOCUS)&&!(item.itemState&ODS_NOFOCUSRECT);
        const bool pressed=(item.itemState&ODS_SELECTED)!=0;
        const auto color=active||keyboardFocus||pressed?palette.accent:palette.text;
        const Card* card=nullptr;for(auto& entry:pages[size_t(page)])if(entry.action==int(item.CtlID))card=&entry;
        if(card){icon(g,card->icon,{box.X+(box.Width-32)/2,box.Y+6,32,32},color);ui::drawLabel(g,card->label,{box.X+4,box.Y+42,box.Width-8,22},13.f,color,true,true);auto current=value(card->action);ui::drawLabel(g,current.empty()?card->detail:current,{box.X+4,box.Y+67,box.Width-8,17},11.f,palette.muted,false,true);}
        else{wchar_t label[128]{};GetWindowTextW(item.hwndItem,label,128);ui::drawLabel(g,label,box,13.f,color,active,true);}
    }
    void paint(HDC dc){
        Graphics g(dc);g.SetSmoothingMode(SmoothingModeAntiAlias);const auto& palette=appearance::palette();RECT client{};GetClientRect(window,&client);SolidBrush background(palette.panel);g.FillRectangle(&background,0,0,client.right,client.bottom);
        text(g,L"그래프 관리",20,16,21,palette.text,true);text(g,L"아이콘을 눌러 편집 · 변경 즉시 적용 · 실행 취소로 복원",20,46,12,palette.muted);
        text(g,L"선택 그래프 미리보기",preview.X,68,12,palette.muted,true);appearance::rounded(g,preview,8,Color(255,255,255,255));
        if(graph())try{if(!cachedScene)cachedScene=makeScene(doc.subset({owner}),measure);const auto& sc=*cachedScene;auto b=sc.bounds;double scale=std::min((preview.Width-32)/std::max(1.,b.w),(preview.Height-32)/std::max(1.,b.h));auto saved=g.Save();g.SetClip(preview);g.TranslateTransform(preview.X+(preview.Width-float(b.w*scale))/2-float(b.x*scale),preview.Y+(preview.Height-float(b.h*scale))/2-float(b.y*scale));g.ScaleTransform(float(scale),float(scale));render(g,sc);g.Restore(saved);}catch(const std::exception&){text(g,L"미리보기를 표시할 수 없습니다.",preview.X+16,preview.Y+24,12,palette.muted);}
        else text(g,L"그래프가 삭제되었습니다. 창을 닫고 다시 선택하세요.",preview.X+12,preview.Y+24,12,palette.muted);
        text(g,page==4?L"크기·굵기 위에서 휠로 조절 · Ctrl+Z: 실행 취소":page==1?L"크기·간격 위에서 휠로 조절할 수 있습니다.":page==3?L"상태·위/아래 영역 선택 → 삽입된 해당 구조만 편집합니다.":L"데이터 표는 하단 패널에서 입력하고 그래프에 즉시 반영합니다.",20,float(client.bottom-70),11,palette.muted);
    }
    void act(int action){
        if(action==131){openData();return;}
        if(action==4400){if(flushGrid()){dataOpen=false;layout();}return;}
        if(!flushGrid())return;
        if(grid){DestroyWindow(grid);grid=nullptr;dataOpen=false;layout();}
        if(action==245||action==plotWaveWidthCommand)weightAction=action;
        if(action==104||action==105){command(action);targets();InvalidateRect(window,nullptr,FALSE);for(auto h:cards)InvalidateRect(h,nullptr,FALSE);return;}
        if(!graph())throw std::runtime_error("편집할 그래프가 없습니다.");if(doc.isLocked(owner))throw std::runtime_error("그래프 잠금을 해제한 뒤 편집하세요.");
        selection={owner};
        if(action>=editStructure&&action<=removeStructure){
            const int index=int(SendDlgItemMessageW(window,slotControl,CB_GETCURSEL,0,0));if(index<0||size_t(index)>=graph()->states.size())throw std::runtime_error("구조를 넣을 상태를 먼저 선택하세요.");
            targetContainer=owner;targetSlot=size_t(index);targetAbove=SendDlgItemMessageW(window,sideControl,CB_GETCURSEL,0,0)==0;
            if(action==editStructure){auto* p=graph();auto& state=p->states[targetSlot];if((targetAbove?state.above:state.below).empty()&&!(targetAbove?state.below:state.above).empty()){targetAbove=!targetAbove;SendDlgItemMessageW(window,sideControl,CB_SETCURSEL,targetAbove?0:1,0);}editTargetStructure();}else if(action==pasteStructure)fillTarget(false);else if(action==fileStructure)fillTarget(true);else if(action==sizeStructure)resizeSelected();
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
    case WM_CREATE:{ui::enableButtonHelp();self->font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,ui::interfaceFont());const wchar_t* tabs[]={L"데이터·경로",L"축·글자",L"반응·주석",L"구조",L"굵기·물결"};for(int i=0;i<5;i++)self->button(tabFirst+i,tabs[i]);self->button(104,L"실행 취소");self->button(105,L"다시 실행");self->button(IDCANCEL,L"닫기");self->button(4400,L"도구로 돌아가기");
        auto slot=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,0,0,100,300,h,(HMENU)slotControl,GetModuleHandleW(nullptr),nullptr);auto side=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST,0,0,100,140,h,(HMENU)sideControl,GetModuleHandleW(nullptr),nullptr);SendMessageW(slot,WM_SETFONT,(WPARAM)self->font,TRUE);SendMessageW(side,WM_SETFONT,(WPARAM)self->font,TRUE);SendMessageW(side,CB_ADDSTRING,0,(LPARAM)L"상태 위 구조");SendMessageW(side,CB_ADDSTRING,0,(LPARAM)L"상태 아래 구조");SendMessageW(side,CB_SETCURSEL,0,0);self->initializeCards();appearance::titlebar(h);return 0;}
    case WM_SIZE:self->layout();return 0;
    case WM_TIMER:if(wp==1){double t=std::min(1.,double(GetTickCount64()-self->transitionStart)/120.);self->transitionOffset=int(std::round(8*std::pow(1-t,3)));self->layout();if(t>=1)KillTimer(h,1);return 0;}break;
    case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={900,640};return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PRINTCLIENT:self->paint(reinterpret_cast<HDC>(wp));return 0;
    case WM_PAINT:{PAINTSTRUCT paint;auto dc=BeginPaint(h,&paint);RECT r{};GetClientRect(h,&r);auto buffer=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,std::max(1L,r.right),std::max(1L,r.bottom));auto old=SelectObject(buffer,bitmap);self->paint(buffer);BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(h,&paint);return 0;}
    case WM_DRAWITEM:self->draw(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:return self->colors.control(message,reinterpret_cast<HDC>(wp));
    case WM_COMMAND:{int action=LOWORD(wp);if(action==IDCANCEL){if(!self->flushGrid())return 0;DestroyWindow(h);return 0;}if(action>=tabFirst&&action<tabFirst+5){self->selectPage(action-tabFirst);return 0;}if(action==slotControl||action==sideControl){targetContainer=self->owner;targetSlot=size_t(SendDlgItemMessageW(h,slotControl,CB_GETCURSEL,0,0));targetAbove=SendDlgItemMessageW(h,sideControl,CB_GETCURSEL,0,0)==0;return 0;}if(self->busy)return 0;
        const auto previousFocus=GetFocus();const bool modal=action!=131&&action!=4400&&action!=104&&action!=105;self->busy=true;if(modal)EnableWindow(h,FALSE);try{self->act(action);}catch(const std::exception& error){MessageBoxW(h,ui::wide(error.what()).c_str(),L"그래프 관리",MB_OK|MB_ICONINFORMATION);}self->busy=false;if(modal)EnableWindow(h,TRUE);
        if(action==136||action==addEnergyGapCommand){DestroyWindow(h);SetForegroundWindow(win);SetFocus(win);}else if(modal&&IsWindowVisible(h)){SetActiveWindow(h);if(previousFocus&&(previousFocus==h||IsChild(h,previousFocus))&&IsWindowVisible(previousFocus))SetFocus(previousFocus);}return 0;}
    case WM_MOUSEWHEEL:self->wheel({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)},GET_WHEEL_DELTA_WPARAM(wp));return 0;
    case WM_KEYDOWN:if(wp==VK_ADD||wp==VK_OEM_PLUS||wp==VK_SUBTRACT||wp==VK_OEM_MINUS){self->stepWeight(wp==VK_ADD||wp==VK_OEM_PLUS);return 0;}break;
    case WM_CLOSE:if(!self->flushGrid())return 0;DestroyWindow(h);return 0;
    case WM_NCDESTROY:if(plotManagerWindow==h)plotManagerWindow=nullptr;SetWindowLongPtrW(h,GWLP_USERDATA,0);delete self;return DefWindowProcW(h,message,wp,lp);
    }
    return DefWindowProcW(h,message,wp,lp);
}
}
void managePlot(int id,bool visible){
    if(!plot(id))throw std::runtime_error("관리할 그래프를 선택하세요.");
    if(plotManagerWindow){auto* manager=reinterpret_cast<Manager*>(GetWindowLongPtrW(plotManagerWindow,GWLP_USERDATA));if(manager->owner!=id){if(!manager->flushGrid())return;if(manager->grid){DestroyWindow(manager->grid);manager->grid=nullptr;}manager->dataOpen=false;manager->cachedScene.reset();}manager->owner=id;manager->targets();manager->layout();InvalidateRect(plotManagerWindow,nullptr,FALSE);if(visible){ShowWindow(plotManagerWindow,SW_SHOWNORMAL);SetForegroundWindow(plotManagerWindow);}return;}
    WNDCLASSW wc{};wc.lpfnWndProc=managerProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawPlotManager";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    auto* state=new Manager;state->owner=id;auto rect=ui::ownedWindowRect(win,1120,780);
    auto h=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"그래프 관리",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,win,nullptr,wc.hInstance,state);
    if(!h){delete state;throw std::runtime_error("그래프 관리 창을 열 수 없습니다.");}plotManagerWindow=h;if(visible){ShowWindow(h,SW_SHOW);UpdateWindow(h);}
}
void closePlotManager(){if(plotManagerWindow)DestroyWindow(plotManagerWindow);}
void refreshPlotManager(){if(plotManagerWindow){auto* m=reinterpret_cast<Manager*>(GetWindowLongPtrW(plotManagerWindow,GWLP_USERDATA));if(m)m->cachedScene.reset();InvalidateRect(plotManagerWindow,nullptr,FALSE);if(m)for(auto card:m->cards)InvalidateRect(card,nullptr,FALSE);}}
bool handlePlotManagerMessage(MSG& message){if(!plotManagerWindow||!IsWindowEnabled(plotManagerWindow)||(message.hwnd!=plotManagerWindow&&!IsChild(plotManagerWindow,message.hwnd)))return false;if(message.message==WM_MOUSEWHEEL){auto* m=reinterpret_cast<Manager*>(GetWindowLongPtrW(plotManagerWindow,GWLP_USERDATA));if(!m->grid||!IsChild(m->grid,message.hwnd)){SendMessageW(plotManagerWindow,message.message,message.wParam,message.lParam);return true;}}if(message.message==WM_KEYDOWN&&(message.wParam==VK_ADD||message.wParam==VK_OEM_PLUS||message.wParam==VK_SUBTRACT||message.wParam==VK_OEM_MINUS)){auto* m=reinterpret_cast<Manager*>(GetWindowLongPtrW(plotManagerWindow,GWLP_USERDATA));if(m->page==4){m->stepWeight(message.wParam==VK_ADD||message.wParam==VK_OEM_PLUS);return true;}}if(message.message==WM_KEYDOWN&&(GetKeyState(VK_CONTROL)&0x8000)&&message.wParam=='Z'){auto* m=reinterpret_cast<Manager*>(GetWindowLongPtrW(plotManagerWindow,GWLP_USERDATA));if(m->grid&&IsChild(m->grid,message.hwnd))return false;SendMessageW(plotManagerWindow,WM_COMMAND,(GetKeyState(VK_SHIFT)&0x8000)?105:104,0);return true;}return IsDialogMessageW(plotManagerWindow,&message)!=FALSE;}
}

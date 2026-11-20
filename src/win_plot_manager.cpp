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
constexpr int tabFirst=4000,slotControl=4200,sideControl=4201,panelBack=4400,pathControl=4401;
constexpr int editStructure=4300,pasteStructure=4301,fileStructure=4302,sizeStructure=4303,removeStructure=4304;
struct Card{int action,icon;const wchar_t* label;const wchar_t* detail;int slot=-1;};
const std::vector<std::vector<Card>> pages={
    {{131,131,L"데이터 표",L"상태·에너지 입력"},{132,132,L"경로 추가",L"비교 경로 만들기"},{133,133,L"경로 스타일",L"이름·색·선·표식"},{changePlotStyleCommand,130,L"그래프 형태",L"곡선·직선·준위"},{142,142,L"구간별 색상",L"준위·연결선 색"},{135,135,L"양쪽 분기",L"좌우 경로 구성"}},
    {{137,137,L"제목·축 설정",L"표시·범위·단위·눈금"},{plotAxisLengthsCommand,239,L"축 길이",L"가로·세로 길이"},{plotFontCommand,253,L"글꼴·스타일",L"글꼴·굵게·기울임"},{240,240,L"상태 이름 크기",L"축 아래 이름"},{241,241,L"데이터 값 크기",L"에너지 숫자"},{246,246,L"Y축 제목 간격",L"제목과 축 사이 여백"},{plotLegendCommand,plotLegendCommand,L"범례 관리",L"표시·글자·위치",7}},
    {{139,139,L"반응 이름·화살표",L"범위·글자·화살촉"},{136,136,L"화살표 자석 배치",L"캔버스에서 두 점 선택"},{plotAnnotationsCommand,236,L"TS·이름 위치",L"생략·갭·라벨 위치"},{addEnergyGapCommand,237,L"에너지 갭 추가",L"캔버스에 지시선 배치"},{151,151,L"스핀 교차점",L"교차 에너지·라벨"},{138,138,L"출판용 정돈",L"비율·배치 자동 정리"}},
    {{editStructure,186,L"구조 그리기·편집",L"선택한 위·아래 영역"},{pasteStructure,123,L"구조 붙여넣기",L"클립보드에서 삽입"},{fileStructure,124,L"구조 파일 삽입",L"CDXML·이미지 파일"},{sizeStructure,165,L"구조 크기",L"선택 구조 확대·축소"},{removeStructure,144,L"구조 제거",L"선택 영역만 비우기"}},
    {{242,242,L"X축 표시·화살표",L"축선 굵기·화살촉"},{243,243,L"Y축 표시·화살표",L"축선 굵기·화살촉"},{244,244,L"곡선 굵기",L"경로 선 굵기"},{245,245,L"그림 선 굵기",L"선택 후 + / - 조절"},{plotWaveWidthCommand,250,L"물결 굵기",L"선택 후 + / - 조절"},{plotWaveGapCommand,251,L"물결·곡선 간격",L"본선 절단 여백"}}
};
struct Manager{
    HWND window=nullptr;int owner=0,page=0;bool busy=false;
    struct View {double scale;Vec origin;};
    struct LegendDrag {Vec start,offset;View view;};
    HWND grid=nullptr,form=nullptr,subpane=nullptr;
    std::vector<HWND> panes;
    std::wstring notice;
    int panel=0;
    std::optional<EnergyPlot> draft;
    std::optional<LegendDrag> legendDrag;
    int weightAction=0,wheelAction=0,wheelDelta=0;std::optional<Scene> cachedScene;
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
    void mount(HWND child){
        if(subpane&&IsWindow(subpane))ShowWindow(subpane,SW_HIDE);
        if(child)panes.push_back(child);
        else if(!panes.empty())panes.pop_back();
        subpane=panes.empty()?nullptr:panes.back();
        layout();
    }
    void runTool(int action){
        struct OwnerGuard {
            HWND owner;bool enabled;
            explicit OwnerGuard(HWND h):owner(h),enabled(IsWindowEnabled(h)!=FALSE){if(enabled)EnableWindow(owner,FALSE);}
            ~OwnerGuard(){if(enabled&&IsWindow(owner))EnableWindow(owner,TRUE);}
        } guard(win);
        ui::InlineDialogHost host(window,[this](HWND child){mount(child);});
        act(action);
    }
    void openData(){
        if(panel==131&&grid){layout();SetFocus(GetDlgItem(grid,10));return;}
        clearPanel();
        auto* p=graph();if(!p)return;EnergyPlot original=*p;
        if(original.series.empty()){EnergySeries path;path.name="Path 1";for(auto& state:original.states)path.values.push_back(state.energy);original.series.push_back(path);}
        std::vector<std::wstring> columns={L"단계명"};for(auto& path:original.series)columns.push_back(ui::wide(path.name));
        ui::Table rows;for(size_t i=0;i<original.states.size();i++){std::vector<std::string> row={original.states[i].label};for(auto& path:original.series)row.push_back(path.hasValue(i)?energyCellText(path,i,number(path.values[i])):"");rows.push_back(row);}
        grid=ui::embeddedTable(window,columns,rows,[this,last=rows](const ui::Table& data) mutable ->std::wstring{
            if(data==last)return {};
            try{auto* current=graph();if(!current)return L"그래프가 없습니다.";if(doc.isLocked(owner))return L"그래프 잠금을 해제하세요.";
                auto revised=energyGridData(*current,data);Document before=doc;auto old=*current;relocatePlot(old,revised);*graph()=std::move(revised);
                if(toNative(before)!=toNative(doc)){history.checkpoint(before);changed();}last=data;return {};
            }catch(const std::exception& e){return ui::wide(e.what());}
        },[](size_t column,const std::string& value)->std::wstring{return column==0||validEnergyGridValue(value)?L"":L"에너지를 숫자로 입력하세요. 빈칸은 생략됩니다.";},[this](HWND,size_t column)->std::optional<std::wstring>{
            if(!column||!graph())return {};auto* p=graph();if(column>p->series.size())return {};
            ui::InlineDialogHost host(window,[this](HWND child){mount(child);});
            auto result=ui::form(window,L"경로 이름 변경",{{L"경로 이름",p->series[column-1].name,false,{},false,4,true}});if(!result)return {};
            if(!graph()||doc.isLocked(owner))return {};Document before=doc;graph()->series[column-1].name=result->front();if(toNative(before)!=toNative(doc)){history.checkpoint(before);changed();}return ui::wide(result->front());
        });
        if(!grid)throw std::runtime_error("데이터 표를 열 수 없습니다.");
        panel=131;layout();SetFocus(GetDlgItem(grid,10));ui::attachWindowHelp(window);
    }
    int panelPage() const {
        if(panel==plotLegendCommand)return 1;
        if(panel==139)return 2;
        return 0;
    }
    bool panelVisible() const {return panel&&page==panelPage();}
    void clearPanel(){
        if(legendDrag){legendDrag.reset();if(GetCapture()==window)ReleaseCapture();}
        if(grid){DestroyWindow(grid);grid=nullptr;}
        if(form){DestroyWindow(form);form=nullptr;}
        panel=0;draft.reset();cachedScene.reset();
    }
    void openStages(){
        const auto* p=graph();
        ui::Table rows;
        const auto stages=p->stages.empty()?defaultMechanismStages():p->stages;
        for(const auto& stage:stages)rows.push_back({stage.label,number(stage.start),number(stage.end),
            number(stage.fontSize),number(stage.arrowWidth),number(stage.arrowHeadSize)});
        grid=ui::embeddedStageTable(window,rows,[this,last=rows](const ui::Table& data) mutable ->std::wstring{
            if(data==last)return {};
            try{
                auto* current=graph();
                if(!current||doc.isLocked(owner))return L"편집할 그래프를 확인하세요.";
                auto stages=mechanismStageData(data);
                auto revised=*current;
                revised.stages=std::move(stages);
                Document before=doc;
                relocatePlot(*current,revised);
                *current=std::move(revised);
                if(toNative(before)!=toNative(doc)){history.checkpoint(before);changed();}
                last=data;
                return {};
            }catch(const std::exception& e){return ui::wide(e.what());}
        });
        if(!grid)throw std::runtime_error("반응 이름 편집을 열 수 없습니다.");
        panel=139;layout();SetFocus(GetDlgItem(grid,10));ui::attachWindowHelp(window);
    }

    using PlotEdit=std::function<void(EnergyPlot&,const std::vector<std::string>&)>;
    void openForm(int action,const wchar_t* title,const std::vector<ui::Field>& fields,const PlotEdit& edit){
        auto update=[this,edit](const std::vector<std::string>& values,bool apply)->std::wstring{
            try{
                auto* p=graph();
                if(!p||doc.isLocked(owner))return L"편집할 그래프를 확인하세요.";
                auto candidate=*p;
                edit(candidate,values);
                if(apply){
                    Document before=doc;
                    *p=std::move(candidate);
                    if(toNative(before)!=toNative(doc)){history.checkpoint(before);changed();}
                }else{
                    draft=std::move(candidate);
                    cachedScene.reset();InvalidateRect(window,nullptr,FALSE);
                }
                return {};
            }catch(const std::exception& e){return ui::wide(e.what());}
        };
        form=ui::embeddedForm(window,title,fields,
            [update](const auto& values){return update(values,false);},
            [update](const auto& values){return update(values,true);},
            [this](){SendMessageW(window,WM_COMMAND,panelBack,0);},
            [this](COLORREF color){
                ui::InlineDialogHost host(window,[this](HWND child){mount(child);});
                return ui::chooseColor(window,color);
            });
        if(!form)throw std::runtime_error("관리창 편집을 열 수 없습니다.");
        panel=action;layout();SetFocus(GetDlgItem(form,100));ui::attachWindowHelp(window);
    }

    void openLegend(){
        const auto& p=*graph();
        auto fonts=ui::fontFamilies(window);
        constexpr auto commonFont=L"그래프 글꼴 설정 따름";
        fonts.insert(fonts.begin(),commonFont);
        openForm(plotLegendCommand,L"범례 관리",{
            {L"범례 표시",std::to_string(p.legendVisibility),false,{L"자동",L"표시",L"숨김"}},
            {L"글꼴",p.legendFontFamily.empty()?ui::utf8(commonFont):p.legendFontFamily,false,
                std::move(fonts),false,4,false,true},
            {L"글자 스타일",std::to_string(p.legendFace+1),false,{L"그래프 글꼴 설정 따름",L"보통",L"굵게",L"기울임",L"굵게 기울임"}},
            {L"글자 크기 (pt, 0: 자동)",number(p.legendFontSize)},
            ui::numericField(L"X 이동 (pt, 오른쪽 +)",p.legendOffset.x,-10000,10000,1),
            ui::numericField(L"Y 이동 (pt, 아래 +)",p.legendOffset.y,-10000,10000,1)
        },[commonFont](EnergyPlot& target,const auto& values){
            target.legendVisibility=std::stoi(values[0]);target.legendFace=std::stoi(values[2])-1;
            target.legendFontSize=numeric(values[3],0,72);
            if(target.legendFontSize>0&&target.legendFontSize<4)
                throw std::runtime_error("범례 글자 크기는 0(자동) 또는 4~72 pt로 입력하세요.");
            target.legendOffset={numeric(values[4],-10000,10000),numeric(values[5],-10000,10000)};
            auto family=values[1]==ui::utf8(commonFont)?std::string{}:values[1];
            if(!family.empty()&&family!=target.legendFontFamily){
                const FontFamily chosen(ui::wide(family).c_str());
                if(chosen.GetLastStatus()!=Ok)throw std::runtime_error("설치된 글꼴 이름을 선택하세요.");
            }
            target.legendFontFamily=std::move(family);
            validateEnergyAnnotations(target);
        });
    }

    static EnergySeries basePath(const EnergyPlot& plot){
        EnergySeries result;
        for(const auto& state:plot.states)result.values.push_back(state.energy);
        return result;
    }

    void openSeries(bool add,size_t index=0){
        const auto& p=*graph();
        const size_t count=p.series.size();
        if(add&&count>=100)throw std::runtime_error("경로는 최대 100개입니다.");
        EnergySeries path=count?p.series.at(index):basePath(p);
        if(add){
            path=EnergySeries{};
            path.name="Path "+std::to_string(std::max(size_t(1),count)+1);
            path.color="#b44758";
        }
        auto radius=ui::numericField(L"표식 반지름 (pt)",path.markerSize,.1,20,.5);
        radius.stepWithSigns=true;radius.stepButtons=true;
        openForm(add?132:133,add?L"경로 추가":L"경로 스타일",{
            {L"경로 이름",path.name,false,{},false,4,true},
            {L"선 색상",path.color,false,{},true},
            {L"선 종류",std::to_string(path.dash),false,{L"실선",L"파선",L"점선"}},
            {L"선 굵기 (pt)",number(path.width)},
            {L"기본 표식",std::to_string(path.marker),false,{L"없음",L"원",L"사각",L"마름모",L"삼각"}},
            radius
        },[add,index,count](EnergyPlot& target,const auto& values){
            if(target.series.size()!=count)throw std::runtime_error("경로 목록이 변경되었습니다. 이 편집을 닫고 다시 여세요.");
            if(target.series.empty())target.series.push_back(basePath(target));
            if(add){
                auto extra=basePath(target);
                extra.present=target.series.front().present;
                target.series.push_back(std::move(extra));
            }
            auto& series=add?target.series.back():target.series.at(index);
            series.name=values[0];series.color=colorValue(values[1]);
            series.dash=std::stoi(values[2]);series.width=numeric(values[3],.1,10);
            series.marker=std::stoi(values[4]);series.markerSize=numeric(values[5],.1,20);
            if(series.dash<0||series.dash>2||series.marker<0||series.marker>4)
                throw std::runtime_error("선 종류와 표식을 선택하세요.");
        });
        auto combo=GetDlgItem(window,pathControl);
        SendMessageW(combo,CB_RESETCONTENT,0,0);
        if(!add)for(const auto& entry:p.series)SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)ui::wide(entry.name).c_str());
        SendMessageW(combo,CB_SETCURSEL,index,0);
        SetWindowPos(combo,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        layout();
        if(add)SendMessageW(form,WM_COMMAND,MAKEWPARAM(100,EN_CHANGE),0);
    }

    const Scene& currentScene(){
        if(!cachedScene){
            auto copy=doc.subset({owner});
            if(panelVisible()&&draft&&!copy.plots.empty())copy.plots.front()=*draft;
            cachedScene=makeScene(copy,measure);
        }
        return *cachedScene;
    }

    View fitPreview(const Scene& scene) const {
        if(legendDrag)return legendDrag->view;
        const auto& b=scene.bounds;
        const double scale=std::max(.01,std::min((preview.Width-32)/std::max(1.,b.w),(preview.Height-32)/std::max(1.,b.h)));
        return {scale,{preview.X+(preview.Width-b.w*scale)/2-b.x*scale,
            preview.Y+(preview.Height-b.h*scale)/2-b.y*scale}};
    }

    bool startLegendDrag(Vec cursor){
        if(panel!=plotLegendCommand||!panelVisible()||!form||!graph()||doc.isLocked(owner)||
            !preview.Contains(float(cursor.x),float(cursor.y)))return false;
        const auto& scene=currentScene();
        const auto view=fitPreview(scene);
        const auto point=(cursor-view.origin)*(1/view.scale);
        const double pad=5/view.scale;
        for(const auto& item:scene.items){
            if(item.textRole!=7||item.points.empty())continue;
            const auto start=item.points.front();
            const double width=item.type==Primitive::Text?measure(item.text,item.fontSize,item.font,item.face):item.points.back().x-start.x;
            const double height=item.type==Primitive::Text?item.fontSize*1.3:0;
            if(point.x>=start.x-pad&&point.x<=start.x+width+pad&&point.y>=start.y-pad&&point.y<=start.y+height+pad){
                legendDrag=LegendDrag{cursor,draft?draft->legendOffset:graph()->legendOffset,view};
                SetCapture(window);return true;
            }
        }
        return false;
    }

    void moveLegend(Vec cursor){
        if(!legendDrag||!form)return;
        const auto delta=(cursor-legendDrag->start)*(1/legendDrag->view.scale);
        const Vec offset=legendDrag->offset+delta;
        SetDlgItemTextW(form,104,ui::wide(number(std::clamp(offset.x,-10000.,10000.))).c_str());
        SetDlgItemTextW(form,105,ui::wide(number(std::clamp(offset.y,-10000.,10000.))).c_str());
    }

    void endLegendDrag(){
        if(!legendDrag)return;
        legendDrag.reset();
        if(GetCapture()==window)ReleaseCapture();
        InvalidateRect(window,nullptr,FALSE);
    }

    void layout(){
        RECT client{};GetClientRect(window,&client);const int width=client.right,height=client.bottom;
        const bool expanded=panelVisible()||subpane;
        const bool showForm=form&&expanded&&!subpane;
        const int panelTop=std::max(180,height-460);
        const int gap=10;
        preview={20.f,88.f,float(width-40),float(panelTop-110)};
        const int tabWidth=(width-40-4*6)/5;
        auto batch=BeginDeferWindowPos(48);
        auto place=[&](HWND h,int x,int y,int w,int ht,bool visible=true){batch=DeferWindowPos(batch,h,nullptr,x,y,w,ht,SWP_NOZORDER|SWP_NOACTIVATE|(visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));};
        for(int i=0;i<5;i++){
            auto tab=GetDlgItem(window,tabFirst+i);
            place(tab,20+i*(tabWidth+6),panelTop,tabWidth,34);
            EnableWindow(tab,!subpane);
        }
        place(GetDlgItem(window,slotControl),20,panelTop+46,width-192,28,page==3&&!subpane);place(GetDlgItem(window,sideControl),width-162,panelTop+46,142,28,page==3&&!subpane);
        const int start=panelTop+(page==3?84:46),columns=4,cardWidth=(width-40-(columns-1)*gap)/columns;
        for(auto& category:pages)for(auto& card:category){auto h=GetDlgItem(window,card.action);auto found=std::find_if(pages[size_t(page)].begin(),pages[size_t(page)].end(),[&](const Card& c){return c.action==card.action;});bool show=found!=pages[size_t(page)].end()&&!expanded;int i=show?(card.slot>=0?card.slot:int(found-pages[size_t(page)].begin())):0;place(h,20+(i%columns)*(cardWidth+gap),start+(i/columns)*100,cardWidth,90,show);}
        if(grid){
            const int gridTop=panel==139?64:44;
            place(grid,20,panelTop+gridTop,width-40,height-panelTop-gridTop-56,expanded&&!subpane);
        }
        if(form)place(form,20,panelTop+44,width-40,height-panelTop-100,showForm);
        if(subpane)place(subpane,20,panelTop+43,width-40,height-panelTop-51);
        place(GetDlgItem(window,pathControl),width-190,panelTop+44,170,190,showForm&&panel==133&&graph()&&graph()->series.size()>1);
        place(GetDlgItem(window,104),20,height-46,104,30,!subpane);place(GetDlgItem(window,105),132,height-46,104,30,!subpane);
        place(GetDlgItem(window,panelBack),width-244,height-46,112,30,expanded&&!showForm&&!subpane);
        place(GetDlgItem(window,IDCANCEL),width-124,height-46,104,30,!subpane);
        EndDeferWindowPos(batch);InvalidateRect(window,nullptr,FALSE);
    }
    void selectPage(int selected){
        if(subpane||!flushGrid())return;
        page=selected;cachedScene.reset();targets();layout();for(int i=0;i<5;i++)InvalidateRect(GetDlgItem(window,tabFirst+i),nullptr,FALSE);
    }
    void initializeCards(){
        for(const auto& category:pages)for(const auto& card:category)cards.push_back(button(card.action,card.label));
        selectPage(0);ui::attachWindowHelp(window);
    }
    void stepWeight(bool increase){
        if(!subpane&&page==4)stepPlotAppearance(owner,weightAction,increase?1:-1);
    }
    void wheel(POINT cursor,int delta){
        if(busy||panelVisible())return;
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
        switch(id){case 240:n=p->stateFontSize;break;case 241:n=p->valueFontSize;break;case 242:if(!p->showXAxis)return L"숨김";n=p->xAxisWidth;break;case 243:if(!p->showYAxis)return L"숨김";n=p->yAxisWidth;break;case 244:n=p->curveWidth;break;case 245:n=p->structureInkScale*100;percent=true;break;case 246:n=p->yTitleGap;break;case plotWaveWidthCommand:n=p->waveWidth;break;case plotWaveGapCommand:n=p->waveGap;break;default:numeric=false;}
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
        text(g,L"그래프 관리",20,16,21,palette.text,true);
        text(g,panelVisible()&&panel==plotLegendCommand?L"미리보기 · 범례를 끌어 위치 이동":L"그래프 미리보기",preview.X,68,12,palette.muted,true);appearance::rounded(g,preview,8,Color(255,255,255,255));
        if(graph())try{
            const auto& sc=currentScene();
            const auto view=fitPreview(sc);
            auto saved=g.Save();
            g.SetClip(preview);
            g.TranslateTransform(float(view.origin.x),float(view.origin.y));
            g.ScaleTransform(float(view.scale),float(view.scale));
            render(g,sc);
            g.Restore(saved);
        }catch(const std::exception&){text(g,L"미리보기를 표시할 수 없습니다.",preview.X+16,preview.Y+24,12,palette.muted);}
        else text(g,L"그래프가 삭제되었습니다. 창을 닫고 다시 선택하세요.",preview.X+12,preview.Y+24,12,palette.muted);
        if(!subpane&&panelVisible()&&panel==139){
            RECT tabs{};
            GetWindowRect(GetDlgItem(window,tabFirst),&tabs);
            POINT at{tabs.left,tabs.top};
            ScreenToClient(window,&at);
            text(g,L"반응 이름·화살표",20,float(at.y+42),14,palette.text,true);
        }
        if(!notice.empty()&&!subpane)text(g,notice,20,float(client.bottom-73),13,palette.accent);
        else if(!subpane&&!panelVisible()&&page!=2)text(g,page==4?L"크기·굵기 위에서 휠로 조절 · Ctrl+Z: 실행 취소":page==1?L"크기·간격 위에서 휠로 조절할 수 있습니다.":page==3?L"상태와 위·아래 영역을 고른 뒤 구조를 그리거나 편집하세요.":L"데이터 표는 하단 패널에서 입력하고 그래프에 즉시 반영합니다.",20,float(client.bottom-70),11,palette.muted);
    }
    void act(int action){
        if(!flushGrid())return;
        if(action==panelBack){clearPanel();layout();SetFocus(GetDlgItem(window,tabFirst+page));return;}
        if(action==131){openData();return;}
        if(form||grid){clearPanel();layout();}
        if(action==245||action==plotWaveWidthCommand)weightAction=action;
        if(action==104||action==105){command(action);targets();InvalidateRect(window,nullptr,FALSE);for(auto h:cards)InvalidateRect(h,nullptr,FALSE);return;}
        if(!graph())throw std::runtime_error("편집할 그래프가 없습니다.");if(doc.isLocked(owner))throw std::runtime_error("그래프 잠금을 해제한 뒤 편집하세요.");
        selection={owner};
        if(action==137){plotSettings(owner);return;}
        if(action==139){openStages();return;}
        if(action==plotLegendCommand){openLegend();return;}
        if(action==132||action==133){openSeries(action==132);return;}
        if(action>=editStructure&&action<=removeStructure){
            const int index=int(SendDlgItemMessageW(window,slotControl,CB_GETCURSEL,0,0));if(index<0||size_t(index)>=graph()->states.size())throw std::runtime_error("구조를 넣을 상태를 먼저 선택하세요.");
            targetContainer=owner;targetSlot=size_t(index);targetAbove=SendDlgItemMessageW(window,sideControl,CB_GETCURSEL,0,0)==0;
            if(action==editStructure){editTargetStructure();}else if(action==pasteStructure)fillTarget(false);else if(action==fileStructure)fillTarget(true);else if(action==sizeStructure)resizeSelected();
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
    case WM_CREATE:{ui::enableButtonHelp();self->font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,ui::interfaceFont());const wchar_t* tabs[]={L"데이터·경로",L"축·글자",L"반응·주석",L"구조",L"굵기·물결"};for(int i=0;i<5;i++)self->button(tabFirst+i,tabs[i]);self->button(104,L"실행 취소");self->button(105,L"다시 실행");self->button(IDCANCEL,L"닫기");self->button(panelBack,L"확인");
        auto path=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,
            0,0,170,180,h,reinterpret_cast<HMENU>(INT_PTR(pathControl)),nullptr,nullptr);
        SendMessageW(path,WM_SETFONT,(WPARAM)self->font,TRUE);ui::themeCombo(path);
        SetWindowPos(path,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        auto slot=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,0,0,100,300,h,reinterpret_cast<HMENU>(INT_PTR(slotControl)),GetModuleHandleW(nullptr),nullptr);auto side=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST,0,0,100,140,h,reinterpret_cast<HMENU>(INT_PTR(sideControl)),GetModuleHandleW(nullptr),nullptr);SendMessageW(slot,WM_SETFONT,(WPARAM)self->font,TRUE);SendMessageW(side,WM_SETFONT,(WPARAM)self->font,TRUE);SendMessageW(side,CB_ADDSTRING,0,(LPARAM)L"상태 위 구조");SendMessageW(side,CB_ADDSTRING,0,(LPARAM)L"상태 아래 구조");SendMessageW(side,CB_SETCURSEL,0,0);ui::themeCombo(slot);ui::themeCombo(side);self->initializeCards();appearance::titlebar(h);return 0;}
    case WM_SIZE:self->layout();return 0;
    case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={900,640};return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PRINTCLIENT:self->paint(reinterpret_cast<HDC>(wp));return 0;
    case WM_PAINT:{PAINTSTRUCT paint;auto dc=BeginPaint(h,&paint);RECT r{};GetClientRect(h,&r);auto buffer=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,std::max(1L,r.right),std::max(1L,r.bottom));auto old=SelectObject(buffer,bitmap);self->paint(buffer);BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(h,&paint);return 0;}
    case WM_DRAWITEM:self->draw(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:return self->colors.control(message,reinterpret_cast<HDC>(wp));
    case WM_LBUTTONDOWN:try{self->startLegendDrag({double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))});}catch(const std::exception&){}return 0;
    case WM_MOUSEMOVE:if(self->legendDrag)self->moveLegend({double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))});return 0;
    case WM_LBUTTONUP:self->endLegendDrag();return 0;
    case WM_CAPTURECHANGED:self->endLegendDrag();return 0;
    case WM_COMMAND:{int action=LOWORD(wp);
        if(self->subpane||self->busy)return 0;
        if(action==pathControl){
            const auto index=SendDlgItemMessageW(h,pathControl,CB_GETCURSEL,0,0);
            if(HIWORD(wp)==CBN_SELCHANGE&&index>=0&&self->panel==133&&self->graph()&&size_t(index)<self->graph()->series.size()){
                self->clearPanel();
                try{self->openSeries(false,size_t(index));SetFocus(GetDlgItem(h,pathControl));}
                catch(const std::exception& error){self->notice=ui::wide(error.what());self->layout();}
            }
            return 0;
        }
        if(action==IDCANCEL){if(!self->flushGrid())return 0;DestroyWindow(h);return 0;}if(action>=tabFirst&&action<tabFirst+5){self->selectPage(action-tabFirst);return 0;}if(action==slotControl||action==sideControl){targetContainer=self->owner;targetSlot=size_t(SendDlgItemMessageW(h,slotControl,CB_GETCURSEL,0,0));targetAbove=SendDlgItemMessageW(h,sideControl,CB_GETCURSEL,0,0)==0;return 0;}if(self->busy)return 0;
        self->busy=true;self->notice.clear();
        try{self->runTool(action);}catch(const std::exception& error){self->notice=ui::wide(error.what());InvalidateRect(h,nullptr,FALSE);}
        self->busy=false;
        if((action==136&&snapPlot)||(action==addEnergyGapCommand&&pendingEnergyGap)){
            DestroyWindow(h);SetForegroundWindow(win);SetFocus(win);
        }else if(!GetFocus()&&IsWindowVisible(h))SetFocus(GetDlgItem(h,tabFirst+self->page));
        return 0;}
    case WM_MOUSEWHEEL:self->wheel({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)},GET_WHEEL_DELTA_WPARAM(wp));return 0;
    case WM_KEYDOWN:if(wp==VK_ADD||wp==VK_OEM_PLUS||wp==VK_SUBTRACT||wp==VK_OEM_MINUS){self->stepWeight(wp==VK_ADD||wp==VK_OEM_PLUS);return 0;}break;
    case WM_CLOSE:if(self->subpane||self->busy||!self->flushGrid())return 0;DestroyWindow(h);return 0;
    case WM_NCDESTROY:if(plotManagerWindow==h)plotManagerWindow=nullptr;SetWindowLongPtrW(h,GWLP_USERDATA,0);delete self;return DefWindowProcW(h,message,wp,lp);
    }
    return DefWindowProcW(h,message,wp,lp);
}
}
void managePlot(int id,bool visible){
    if(!plot(id))throw std::runtime_error("관리할 그래프를 선택하세요.");
    if(plotManagerWindow){
        auto* manager=reinterpret_cast<Manager*>(GetWindowLongPtrW(plotManagerWindow,GWLP_USERDATA));
        if(manager->owner!=id){if(!manager->flushGrid())return;manager->clearPanel();}
        manager->owner=id;manager->targets();manager->layout();
        if(visible){
            if(IsIconic(plotManagerWindow))ShowWindow(plotManagerWindow,SW_RESTORE);
            else if(!IsWindowVisible(plotManagerWindow))ShowWindow(plotManagerWindow,SW_SHOW);
            SetForegroundWindow(plotManagerWindow);
        }
        return;
    }
    WNDCLASSW wc{};wc.lpfnWndProc=managerProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawPlotManager";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    auto* state=new Manager;state->owner=id;auto rect=ui::ownedWindowRect(win,1120,780);
    auto h=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"그래프 관리",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,win,nullptr,wc.hInstance,state);
    if(!h){delete state;throw std::runtime_error("그래프 관리 창을 열 수 없습니다.");}plotManagerWindow=h;if(visible){ShowWindow(h,SW_SHOW);UpdateWindow(h);}
}
void openPlotManagerTool(int id,int action){
    managePlot(id);
    auto* manager=reinterpret_cast<Manager*>(GetWindowLongPtrW(plotManagerWindow,GWLP_USERDATA));
    if(!manager||manager->owner!=id||!manager->flushGrid())return;
    manager->selectPage(action==139?2:(action==137||action==plotLegendCommand)?1:0);
    SendMessageW(plotManagerWindow,WM_COMMAND,action,0);
}
void closePlotManager(){if(plotManagerWindow)DestroyWindow(plotManagerWindow);}
void refreshPlotManager(){if(plotManagerWindow){auto* m=reinterpret_cast<Manager*>(GetWindowLongPtrW(plotManagerWindow,GWLP_USERDATA));if(m)m->cachedScene.reset();InvalidateRect(plotManagerWindow,nullptr,FALSE);if(m)for(auto card:m->cards)InvalidateRect(card,nullptr,FALSE);}}
bool handlePlotManagerMessage(MSG& message){
    const auto window=plotManagerWindow;
    if(!window||!IsWindowEnabled(window)||(message.hwnd!=window&&!IsChild(window,message.hwnd)))return false;
    auto* manager=reinterpret_cast<Manager*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    const bool inGrid=manager->grid&&IsChild(manager->grid,message.hwnd);
    const bool inForm=manager->form&&IsChild(manager->form,message.hwnd);
    if(message.message==WM_MOUSEWHEEL&&!inGrid){
        if(manager->form&&manager->panelVisible()&&SendMessageW(manager->form,message.message,message.wParam,message.lParam))return true;
        if(!inForm){SendMessageW(window,message.message,message.wParam,message.lParam);return true;}
    }
    if(message.message==WM_KEYDOWN){
        const auto k=message.wParam;
        if(!manager->subpane&&manager->page==4&&(k==VK_ADD||k==VK_OEM_PLUS||k==VK_SUBTRACT||k==VK_OEM_MINUS)){
            manager->stepWeight(k==VK_ADD||k==VK_OEM_PLUS);return true;
        }
        if(inForm&&(k==VK_ESCAPE||k==VK_RETURN)){
            wchar_t kind[24]{};
            GetClassNameW(message.hwnd,kind,int(std::size(kind)));
            if(k==VK_ESCAPE||wcscmp(kind,L"Edit")==0){
                SendMessageW(manager->form,WM_COMMAND,k==VK_ESCAPE?IDCANCEL:IDOK,0);return true;
            }
        }
        if((GetKeyState(VK_CONTROL)&0x8000)&&k=='Z'){
            if(inGrid||inForm)return false;
            SendMessageW(window,WM_COMMAND,(GetKeyState(VK_SHIFT)&0x8000)?105:104,0);return true;
        }
    }
    return IsDialogMessageW(window,&message)!=FALSE;
}
}

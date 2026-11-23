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
constexpr int tabFirst=4000,slotControl=4200,sideControl=4201,panelBack=4400,pathControl=4401,verticalBranch=4402,distributeStages=4403,dataFont=4404,photoExcitation=4405;
constexpr int editStructure=4300,pasteStructure=4301,fileStructure=4302,sizeStructure=4303,removeStructure=4304,structureBorder=4305;
constexpr int axesManager=4406;
int tableFontPoints=12;
struct Card{int action,icon;const wchar_t* label;const wchar_t* detail;int slot=-1;};
const std::vector<std::vector<Card>> pages={
    {{131,131,L"데이터 표",L"상태·에너지 입력"},{132,132,L"경로 추가",L"비교 경로 만들기"},{133,133,L"경로 스타일",L"이름·색·선·표식"},{142,142,L"구간별 색상",L"준위·연결선 색"},{135,135,L"양쪽 분기",L"좌우 경로 구성"},{verticalBranch,4402,L"상태에서 분기",L"분기점 선택·에너지 입력"}},
    {{137,137,L"그래프 설정",L"제목·크기·표시"},{changePlotStyleCommand,130,L"그래프 형태",L"곡선·직선·준위"},{plotFontCommand,253,L"글꼴·스타일",L"글꼴·굵게·기울임"},{138,138,L"출판용 정돈",L"비율·배치 자동 정리"},{241,241,L"데이터 값 크기",L"에너지 숫자",4},{axesManager,242,L"축 관리",L"X·Y축 선·제목·눈금·단위",5},{plotLegendCommand,plotLegendCommand,L"범례 관리",L"표시·글자·위치",7}},
    {{139,139,L"반응 이름·화살표",L"이름 배치·줄·위치"},{136,136,L"화살표 자석 배치",L"캔버스에서 두 점 선택"},{plotAnnotationsCommand,236,L"TS·이름 위치",L"생략·갭·라벨 위치"},{addEnergyGapCommand,237,L"에너지 갭 추가",L"캔버스에 지시선 배치"},{energyGapStyleCommand,energyGapStyleCommand,L"갭 글꼴·색상",L"글꼴·크기·굵게·기울임"},{151,151,L"스핀 교차점",L"교차 에너지·라벨"},{photoExcitation,4405,L"광여기 hν",L"빛에 의한 준위 전이"}},
    {{editStructure,186,L"구조 그리기·편집",L"선택한 위·아래 영역"},{pasteStructure,123,L"구조 붙여넣기",L"클립보드에서 삽입"},{fileStructure,124,L"구조 파일 삽입",L"CDXML·이미지 파일"},{sizeStructure,165,L"구조 크기",L"선택 구조 확대·축소"},{removeStructure,144,L"구조 제거",L"선택 영역만 비우기"},{structureBorder,4305,L"사각형 테두리",L"표시·색상·여백·굵기"}},
    {{244,244,L"곡선 굵기",L"경로 선 굵기"},{245,245,L"그림 선 굵기",L"선택 후 + / - 조절"},{plotWaveWidthCommand,250,L"물결 관리",L"물결 굵기·곡선 간격"}}
};
struct Manager{
    HWND window=nullptr;int owner=0,page=0;bool busy=false;
    struct View {double scale;Vec origin;};
    struct LegendDrag {Vec start,offset;View view;};
    struct AnnotationFrame {yd::Rect bounds;Vec plotPosition;};
    HWND grid=nullptr,form=nullptr,subpane=nullptr;
    std::vector<HWND> panes;
    std::wstring notice;
    int panel=0;
    int branchDataRow=-1;
    int axisInitialTab=-1;
    std::optional<EnergyPlot> draft;
    std::optional<LegendDrag> legendDrag;
    std::optional<AnnotationFrame> annotationFrame;
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
    static ui::Table dataRows(const EnergyPlot& graph){
        ui::Table rows;
        for(size_t i=0;i<graph.states.size();++i){
            std::vector<std::string> row={graph.states[i].label};
            if(graph.series.empty())row.push_back(number(graph.states[i].energy));
            for(const auto& path:graph.series)row.push_back(path.hasValue(i)?energyCellText(path,i,number(path.values[i])):"");
            rows.push_back(std::move(row));
        }
        return rows;
    }
    void openData(){
        if(panel==131&&grid){layout();SetFocus(GetDlgItem(grid,10));return;}
        clearPanel();
        auto* p=graph();if(!p)return;EnergyPlot original=*p;
        if(original.series.empty()){EnergySeries path;path.name="Path 1";for(auto& state:original.states)path.values.push_back(state.energy);original.series.push_back(path);}
        std::vector<std::wstring> columns={L"단계명"};for(auto& path:original.series)columns.push_back(ui::wide(path.name));
        auto rows=dataRows(original);
        grid=ui::embeddedTable(window,columns,rows,[this,last=rows](const ui::Table& data) mutable ->std::wstring{
            if(data==last)return {};
            try{auto* current=graph();if(!current)return L"그래프가 없습니다.";if(doc.isLocked(owner))return L"그래프 잠금을 해제하세요.";
                auto revised=energyGridData(*current,data);Document before=doc;auto old=*current;relocatePlot(old,revised);*graph()=std::move(revised);
                if(toNative(before)!=toNative(doc)){history.checkpoint(before);changed();}last=dataRows(*graph());return {};
            }catch(const std::exception& e){return ui::wide(e.what());}
        },[](size_t column,const std::string& value)->std::wstring{return column==0||validEnergyGridValue(value)?L"":L"에너지를 숫자로 입력하세요. 빈칸은 생략됩니다.";},[this](HWND,size_t column)->std::optional<std::wstring>{
            if(!column||!graph())return {};auto* p=graph();if(column>p->series.size())return {};
            ui::InlineDialogHost host(window,[this](HWND child){mount(child);});
            auto result=ui::form(window,L"경로 이름 변경",{{L"경로 이름",p->series[column-1].name,false,{},false,4,true}});if(!result)return {};
            if(!graph()||doc.isLocked(owner))return {};Document before=doc;graph()->series[column-1].name=result->front();if(toNative(before)!=toNative(doc)){history.checkpoint(before);changed();}return ui::wide(result->front());
        },[this](){return graph()?dataRows(*graph()):ui::Table{};});
        if(!grid)throw std::runtime_error("데이터 표를 열 수 없습니다.");
        if(tableFontPoints!=12)ui::setEmbeddedTableFontSize(grid,tableFontPoints);
        panel=131;layout();SetFocus(GetDlgItem(grid,10));ui::attachWindowHelp(window);
    }
    void editDataFont(){
        const auto* p=graph();
        if(!p||!grid||doc.isLocked(owner))return;
        const EnergyPlot original=*p;
        auto settings=[&](const std::vector<std::string>& values){
            auto graph=original;
            graph.stateFontSize=numeric(values[0],0,72);
            graph.valueFontSize=numeric(values[1],0,72);
            for(double size:{graph.stateFontSize,graph.valueFontSize})
                if(size>0&&size<4)throw std::runtime_error("그래프 글자는 0(자동) 또는 4~72 pt로 입력하세요.");
            if(graph.valueFontSize!=original.valueFontSize)graph.fixedValueFontSize=graph.valueFontSize>0;
            const int tableSize=int(numeric(values[2],8,24));
            return std::pair{std::move(graph),tableSize};
        };
        const auto edited=ui::form(window,L"데이터 표 · 글자 크기",{
            ui::numericField(L"그래프 상태 이름 (pt, 0: 자동)",original.stateFontSize,0,72,1),
            ui::numericField(L"그래프 에너지 숫자 (pt, 0: 자동)",original.valueFontSize,0,72,1),
            ui::numericField(L"표 편집 화면 글자 (pt)",tableFontPoints,8,24,1)
        },[&](const auto& values)->std::wstring{
            try{
                draft=settings(values).first;
                cachedScene.reset();InvalidateRect(window,nullptr,FALSE);return {};
            }catch(const std::exception& error){return ui::wide(error.what());}
        });
        draft.reset();cachedScene.reset();InvalidateRect(window,nullptr,FALSE);
        SetFocus(GetDlgItem(grid,10));
        if(!edited)return;
        auto [updated,tableSize]=settings(*edited);
        if(tableSize!=tableFontPoints){
            tableFontPoints=tableSize;
            ui::setEmbeddedTableFontSize(grid,tableFontPoints);
        }
        if(auto* current=graph()){
            Document before=doc;
            *current=std::move(updated);
            if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}
        }
    }
    int panelPage() const {
        if(panel==structureBorder)return 3;
        if(panel==plotLegendCommand)return 1;
        if(panel==139)return 2;
        return 0;
    }
    bool panelVisible() const {return panel&&page==panelPage();}
    void clearPanel(){
        if(legendDrag){legendDrag.reset();if(GetCapture()==window)ReleaseCapture();}
        if(grid){DestroyWindow(grid);grid=nullptr;}
        if(form){DestroyWindow(form);form=nullptr;}
        panel=0;draft.reset();cachedScene.reset();annotationFrame.reset();
    }
    void openStages(){
        const auto* p=graph();
        const AnnotationFrame frame{currentScene().bounds,p->p};
        ui::Table rows;
        const auto stages=p->stages.empty()?defaultMechanismStages():p->stages;
        for(const auto& stage:stages)rows.push_back({stage.label,number(stage.start),number(stage.end),
            number(stage.fontSize),number(stage.arrowWidth),number(stage.arrowHeadSize),stage.captionBelow?"1":"0",
             number(stage.offset.x),number(stage.offset.y),std::to_string(stage.row),stage.levelRange?"1":"0"});
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
        panel=139;annotationFrame=frame;layout();SetFocus(GetDlgItem(grid,10));ui::attachWindowHelp(window);
    }

    using PlotEdit=std::function<void(EnergyPlot&,const std::vector<std::string>&)>;
    void openForm(int action,const wchar_t* title,const std::vector<ui::Field>& fields,const PlotEdit& edit){
        auto update=[this,edit,action](const std::vector<std::string>& values,bool apply)->std::wstring{
            try{
                auto* p=graph();
                if(!p||doc.isLocked(owner))return L"편집할 그래프를 확인하세요.";
                auto candidate=*p;
                edit(candidate,values);
                if(apply){
                    Document before=doc;
                    *p=std::move(candidate);
                    if(toNative(before)!=toNative(doc)){history.checkpoint(before);changed();}
                    if(action==verticalBranch)branchDataRow=graph()->series.back().branchState+1;
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
            [this](){
                const int row=branchDataRow;branchDataRow=-1;
                SendMessageW(window,WM_COMMAND,panelBack,0);
                if(row>=0){selectPage(0);openData();ui::selectEmbeddedTableCell(grid,row,int(graph()->series.size()));}
            },
            [this](COLORREF color){
                ui::InlineDialogHost host(window,[this](HWND child){mount(child);});
                return ui::chooseColor(window,color);
            });
        if(!form)throw std::runtime_error("관리창 편집을 열 수 없습니다.");
        panel=action;layout();SetFocus(GetDlgItem(form,100));ui::attachWindowHelp(window);
    }

    void openStructureBorder(size_t index,bool above){
        const auto& state=graph()->states.at(index);
        const auto border=above?state.aboveBorder:state.belowBorder;
        openForm(structureBorder,L"구조 · 사각형 테두리",{
            {L"테두리 표시",border.enabled?"1":"0",false,{L"숨김",L"표시"}},
            {L"테두리 색상",border.color,false,{},true},
            ui::numericField(L"구조와의 여백 (pt)",border.padding,2,100,1),
            ui::numericField(L"테두리 선 굵기 (pt)",border.width,.1,10,.1)
        },[index,above](EnergyPlot& target,const auto& values){
            auto& state=target.states.at(index);
            auto& border=above?state.aboveBorder:state.belowBorder;
            border.enabled=values[0]=="1";
            border.color=colorValue(values[1]);
            border.padding=numeric(values[2],2,100);
            border.width=numeric(values[3],.1,10);
            validateEnergyAnnotations(target);
        });
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

    void openVerticalBranch(){
        const auto& source=*graph();
        if(source.states.size()<2||source.mode==independentEnergyLevels)
            throw std::runtime_error("연결된 상태가 두 개 이상인 그래프를 선택하세요.");
        std::vector<std::wstring> paths,states;
        for(const auto& path:source.series)paths.push_back(ui::wide(path.name));
        if(paths.empty())paths.push_back(L"Path 1");
        for(size_t i=0;i<source.states.size();++i)
            states.push_back(std::to_wstring(i+1)+L" · "+ui::wide(source.states[i].label));
        const size_t initial=targetContainer==owner?std::min(targetSlot,source.states.size()-1):0;
        openForm(verticalBranch,L"상태에서 분기 · 적용 후 데이터 표에서 에너지 입력",{
            {L"분기할 원본 경로","0",false,paths},
            {L"분기점 상태",std::to_string(initial),false,states},
            {L"새 분기 경로 이름","Branch "+std::to_string(paths.size()+1)}
        },[](EnergyPlot& target,const auto& values){
            target=verticalBranchPlot(target,size_t(std::stoul(values[0])),size_t(std::stoul(values[1])),values[2]);
        });
        SendMessageW(form,WM_COMMAND,MAKEWPARAM(102,EN_CHANGE),0);
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
        auto b=scene.bounds;
        if(panel==139&&panelVisible()&&annotationFrame){
            // Moving a reaction caption must not pan or zoom the unchanged axes.
            // Retain world bounds so explicit window resizes still fit correctly.
            b=annotationFrame->bounds;
            if(const auto* p=plot(owner)){
                const auto delta=p->p-annotationFrame->plotPosition;
                b.x+=delta.x;b.y+=delta.y;
            }
        }
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

    void editPreview(Vec cursor){
        if(busy||subpane||panelVisible()||!graph()||!preview.Contains(float(cursor.x),float(cursor.y)))return;
        const auto& scene=currentScene();const auto view=fitPreview(scene);
        const auto point=(cursor-view.origin)*(1/view.scale);const double pad=5/view.scale;
        if(auto axis=plotAxisTitleAt(scene,point,pad)){
            selectPage(1);axisInitialTab=axis->xAxis?1:4;
            SendMessageW(window,WM_COMMAND,axesManager,0);return;
        }
        int action=0,row=-1,column=1;
        for(auto it=scene.items.rbegin();it!=scene.items.rend();++it){
            const auto& item=*it;
            if(item.owner!=owner||item.type!=Primitive::Text||!item.textRole||item.points.empty()||item.rotation!=0)continue;
            const auto at=item.points.front();const double width=measure(item.text,item.fontSize,item.font,item.face);
            if(point.x<at.x-pad||point.x>at.x+width+pad||point.y<at.y-pad||point.y>at.y+item.fontSize*1.3+pad)continue;
            if(item.textRole==4)action=137;
            else if(item.textRole==7)action=plotLegendCommand;
            else if(item.textRole==6||item.textRole==8){action=131;row=item.textIndex;column=item.textRole==6?1:2;}
            else if(item.textRole==12){action=139;row=item.textIndex;}
            if(action)break;
        }
        if(!action){
            auto found=scene.plotSnapPoints.find(owner);double best=12;
            if(found!=scene.plotSnapPoints.end())for(const auto& snap:found->second){
                const double distance=(snap.p*view.scale+view.origin-cursor).length();
                if(distance<best){best=distance;action=131;row=snap.state;column=snap.path+2;}
            }
        }
        if(!action)return;
        selectPage(action==139?2:action==131?0:1);
        SendMessageW(window,WM_COMMAND,action,0);
        if(!grid||row<0)return;
        auto list=GetDlgItem(grid,10);if(!list||row>=ListView_GetItemCount(list))return;
        column=std::clamp(column,1,std::max(1,Header_GetItemCount(ListView_GetHeader(list))-1));
        ListView_EnsureVisible(list,row,FALSE);RECT cell{},client{};
        ListView_GetSubItemRect(list,row,column,LVIR_BOUNDS,&cell);GetClientRect(list,&client);
        if(cell.left<45)ListView_Scroll(list,cell.left-45,0);
        else if(cell.right>client.right)ListView_Scroll(list,cell.right-client.right,0);
        ListView_GetSubItemRect(list,row,column,LVIR_BOUNDS,&cell);
        // Reuse the grid's existing cell-selection and script-editing path.
        SendMessageW(list,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(std::clamp(cell.left+5,46L,std::max(46L,client.right-5)),cell.top+5));
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
        place(GetDlgItem(window,slotControl),20,panelTop+46,width-192,28,page==3&&!expanded);place(GetDlgItem(window,sideControl),width-162,panelTop+46,142,28,page==3&&!expanded);
        const int start=panelTop+(page==3?84:46),columns=4,cardWidth=(width-40-(columns-1)*gap)/columns;
        for(auto& category:pages)for(auto& card:category){auto h=GetDlgItem(window,card.action);auto found=std::find_if(pages[size_t(page)].begin(),pages[size_t(page)].end(),[&](const Card& c){return c.action==card.action;});bool show=found!=pages[size_t(page)].end()&&!expanded;int i=show?(card.slot>=0?card.slot:int(found-pages[size_t(page)].begin())):0;place(h,20+(i%columns)*(cardWidth+gap),start+(i/columns)*100,cardWidth,90,show);}
        place(GetDlgItem(window,distributeStages),width-188,panelTop+37,168,26,grid&&panel==139&&expanded&&!subpane);
        place(GetDlgItem(window,dataFont),width-188,panelTop+37,168,26,grid&&panel==131&&expanded&&!subpane);
        if(grid){
            const int gridTop=panel==139||panel==131?64:44;
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
    bool selectPage(int selected){
        if(subpane||!flushGrid())return false;
        page=selected;cachedScene.reset();targets();layout();for(int i=0;i<5;i++)InvalidateRect(GetDlgItem(window,tabFirst+i),nullptr,FALSE);
        return true;
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
            if(card.action!=242&&card.action!=243)stepPlotAppearance(owner,card.action,steps);
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
        if(card){
            const float left=(box.Width-36)/2;
            appearance::rounded(g,{left,3,36,36},9,keyboardFocus||pressed?palette.hover:palette.raised);
            if(card->action==axesManager){icon(g,242,{left+1,8,23,23},color);icon(g,243,{left+17,8,23,23},color);}
            else icon(g,card->icon,{left+4,7,28,28},color);
            ui::drawLabel(g,card->label,{box.X+4,box.Y+42,box.Width-8,22},13.f,color,true,true);auto current=value(card->action);ui::drawLabel(g,current.empty()?card->detail:current,{box.X+4,box.Y+67,box.Width-8,17},11.f,palette.muted,false,true);}
        else if(item.CtlID==dataFont){
            appearance::rounded(g,{0,0,box.Height,box.Height},7,keyboardFocus||pressed?palette.hover:palette.raised);
            icon(g,241,{4,4,box.Height-8,box.Height-8},color);
            ui::drawLabel(g,L"글자 크기",{box.Height+7,0,box.Width-box.Height-7,box.Height},13.f,color,true,false);
        }else if(item.CtlID==distributeStages){
            appearance::rounded(g,{0,0,box.Height,box.Height},7,keyboardFocus||pressed?palette.hover:palette.raised);
            Pen pen(color,1.3f);
            for(float x:{6.f,12.f,18.f}){g.DrawLine(&pen,x,9.f,x,17.f);}
            g.DrawLine(&pen,6.f,13.f,18.f,13.f);
            ui::drawLabel(g,L"자동 균등 배치",{box.Height+7,0,box.Width-box.Height-7,box.Height},13.f,color,true,false);
        }else{wchar_t label[128]{};GetWindowTextW(item.hwndItem,label,128);ui::drawLabel(g,label,box,13.f,color,active,true);}
    }
    void paint(HDC dc){
        Graphics g(dc);g.SetSmoothingMode(SmoothingModeAntiAlias);const auto& palette=appearance::palette();RECT client{};GetClientRect(window,&client);SolidBrush background(palette.panel);g.FillRectangle(&background,0,0,client.right,client.bottom);
        text(g,L"그래프 관리",20,16,21,palette.text,true);
        text(g,panelVisible()&&panel==plotLegendCommand?L"미리보기 · 범례를 끌어 위치 이동":L"더블클릭: 축 제목·이름·값·범례·경로의 상태 점 편집",preview.X,68,12,palette.muted,true);appearance::rounded(g,preview,8,Color(255,255,255,255));
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
            text(g,L"이름 0: 가운데 / 1: 아래  ·  범위 기준 0: 축 / 1: 물질 중앙",20,float(at.y+42),13,palette.text,true);
        }
        if(!notice.empty()&&!subpane)text(g,notice,20,float(client.bottom-73),13,palette.accent);
        else if(!subpane&&!panelVisible()&&page!=2)text(g,page==4?L"크기·굵기 위에서 휠로 조절 · Ctrl+Z: 실행 취소":page==1?L"크기·간격 위에서 휠로 조절할 수 있습니다.":page==3?L"상태와 위·아래 영역을 고른 뒤 구조를 그리거나 편집하세요.":L"데이터 표는 하단 패널에서 입력하고 그래프에 즉시 반영합니다.",20,float(client.bottom-70),11,palette.muted);
    }
    void act(int action){
        if(action==distributeStages){if(grid&&panel==139)ui::distributeEmbeddedStages(grid);return;}
        if(!flushGrid())return;
        if(action==dataFont&&panel==131){editDataFont();return;}
        if(action==panelBack){clearPanel();layout();SetFocus(GetDlgItem(window,tabFirst+page));return;}
        if(action==131){openData();return;}
        if(form||grid){clearPanel();layout();}
        if(action==245||action==plotWaveWidthCommand)weightAction=action;
        if(action==104||action==105){command(action);targets();InvalidateRect(window,nullptr,FALSE);for(auto h:cards)InvalidateRect(h,nullptr,FALSE);return;}
        if(!graph())throw std::runtime_error("편집할 그래프가 없습니다.");if(doc.isLocked(owner))throw std::runtime_error("그래프 잠금을 해제한 뒤 편집하세요.");
        selection={owner};
        if(action==137){plotSettings(owner,true);return;}
        if(action==axesManager){const int tab=axisInitialTab;axisInitialTab=-1;editPlotAxes(owner,tab);return;}
        if(action==verticalBranch){openVerticalBranch();return;}
        if(action==photoExcitation){editPlotPhotoExcitation(owner);return;}
        if(action==139){openStages();return;}
        if(action==plotLegendCommand){openLegend();return;}
        if(action==132||action==133){openSeries(action==132);return;}
        if(action>=editStructure&&action<=structureBorder){
            const int index=int(SendDlgItemMessageW(window,slotControl,CB_GETCURSEL,0,0));if(index<0||size_t(index)>=graph()->states.size())throw std::runtime_error("구조를 넣을 상태를 먼저 선택하세요.");
            targetContainer=owner;targetSlot=size_t(index);targetAbove=SendDlgItemMessageW(window,sideControl,CB_GETCURSEL,0,0)==0;
            if(action==structureBorder){openStructureBorder(targetSlot,targetAbove);return;}
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
    case WM_CREATE:{ui::enableButtonHelp();self->font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,ui::interfaceFont());const wchar_t* tabs[]={L"데이터·경로",L"축·글자",L"반응·주석",L"구조",L"굵기·물결"};for(int i=0;i<5;i++)self->button(tabFirst+i,tabs[i]);self->button(104,L"실행 취소");self->button(105,L"다시 실행");self->button(IDCANCEL,L"닫기");self->button(panelBack,L"확인");self->button(distributeStages,L"자동 균등 배치");self->button(dataFont,L"글자 크기");
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
    case WM_LBUTTONDBLCLK:try{self->editPreview({double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))});}catch(const std::exception& error){self->notice=ui::wide(error.what());InvalidateRect(h,nullptr,FALSE);}return 0;
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
    WNDCLASSW wc{};wc.lpfnWndProc=managerProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawPlotManager";wc.style=CS_DBLCLKS;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
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
void openPlotAxesManager(int id,int detail){
    managePlot(id);
    auto* manager=reinterpret_cast<Manager*>(GetWindowLongPtrW(plotManagerWindow,GWLP_USERDATA));
    if(!manager||manager->owner!=id||!manager->selectPage(1))return;
    manager->axisInitialTab=detail;
    SendMessageW(plotManagerWindow,WM_COMMAND,axesManager,0);
}
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
        if(!manager->subpane&&!manager->busy&&(GetKeyState(VK_CONTROL)&0x8000)){
            if(k==VK_TAB||(k>='1'&&k<='5')){
                const int next=k==VK_TAB?(manager->page+((GetKeyState(VK_SHIFT)&0x8000)?4:1))%5:int(k-'1');
                if(manager->selectPage(next))SetFocus(GetDlgItem(window,tabFirst+manager->page));return true;
            }
            if(k==VK_RETURN&&!inGrid&&!inForm){manager->selectPage(0);SendMessageW(window,WM_COMMAND,131,0);return true;}
        }
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

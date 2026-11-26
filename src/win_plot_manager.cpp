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
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>

namespace yd::editor {
HWND plotManagerWindow=nullptr;
namespace {
constexpr int tabFirst=4000,slotControl=4200,sideControl=4201,panelBack=4400,pathControl=4401,verticalBranch=4402,distributeStages=4403,dataFont=4404,photoExcitation=4405;
constexpr int editStructure=4300,pasteStructure=4301,fileStructure=4302,sizeStructure=4303,removeStructure=4304,structureBorder=4305;
constexpr int axesManager=4406;
constexpr int pathLabel=4407;
constexpr int previewFit=4410,previewIn=4411,previewOut=4412;
constexpr int pageOrder[]={0,2,1,3,4};
LRESULT CALLBACK managerButtonProc(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(msg==WM_MOUSEMOVE&&!GetPropW(h,L"YoungDrawHover")){SetPropW(h,L"YoungDrawHover",reinterpret_cast<HANDLE>(1));TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);InvalidateRect(h,nullptr,FALSE);}
    if(msg==WM_MOUSELEAVE){RemovePropW(h,L"YoungDrawHover");InvalidateRect(h,nullptr,FALSE);}
    if(msg==WM_NCDESTROY)RemovePropW(h,L"YoungDrawHover");
    return DefSubclassProc(h,msg,wp,lp);
}
int tableFontPoints=12;
struct Card{int action,icon;const wchar_t* label;const wchar_t* detail;int slot=-1;};
const std::vector<std::vector<Card>> pages={
    {{changePlotStyleCommand,130,L"그래프 형태",L"곡선·직선·준위"},{axesManager,242,L"축 관리",L"X축·Y축 통합 설정"},{131,131,L"데이터 표",L"상태·에너지·행·열 편집"},{139,139,L"반응 이름·화살표",L"이름·위치·자석 배치"},{132,132,L"경로 추가",L"비교 경로 만들기"},{135,135,L"양쪽 분기",L"좌우 경로 구성"},{verticalBranch,4402,L"상태에서 분기",L"분기점 선택·에너지 입력"}},
    {{133,133,L"경로 스타일",L"이름·색·선·표식"},{142,142,L"구간별 색상",L"준위·연결선 색"},{137,137,L"그래프 설정",L"제목·크기·표시"},{plotFontCommand,253,L"글꼴·스타일",L"글꼴·굵게·기울임"},{138,138,L"출판용 정돈",L"비율·배치 자동 정리"},{241,241,L"데이터 값 크기",L"에너지 숫자"},{plotLegendCommand,plotLegendCommand,L"범례 관리",L"표시·글자·위치"}},
    {{plotAnnotationsCommand,236,L"TS·이름 위치",L"미리보기에서 상태 클릭"},{addEnergyGapCommand,237,L"에너지 갭",L"추가·배치·글꼴·색상"},{151,151,L"스핀 교차점",L"교차 에너지·라벨"},{photoExcitation,4405,L"광여기 hν",L"빛에 의한 준위 전이"}},
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
    std::shared_ptr<EnergyPlot> seriesSession;
    size_t seriesIndex=0,seriesCount=0;
    std::optional<LegendDrag> legendDrag;
    std::optional<AnnotationFrame> annotationFrame;
    std::optional<AnnotationFrame> transientFrame;
    double previewZoom=1;Vec previewPan;std::optional<Vec> panStart;Vec panBefore;bool panMoved=false;
    int previewWheel=0;bool panRight=false;
    int annotationIndex=-1,gapIndex=-1;
    int weightAction=0,wheelAction=0,wheelDelta=0;std::optional<Scene> cachedScene;
    HFONT font=nullptr;ui::DialogColors colors;std::vector<HWND> cards;RectF preview;
    ~Manager(){if(font)DeleteObject(font);}
    EnergyPlot* graph(){return plot(owner);}
    HWND button(int id,const wchar_t* label){auto h=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,(HMENU)INT_PTR(id),GetModuleHandleW(nullptr),nullptr);SetWindowTheme(h,L"",L"");SetWindowSubclass(h,managerButtonProc,1,0);SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);return h;}
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
        const auto previous=transientFrame;if(graph())transientFrame=AnnotationFrame{currentScene().bounds,graph()->p};
        try{act(action);}catch(...){transientFrame=previous;throw;}
        transientFrame=previous;InvalidateRect(window,nullptr,FALSE);
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
        std::vector<size_t> sourceColumns;for(size_t i=0;i<columns.size();++i)sourceColumns.push_back(i);
        grid=ui::embeddedTable(window,columns,rows,{},[](size_t column,const std::string& value)->std::wstring{return column==0||validEnergyGridValue(value)?L"":L"에너지를 숫자로 입력하세요. 빈칸은 생략됩니다.";},[this](HWND,size_t column)->std::optional<std::wstring>{
            if(!column||!graph())return {};auto* p=graph();if(column>p->series.size())return {};
            ui::InlineDialogHost host(window,[this](HWND child){mount(child);});
            auto result=ui::form(window,L"경로 이름 변경",{{L"경로 이름",p->series[column-1].name,false,{},false,4,true}});if(!result)return {};
            if(!graph()||doc.isLocked(owner))return {};return ui::wide(result->front());
        },[this](){return graph()?dataRows(*graph()):ui::Table{};},
        [this,versions=std::map<size_t,EnergyPlot>{{0,*p}},last=rows,lastSources=sourceColumns,lastTitles=columns,lastRevision=size_t(0)](const ui::Table& data,const std::vector<size_t>& sources,const std::vector<std::wstring>& titles,const ui::TableHistory& tableHistory) mutable ->std::wstring{
            for(auto i=versions.begin();i!=versions.end();)if(std::find(tableHistory.retained.begin(),tableHistory.retained.end(),i->first)==tableHistory.retained.end())i=versions.erase(i);else ++i;
            if(data==last&&sources==lastSources&&titles==lastTitles&&tableHistory.revision==lastRevision)return {};
            try{
                auto* current=graph();if(!current)return L"그래프가 없습니다.";if(doc.isLocked(owner))return L"그래프 잠금을 해제하세요.";
                auto base=*current;
                const auto saved=versions.find(tableHistory.revision);
                if(saved!=versions.end()){
                    const auto& restored=saved->second;
                    base.states=restored.states;base.series=restored.series;base.crossings=restored.crossings;
                    base.width=restored.width;
                }else if(sources!=lastSources){
                    std::vector<size_t> paths;
                    for(size_t i=1;i<sources.size();++i){
                        const auto found=std::find(lastSources.begin()+1,lastSources.end(),sources[i]);
                        if(found!=lastSources.end())paths.push_back(size_t(found-lastSources.begin()-1));
                    }
                    // Select from the current graph so renamed/reordered rows,
                    // attached structures and exact surviving energies stay together.
                    base=selectEnergyPaths(base,paths);
                    for(size_t i=1;i<sources.size();++i)if(std::find(lastSources.begin()+1,lastSources.end(),sources[i])==lastSources.end()){
                        EnergySeries path;path.name=ui::utf8(titles.at(i));path.values.assign(base.states.size(),0);path.present.assign(base.states.size(),false);
                        const char* colors[]={"#167d9a","#b44758","#27854a","#7a52a1"};path.color=colors[base.series.size()%4];base.series.push_back(std::move(path));
                    }
                }
                auto cells=data;
                const auto displayed=dataRows(base);
                // Use the same exact-label-first row mapping as energyGridData,
                // including rows shifted by deletion or renamed in place.
                std::vector<int> sourceRows(cells.size(),-1);std::set<size_t> usedRows;
                for(size_t row=0;row<cells.size();++row)for(size_t old=0;old<displayed.size();++old)
                    if(!usedRows.count(old)&&cells[row][0]==displayed[old][0]){sourceRows[row]=int(old);usedRows.insert(old);break;}
                for(size_t row=0;row<cells.size();++row)if(sourceRows[row]<0&&row<displayed.size()&&!usedRows.count(row)){sourceRows[row]=int(row);usedRows.insert(row);}
                for(size_t row=0;row<cells.size();++row){
                    if(sourceRows[row]<0||cells[row].size()!=base.series.size()+1)continue;
                    const size_t old=size_t(sourceRows[row]);
                    for(size_t path=0;path<base.series.size();++path){
                        if(!base.series[path].hasValue(old)||cells[row][path+1]!=displayed[old][path+1])continue;
                        std::ostringstream exact;exact.imbue(std::locale::classic());exact<<std::setprecision(std::numeric_limits<double>::max_digits10)<<base.series[path].values[old];
                        cells[row][path+1]=energyCellText(base.series[path],old,exact.str());
                    }
                }
                // Renaming a header must not parse the rounded display cells or
                // change the stored energy precision and state metadata.
                auto revised=saved!=versions.end()||data==displayed?base:energyGridData(base,cells);
                for(size_t i=0;i<revised.series.size();++i)revised.series[i].name=ui::utf8(titles.at(i+1));
                versions[tableHistory.revision]=revised;
                Document before=doc;const auto old=*current;relocatePlot(old,revised);*graph()=std::move(revised);
                if(toNative(before)!=toNative(doc)){history.checkpoint(before);changed();}
                last=dataRows(*graph());lastSources=sources;lastTitles=titles;lastRevision=tableHistory.revision;return {};
            }catch(const std::exception& error){return ui::wide(error.what());}
        });
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
        if(panel==plotLegendCommand||panel==133)return 1;
        return 0;
    }
    bool panelVisible() const {return panel&&page==panelPage();}
    void clearPanel(){
        if(legendDrag){legendDrag.reset();if(GetCapture()==window)ReleaseCapture();}
        if(grid){DestroyWindow(grid);grid=nullptr;}
        if(form){DestroyWindow(form);form=nullptr;}
        panel=0;draft.reset();seriesSession.reset();cachedScene.reset();annotationFrame.reset();
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
    void openForm(int action,const wchar_t* title,const std::vector<ui::Field>& fields,const PlotEdit& edit,bool keepOpenOnApply=false){
        annotationFrame=AnnotationFrame{currentScene().bounds,graph()->p};
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
                    draft.reset();cachedScene.reset();InvalidateRect(window,nullptr,FALSE);
                    if(action==133){seriesCount=p->series.size();refreshSeriesChoices();}
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
            },keepOpenOnApply);
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

    void refreshSeriesChoices(){
        auto combo=GetDlgItem(window,pathControl);
        SendMessageW(combo,CB_RESETCONTENT,0,0);
        if(seriesSession)for(const auto& entry:seriesSession->series)
            SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)ui::wide(entry.name).c_str());
        SendMessageW(combo,CB_SETCURSEL,seriesIndex,0);
    }

    void openSeries(bool add,size_t index=0,std::shared_ptr<EnergyPlot> session={}){
        const auto& p=*graph();
        const size_t count=p.series.size();
        if(add&&count>=100)throw std::runtime_error("경로는 최대 100개입니다.");
        if(!add){
            seriesSession=session?std::move(session):std::make_shared<EnergyPlot>(p);
            seriesIndex=index;seriesCount=count;
        }
        const auto& source=add?p:*seriesSession;
        EnergySeries path=count?source.series.at(index):basePath(source);
        if(source.curveWidth>0)path.width=source.curveWidth/(source.inkScale*(source.thinLines?.5:1.));
        if(add){
            path=EnergySeries{};
            path.name="Path "+std::to_string(std::max(size_t(1),count)+1);
            path.color="#b44758";
        }
        auto radius=ui::numericField(L"표식 반지름 (pt)",path.markerSize,.1,20,.5);
        radius.stepWithSigns=true;radius.stepButtons=true;
        openForm(add?132:133,add?L"경로 추가":L"경로 스타일 · 경로를 바꿔도 입력 유지",{
            {L"경로 이름",path.name,false,{},false,4,true},
            {L"선 색상",path.color,false,{},true},
            {L"선 종류",std::to_string(path.dash),false,{L"실선",L"파선",L"점선"}},
            {L"선 굵기 (pt)",number(path.width)},
            {L"기본 표식",std::to_string(path.marker),false,{L"없음",L"원",L"사각",L"마름모",L"삼각"}},
            radius
        },[this,add,index,count,session=seriesSession](EnergyPlot& target,const auto& values){
            if(target.series.size()!=(add?count:seriesCount))throw std::runtime_error("경로 목록이 변경되었습니다. 이 편집을 닫고 다시 여세요.");
            if(!add){target.series=session->series;target.curveWidth=session->curveWidth;}
            if(target.series.empty())target.series.push_back(basePath(target));
            if(target.curveWidth>0){
                const double width=target.curveWidth/(target.inkScale*(target.thinLines?.5:1.));
                for(auto& existing:target.series)existing.width=width;
                target.curveWidth=0;
            }
            if(add){
                auto extra=basePath(target);
                extra.present=target.series.front().present;
                target.series.push_back(std::move(extra));
            }
            auto& series=add?target.series.back():target.series.at(index);
            series.name=values[0];series.color=colorValue(values[1]);
            series.dash=std::stoi(values[2]);series.width=numeric(values[3],.1,100);
            series.marker=std::stoi(values[4]);series.markerSize=numeric(values[5],.1,20);
            if(series.dash<0||series.dash>2||series.marker<0||series.marker>4)
                throw std::runtime_error("선 종류와 표식을 선택하세요.");
            if(!add)*session=target;
        },!add);
        auto combo=GetDlgItem(window,pathControl);
        refreshSeriesChoices();
        if(!add)SetDlgItemTextW(form,IDOK,L"모두 적용");
        SetWindowPos(combo,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        layout();
        SendMessageW(form,WM_COMMAND,MAKEWPARAM(100,EN_CHANGE),0);
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
        const auto* frame=annotationFrame&&panelVisible()&&(panel==139||form)?&*annotationFrame:transientFrame?&*transientFrame:nullptr;
        if(frame){
            // Keep setting changes visible without automatically refitting the view.
            // Retain world bounds so explicit window resizes still fit correctly.
            b=frame->bounds;
            if(const auto* p=plot(owner)){
                const auto delta=p->p-frame->plotPosition;
                b.x+=delta.x;b.y+=delta.y;
            }
        }
        const double scale=std::max(.01,std::min((preview.Width-32)/std::max(1.,b.w),(preview.Height-32)/std::max(1.,b.h)))*previewZoom;
        return {scale,{preview.X+(preview.Width-b.w*scale)/2-b.x*scale+previewPan.x,
            preview.Y+(preview.Height-b.h*scale)/2-b.y*scale+previewPan.y}};
    }
    void zoomPreview(double factor,Vec cursor){
        if(!graph())return;endLegendDrag();panStart.reset();
        const auto& sc=currentScene();const auto before=fitPreview(sc);const auto point=(cursor-before.origin)*(1/before.scale);
        previewZoom=std::clamp(previewZoom*factor,.5,8.);const auto after=fitPreview(sc);
        previewPan=previewPan+cursor-(point*after.scale+after.origin);InvalidateRect(window,nullptr,FALSE);
    }
    bool previewCommand(int action){
        if(action==previewFit){previewZoom=1;previewPan={};previewWheel=0;endLegendDrag();if(graph()){const AnnotationFrame frame{currentScene().bounds,graph()->p};if(annotationFrame)annotationFrame=frame;if(transientFrame)transientFrame=frame;}InvalidateRect(window,nullptr,FALSE);return true;}
        if(action!=previewIn&&action!=previewOut)return false;
        zoomPreview(action==previewIn?1.25:.8,{preview.X+preview.Width/2,preview.Y+preview.Height/2});return true;
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

    void editPreview(Vec cursor,bool annotationOnly=false){
        if(busy||subpane||form||!graph()||!preview.Contains(float(cursor.x),float(cursor.y))||!flushGrid())return;
        const auto& scene=currentScene();const auto view=fitPreview(scene);
        const auto point=(cursor-view.origin)*(1/view.scale);const double pad=5/view.scale;
        for(const auto& gap:scene.gapCaptions)if(gap.owner==owner&&point.x>=gap.bounds.x-pad&&point.x<=gap.bounds.x+gap.bounds.w+pad&&point.y>=gap.bounds.y-pad&&point.y<=gap.bounds.y+gap.bounds.h+pad){gapIndex=int(gap.index);SendMessageW(window,WM_COMMAND,addEnergyGapCommand,0);return;}
        if(auto axis=plotAxisTitleAt(scene,point,pad)){
            selectPage(0);axisInitialTab=axis->xAxis?0:1;
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
            else if(item.textRole==6||item.textRole==8){action=annotationOnly&&item.textRole==6?plotAnnotationsCommand:131;row=item.textIndex;column=item.textRole==6?1:2;if(action==plotAnnotationsCommand)annotationIndex=row;}
            else if(item.textRole==12){action=139;row=item.textIndex;}
            if(action)break;
        }
        if(!action&&!annotationOnly){
            auto found=scene.plotSnapPoints.find(owner);double best=12;
            if(found!=scene.plotSnapPoints.end())for(const auto& snap:found->second){
                const double distance=(snap.p*view.scale+view.origin-cursor).length();
                if(distance<best){best=distance;action=131;row=snap.state;column=snap.path+2;}
            }
        }
        if(!action)return;
        if(annotationOnly&&action!=plotAnnotationsCommand&&action!=139)return;
        selectPage(action==139||action==131?0:1);
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
    void previewDown(Vec cursor,bool right=false){
        if(!preview.Contains(float(cursor.x),float(cursor.y)))return;
        if(!right&&snapPlot==owner&&!busy&&!subpane){
            const auto& sc=currentScene();const auto view=fitPreview(sc);std::optional<PlotSnapPoint> hit;double distance=16;
            const auto points=sc.plotSnapPoints.find(owner);if(points!=sc.plotSnapPoints.end())for(auto point:points->second){const double d=(point.p*view.scale+view.origin-cursor).length();if(d<distance){distance=d;hit=point;}}
            if(!hit)return;if(!snapStart){snapStart=hit;notice=L"첫 점을 고정했습니다. 두 번째 점을 클릭하세요. Esc: 취소";}
            else{auto label=snapLabel;label.start=std::min(snapStart->percent,hit->percent);label.end=std::max(snapStart->percent,hit->percent);if(label.end-label.start<1)return;history.checkpoint(doc);if(snapStage<0)graph()->stages.push_back(label);else graph()->stages.at(size_t(snapStage))=label;snapPlot=0;snapStart.reset();changed();notice=L"선택한 두 준위에 반응 이름·화살표를 배치했습니다.";}
            InvalidateRect(window,nullptr,FALSE);return;
        }
        if(!right&&startLegendDrag(cursor))return;
        panStart=cursor;panBefore=previewPan;panMoved=false;panRight=right;SetCapture(window);
    }
    void previewMove(Vec cursor){
        if(legendDrag){moveLegend(cursor);return;}if(!panStart)return;
        const auto delta=cursor-*panStart;if(delta.length()>3)panMoved=true;
        if(panMoved){previewPan=panBefore+delta;InvalidateRect(window,nullptr,FALSE);}
    }
    void previewUp(Vec cursor){
        if(legendDrag){endLegendDrag();return;}if(!panStart)return;const bool click=!panMoved&&!panRight;panStart.reset();if(GetCapture()==window)ReleaseCapture();if(click)editPreview(cursor,true);
    }

    void layout(){
        RECT client{};GetClientRect(window,&client);const int width=client.right,height=client.bottom;
        const bool expanded=panelVisible()||subpane;
        const bool showForm=form&&expanded&&!subpane;
        const int editorWidth=std::clamp(int(width*.47),560,720),editorLeft=width-editorWidth-20;
        const int panelTop=64,gap=10;
        preview={20.f,96.f,float(editorLeft-40),float(std::max(80,height-156))};
        const int tabWidth=(editorWidth-4*6)/5;
        auto batch=BeginDeferWindowPos(48);
        auto place=[&](HWND h,int x,int y,int w,int ht,bool visible=true){batch=DeferWindowPos(batch,h,nullptr,x,y,w,ht,SWP_NOZORDER|SWP_NOACTIVATE|(visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));};
        for(int i=0;i<5;i++){
            auto tab=GetDlgItem(window,tabFirst+pageOrder[i]);
            place(tab,editorLeft+i*(tabWidth+6),panelTop,tabWidth,34);
            EnableWindow(tab,!subpane);
        }
        place(GetDlgItem(window,slotControl),editorLeft,panelTop+46,editorWidth-152,28,page==3&&!expanded);place(GetDlgItem(window,sideControl),width-162,panelTop+46,142,28,page==3&&!expanded);
        const int start=panelTop+(page==3?84:46),columns=3,cardWidth=(editorWidth-(columns-1)*gap)/columns;
        for(auto& category:pages)for(auto& card:category){auto h=GetDlgItem(window,card.action);auto found=std::find_if(pages[size_t(page)].begin(),pages[size_t(page)].end(),[&](const Card& c){return c.action==card.action;});bool show=found!=pages[size_t(page)].end()&&!expanded;int i=show?(card.slot>=0?card.slot:int(found-pages[size_t(page)].begin())):0;place(h,editorLeft+(i%columns)*(cardWidth+gap),start+(i/columns)*100,cardWidth,90,show);}
        place(GetDlgItem(window,136),editorLeft,panelTop+44,140,26,grid&&panel==139&&expanded&&!subpane);
        place(GetDlgItem(window,distributeStages),width-188,panelTop+37,168,26,grid&&panel==139&&expanded&&!subpane);
        place(GetDlgItem(window,dataFont),width-188,panelTop+37,168,26,grid&&panel==131&&expanded&&!subpane);
        if(grid){
            const int gridTop=panel==139||panel==131?64:44;
            place(grid,editorLeft,panelTop+gridTop,editorWidth,height-panelTop-gridTop-56,expanded&&!subpane);
        }
        const bool pathSelector=showForm&&panel==133&&graph()&&graph()->series.size()>1;
        const int formTop=panelTop+(pathSelector?86:44);
        if(form)place(form,editorLeft,formTop,editorWidth,height-formTop-56,showForm);
        if(subpane)place(subpane,editorLeft,panelTop+43,editorWidth,height-panelTop-51);
        place(GetDlgItem(window,pathLabel),editorLeft+16,panelTop+50,142,24,pathSelector);
        place(GetDlgItem(window,pathControl),editorLeft+174,panelTop+46,editorWidth-190,190,pathSelector);
        place(GetDlgItem(window,104),editorLeft,height-46,86,30,!subpane);place(GetDlgItem(window,105),editorLeft+92,height-46,86,30,!subpane);
        place(GetDlgItem(window,panelBack),width-236,height-46,104,30,expanded&&!showForm&&!subpane);
        place(GetDlgItem(window,IDCANCEL),width-124,height-46,104,30,!subpane);
        place(GetDlgItem(window,previewOut),20,height-46,32,30);place(GetDlgItem(window,previewIn),58,height-46,32,30);place(GetDlgItem(window,previewFit),96,height-46,104,30);
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
        auto local=cursor;ScreenToClient(window,&local);
        if(preview.Contains(float(local.x),float(local.y))){previewWheel+=delta;const int steps=previewWheel/WHEEL_DELTA;previewWheel%=WHEEL_DELTA;if(steps)zoomPreview(std::pow(1.2,steps),{double(local.x),double(local.y)});return;}
        if(busy||panelVisible())return;
        for(const auto& card:pages[size_t(page)]){
            RECT bounds{};GetWindowRect(GetDlgItem(window,card.action),&bounds);
            if(!PtInRect(&bounds,cursor))continue;
            if(wheelAction!=card.action){wheelAction=card.action;wheelDelta=0;}
            wheelDelta+=delta;
            const int steps=wheelDelta/WHEEL_DELTA;wheelDelta%=WHEEL_DELTA;
            if(card.action!=242&&card.action!=243&&card.action!=244)stepPlotAppearance(owner,card.action,steps);
            return;
        }
        wheelAction=0;
    }
    std::wstring value(int id){auto* p=graph();if(!p)return L"그래프 없음";
        if(id==plotFontCommand)return ui::wide(p->fontFamily);
        if(id==244)return L"경로별 설정";
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
        const bool hovered=GetPropW(item.hwndItem,L"YoungDrawHover")!=nullptr;
        const auto color=tab&&active?palette.onAccent:active||keyboardFocus||pressed?palette.accent:palette.text;
        if(tab||item.CtlID==previewFit||item.CtlID==previewIn||item.CtlID==previewOut)appearance::rounded(g,{1,1,box.Width-2,box.Height-2},11,tab&&active?palette.accent:hovered||keyboardFocus?palette.hover:palette.raised);
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
        }else{wchar_t label[128]{};GetWindowTextW(item.hwndItem,label,128);ui::drawLabel(g,label,box,tab?12.f:13.f,color,active,true);}
    }
    void paint(HDC dc){
        Graphics g(dc);g.SetSmoothingMode(SmoothingModeAntiAlias);const auto& palette=appearance::palette();RECT client{};GetClientRect(window,&client);SolidBrush background(palette.panel);g.FillRectangle(&background,0,0,client.right,client.bottom);
        text(g,L"그래프 관리",20,16,21,palette.text,true);
        ui::drawLabel(g,panelVisible()&&panel==plotLegendCommand?L"미리보기 · 범례를 끌어 위치 이동":L"휠: 확대·축소 · 드래그: 이동 · 주석 클릭: 편집",{preview.X,64,preview.Width,26},11,palette.muted,false,false);appearance::rounded(g,preview,10,Color(255,255,255,255));
        if(graph())try{
            const auto& sc=currentScene();
            const auto view=fitPreview(sc);
            auto saved=g.Save();
            g.SetClip(preview);
            g.TranslateTransform(float(view.origin.x),float(view.origin.y));
            g.ScaleTransform(float(view.scale),float(view.scale));
            render(g,sc);
            if(snapPlot==owner){Pen marker(palette.accent,1.5f/float(view.scale));const auto points=sc.plotSnapPoints.find(owner);if(points!=sc.plotSnapPoints.end())for(const auto& point:points->second){const float r=4.f/float(view.scale);g.DrawEllipse(&marker,float(point.p.x)-r,float(point.p.y)-r,r*2,r*2);}}
            g.Restore(saved);
        }catch(const std::exception&){text(g,L"미리보기를 표시할 수 없습니다.",preview.X+16,preview.Y+24,12,palette.muted);}
        else text(g,L"그래프가 삭제되었습니다. 창을 닫고 다시 선택하세요.",preview.X+12,preview.Y+24,12,palette.muted);
        const float editorLeft=preview.X+preview.Width+20;
        if(!notice.empty()&&!subpane)ui::drawLabel(g,notice,{editorLeft,float(client.bottom-82),float(client.right)-editorLeft-20,32},12,palette.accent,false,false);
        else if(!subpane&&!panelVisible())text(g,page==4?L"경로별 굵기를 선택해 조절 · Ctrl+Z: 실행 취소":L"도구를 선택하거나 미리보기의 주석을 클릭하세요.",editorLeft,float(client.bottom-70),11,palette.muted);
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
        if(action==136){snapPlotStages(owner);if(snapPlot)notice=L"미리보기에서 화살표 양끝의 준위 점을 클릭하세요. Esc: 취소";return;}
        if(action==plotAnnotationsCommand){const int index=annotationIndex;annotationIndex=-1;editPlotAnnotations(owner,index);return;}
        if(action==energyGapStyleCommand){editEnergyGapStyle(owner);return;}
        if(action==addEnergyGapCommand){const int index=gapIndex;gapIndex=-1;manageEnergyGaps(owner,index);return;}
        if(action==139){openStages();return;}
        if(action==plotLegendCommand){openLegend();return;}
        if(action==132||action==133){selectPage(action==133?1:0);openSeries(action==132);return;}
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
    case WM_CREATE:{ui::enableButtonHelp();self->font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,ui::interfaceFont());const wchar_t* tabs[]={L"그래프·축·데이터",L"색·선·글자",L"반응·주석",L"구조",L"굵기·물결"};for(int i:pageOrder)self->button(tabFirst+i,tabs[i]);self->button(previewOut,L"−");self->button(previewIn,L"+");self->button(previewFit,L"전체 보기");self->button(136,L"자석 배치");self->button(104,L"실행 취소");self->button(105,L"다시 실행");self->button(IDCANCEL,L"닫기");self->button(panelBack,L"확인");self->button(distributeStages,L"자동 균등 배치");self->button(dataFont,L"글자 크기");
        auto path=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,
            0,0,170,180,h,reinterpret_cast<HMENU>(INT_PTR(pathControl)),nullptr,nullptr);
        SendMessageW(path,WM_SETFONT,(WPARAM)self->font,TRUE);ui::themeCombo(path);
        const auto label=CreateWindowW(L"STATIC",L"편집할 경로",WS_CHILD,0,0,1,1,h,reinterpret_cast<HMENU>(INT_PTR(pathLabel)),nullptr,nullptr);
        SendMessageW(label,WM_SETFONT,(WPARAM)self->font,TRUE);
        SetWindowPos(path,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        auto slot=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,0,0,100,300,h,reinterpret_cast<HMENU>(INT_PTR(slotControl)),GetModuleHandleW(nullptr),nullptr);auto side=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST,0,0,100,140,h,reinterpret_cast<HMENU>(INT_PTR(sideControl)),GetModuleHandleW(nullptr),nullptr);SendMessageW(slot,WM_SETFONT,(WPARAM)self->font,TRUE);SendMessageW(side,WM_SETFONT,(WPARAM)self->font,TRUE);SendMessageW(side,CB_ADDSTRING,0,(LPARAM)L"상태 위 구조");SendMessageW(side,CB_ADDSTRING,0,(LPARAM)L"상태 아래 구조");SendMessageW(side,CB_SETCURSEL,0,0);ui::themeCombo(slot);ui::themeCombo(side);self->initializeCards();appearance::titlebar(h);return 0;}
    case WM_SIZE:self->layout();return 0;
    case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={900,640};return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PRINTCLIENT:self->paint(reinterpret_cast<HDC>(wp));return 0;
    case WM_PAINT:{PAINTSTRUCT paint;auto dc=BeginPaint(h,&paint);RECT r{};GetClientRect(h,&r);auto buffer=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,std::max(1L,r.right),std::max(1L,r.bottom));auto old=SelectObject(buffer,bitmap);self->paint(buffer);BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(h,&paint);return 0;}
    case WM_DRAWITEM:self->draw(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:return self->colors.control(message,reinterpret_cast<HDC>(wp));
    case WM_LBUTTONDOWN:case WM_RBUTTONDOWN:try{self->previewDown({double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))},message==WM_RBUTTONDOWN);}catch(const std::exception&){}return 0;
    case WM_LBUTTONDBLCLK:try{self->editPreview({double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))});}catch(const std::exception& error){self->notice=ui::wide(error.what());InvalidateRect(h,nullptr,FALSE);}return 0;
    case WM_MOUSEMOVE:self->previewMove({double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))});return 0;
    case WM_LBUTTONUP:case WM_RBUTTONUP:try{self->previewUp({double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))});}catch(const std::exception& error){self->notice=ui::wide(error.what());InvalidateRect(h,nullptr,FALSE);}return 0;
    case WM_CAPTURECHANGED:self->panStart.reset();self->endLegendDrag();return 0;
    case WM_COMMAND:{int action=LOWORD(wp);
        if(self->previewCommand(action))return 0;
        if(self->subpane||self->busy)return 0;
        if(action==pathControl){
            const auto index=SendDlgItemMessageW(h,pathControl,CB_GETCURSEL,0,0);
            if(HIWORD(wp)==CBN_SELCHANGE&&index>=0&&self->panel==133&&self->graph()&&size_t(index)<self->graph()->series.size()){
                if(!ui::validateEmbeddedForm(self->form)){
                    SendDlgItemMessageW(h,pathControl,CB_SETCURSEL,self->seriesIndex,0);
                    return 0;
                }
                auto session=self->seriesSession;
                self->clearPanel();
                try{self->openSeries(false,size_t(index),std::move(session));SetFocus(GetDlgItem(h,pathControl));}
                catch(const std::exception& error){self->notice=ui::wide(error.what());self->layout();}
            }
            return 0;
        }
        if(action==IDCANCEL){if(!self->flushGrid())return 0;DestroyWindow(h);return 0;}if(action>=tabFirst&&action<tabFirst+5){self->selectPage(action-tabFirst);return 0;}if(action==slotControl||action==sideControl){targetContainer=self->owner;targetSlot=size_t(SendDlgItemMessageW(h,slotControl,CB_GETCURSEL,0,0));targetAbove=SendDlgItemMessageW(h,sideControl,CB_GETCURSEL,0,0)==0;return 0;}if(self->busy)return 0;
        self->busy=true;self->notice.clear();
        try{self->runTool(action);}catch(const std::exception& error){self->notice=ui::wide(error.what());InvalidateRect(h,nullptr,FALSE);}
        self->busy=false;
        if(action==addEnergyGapCommand&&pendingEnergyGap){
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
        if(manager->owner!=id){if(!manager->flushGrid())return;manager->clearPanel();manager->previewZoom=1;manager->previewPan={};}
        manager->owner=id;manager->targets();manager->layout();
        if(visible){
            if(IsIconic(plotManagerWindow))ShowWindow(plotManagerWindow,SW_RESTORE);
            else if(!IsWindowVisible(plotManagerWindow))ShowWindow(plotManagerWindow,SW_SHOW);
            SetForegroundWindow(plotManagerWindow);
        }
        return;
    }
    WNDCLASSW wc{};wc.lpfnWndProc=managerProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawPlotManager";wc.style=CS_DBLCLKS;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    auto* state=new Manager;state->owner=id;MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(win,MONITOR_DEFAULTTONEAREST),&monitor);auto rect=ui::ownedWindowRect(win,std::max(900L,std::min(1520L,monitor.rcWork.right-monitor.rcWork.left-40)),std::max(640L,std::min(960L,monitor.rcWork.bottom-monitor.rcWork.top-40)));
    auto h=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"그래프 관리",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,win,nullptr,wc.hInstance,state);
    if(!h){delete state;throw std::runtime_error("그래프 관리 창을 열 수 없습니다.");}plotManagerWindow=h;if(visible){ShowWindow(h,SW_SHOW);UpdateWindow(h);}
}
void openPlotManagerTool(int id,int action){
    managePlot(id);
    auto* manager=reinterpret_cast<Manager*>(GetWindowLongPtrW(plotManagerWindow,GWLP_USERDATA));
    if(!manager||manager->owner!=id||!manager->flushGrid())return;
    manager->selectPage(action==137||action==plotLegendCommand?1:0);
    SendMessageW(plotManagerWindow,WM_COMMAND,action,0);
}
void closePlotManager(){if(plotManagerWindow)DestroyWindow(plotManagerWindow);}
void openPlotAxesManager(int id,int detail){
    managePlot(id);
    auto* manager=reinterpret_cast<Manager*>(GetWindowLongPtrW(plotManagerWindow,GWLP_USERDATA));
    if(!manager||manager->owner!=id||!manager->selectPage(0))return;
    manager->axisInitialTab=detail;
    SendMessageW(plotManagerWindow,WM_COMMAND,axesManager,0);
}
std::optional<PlotManagerPreview> plotManagerPreview(){
    if(!plotManagerWindow)return {};auto* m=reinterpret_cast<Manager*>(GetWindowLongPtrW(plotManagerWindow,GWLP_USERDATA));if(!m||!m->graph())return {};const auto view=m->fitPreview(m->currentScene());return PlotManagerPreview{{m->preview.X,m->preview.Y,m->preview.Width,m->preview.Height},view.scale,view.origin};
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
        if(k==VK_ESCAPE&&snapPlot==manager->owner){snapPlot=0;snapStart.reset();manager->notice.clear();InvalidateRect(window,nullptr,FALSE);return true;}
        if(!manager->subpane&&!manager->busy&&(GetKeyState(VK_CONTROL)&0x8000)){
            if(k==VK_TAB||(k>='1'&&k<='5')){
                const int index=int(std::find(std::begin(pageOrder),std::end(pageOrder),manager->page)-std::begin(pageOrder));
                const int next=pageOrder[k==VK_TAB?(index+((GetKeyState(VK_SHIFT)&0x8000)?4:1))%5:int(k-'1')];
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

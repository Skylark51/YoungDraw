#include "win_editor_plot.h"
#include "win_editor_state.h"
#include "win_editor_commands.h"
#include "win_editor_reaction.h"
#include "win_editor_text.h"
#include "cdxml.h"
#include "mechanism_data.h"
#include "plot_data.h"
#include "win_dialogs.h"
#include "win_ui.h"
#include <sstream>

namespace yd::editor {

int snapPlot=0,snapStage=-1;MechanismStage snapLabel;std::optional<PlotSnapPoint> snapStart;
std::optional<PlotSnapPoint> nearestPlotSnap(const Scene& sc,Vec cursor){
    auto found=sc.plotSnapPoints.find(snapPlot);if(found==sc.plotSnapPoints.end())return {};
    std::optional<PlotSnapPoint> best;double distance=14;
    for(const auto& point:found->second){double d=(point.p*zoom+origin-cursor).length();if(d<distance){distance=d;best=point;}}
    return best;
}

void relocatePlot(const EnergyPlot& before,const EnergyPlot& after){
    for(size_t i=0;i<after.states.size();i++)for(size_t j=0;j<before.states.size();j++){
        if(!after.states[i].above.empty()&&after.states[i].above==before.states[j].above)doc.translate(after.states[i].above,plotAnchor(after,i,true)-plotAnchor(before,j,true));
        if(!after.states[i].below.empty()&&after.states[i].below==before.states[j].below)doc.translate(after.states[i].below,plotAnchor(after,i,false)-plotAnchor(before,j,false));
    }
}
void editPlot(int id){auto* ptr=plot(id);if(!ptr)return;Document before=doc;EnergyPlot original=*ptr;if(original.series.empty()){EnergySeries path;path.name="Path 1";for(auto& state:original.states)path.values.push_back(state.energy);original.series.push_back(path);}
    const bool independent=original.mode==independentEnergyLevels;
    std::vector<std::wstring> columns={independent?L"다중도 / 상태":L"단계명"};
    for(auto& path:original.series)
        columns.push_back(independent&&original.series.size()==1&&path.name=="Path 1"?L"에너지":ui::wide(path.name));
    ui::Table rows;
    for(size_t i=0;i<original.states.size();i++){std::vector<std::string> row={original.states[i].label};for(auto& path:original.series)row.push_back(path.hasValue(i)?number(path.values[i]):"");rows.push_back(row);}
    auto update=[&](const ui::Table& data)->std::wstring{
        try{auto p=data==rows?original:energyGridData(original,data);doc=before;relocatePlot(original,p);*plot(id)=std::move(p);redraw();UpdateWindow(win);return {};}
        catch(const std::exception& e){return ui::wide(e.what());}
    };
    auto cellError=[](size_t column,const std::string& value)->std::wstring{
        if(column==0||validEnergyGridValue(value))return {};
        return L"에너지를 숫자로 입력하세요 (−1000000~1000000). 빈칸은 그래프에서 생략됩니다.";
    };
    auto columnEdit=[&](HWND parent,size_t column)->std::optional<std::wstring>{
        if(column==0)return {};
        auto& name=original.series[column-1].name;
        auto result=ui::form(parent,L"경로 이름 변경",{{L"경로 이름",name,false,{},false,4,true}});
        if(!result)return {};
        name=std::move(result->front());
        return ui::wide(name);
    };
    auto result=ui::table(win,independent?L"스핀 다중도 비교 — 에너지 표":L"에너지 그래프 — 데이터 표",columns,rows,update,true,
        independent?L"다중도 = 2S + 1 · 한 행에 한 상태 · 여러 셀에 붙여넣기 가능":
        L"경로 열 제목 더블클릭: 이름 변경 · 여러 셀에 붙여넣기 가능 · 캔버스에서 Alt+드래그: 준위 가로 이동",false,0,cellError,columnEdit);
    if(result){
        // Apply the returned table explicitly; committing must not depend on a prior preview.
        update(*result);
        if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}
    }else{doc=before;redraw();}

}
bool adjustSelectedPlotWeight(bool increase){
    std::vector<int> ids;for(int id:selection)if(plot(id)&&!doc.isLocked(id))ids.push_back(id);
    if(ids.empty())return false;Document before=doc;
    for(int id:ids){auto* p=plot(id);p->inkScale=std::clamp(p->inkScale*(increase?1.15:1/1.15),.25,4.);}
    if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();status=L"그래프 획 굵기를 조정했습니다. + / -: 축·글씨·삽입 구조 함께 조정 · Ctrl+Z: 되돌리기";}
    return true;
}
void editPlotAxisLengths(int id){
    if(!plot(id)||doc.isLocked(id))throw std::runtime_error("축 길이를 바꿀 그래프를 선택하세요.");
    const Document before=doc;const auto axes=scene().plotAxes.at(id);
    auto preview=[&](const std::vector<std::string>& values)->std::wstring{try{
        auto candidate=resizedPlotAxes(before,id,numeric(values[0],100,4000),numeric(values[1],70,4000),measure);
        auto& p=*std::find_if(candidate.plots.begin(),candidate.plots.end(),[&](const auto& p){return p.id==id;});p.inkScale=numeric(values[2],25,400)/100;
        doc=std::move(candidate);redraw();UpdateWindow(win);return {};
    }catch(const std::exception& e){return ui::wide(e.what());}};
    const auto result=ui::form(win,L"그래프 축 길이·굵기",{
        {L"가로축 길이 (pt)",number((axes.xEnd-axes.origin).length())},
        {L"세로축 길이 (pt)",number((axes.yEnd-axes.origin).length())},
        {L"전체 획 굵기 (%, 100: 기본)",number(plot(id)->inkScale*100)},
        {L"배치","축 교차점은 고정하고 이름·주석·삽입 구조가 따라갑니다."}},preview);
    if(result){if(preview(*result).empty()&&toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}
    else{doc=before;redraw();}
}
void editPlotAppearance(int id,int field){
    const auto* p=plot(id);if(!p||doc.isLocked(id))throw std::runtime_error("조절할 에너지 그래프를 선택하세요.");
    if(field<0||field>9)return;
    const Document before=doc;
    const wchar_t* labels[]={L"축 아래 이름 크기 (pt, 0: 자동)",L"데이터 값 크기 (pt, 0: 자동)",L"X축 굵기 (pt, 0: 자동)",L"Y축 굵기 (pt, 0: 자동)",L"곡선 굵기 (pt, 0: 경로별 설정)",L"삽입 그림 선 굵기 (%, 100: 기본)",L"Y축 제목 간격 (pt, 0: 자동, 최대 300)",L"범례 글자 크기 (pt, 0: 자동, 4~72)",L"물결 굵기 (pt, 0: 자동, 0.1~20)",L"물결·곡선 추가 여백 (pt, -1: 자동, 0~100)"};
    const double current[]={p->stateFontSize,p->valueFontSize,p->xAxisWidth,p->yAxisWidth,p->curveWidth,p->structureInkScale*100,p->yTitleGap,p->legendFontSize,p->waveWidth,p->waveGap};
    auto update=[&](const std::vector<std::string>& values)->std::wstring{try{
        const double value=numeric(values[0],field==5?25:field==9?-1:0,(field<2||field==7)?72:field==5?400:field==6?300:field==8?20:100);
        if((field<2||field==7)&&value>0&&value<4)return L"글자 크기는 0(자동) 또는 4~72 pt로 입력하세요.";
        if(field==8&&value>0&&value<.1)return L"물결 굵기는 0(자동) 또는 0.1~20 pt로 입력하세요.";
        if(field==9&&value<0&&value!=-1)return L"물결 간격은 -1(자동) 또는 0~100 pt로 입력하세요.";
        doc=before;auto& graph=*plot(id);
        double* targets[]={&graph.stateFontSize,&graph.valueFontSize,&graph.xAxisWidth,&graph.yAxisWidth,&graph.curveWidth,&graph.structureInkScale,&graph.yTitleGap,&graph.legendFontSize,&graph.waveWidth,&graph.waveGap};
        *targets[field]=field==5?value/100:value;if(field==1)graph.fixedValueFontSize=value>0;redraw();UpdateWindow(win);return {};
    }catch(const std::exception& e){return ui::wide(e.what());}};
    auto result=ui::form(win,L"그래프 개별 크기·굵기",{{labels[field],number(current[field])}},update);
    if(result){if(update(*result).empty()&&toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}
    else{doc=before;redraw();}
}
void changePlotStyle(int id){
    const auto* p=plot(id);if(!p||doc.isLocked(id))throw std::runtime_error("스타일을 바꿀 에너지 그래프를 선택하세요.");
    const Document before=doc;const int originalMode=p->mode;
    auto preview=[&](int mode){doc=before;plot(id)->mode=mode;redraw();UpdateWindow(win);};
    const auto choice=ui::graphChoice(win,originalMode,true,preview);
    if(!choice){doc=before;redraw();return;}
    preview(choice->mode);
    if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();status=L"그래프 스타일을 변경했습니다. 데이터·구조·주석은 유지됩니다. Ctrl+Z: 되돌리기";}
}
void addBilateralBranch(int id){
    auto* p=plot(id);if(!p)throw std::runtime_error("에너지 그래프를 선택하세요.");
    Document before=doc;EnergyPlot original=*p;std::vector<std::wstring> names;
    for(auto& path:original.series)names.push_back(ui::wide(path.name));if(names.empty())names.push_back(L"Path 1");
    auto update=[&](const std::vector<std::string>& v)->std::wstring{try{
        auto candidate=bilateralPlot(original,size_t(std::stoul(v[0])),v[1]);doc=before;
        relocatePlot(original,candidate);*plot(id)=std::move(candidate);redraw();UpdateWindow(win);return {};
    }catch(const std::exception& e){return ui::wide(e.what());}};
    auto result=ui::form(win,L"양쪽 분기 추가 — 미리보기",{
        {L"왼쪽에 복제할 원본 경로","0",false,names},{L"새 왼쪽 경로 이름","Left branch"},
        {L"배치 안내","첫 상태를 중앙에 두고 양쪽으로 분기합니다."},
        {L"에너지 편집","적용 후 데이터 표에서 좌우 값을 따로 수정하세요."}},update);
    if(result){update(*result);if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();fit();status=L"양쪽 분기를 추가했습니다. 데이터 표에서 좌우 경로의 에너지를 수정하세요. Ctrl+Z로 되돌립니다.";}}
    else{doc=before;redraw();}
}
void publicationLayout(int id){
    auto* p=plot(id);if(!p)throw std::runtime_error("에너지 그래프를 선택하세요.");
    Document before=doc;auto original=*p;auto fitted=publicationPlot(original);
    // Keep neighboring inserted structures clear when reducing the graph width.
    double spacing=0;
    for(bool above:{false,true}){
        double previous=0;for(size_t i=0;i<original.states.size();i++){
            auto& members=above?original.states[i].above:original.states[i].below;
            double width=members.empty()?0:makeScene(doc.subset(members),measure).bounds.w;
            if(i&&(previous>0||width>0))spacing=std::max(spacing,(previous+width)/2+18);previous=width;
        }
    }
    fitted.width=std::max(fitted.width,154.+spacing*(std::max(size_t(1),original.states.size())-1));
    fitted.height=std::max(fitted.height,std::min(600.,fitted.width*.50));
    relocatePlot(original,fitted);*plot(id)=std::move(fitted);history.checkpoint(before);
    status=L"출판용 비율과 자동 글자 크기를 적용했습니다. Ctrl+Z로 되돌릴 수 있습니다.";changed();fit();
}
static std::optional<double> kilojoulesPerUnit(const std::string& unit){
    if(unit=="kcal/mol"||unit=="kcal mol^{-1}"||unit==ui::utf8(L"kcal mol⁻¹"))return 4.184;
    if(unit=="kJ/mol"||unit=="kJ mol^{-1}"||unit==ui::utf8(L"kJ mol⁻¹"))return 1.;
    if(unit=="eV")return 96.4853321233;
    return {};
}
void plotSettings(int id){auto* p=plot(id);if(!p)throw std::runtime_error("에너지 그래프를 선택하세요.");EnergyPlot original=*p;Document before=doc;
    auto update=[&](const std::vector<std::string>& v)->std::wstring{try{EnergyPlot p=original;p.title=v[0];p.unit=v[1]==ui::utf8(L"kcal mol⁻¹")?"kcal mol^{-1}":v[1];p.xLabel=v[2];p.yLabel=v[3];p.xColor=colorValue(v[4]);p.yColor=colorValue(v[5]);auto size=values(v[6]);if(size.size()!=2||size[0]<180||size[1]<140||size[0]>4000||size[1]>4000)return L"크기는 180×140 ~ 4000×4000 pt로 입력하세요.";p.width=size[0];p.height=size[1];p.mode=std::stoi(v[7]);p.showValues=v[8]=="1";p.yTickStep=v[9].empty()?0:numeric(v[9],0);p.yMin=v[10].empty()?std::optional<double>{}:numeric(v[10]);p.yMax=v[11].empty()?std::optional<double>{}:numeric(v[11]);
 p.valueGap=numeric(v[26],0,200);p.yAxisWidth=numeric(v[27],0,100);
 p.axisFontSize=numeric(v[12],0,72);p.tickFontSize=numeric(v[13],0,72);p.valueFontSize=numeric(v[14],0,72);p.energyLabelFontSize=numeric(v[15],0,72);p.thinLines=v[16]=="1";p.boldLevels=v[17]=="1";
 for(double font:{p.axisFontSize,p.tickFontSize,p.valueFontSize,p.energyLabelFontSize})if(font>0&&font<4)return L"글자 크기는 0(자동) 또는 4~72 pt로 입력하세요.";
 if(v[18]=="1"&&p.unit!=original.unit){
     auto from=kilojoulesPerUnit(original.unit),to=kilojoulesPerUnit(p.unit);
     if(!from||!to)return L"수치 변환은 kcal/mol, kJ/mol, eV 사이에서 사용할 수 있습니다.";
     const double factor=*from / *to;
     for(auto& state:p.states)state.energy*=factor;
     for(auto& crossing:p.crossings)crossing.energy*=factor;
     for(auto& series:p.series)for(auto& value:series.values)value*=factor;
     if(p.yMin)*p.yMin*=factor;
     if(p.yMax)*p.yMax*=factor;
     p.yTickStep*=factor;
 }
 if(v[25]!="0"){p.energySymbol=v[25]=="1"?"ΔE":"ΔG";p.yLabel=p.energySymbol;}
 p.showXAxis=v[23]=="1";p.showYAxis=v[24]=="1";p.showYTicks=v[19]=="1";p.energyAtTop=v[20]=="1";p.legendOffset={numeric(v[21],-10000,10000),numeric(v[22],-10000,10000)};
 energyAxis(p);if(p.yTickStep>0&&(p.yTickStep<1e-9||energyAxis(p).step>p.yTickStep*1.001))return L"y축 눈금이 너무 촘촘합니다. 더 큰 간격을 입력하세요.";doc=before;relocatePlot(original,p);*plot(id)=p;redraw();UpdateWindow(win);return {};}catch(const std::exception& e){return ui::wide(e.what());}};
    auto r=ui::form(win,L"그래프·축 설정",{{L"제목",p->title,false,{},false,4,true},{L"에너지 단위 (선택 / 직접 입력)",p->unit=="kcal mol^{-1}"?ui::utf8(L"kcal mol⁻¹"):p->unit,false,{L"kcal/mol",L"kcal mol⁻¹",L"kJ/mol",L"eV"},false,4,true,true},{L"x축 (위·아래첨자 버튼)",p->xLabel,false,{},false,4,true},{L"y축 (위·아래첨자 버튼)",p->yLabel,false,{},false,4,true},{L"x축 글자색",p->xColor,false,{},true},{L"y축 글자색",p->yColor,false,{},true},{L"너비 / 높이 (pt)",number(p->width)+" "+number(p->height)},{L"그래프 형태",std::to_string(p->mode),false,{L"부드러운 곡선",L"직선",L"에너지 준위 (점선 연결)",L"평평한 준위와 직선",L"독립 에너지 준위"}},{L"에너지 값 표시",p->showValues?"1":"0",false,{L"아니요",L"예"}},{L"y축 눈금 간격 (0: 자동)",number(p->yTickStep)},
 {L"y축 최솟값 (빈칸: 자동)",p->yMin?number(*p->yMin):""},{L"y축 최댓값 (빈칸: 자동)",p->yMax?number(*p->yMax):""},
 {L"x축·그래프 제목 (pt, 0: 자동)",number(p->axisFontSize)},{L"눈금·상태 이름 (pt, 0: 자동)",number(p->tickFontSize)},{L"데이터 값 크기 (pt, 0: 자동)",number(p->valueFontSize)},{L"에너지 축 제목 (pt, 0: 자동)",number(p->energyLabelFontSize)},{L"그래프 두께",p->thinLines?"1":"0",false,{L"굵게 (기존)",L"얇게 (선·축·글자)"}},{L"평평한 준위선만 굵게",p->boldLevels?"1":"0",false,{L"아니요 (그래프 두께에 맞춤)",L"예 (기존 굵은 두께 유지)"}},
 {L"단위를 바꿀 때","0",false,{L"단위 표시만 변경",L"에너지와 축 수치도 변환"}},
 {L"y축 눈금·숫자 표시",p->showYTicks?"1":"0",false,{L"숨김",L"표시"}},
 {L"에너지 축 제목 배치",p->energyAtTop?"1":"0",false,{L"세로 축 제목",L"축 위에 E (세로 제목 숨김)"}},
 {L"범례 X 이동 (pt, 오른쪽 +)",number(p->legendOffset.x)},
 {L"범례 Y 이동 (pt, 아래 +)",number(p->legendOffset.y)},
 {L"x축 (선·제목) 표시",p->showXAxis?"1":"0",false,{L"숨김",L"표시"}},
 {L"y축 (선·눈금·제목) 표시",p->showYAxis?"1":"0",false,{L"숨김",L"표시"}},
 {L"에너지 표기", "0",false,{L"현재 표기 유지",L"ΔE · 에너지",L"ΔG · 자유 에너지"}},{L"에너지 값과 곡선 간격 (pt)",number(p->valueGap)},{L"y축 굵기 (pt, 0: 그래프와 동일)",number(p->yAxisWidth)}},update,
 {{L"기본",{0,25,1,18,6}},{L"축",{2,3,9,10,11}},{L"축 표시",{23,24,19,20}},{L"글자",{4,5,8,12,13,14,15,26}},{L"선 스타일",{7,16,17,27}},{L"범례 위치",{21,22}}});if(r){if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}else{doc=before;redraw();}
}
std::optional<PendingEnergyGap> pendingEnergyGap;
void addEnergyGap(int id){
    const auto* p=plot(id);if(!p||doc.isLocked(id)||p->states.size()<2)throw std::runtime_error("에너지 갭을 넣을 그래프를 선택하세요.");
    std::vector<std::wstring> names,paths;for(size_t i=0;i<p->states.size();++i)names.push_back(std::to_wstring(i+1)+L". "+ui::wide(p->states[i].label));
    for(const auto& path:p->series)paths.push_back(ui::wide(path.name));if(paths.empty())paths.push_back(L"Path 1");
    const std::vector<ui::Field> fields={
        {L"출발 상태","0",false,names},{L"도착 상태","1",false,names},{L"경로","0",false,paths},
        {L"갭 문구 (빈칸: "+ui::wide(plotEnergySymbol(*p))+L" 자동 계산)","",true,{},false,2,true},
        {L"배치 방법","적용 후 캔버스에서 원하는 위치를 클릭하세요."}};
    auto validate=[&](const std::vector<std::string>& v)->std::wstring{
        const size_t from=std::stoul(v[0]),to=std::stoul(v[1]),path=std::stoul(v[2]);if(from==to)return L"서로 다른 출발·도착 상태를 선택하세요.";
        if(!p->series.empty()&&(!p->series[path].hasValue(from)||!p->series[path].hasValue(to)))return L"두 상태에 모두 에너지 값이 있어야 합니다.";
        const double a=p->series.empty()?p->states[from].energy:p->series[path].values[from],b=p->series.empty()?p->states[to].energy:p->series[path].values[to];
        if(std::abs(a-b)<1e-10)return L"두 상태의 에너지가 같아 표시할 갭이 없습니다.";
        const auto sc=scene();const auto points=sc.plotSnapPoints.find(id);
        if(points==sc.plotSnapPoints.end()||std::none_of(points->second.begin(),points->second.end(),[&](const auto& point){return point.path==int(path)&&point.state==int(from);})||std::none_of(points->second.begin(),points->second.end(),[&](const auto& point){return point.path==int(path)&&point.state==int(to);}))return L"두 상태가 보이도록 y축 범위를 조정하세요.";
        return {};
    };
    auto result=ui::form(win,L"에너지 갭 추가",fields,validate);if(!result)return;
    pendingEnergyGap=PendingEnergyGap{id,std::stoi((*result)[0]),std::stoi((*result)[1]),std::stoi((*result)[2]),(*result)[3]};
    tool=Select;selection={id};status=L"갭을 넣을 위치를 클릭하세요. 가로: 화살표 위치 · 세로: 문구 위치 · Esc: 취소";redraw();
}
bool placeEnergyGap(Vec at){
    if(!pendingEnergyGap)return false;const auto pending=*pendingEnergyGap;auto* p=plot(pending.owner);
    if(!p||doc.isLocked(pending.owner)){pendingEnergyGap.reset();return true;}
    const auto sc=scene();auto found=sc.plotSnapPoints.find(pending.owner);if(found==sc.plotSnapPoints.end()){pendingEnergyGap.reset();return true;}
    std::optional<Vec> from,to;
    for(const auto& point:found->second)if(point.path==pending.path&&point.edge==0){if(point.state==pending.from)from=point.p;if(point.state==pending.to)to=point.p;}
    if(!from||!to){pendingEnergyGap.reset();return true;}
    auto& state=p->states.at(size_t(pending.to));const auto where=world(at);const double dx=where.x-to->x,dy=where.y-(from->y+to->y)/2;
    if(std::abs(dx)>10000||std::abs(dy)>10000){status=L"그래프 가까이에 갭을 배치하세요. Esc: 취소";redraw();return true;}
    history.checkpoint(doc);state.gapFrom=pending.from;state.gapPath=pending.path;state.gapLabel=pending.label;
    state.gapOffset={dx,std::abs(dy)<1e-6?1e-6:dy};pendingEnergyGap.reset();changed();status=L"에너지 갭을 추가했습니다. 갭 문구를 드래그해 이동 · TS·위치에서 수정·제거 · Ctrl+Z: 되돌리기";return true;
}
std::optional<std::pair<int,size_t>> plotStateAt(Vec at){
    const auto point=world(at);const auto sc=scene();
    if(const auto text=textAt(point);text&&(text->role==6||text->role==8)){
        if(const auto* p=plot(text->owner);p&&!doc.isLocked(p->id)&&text->index>=0&&size_t(text->index)<p->states.size())return std::pair{p->id,size_t(text->index)};
    }
    for(const auto& p:doc.plots)if(!doc.isLocked(p.id))for(size_t i=0;i<p.states.size();++i){
        for(const auto* members:{&p.states[i].above,&p.states[i].below})for(int member:*members){
            auto found=sc.objectBounds.find(member);if(found==sc.objectBounds.end())continue;const auto& b=found->second;const double pad=4/zoom;
            if(point.x>=b.x-pad&&point.x<=b.x+b.w+pad&&point.y>=b.y-pad&&point.y<=b.y+b.h+pad)return std::pair{p.id,i};
        }
    }
    std::optional<std::pair<int,size_t>> result;double distance=12;
    for(const auto& [id,points]:sc.plotSnapPoints)if(!doc.isLocked(id))for(const auto& p:points){
        const double d=(screen(p.p)-at).length();if(d<distance){distance=d;result=std::pair{id,size_t(p.state)};}
    }
    return result;
}
void setPlotTransitionState(int id,size_t index,bool enabled){
    auto* p=plot(id);if(!p||doc.isLocked(id)||index>=p->states.size())return;
    if(p->states[index].transitionState==enabled)return;
    history.checkpoint(doc);p->states[index].transitionState=enabled;selection={id};changed();
    status=enabled?L"TS로 지정했습니다. 삽입 구조에 [ ]‡가 표시됩니다. Ctrl+Z: 되돌리기":L"TS 지정을 해제했습니다. 구조는 유지하고 [ ]‡만 숨깁니다. Ctrl+Z: 되돌리기";
}
void compressPlotEnergyInterval(int id,size_t index){
    auto* p=plot(id);if(!p||doc.isLocked(id)||index+1>=p->states.size()||p->mode==independentEnergyLevels)return;
    auto candidate=*p;auto& state=candidate.states[index];state.breakAfter=true;state.breakStyle=1;state.breakScale=.1;state.breakOmit=50;state.breakUpperScale=3;state.breakLow.reset();state.breakHigh.reset();candidate.yMin.reset();candidate.yMax.reset();
    if(!candidate.series.empty()&&(!candidate.series[size_t(state.breakPath)].hasValue(index)||!candidate.series[size_t(state.breakPath)].hasValue(index+1)))throw std::runtime_error("두 상태의 에너지가 있는 경로를 TS·위치에서 선택하세요.");
    const double a=candidate.series.empty()?candidate.states[index].energy:candidate.series[size_t(state.breakPath)].values[index],b=candidate.series.empty()?candidate.states[index+1].energy:candidate.series[size_t(state.breakPath)].values[index+1];
    if(std::abs(a-b)<1e-9)throw std::runtime_error("두 상태의 에너지가 같아 생략할 구간이 없습니다.");
    validateEnergyAnnotations(candidate);if(energyDisplayBands(candidate).empty())throw std::runtime_error("두 상태의 에너지가 같아 생략할 구간이 없습니다.");
    Document fitted=doc;for(auto& graph:fitted.plots)if(graph.id==id)graph=candidate;
    const auto axes=makeScene(fitted,measure).plotAxes.at(id);const double xLength=(axes.xEnd-axes.origin).length(),yLength=(axes.yEnd-axes.origin).length();
    if(yLength<xLength*.45)fitted=resizedPlotAxes(fitted,id,xLength,xLength*.45,measure);
    const auto beforeScene=makeScene(doc,measure),afterScene=makeScene(fitted,measure);
    const auto fittedGraph=std::find_if(fitted.plots.begin(),fitted.plots.end(),[&](const auto& graph){return graph.id==id;});
    auto statePoint=[&](const Scene& scene,size_t index,int path)->std::optional<Vec>{auto found=scene.plotSnapPoints.find(id);if(found!=scene.plotSnapPoints.end())for(auto point:found->second)if(point.state==int(index)&&point.path==path&&point.edge==0)return point.p;return {};};
    std::set<int> moved;
    for(size_t i=0;i<p->states.size();++i){const auto a=statePoint(beforeScene,i,p->states[i].labelPath),b=statePoint(afterScene,i,p->states[i].labelPath);if(!a||!b)continue;
        for(bool above:{false,true}){std::vector<int> ids;for(int member:above?p->states[i].above:p->states[i].below)if(moved.insert(member).second)ids.push_back(member);
            const auto alreadyMoved=plotAnchor(*fittedGraph,i,above)-plotAnchor(*p,i,above);fitted.translate(ids,*b-*a-alreadyMoved,false);
            if(!ids.empty()){const auto box=makeScene(fitted.subset(ids),measure).bounds;const double gap=std::max(18.,candidate.valueFontSize*1.3);
                if(b->x>=box.x-8&&b->x<=box.x+box.w+8&&b->y>=box.y-gap&&b->y<=box.y+box.h+gap)
                    fitted.translate(ids,{0,above?b->y-gap-box.y-box.h:b->y+gap-box.y},false);}
        }
    }
    history.checkpoint(doc);doc=std::move(fitted);selection={id};changed();status=L"큰 에너지 구간을 생략하고 위쪽 봉우리를 3배 확대했습니다. TS·위치에서 생략량·높이 조정 · Ctrl+Z: 되돌리기";
}
bool plotContextMenu(Vec at){
    if(isolatedStructureEditing||stageCopyOwner)return false;
    if(!finishInline(true))return true;
    const auto target=plotStateAt(at);if(!target)return false;
    const auto [id,index]=*target;selection={id};SetFocus(win);redraw();
    const bool marked=plot(id)->states[index].transitionState;
    auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING|(marked?MF_CHECKED:0),1,marked?L"TS 지정 해제 ([ ]‡ 숨김)":L"TS로 지정 ([ ]‡ 표시)");
    AppendMenuW(menu,MF_STRING,2,L"상태 이름 편집…");AppendMenuW(menu,MF_STRING,3,L"TS·이름 위치 설정…");
    if(index+1<plot(id)->states.size()&&plot(id)->mode!=independentEnergyLevels)AppendMenuW(menu,MF_STRING,4,L"다음 구간 약 50 생략 · 봉우리 3배 (물결선)");
    POINT position{LONG(at.x),LONG(at.y)};ClientToScreen(win,&position);
    const int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,position.x,position.y,0,win,nullptr);DestroyMenu(menu);
    if(choice==1)setPlotTransitionState(id,index,!marked);
    if(choice==2){tool=Text;beginInline(InlineTarget{id,6,int(index),world(at),doc.style.fontSize},world(at));}
    if(choice==3)editPlotAnnotations(id);
    if(choice==4)compressPlotEnergyInterval(id,index);
    return true;
}
void editPlotAnnotations(int id){
    auto* p=plot(id);if(!p||doc.isLocked(id)||p->states.empty())throw std::runtime_error("편집할 에너지 그래프를 선택하세요.");
    std::vector<ui::SearchItem> entries;std::vector<std::wstring> states,paths;
    for(size_t i=0;i<p->states.size();++i){auto name=std::to_wstring(i+1)+L". "+ui::wide(p->states[i].label);states.push_back(name);entries.push_back({name,L"이름 위치 · TS [ ]‡ · 다음 구간 // · 에너지 갭"});}
    auto chosen=ui::search(win,L"곡선·TS·에너지 갭 — 상태 선택",entries,L"편집",true);if(!chosen)return;
    const auto index=*chosen;const auto original=*p;const Document before=doc;const auto& state=original.states[index];
    for(const auto& path:original.series)paths.push_back(ui::wide(path.name));if(paths.empty())paths.push_back(L"Path 1");
    std::vector<std::wstring> gapSources={L"표시 안 함"};gapSources.insert(gapSources.end(),states.begin(),states.end());
    std::vector<ui::Field> fields={
        {L"구조 / 상태 이름",state.label,true,{},false,3,true},
        {L"이름 배치",std::to_string(state.labelPlacement),false,{L"x축",L"곡선 위",L"곡선 아래",L"숨김"}},
        {L"이름 기준 경로",std::to_string(state.labelPath),false,paths},
        {L"이름 X 이동 (pt, 오른쪽 +)",number(state.labelOffset.x)},
        {L"이름 Y 이동 (pt, 아래 +)",number(state.labelOffset.y)},
        {L"상태 유형",state.transitionState?"1":"0",false,{L"일반 상태",L"TS · [구조]‡ 표시"}},
        {L"다음 상태까지의 생략 표시",state.breakAfter?(state.breakStyle==1?"2":"1"):"0",false,{L"연결",L"슬래시 //",L"물결선 · 큰 에너지 구간 압축"}},
        {L"// 위치 (5~95%, 구간 왼쪽 기준)",number(state.breakPosition*100)},
        {L"에너지 갭의 출발 상태",std::to_string(state.gapFrom+1),false,gapSources},
        {L"에너지 갭 경로",std::to_string(state.gapPath),false,paths},
        {L"갭 화살표 X (현재 상태 기준 pt)",number(state.gapOffset.x)},
        {L"갭 글자 Y 이동 (pt, 아래 +)",number(state.gapOffset.y)},
        {L"갭 문구 (빈칸: "+ui::wide(plotEnergySymbol(original))+L" 자동 계산)",state.gapLabel,true,{},false,2,true},
        {L"// 생략 스케일 (%, 100: 원래 비율)",number(state.breakScale*100)},
        {L"생략 에너지 구간의 기준 경로",std::to_string(state.breakPath),false,paths},
        {L"압축 구간 하한 (에너지, 빈칸: 자동)",state.breakLow?number(*state.breakLow):""},
        {L"압축 구간 상한 (에너지, 빈칸: 자동)",state.breakHigh?number(*state.breakHigh):""},
        {L"생략할 에너지량 (현재 단위, 0: 비율 사용)",number(state.breakOmit)},
        {L"물결선 위쪽 높이 배율 (1~10)",number(state.breakUpperScale)}
    };
    auto update=[&](const std::vector<std::string>& v)->std::wstring{try{
        auto candidate=original;auto& edit=candidate.states[index];edit.label=v[0];edit.labelPlacement=std::stoi(v[1]);edit.labelPath=std::stoi(v[2]);
        edit.labelOffset={numeric(v[3],-10000,10000),numeric(v[4],-10000,10000)};edit.transitionState=v[5]=="1";edit.breakAfter=v[6]!="0";edit.breakStyle=v[6]=="2"?1:0;
        if(v[15].empty()!=v[16].empty())return L"압축 구간의 하한과 상한을 모두 입력하거나 둘 다 비워 두세요.";
        edit.breakLow=v[15].empty()?std::optional<double>{}:numeric(v[15],-1e6,1e6);edit.breakHigh=v[16].empty()?std::optional<double>{}:numeric(v[16],-1e6,1e6);
        edit.breakOmit=numeric(v[17],0,1e6);edit.breakUpperScale=numeric(v[18],1,10);
        edit.breakScale=numeric(v[13],1,400)/100;edit.breakPath=std::stoi(v[14]);
        if(edit.breakAfter&&(std::abs(edit.breakScale-1)>1e-9||edit.breakOmit>0||edit.breakUpperScale!=1)){
            if(index+1>=candidate.states.size())return L"생략할 다음 상태가 없습니다.";
            double a=candidate.states[index].energy,b=candidate.states[index+1].energy;
            if(!candidate.series.empty()){const auto& reference=candidate.series[size_t(edit.breakPath)];
                if(!reference.hasValue(index)||!reference.hasValue(index+1))return L"생략 스케일의 기준 경로에는 두 상태의 에너지가 모두 필요합니다.";a=reference.values[index];b=reference.values[index+1];}
            if(std::abs(a-b)<1e-9)return L"같은 에너지 사이에는 생략 스케일을 적용할 구간이 없습니다.";
        }
        edit.breakPosition=numeric(v[7],5,95)/100;edit.gapFrom=std::stoi(v[8])-1;edit.gapPath=std::stoi(v[9]);
        if(edit.gapFrom==int(index))return L"갭의 출발 상태는 현재 상태와 달라야 합니다.";
        if(edit.breakAfter&&(index+1>=candidate.states.size()||candidate.mode==independentEnergyLevels))return L"구간 생략은 다음 상태로 이어지는 그래프에서 사용하세요.";
        if(edit.gapFrom>=0&&!candidate.series.empty()){const auto& path=candidate.series[size_t(edit.gapPath)];if(!path.hasValue(index)||!path.hasValue(size_t(edit.gapFrom)))return L"갭의 두 상태 모두 선택 경로에 에너지가 있어야 합니다.";}
        edit.gapOffset={numeric(v[10],-10000,10000),numeric(v[11],-10000,10000)};edit.gapLabel=v[12];
        validateEnergyAnnotations(candidate);doc=before;*plot(id)=std::move(candidate);redraw();UpdateWindow(win);return {};
    }catch(const std::exception& e){return ui::wide(e.what());}};
    auto result=ui::form(win,L"곡선·TS·에너지 갭 — "+states[index],fields,update,
        {{L"이름·위치",{0,1,2,3,4}},{L"TS·구간 생략",{5,6,7,14}},{L"생략량·봉우리 높이",{17,18,15,16,13}},{L"에너지 갭",{8,9,10,11,12}}});
    if(result){if(update(*result).empty()&&toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}
    else{doc=before;redraw();}
}
void editPlotCrossings(int id){
    auto* p=plot(id);
    if(p&&p->mode==independentEnergyLevels)
        throw std::runtime_error("스핀 교차점은 경로가 연결된 그래프 형태에서 편집할 수 있습니다.");
    if(!p||doc.isLocked(id)||p->states.size()<2||p->series.size()<2)
        throw std::runtime_error("두 경로와 두 상태 이상이 있는 에너지 그래프를 선택하세요.");
    const Document before=doc;
    const auto original=*p;
    auto stateName=[&](size_t i){
        const auto& label=original.states[i].label;
        return label.empty()?L"상태 "+std::to_wstring(i+1):ui::wide(label);
    };
    std::vector<std::wstring> segments,paths;
    for(size_t i=0;i+1<original.states.size();++i)
        segments.push_back(stateName(i)+L" → "+stateName(i+1));
    for(const auto& path:original.series)
        paths.push_back(path.name.empty()?L"경로 "+std::to_wstring(paths.size()+1):ui::wide(path.name));
    auto distinguishDuplicates=[](std::vector<std::wstring>& names){
        const auto labels=names;
        for(size_t i=0;i<names.size();++i)
            if(std::count(labels.begin(),labels.end(),labels[i])>1)
                names[i]+=L" ("+std::to_wstring(i+1)+L")";
    };
    distinguishDuplicates(segments);
    distinguishDuplicates(paths);

    const auto unit=ui::wide(original.unit=="kcal mol^{-1}"?"kcal mol⁻¹":original.unit);
    const auto count=original.crossings.size();
    size_t index=count;
    if(count){
        std::vector<ui::SearchItem> entries;
        for(const auto& crossing:original.crossings){
            const auto& range=segments[crossing.segment];
            auto detail=paths[crossing.firstPath]+L" / "+paths[crossing.secondPath]+
                        L"   ·   "+ui::wide(number(crossing.energy))+L" "+unit;
            entries.push_back({range+L"   ·   "+ui::wide(crossing.label),std::move(detail)});
        }
        const bool canAdd=count<segments.size();
        auto choices=entries;
        if(canAdd)choices.insert(choices.begin(),{L"새 교차점 추가",L"구간과 두 스핀 경로를 고르고 에너지를 입력합니다."});
        choices.push_back({L"교차점 삭제…",L"그래프에서 제거할 교차점을 선택합니다."});
        const auto choice=ui::search(win,L"스핀 교차점 관리",choices,L"선택",true);
        if(!choice)return;
        if(*choice==choices.size()-1){
            if(auto remove=ui::search(win,L"삭제할 스핀 교차점",entries,L"삭제",true)){
                history.checkpoint(before);
                auto& crossings=plot(id)->crossings;
                crossings.erase(crossings.begin()+*remove);
                changed();
            }
            return;
        }
        if(!canAdd||*choice!=0)index=*choice-size_t(canAdd);
    }

    const bool adding=index==count;
    const auto current=adding?EnergyCrossing{}:original.crossings[index];
    std::vector<int> available;
    std::vector<std::wstring> segmentChoices;
    for(size_t i=0;i<segments.size();++i){
        if((adding||int(i)!=current.segment)&&std::any_of(original.crossings.begin(),original.crossings.end(),[&](const EnergyCrossing& crossing){
            return crossing.segment==int(i);
        }))continue;
        available.push_back(int(i));
        segmentChoices.push_back(segments[i]);
    }
    const auto selectedSegment=adding?0:std::find(available.begin(),available.end(),current.segment)-available.begin();
    std::vector<ui::Field> fields={
        {L"교차 구간",std::to_string(selectedSegment),false,segmentChoices},
        {L"첫 번째 스핀 경로",std::to_string(current.firstPath),false,paths},
        {L"두 번째 스핀 경로",std::to_string(current.secondPath),false,paths},
        {unit.empty()?L"교차 에너지":L"교차 에너지 ("+unit+L")",adding?"":number(current.energy)},
        {L"구간 내 가로 위치 (%)\n0 왼쪽 · 50 중앙 · 100 오른쪽",number(current.position*100)},
        {L"교차점 이름 (빈칸: 경로 이름)",current.label,true,{},false,2,true},
        {L"라벨 X (pt, 오른쪽 +)",current.labelOffset?number(current.labelOffset->x):""},
        {L"라벨 Y (pt, 아래 +)\nX와 Y 모두 빈칸이면 자동",current.labelOffset?number(current.labelOffset->y):""},
        {L"지시선 굵기 (pt, 0: 숨김)",number(current.leaderWidth)}
    };
    auto update=[&](const std::vector<std::string>& input)->std::wstring{
        try{
            auto crossing=current;
            crossing.segment=available.at(size_t(std::stoi(input[0])));
            crossing.firstPath=std::stoi(input[1]);
            crossing.secondPath=std::stoi(input[2]);
            if(crossing.firstPath==crossing.secondPath)return L"서로 다른 두 스핀 경로를 선택하세요.";
            if(input[3].empty())return L"교차점의 에너지를 입력하세요.";
            if(adding||input[3]!=fields[3].value){
                try{crossing.energy=numeric(input[3],-1e6,1e6);}
                catch(const std::exception&){return L"교차 에너지를 −1000000~1000000 사이 숫자로 입력하세요.";}
            }
            if(input[4]!=fields[4].value){
                try{crossing.position=numeric(input[4],0,100)/100;}
                catch(const std::exception&){return L"가로 위치를 0보다 크고 100보다 작은 숫자로 입력하세요.";}
                if(crossing.position<=0||crossing.position>=1)
                    return L"가로 위치를 0보다 크고 100보다 작은 숫자로 입력하세요.";
            }
            crossing.label=input[5].find_first_not_of(" \t\r\n")==std::string::npos
                ?ui::utf8(paths.at(size_t(crossing.firstPath))+L" / "+paths.at(size_t(crossing.secondPath)))
                :input[5];
            if(input[6]!=fields[6].value||input[7]!=fields[7].value){
                if(input[6].empty()!=input[7].empty())return L"라벨 X와 Y를 모두 입력하거나 둘 다 비워 두세요.";
                try{
                    if(input[6].empty())crossing.labelOffset.reset();
                    else{
                        auto offset=current.labelOffset.value_or(Vec{});
                        if(input[6]!=fields[6].value)offset.x=numeric(input[6],-1e4,1e4);
                        if(input[7]!=fields[7].value)offset.y=numeric(input[7],-1e4,1e4);
                        crossing.labelOffset=offset;
                    }
                }catch(const std::exception&){return L"라벨 X와 Y를 −10000~10000 사이 숫자로 입력하세요.";}
            }
            if(input[8]!=fields[8].value){
                try{crossing.leaderWidth=numeric(input[8],0,6);}
                catch(const std::exception&){return L"지시선 굵기를 0~6 pt 사이 숫자로 입력하세요.";}
            }
            auto candidate=original;
            if(adding)candidate.crossings.push_back(std::move(crossing));
            else candidate.crossings[index]=std::move(crossing);
            validateEnergyCrossings(candidate);
            *plot(id)=std::move(candidate);
            redraw();UpdateWindow(win);
            return {};
        }catch(const std::exception& e){return ui::wide(e.what());}
    };
    const auto result=ui::form(win,adding?L"스핀 교차점 추가":L"스핀 교차점 편집",fields,update,
        {{L"교차점",{0,1,2,3,4,5}},{L"라벨 위치·지시선",{6,7,8}}});
    if(result){
        if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}
    }else{*plot(id)=original;redraw();}
}
void editPlotStages(int id){
    auto* p=plot(id);if(!p)throw std::runtime_error("에너지 그래프를 선택하세요.");Document before=doc;
    auto stages=p->stages.empty()?defaultMechanismStages():p->stages;ui::Table rows;
    for(auto& stage:stages)rows.push_back({stage.label,number(stage.start),number(stage.end),number(stage.fontSize),number(stage.arrowWidth),number(stage.arrowHeadSize)});
    auto update=[&](const ui::Table& cells)->std::wstring{try{
        auto candidate=mechanismStageData(cells);doc=before;plot(id)->stages=std::move(candidate);redraw();UpdateWindow(win);return {};
    }catch(const std::exception& e){return ui::wide(e.what());}};
    auto result=ui::table(win,L"반응 이름·화살표 범위",{L"반응 이름",L"시작 (%)",L"끝 (%)",L"글자 크기 (pt, 0: 자동)",L"화살표 굵기 (0: 자동, 0.1~100 pt)",L"화살촉 길이 (pt, 0: 자동)"},rows,update,true,
        L"x축 위에 ← 반응 이름 → 표시 · 범위: x축 전체 폭의 0~100% (최소 1%) · 겹치는 범위는 다른 줄에 표시 · 글자: 0(상태 이름과 같은 크기) 또는 4~72 pt · 화살표: 굵기 0(자동) / 0.1~100 pt, 화살촉 0(구간 길이에 맞춤) / 0.1~40 pt · 긴 설명은 줄바꿈 · 모두 삭제하면 숨김");
    if(result){update(*result);if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}
    else{doc=before;redraw();}
}
void snapPlotStages(int id){
    auto* p=plot(id);if(!p||doc.isLocked(id))throw std::runtime_error("편집할 에너지 그래프를 선택하세요.");
    auto sc=scene();if(sc.plotSnapPoints[id].empty())throw std::runtime_error("축 범위 안에 에너지 준위가 없습니다.");
    std::vector<std::wstring> names={L"새 반응 이름 추가"};for(auto& stage:p->stages)names.push_back(ui::wide(stage.label));
    auto choice=ui::form(win,L"화살표 자석 배치",{{L"배치할 반응 이름","0",false,names}});if(!choice)return;
    int index=std::stoi((*choice)[0])-1;
    if(index<0&&p->stages.size()>=100)throw std::runtime_error("반응 이름은 최대 100개입니다.");
    MechanismStage label=index<0?MechanismStage{"Mechanism",0,100}:p->stages[size_t(index)];
    auto text=ui::form(win,L"반응 이름",{{L"반응 이름",label.label,false,{},false,4,true},{L"글자 크기 (pt, 0: 자동)",number(label.fontSize)},{L"화살표 굵기 (0: 자동, 0.1~100 pt)",number(label.arrowWidth)},{L"화살촉 길이 (pt, 0: 자동)",number(label.arrowHeadSize)}});if(!text)return;
    auto checked=mechanismStageData({{(*text)[0],"0","100",(*text)[1],(*text)[2],(*text)[3]}});if(checked.empty())return;
    snapLabel=checked.front();snapPlot=id;snapStage=index;snapStart.reset();selection={id};tool=Select;
    status=L"자석 배치: 준위의 왼쪽 끝·중앙·오른쪽 끝 중 첫 점을 클릭하세요. Esc: 취소";redraw();
}
void editPlotColors(int id){
    auto* p=plot(id);if(!p||doc.isLocked(id))throw std::runtime_error("편집할 에너지 그래프를 선택하세요.");
    EnergyPlot original=*p;if(original.series.empty()){EnergySeries path;for(auto& state:original.states)path.values.push_back(state.energy);original.series.push_back(path);}
    if(original.states.empty())throw std::runtime_error("먼저 에너지 데이터를 입력하세요.");
    std::vector<std::wstring> paths,states;for(auto& path:original.series)paths.push_back(ui::wide(path.name));
    for(size_t i=0;i<original.states.size();i++)states.push_back(std::to_wstring(i+1)+L" · "+ui::wide(original.states[i].label));
    Document before=doc;
    auto update=[&](const std::vector<std::string>& v)->std::wstring{try{
        size_t k=std::stoul(v[0]),a=std::stoul(v[1]),b=std::stoul(v[2]);if(a>b)std::swap(a,b);
        std::string color=v[5]=="1"?"":colorValue(v[4]);int scope=std::stoi(v[3]);
        auto candidate=original;auto& path=candidate.series.at(k);path.levelColors.resize(candidate.states.size());path.segmentColors.resize(candidate.states.size()-1);
        if(scope!=2)for(size_t i=a;i<=b;i++)path.levelColors[i]=color;
        if(scope!=1)for(size_t i=a;i<b;i++)path.segmentColors[i]=color;
        doc=before;*plot(id)=std::move(candidate);redraw();UpdateWindow(win);return {};
    }catch(const std::exception& e){return ui::wide(e.what());}};
    auto result=ui::form(win,L"구간별 색상 — 미리보기",{
        {L"경로","0",false,paths},{L"시작 준위","0",false,states},{L"끝 준위",std::to_string(original.states.size()-1),false,states},
        {L"적용 대상","0",false,{L"준위·표식과 사이 연결선",L"준위·표식만",L"사이 연결선만"}},
        {L"구간 색상",original.series[0].color,false,{},true},{L"색상 설정","0",false,{L"선택한 색상 적용",L"경로 기본 색상으로 복원"}}},update);
    if(result){auto error=update(*result);if(!error.empty()){doc=before;throw std::runtime_error(ui::utf8(error));}if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}
    else{doc=before;redraw();}
}
void editSeries(int id,bool add){
    auto* p=plot(id);if(!p)return;int index=0;
    if(!add&&p->series.empty())throw std::runtime_error("그래프에 경로가 없습니다.");
    if(!add&&p->series.size()>1){std::vector<std::wstring> names;for(auto& s:p->series)names.push_back(ui::wide(s.name));auto choice=ui::form(win,L"경로 선택",{{L"경로","0",false,names}});if(!choice)return;index=std::stoi((*choice)[0]);}
    Document before=doc;EnergySeries original=add?EnergySeries{}:p->series.at(index);
    if(add){original.name="Path "+std::to_string(p->series.size()+1);original.color="#b44758";for(auto& state:p->states)original.values.push_back(state.energy);if(!p->series.empty())original.present=p->series[0].present;}
    auto update=[&](const std::vector<std::string>& v)->std::wstring{
        try{
            EnergySeries s=original;s.name=v[0];s.values=original.values;
            s.color=colorValue(v[2]);s.dash=std::stoi(v[3]);s.width=numeric(v[4],.1,10);s.marker=std::stoi(v[5]);s.markerSize=numeric(v[6],.5,12);
            doc=before;auto* current=plot(id);if(add)current->series.push_back(s);else current->series[size_t(index)]=s;
            if(!add&&index==0)for(size_t i=0;i<current->states.size();i++)current->states[i].energy=s.values[i];redraw();UpdateWindow(win);return {};
        }catch(const std::exception& e){return ui::wide(e.what());}
    };
    auto result=ui::form(win,L"경로 스타일 — 미리보기",{
        {L"경로 이름",original.name,false,{},false,4,true},{L"에너지 편집 안내","에너지 값은 [표 편집]에서 수정하세요."},
        {L"선 색상",original.color,false,{},true},
        {L"선 종류",std::to_string(original.dash),false,{L"실선",L"파선",L"점선"}},
        {L"선 굵기 (pt)",number(original.width)},
        {L"기본 표식",std::to_string(original.marker),false,{L"없음",L"원",L"사각",L"마름모",L"삼각"}},
        {L"표식 반지름 (pt)",number(original.markerSize)}
    },update);
    if(result){if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}
    else{doc=before;redraw();}
}

} // namespace yd::editor

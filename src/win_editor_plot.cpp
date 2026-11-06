#include "win_editor_plot.h"
#include "win_editor_state.h"
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
    std::vector<std::wstring> columns={L"단계명"};for(auto& path:original.series)columns.push_back(ui::wide(path.name));ui::Table rows;
    for(size_t i=0;i<original.states.size();i++){std::vector<std::string> row={original.states[i].label};for(auto& path:original.series)row.push_back(path.hasValue(i)?number(path.values[i]):"");rows.push_back(row);}
    auto update=[&](const ui::Table& data)->std::wstring{
        try{auto p=energyGridData(original,data);doc=before;relocatePlot(original,p);*plot(id)=std::move(p);redraw();UpdateWindow(win);return {};}
        catch(const std::exception& e){return ui::wide(e.what());}
    };
    auto cellError=[](size_t column,const std::string& value)->std::wstring{
        if(column==0||validEnergyGridValue(value))return {};
        return L"에너지를 숫자로 입력하세요 (−1000000~1000000). 빈칸은 그래프에서 생략됩니다.";
    };
    auto result=ui::table(win,L"에너지 그래프 — 데이터 표",columns,rows,update,true,L"",false,0,cellError);
    if(result){
        // Apply the returned table explicitly; committing must not depend on a prior preview.
        update(*result);
        if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}
    }else{doc=before;redraw();}

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
 p.axisFontSize=numeric(v[12],0,72);p.tickFontSize=numeric(v[13],0,72);p.valueFontSize=numeric(v[14],0,72);p.energyLabelFontSize=numeric(v[15],0,72);p.thinLines=v[16]=="1";p.boldLevels=v[17]=="1";
 for(double font:{p.axisFontSize,p.tickFontSize,p.valueFontSize,p.energyLabelFontSize})if(font>0&&font<4)return L"글자 크기는 0(자동) 또는 4~72 pt로 입력하세요.";
 if(v[18]=="1"&&p.unit!=original.unit){
     auto from=kilojoulesPerUnit(original.unit),to=kilojoulesPerUnit(p.unit);
     if(!from||!to)return L"수치 변환은 kcal/mol, kJ/mol, eV 사이에서 사용할 수 있습니다.";
     const double factor=*from / *to;
     for(auto& state:p.states)state.energy*=factor;
     for(auto& series:p.series)for(auto& value:series.values)value*=factor;
     if(p.yMin)*p.yMin*=factor;
     if(p.yMax)*p.yMax*=factor;
     p.yTickStep*=factor;
 }
 energyAxis(p);if(p.yTickStep>0&&(p.yTickStep<1e-9||energyAxis(p).step>p.yTickStep*1.001))return L"y축 눈금이 너무 촘촘합니다. 더 큰 간격을 입력하세요.";doc=before;relocatePlot(original,p);*plot(id)=p;redraw();UpdateWindow(win);return {};}catch(const std::exception& e){return ui::wide(e.what());}};
    auto r=ui::form(win,L"그래프·축 설정",{{L"제목",p->title,false,{},false,4,true},{L"에너지 단위 (선택 / 직접 입력)",p->unit=="kcal mol^{-1}"?ui::utf8(L"kcal mol⁻¹"):p->unit,false,{L"kcal/mol",L"kcal mol⁻¹",L"kJ/mol",L"eV"},false,4,true,true},{L"x축 (위·아래첨자 버튼)",p->xLabel,false,{},false,4,true},{L"y축 (위·아래첨자 버튼)",p->yLabel,false,{},false,4,true},{L"x축 글자색",p->xColor,false,{},true},{L"y축 글자색",p->yColor,false,{},true},{L"너비 / 높이 (pt)",number(p->width)+" "+number(p->height)},{L"그래프 형태",std::to_string(p->mode),false,{L"부드러운 곡선",L"직선",L"에너지 준위 (점선 연결)",L"평평한 준위와 직선"}},{L"에너지 값 표시",p->showValues?"1":"0",false,{L"아니요",L"예"}},{L"y축 눈금 간격 (0: 자동)",number(p->yTickStep)},
 {L"y축 최솟값 (빈칸: 자동)",p->yMin?number(*p->yMin):""},{L"y축 최댓값 (빈칸: 자동)",p->yMax?number(*p->yMax):""},
 {L"x축·그래프 제목 (pt, 0: 자동)",number(p->axisFontSize)},{L"눈금·상태 이름 (pt, 0: 자동)",number(p->tickFontSize)},{L"데이터 값 크기 (pt, 0: 자동)",number(p->valueFontSize)},{L"에너지 축 제목 (pt, 0: 자동)",number(p->energyLabelFontSize)},{L"그래프 두께",p->thinLines?"1":"0",false,{L"굵게 (기존)",L"얇게 (선·축·글자)"}},{L"평평한 준위선만 굵게",p->boldLevels?"1":"0",false,{L"아니요 (그래프 두께에 맞춤)",L"예 (기존 굵은 두께 유지)"}},
 {L"단위를 바꿀 때","0",false,{L"단위 표시만 변경",L"에너지와 축 수치도 변환"}}},update,
 {{L"기본",{0,1,18,6}},{L"축",{2,3,9,10,11}},{L"글자",{4,5,8,12,13,14,15}},{L"선 스타일",{7,16,17}}});if(r){if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}else{doc=before;redraw();}
}
void editPlotStages(int id){
    auto* p=plot(id);if(!p)throw std::runtime_error("에너지 그래프를 선택하세요.");Document before=doc;
    auto stages=p->stages.empty()?defaultMechanismStages():p->stages;ui::Table rows;
    for(auto& stage:stages)rows.push_back({stage.label,number(stage.start),number(stage.end),number(stage.fontSize)});
    auto update=[&](const ui::Table& cells)->std::wstring{try{
        auto candidate=mechanismStageData(cells);doc=before;plot(id)->stages=std::move(candidate);redraw();UpdateWindow(win);return {};
    }catch(const std::exception& e){return ui::wide(e.what());}};
    auto result=ui::table(win,L"반응 이름·화살표 범위",{L"반응 이름",L"시작 (%)",L"끝 (%)",L"글자 크기 (pt, 0: 자동)"},rows,update,true,
        L"x축 위에 ← 반응 이름 → 표시 · 범위: x축 전체 폭의 0~100% (최소 1%) · 겹치는 범위는 다른 줄에 표시 · 글자: 0(상태 이름과 같은 크기) 또는 4~72 pt · 긴 설명은 줄바꿈 · 모두 삭제하면 숨김");
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
    auto text=ui::form(win,L"반응 이름",{{L"반응 이름",label.label,false,{},false,4,true},{L"글자 크기 (pt, 0: 자동)",number(label.fontSize)}});if(!text)return;
    auto checked=mechanismStageData({{(*text)[0],"0","100",(*text)[1]}});if(checked.empty())return;
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

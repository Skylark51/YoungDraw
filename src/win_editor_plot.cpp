#include "win_editor_plot.h"
#include "win_plot_manager.h"
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
void editPlot(int id){
    if(!plot(id))return;managePlot(id);SendMessageW(plotManagerWindow,WM_COMMAND,4000,0);SendMessageW(plotManagerWindow,WM_COMMAND,131,0);
}
std::optional<AxisTitleHit> plotAxisTitleAt(const Scene& scene,Vec point,double tolerance){
    for(auto i=scene.items.rbegin();i!=scene.items.rend();++i){
        const auto& text=*i;
        if(text.type!=Primitive::Text||text.points.empty()||!isPlotAxisTitle(text.textRole)||!plot(text.owner)||doc.isLocked(text.owner))continue;
        const auto delta=point-text.points.front();const double angle=-text.rotation*pi/180;
        const Vec local{std::cos(angle)*delta.x-std::sin(angle)*delta.y,std::sin(angle)*delta.x+std::cos(angle)*delta.y};
        const double width=measure(text.text,text.fontSize,text.font,text.face),height=text.fontSize*1.3;
        if(local.x>=-tolerance&&local.x<=width+tolerance&&local.y>=-tolerance&&local.y<=height+tolerance)
            return AxisTitleHit{text.owner,text.textRole==plotXAxisTitleTextRole};
    }
    return {};
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
        ui::numericField(L"가로축 길이 (pt)",(axes.xEnd-axes.origin).length(),100,4000,5),
        ui::numericField(L"세로축 길이 (pt)",(axes.yEnd-axes.origin).length(),70,4000,5),
        ui::numericField(L"전체 획 굵기 (%, 100: 기본)",plot(id)->inkScale*100,25,400,5),
        {L"조절","숫자 위에서 휠 또는 ↑ / ↓ 키로 조절할 수 있습니다."}},preview);
    if(result){if(preview(*result).empty()&&toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}
    else{doc=before;redraw();}
}
namespace {
struct PlotNumber {
    double value, minimum, maximum, increment, firstPositive, automaticStart;
    double step(int steps) const {
        if(value<=0&&steps>0)return std::clamp(automaticStart+(steps-1)*increment,firstPositive,maximum);
        if(value<=0&&steps<0)return value;
        const double next=value+steps*increment;
        return next<firstPositive&&minimum<firstPositive?0.:std::clamp(next,minimum,maximum);
    }
};
PlotNumber plotNumber(const EnergyPlot& p,int field){
    const double axisStart=std::max(.8,doc.style.lineWidth*.6);
    const PlotNumber settings[]={
        {p.stateFontSize,0,72,1,4,doc.style.fontSize},
        {p.valueFontSize,0,72,1,4,doc.style.fontSize},
        {p.showXAxis?(p.xAxisWidth>0?p.xAxisWidth:-1):0,-1,100,.1,.1,axisStart},
        {p.showYAxis?(p.yAxisWidth>0?p.yAxisWidth:-1):0,-1,100,.1,.1,axisStart},
        {p.curveWidth,0,100,.1,.1,p.series.empty()?doc.style.lineWidth:p.series.front().width},
        {p.structureInkScale*100,25,400,10,25,100},
        {p.yTitleGap,0,300,1,1,1},
        {p.waveWidth,0,20,.1,.1,1},
        {p.waveGap,-1,100,1,0,1}
    };
    return settings[field];
}
void setPlotNumber(EnergyPlot& graph,int field,double value){
    double* targets[]={&graph.stateFontSize,&graph.valueFontSize,&graph.xAxisWidth,&graph.yAxisWidth,
        &graph.curveWidth,&graph.structureInkScale,&graph.yTitleGap,&graph.waveWidth,&graph.waveGap};
    *targets[field]=field==5?value/100:(field==2||field==3)?std::max(0.,value):value;
    if(field==2)graph.showXAxis=value!=0;
    if(field==3)graph.showYAxis=value!=0;
    if(field==1)graph.fixedValueFontSize=value>0;
}
}
bool stepPlotAppearance(int id,int action,int steps){
    const int field=action>=plotAppearanceFirstCommand&&action<=plotAppearanceLastCommand?action-plotAppearanceFirstCommand:
        action==plotWaveWidthCommand?7:action==plotWaveGapCommand?8:-1;
    if(field<0)return false;
    auto* graph=plot(id);
    if(!graph||doc.isLocked(id)||!steps)return true;
    if(field==4){
        if(graph->series.size()>1)return true;
        const double current=graph->curveWidth>0?graph->curveWidth/(graph->inkScale*(graph->thinLines?.5:1.)):
            graph->series.empty()?EnergySeries{}.width:graph->series.front().width;
        const double next=std::clamp(current+steps*.1,.1,100.);
        if(std::abs(next-current)<1e-8)return true;
        history.checkpoint(doc);
        if(graph->series.empty()){EnergySeries path;for(const auto& state:graph->states)path.values.push_back(state.energy);graph->series.push_back(std::move(path));}
        graph->curveWidth=0;graph->series.front().width=next;changed();return true;
    }
    const auto setting=plotNumber(*graph,field);
    const double next=setting.step(steps);
    if(std::abs(next-setting.value)<1e-8)return true;
    history.checkpoint(doc);setPlotNumber(*graph,field,next);changed();
    return true;
}
static std::optional<double> kilojoulesPerUnit(const std::string& unit);
static std::string energyTitleChoice(const EnergyPlot& plot){return plot.yLabel=="ΔE"?"1":plot.yLabel=="ΔG"?"2":"0";}

struct ManagedAxisFields {
    std::vector<ui::Field> fields;
    std::vector<std::pair<std::wstring,std::vector<size_t>>> tabs;
    std::function<std::wstring(Document&,const std::vector<std::string>&)> update;
};
static ManagedAxisFields managedAxisFields(const Document& before,int id,bool xAxis){
    const auto original=*std::find_if(before.plots.begin(),before.plots.end(),[&](const auto& p){return p.id==id;});
    const auto axes=makeScene(before,measure).plotAxes.at(id);
    const auto initialLength=number(xAxis?(axes.xEnd-axes.origin).length():(axes.yEnd-axes.origin).length());
    const auto unit=original.unit=="kcal mol^{-1}"?ui::utf8(L"kcal mol⁻¹"):original.unit;
    std::vector<ui::Field> fields={
        ui::numericField(L"축선 굵기 (pt, 0: 자동)",xAxis?original.xAxisWidth:original.yAxisWidth,0,100,.5),
        ui::numericField(L"화살촉 길이 (pt, 0: 자동)",xAxis?original.xAxisHeadLength:original.yAxisHeadLength,0,100,.5),
        ui::numericField(L"화살촉 너비 (pt, 0: 자동)",xAxis?original.xAxisHeadWidth:original.yAxisHeadWidth,0,100,.5),
        {L"축 길이 (pt)",initialLength},
        {L"축 제목",xAxis?original.xLabel:original.yLabel,false,{},false,4,true},
        {L"축 글자색",xAxis?original.xColor:original.yColor,false,{},true},
        ui::numericField(L"제목 크기 (pt, 0: 자동)",xAxis?original.axisFontSize:original.energyLabelFontSize,0,72,1)
    };
    std::vector<std::pair<std::wstring,std::vector<size_t>>> tabs={{L"축선·화살촉",{0,1,2,3}},{L"제목",{4,5,6}}};
    if(!xAxis){
        const std::vector<ui::Field> yFields={
            ui::numericField(L"Y축 제목 간격 (pt, 0: 자동)",original.yTitleGap,0,300,1),
            {L"제목 배치",original.energyAtTop?"1":"0",false,{L"세로 축 제목",L"축 위에 표시"}},
            {L"에너지 표기",energyTitleChoice(original),false,{L"직접 입력한 제목",L"ΔE · 에너지",L"ΔG · 자유 에너지"}},
            {L"에너지 단위",unit,false,{L"kcal/mol",L"kcal mol⁻¹",L"kJ/mol",L"eV"},false,4,true,true},
            {L"단위를 바꿀 때","0",false,{L"단위 표시만 변경",L"에너지와 축 수치도 변환"}},
            {L"최솟값 (빈칸: 자동)",original.yMin?number(*original.yMin):""},
            {L"최댓값 (빈칸: 자동)",original.yMax?number(*original.yMax):""},
            ui::numericField(L"눈금 간격 (0: 자동)",original.yTickStep,0,1e8,1),
            {L"눈금·숫자 표시",original.showYTicks?"1":"0",false,{L"숨김",L"표시"}},
            ui::numericField(L"눈금 글자 크기 (pt, 0: 자동)",original.tickFontSize,0,72,1),
            {L"Y축 제목의 단위 표기",original.yUnitParenthetical?"1":"0",false,
                {L"ΔE / (kcal mol⁻¹) · IUPAC",L"ΔE (kcal mol⁻¹) · 괄호"}}
        };
        fields.insert(fields.end(),yFields.begin(),yFields.end());
        fields[9].choiceUpdate=ui::ChoiceUpdate{4,{std::nullopt,"ΔE","ΔG"}};
        fields[9].quickChoices={{1,L"ΔE"},{2,L"ΔG"}};
        tabs[1].second.insert(tabs[1].second.end(),{7,8,9});
        tabs.push_back({L"범위·눈금",{12,13,14,15,16}});
        tabs.push_back({L"단위",{10,11,17}});
    }
    const size_t namePlacementIndex=fields.size();
    if(xAxis){
        fields.push_back({L"반응물·TS·생성물 이름","0",false,{L"현재 배치 유지",L"X축에 표시",L"에너지 데이터 아래에 표시"}});
        fields.push_back(ui::numericField(L"상태 이름 크기 (pt, 0: 자동)",original.stateFontSize,0,72,1));
        tabs.push_back({L"상태 이름",{namePlacementIndex,namePlacementIndex+1}});
    }
    const size_t visibilityIndex=fields.size();
    ui::Field visibility{L"축 표시 (클릭하여 전환)",(xAxis?original.showXAxis:original.showYAxis)?"1":"0"};
    visibility.toggle=true;
    fields.push_back(std::move(visibility));
    tabs[0].second.insert(tabs[0].second.begin(),visibilityIndex);
    auto update=[=](Document& candidate,const std::vector<std::string>& values)->std::wstring{try{
        auto& graph=*std::find_if(candidate.plots.begin(),candidate.plots.end(),[&](const auto& p){return p.id==id;});
        (xAxis?graph.xAxisWidth:graph.yAxisWidth)=numeric(values[0],0,100);
        (xAxis?graph.showXAxis:graph.showYAxis)=values[visibilityIndex]=="1";
        (xAxis?graph.xAxisHeadLength:graph.yAxisHeadLength)=numeric(values[1],0,100);
        (xAxis?graph.xAxisHeadWidth:graph.yAxisHeadWidth)=numeric(values[2],0,100);
        (xAxis?graph.xLabel:graph.yLabel)=values[4];
        (xAxis?graph.xColor:graph.yColor)=colorValue(values[5]);
        auto font=[&](size_t index){
            const double size=numeric(values[index],0,72);
            if(size>0&&size<4)throw std::runtime_error("글자 크기는 0(자동) 또는 4~72 pt로 입력하세요.");
            return size;
        };
        (xAxis?graph.axisFontSize:graph.energyLabelFontSize)=font(6);
        if(xAxis){
            graph.stateFontSize=font(namePlacementIndex+1);
            if(values[namePlacementIndex]!="0"){
                const int placement=values[namePlacementIndex]=="1"?0:4;
                for(size_t i=0;i<graph.states.size();++i){
                    auto& state=graph.states[i];
                    if(state.labelPlacement!=placement)state.labelOffset={};
                    state.labelPlacement=placement;
                    if(placement==4&&!graph.series.empty()&&(state.labelPath<0||
                       size_t(state.labelPath)>=graph.series.size()||!graph.series[state.labelPath].hasValue(i))){
                        const auto path=std::find_if(graph.series.begin(),graph.series.end(),[&](const auto& series){return series.hasValue(i);});
                        state.labelPath=path==graph.series.end()?0:int(path-graph.series.begin());
                    }
                }
            }
        }else{
            graph.yTitleGap=numeric(values[7],0,300);
            graph.energyAtTop=values[8]=="1";
            if(values[9]!="0"){graph.energySymbol=values[9]=="1"?"ΔE":"ΔG";graph.yLabel=graph.energySymbol;}
            else if(graph.yLabel=="ΔE"||graph.yLabel=="ΔG")graph.energySymbol=graph.yLabel;
            graph.unit=values[10]==ui::utf8(L"kcal mol⁻¹")?"kcal mol^{-1}":values[10];
            graph.yMin=values[12].empty()?std::optional<double>{}:numeric(values[12]);
            graph.yMax=values[13].empty()?std::optional<double>{}:numeric(values[13]);
            graph.yTickStep=numeric(values[14],0,1e8);
            graph.showYTicks=values[15]=="1";
            graph.tickFontSize=font(16);
            graph.yUnitParenthetical=values[17]=="1";
            if(values[11]=="1"&&graph.unit!=original.unit){
                const auto from=kilojoulesPerUnit(original.unit),to=kilojoulesPerUnit(graph.unit);
                if(!from||!to)return L"수치 변환은 kcal/mol, kJ/mol, eV 사이에서 사용할 수 있습니다.";
                const double factor=*from / *to;
                for(auto& state:graph.states){
                    state.energy*=factor;
                    state.breakOmit*=factor;
                    if(state.breakLow)*state.breakLow*=factor;
                    if(state.breakHigh)*state.breakHigh*=factor;
                }
                for(auto& crossing:graph.crossings)crossing.energy*=factor;
                for(auto& series:graph.series)for(auto& value:series.values)value*=factor;
                if(graph.yMin)*graph.yMin*=factor;
                if(graph.yMax)*graph.yMax*=factor;
                graph.yTickStep*=factor;
            }
            const auto axis=energyAxis(graph);
            if(graph.yTickStep>0&&(graph.yTickStep<1e-9||axis.step>graph.yTickStep*1.001))
                return L"Y축 눈금이 너무 촘촘합니다. 더 큰 간격을 입력하세요.";
        }
        if(values[3]!=initialLength){
            const double length=numeric(values[3],xAxis?100:70,4000);
            const auto current=makeScene(candidate,measure).plotAxes.at(id);
            candidate=resizedPlotAxes(candidate,id,xAxis?length:(current.xEnd-current.origin).length(),
                                      xAxis?(current.yEnd-current.origin).length():length,measure);
        }
        return {};
    }catch(const std::exception& e){return ui::wide(e.what());}};
    return {std::move(fields),std::move(tabs),std::move(update)};
}

static void editManagedAxis(int id,bool xAxis){
    const Document before=doc;
    const auto settings=managedAxisFields(before,id,xAxis);
    auto update=[&](const std::vector<std::string>& values){
        auto candidate=before;
        const auto error=settings.update(candidate,values);
        if(error.empty()){doc=std::move(candidate);redraw();UpdateWindow(win);}
        return error;
    };
    const auto result=ui::form(win,xAxis?L"X축 관리":L"Y축 관리",settings.fields,update,settings.tabs);
    if(result){if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}
    else{doc=before;redraw();}
}

void editPlotAxes(int id,int initialTab){
    if(!plot(id)||doc.isLocked(id))throw std::runtime_error("축을 편집할 그래프를 선택하세요.");
    const Document before=doc;
    const auto x=managedAxisFields(before,id,true),y=managedAxisFields(before,id,false);
    auto fields=x.fields;
    for(auto field:y.fields){if(field.choiceUpdate)field.choiceUpdate->field+=x.fields.size();fields.push_back(std::move(field));}
    // Keep the field identities and transaction shared while grouping everything by axis.
    ui::FormTabStyle style{{242,243},0,nullptr,true};
    std::vector<std::pair<std::wstring,std::vector<size_t>>> tabs={
        {L"X축",{4,6,7,8,9,3,0,5,1,2}},
        {L"Y축",{4,9,10,17,11,8,6,7,5,18,3,0,1,2,12,13,15,14,16}}
    };
    for(auto& index:tabs[1].second)index+=x.fields.size();
    fields[x.fields.size()+9].quickChoices={{0,L"직접"},{1,L"ΔE"},{2,L"ΔG"}};
    fields[x.fields.size()+9].buttonsOnly=true;
    fields[x.fields.size()+17].buttonsOnly=true;
    fields[x.fields.size()+17].quickChoices={{0,L"ΔE / (단위)"},{1,L"ΔE (단위)"}};
    const wchar_t* shortLabels[]={L"선 굵기",L"화살촉 길이",L"화살촉 너비",L"축 길이",L"축 제목",L"글자색",L"제목 크기"};
    for(size_t i=0;i<7;++i){fields[i].label=shortLabels[i];fields[x.fields.size()+i].label=shortLabels[i];}
    fields[7].label=L"상태 이름 배치";fields[8].label=L"이름 크기";fields[9].label=L"X축 표시";
    fields[x.fields.size()+7].label=L"제목 간격";
    fields[x.fields.size()+12].label=L"최솟값";fields[x.fields.size()+13].label=L"최댓값";
    fields[x.fields.size()+14].label=L"눈금 간격";fields[x.fields.size()+15].label=L"눈금·숫자 표시";
    fields[x.fields.size()+16].label=L"눈금 글자 크기";
    fields[x.fields.size()+17].label=L"단위 표기";
    fields[x.fields.size()+18].label=L"Y축 표시";
    auto update=[&](const std::vector<std::string>& values)->std::wstring{
        const auto middle=values.begin()+x.fields.size();
        auto candidate=before;
        auto error=x.update(candidate,{values.begin(),middle});
        if(error.empty())error=y.update(candidate,{middle,values.end()});
        if(error.empty()){doc=std::move(candidate);redraw();UpdateWindow(win);}
        return error;
    };
    static int lastTab=0;
    style.initial=initialTab<0?lastTab:initialTab;style.remember=&lastTab;
    const auto result=ui::form(win,L"축 관리",fields,update,tabs,{},124,style);
    if(result){if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}
    else{doc=before;redraw();}
}

void editPlotAppearance(int id,int field){
    const auto* p=plot(id);if(!p||doc.isLocked(id))throw std::runtime_error("조절할 에너지 그래프를 선택하세요.");
    if(field<0||field>8)return;
    if(field==2||field==3){editManagedAxis(id,field==2);return;}
    if(field==4){
        const Document before=doc;auto original=*p;
        if(original.series.empty()){EnergySeries path;for(const auto& state:original.states)path.values.push_back(state.energy);original.series.push_back(path);}
        const double factor=original.inkScale*(original.thinLines?.5:1.);
        if(original.curveWidth>0)for(auto& path:original.series)path.width=original.curveWidth/factor;
        original.curveWidth=0;std::vector<ui::Field> fields;
        for(const auto& path:original.series)fields.push_back(ui::numericField(ui::wide(path.name)+L" · 선 굵기 (pt)",path.width,.1,100,.1));
        auto update=[&](const std::vector<std::string>& values)->std::wstring{try{auto candidate=original;for(size_t i=0;i<candidate.series.size();++i)candidate.series[i].width=numeric(values.at(i),.1,100);doc=before;*plot(id)=std::move(candidate);redraw();UpdateWindow(win);return {};}catch(const std::exception& error){return ui::wide(error.what());}};
        const auto result=ui::form(win,L"경로별 곡선 굵기",fields,update);
        if(result){if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}else{doc=before;redraw();}return;
    }
    const Document before=doc;
    const wchar_t* labels[]={L"축 아래 이름 크기 (pt, 0: 자동)",L"데이터 값 크기 (pt, 0: 자동)",L"",L"",L"곡선 굵기 (pt, 0: 경로별 설정)",L"삽입 그림 선 굵기 (%, 100: 기본)",L"Y축 제목 간격 (pt, 0: 자동, 최대 300)",L"물결 굵기 (pt, 0: 자동, 0.1~20)",L"물결·곡선 추가 여백 (pt, -1: 자동, 0~100)"};
    const auto setting=plotNumber(*p,field);
    auto update=[&](const std::vector<std::string>& values)->std::wstring{try{
        const double value=numeric(values[0],setting.minimum,setting.maximum);
        if(value>0&&value<setting.firstPositive){
            if(field==7)return L"물결 굵기는 0(자동) 또는 0.1~20 pt로 입력하세요.";
            if(field<2)return L"글자 크기는 0(자동) 또는 4~72 pt로 입력하세요.";
        }
        if(value<0&&value!=-1)return L"자동은 -1, 그 외에는 0 이상을 입력하세요.";
        double waveGap=0;
        if(field==7){
            waveGap=numeric(values[1],-1,100);
            if(waveGap<0&&waveGap!=-1)return L"물결 여백은 -1(자동) 또는 0~100 pt로 입력하세요.";
        }
        doc=before;
        setPlotNumber(*plot(id),field,value);
        if(field==7)plot(id)->waveGap=waveGap;
        redraw();UpdateWindow(win);return {};
    }catch(const std::exception& e){return ui::wide(e.what());}};
    ui::Field control{std::wstring(labels[field])+L"\n휠 또는 ↑ / ↓ 키로 조절",number(setting.value)};
    control.stepValue=[setting](const std::string& text,int steps) mutable {
        auto current=setting;
        try{current.value=numeric(text,setting.minimum,setting.maximum);}catch(const std::exception&){}
        return number(current.step(steps));
    };
    std::vector<ui::Field> fields{std::move(control)};
    if(field==7)fields.push_back(ui::numericField(L"물결·곡선 추가 여백 (pt, -1: 자동)",p->waveGap,-1,100,1));
    const auto result=ui::form(win,field==7?L"물결 관리":L"그래프 개별 크기·굵기",fields,update);
    if(result){if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}
    else{doc=before;redraw();}
}
void editPlotFont(int id){
    const auto* p=plot(id);if(!p||doc.isLocked(id))throw std::runtime_error("글꼴을 바꿀 그래프를 선택하세요.");
    const auto fonts=ui::fontFamilies(win);
    const Document before=doc;const auto family=p->fontFamily;const int face=p->fontFace;
    auto update=[&](const std::vector<std::string>& values)->std::wstring{try{
        const auto name=ui::wide(values[0]);FontFamily chosen(name.c_str());if(values[0].empty()||values[0].size()>128||chosen.GetLastStatus()!=Ok)return L"설치된 글꼴 이름을 선택하세요.";
        const int style=std::stoi(values[1])-1;if(style< -1||style>3)return L"글꼴 스타일을 선택하세요.";
        doc=before;auto* graph=plot(id);graph->fontFamily=values[0];graph->fontFace=style;redraw();UpdateWindow(win);return {};
    }catch(const std::exception& e){return ui::wide(e.what());}};
    const auto result=ui::form(win,L"그래프 글꼴",{{L"글꼴",family,false,fonts,false,4,false,true},{L"스타일",std::to_string(face+1),false,{L"문서·두께 설정 따름",L"보통",L"굵게",L"기울임",L"굵게 기울임"}},{L"적용 범위","축·눈금·상태 이름·데이터 값·반응 이름 및 기본 범례 서식"}},update);
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
void plotSettings(int id,bool generalOnly){auto* p=plot(id);if(!p)throw std::runtime_error("에너지 그래프를 선택하세요.");EnergyPlot original=*p;Document before=doc;
    auto update=[&](const std::vector<std::string>& v)->std::wstring{try{EnergyPlot p=original;p.title=v[0];p.unit=v[1]==ui::utf8(L"kcal mol⁻¹")?"kcal mol^{-1}":v[1];p.xLabel=v[2];p.yLabel=v[3];p.xColor=colorValue(v[4]);p.yColor=colorValue(v[5]);auto size=values(v[6]);if(size.size()!=2||size[0]<180||size[1]<140||size[0]>4000||size[1]>4000)return L"크기는 180×140 ~ 4000×4000 pt로 입력하세요.";p.width=size[0];p.height=size[1];p.mode=std::stoi(v[7]);p.showValues=v[8]=="1";p.yTickStep=v[9].empty()?0:numeric(v[9],0);p.yMin=v[10].empty()?std::optional<double>{}:numeric(v[10]);p.yMax=v[11].empty()?std::optional<double>{}:numeric(v[11]);
 p.valueGap=numeric(v[24],0,200);
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
 if(v[23]!="0"){p.energySymbol=v[23]=="1"?"ΔE":"ΔG";p.yLabel=p.energySymbol;}
 else if(p.yLabel=="ΔE"||p.yLabel=="ΔG")p.energySymbol=p.yLabel;
 const double xStroke=numeric(v[21],-1,100),yStroke=numeric(v[22],-1,100);
 if((xStroke<0&&xStroke!=-1)||(yStroke<0&&yStroke!=-1))return L"축은 0(숨김), -1(자동) 또는 양수 굵기로 입력하세요.";
 p.showXAxis=xStroke!=0;p.xAxisWidth=std::max(0.,xStroke);p.showYAxis=yStroke!=0;p.yAxisWidth=std::max(0.,yStroke);p.showYTicks=v[19]=="1";p.energyAtTop=v[20]=="1";
 energyAxis(p);if(p.yTickStep>0&&(p.yTickStep<1e-9||energyAxis(p).step>p.yTickStep*1.001))return L"y축 눈금이 너무 촘촘합니다. 더 큰 간격을 입력하세요.";doc=before;relocatePlot(original,p);*plot(id)=p;redraw();UpdateWindow(win);return {};}catch(const std::exception& e){return ui::wide(e.what());}};
    std::vector<ui::Field> fields={{L"제목",p->title,false,{},false,4,true},{L"에너지 단위 (선택 / 직접 입력)",p->unit=="kcal mol^{-1}"?ui::utf8(L"kcal mol⁻¹"):p->unit,false,{L"kcal/mol",L"kcal mol⁻¹",L"kJ/mol",L"eV"},false,4,true,true},{L"x축 (위·아래첨자 버튼)",p->xLabel,false,{},false,4,true},{L"y축 (위·아래첨자 버튼)",p->yLabel,false,{},false,4,true},{L"x축 글자색",p->xColor,false,{},true},{L"y축 글자색",p->yColor,false,{},true},{L"너비 / 높이 (pt)",number(p->width)+" "+number(p->height)},{L"그래프 형태",std::to_string(p->mode),false,{L"부드러운 곡선",L"직선",L"에너지 준위 (점선 연결)",L"평평한 준위와 직선",L"독립 에너지 준위"}},{L"에너지 값 표시",p->showValues?"1":"0",false,{L"아니요",L"예"}},{L"y축 눈금 간격 (0: 자동)",number(p->yTickStep)},
 {L"y축 최솟값 (빈칸: 자동)",p->yMin?number(*p->yMin):""},{L"y축 최댓값 (빈칸: 자동)",p->yMax?number(*p->yMax):""},
 {L"x축·그래프 제목 (pt, 0: 자동)",number(p->axisFontSize)},{L"눈금·상태 이름 (pt, 0: 자동)",number(p->tickFontSize)},{L"데이터 값 크기 (pt, 0: 자동)",number(p->valueFontSize)},{L"에너지 축 제목 (pt, 0: 자동)",number(p->energyLabelFontSize)},{L"그래프 두께",p->thinLines?"1":"0",false,{L"굵게 (기존)",L"얇게 (선·축·글자)"}},{L"평평한 준위선만 굵게",p->boldLevels?"1":"0",false,{L"아니요 (그래프 두께에 맞춤)",L"예 (기존 굵은 두께 유지)"}},
 {L"단위를 바꿀 때","0",false,{L"단위 표시만 변경",L"에너지와 축 수치도 변환"}},
 {L"y축 눈금·숫자 표시",p->showYTicks?"1":"0",false,{L"숨김",L"표시"}},
 {L"에너지 축 제목 배치",p->energyAtTop?"1":"0",false,{L"세로 축 제목",L"축 위에 E (세로 제목 숨김)"}},
  {L"X축 (pt, 0: 숨김, -1: 자동)",number(p->showXAxis?(p->xAxisWidth>0?p->xAxisWidth:-1):0)},
 {L"Y축 (pt, 0: 숨김, -1: 자동)",number(p->showYAxis?(p->yAxisWidth>0?p->yAxisWidth:-1):0)},
 {L"에너지 표기",energyTitleChoice(*p),false,{L"직접 입력한 제목",L"ΔE · 에너지",L"ΔG · 자유 에너지"}},{L"에너지 값과 곡선 간격 (pt)",number(p->valueGap)}};
    fields[23].choiceUpdate=ui::ChoiceUpdate{3,{std::nullopt,"ΔE","ΔG"}};
    fields[23].quickChoices={{1,L"ΔE"},{2,L"ΔG"}};
    auto r=ui::form(win,generalOnly?L"그래프 설정":L"그래프·축 설정",fields,update,
 generalOnly?std::vector<std::pair<std::wstring,std::vector<size_t>>>{{L"기본",{0,6}},{L"데이터 값",{8,14,24}},{L"선 스타일",{16,17}}}:
      std::vector<std::pair<std::wstring,std::vector<size_t>>>{{L"기본",{0,23,1,18,6}},{L"축",{2,3,9,10,11}},{L"축 표시",{21,22,19,20}},{L"글자",{4,5,8,12,13,14,15,24}},{L"선 스타일",{7,16,17}}});if(r){if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}else{doc=before;redraw();}
}
std::optional<PendingEnergyGap> pendingEnergyGap;
namespace {
std::vector<ui::Field> energyGapStyleFields(const EnergyGapStyle& style){
    return {{L"글꼴 (빈칸: 자동)",style.fontFamily,false,ui::fontFamilies(win),false,4,false,true},
        ui::numericField(L"크기 (pt, 0: 자동)",style.fontSize,0,72,1),
        {L"스타일",std::to_string(style.face+1),false,{L"그래프 따름",L"보통",L"굵게",L"기울임",L"굵게 기울임"}},
        {L"글자 색상",style.color,false,{},true}};
}
EnergyGapStyle readEnergyGapStyle(const std::vector<std::string>& values,size_t first){
    EnergyGapStyle style;style.fontFamily=values.at(first);style.fontSize=numeric(values.at(first+1),0,72);
    if(style.fontSize>0&&style.fontSize<4)throw std::runtime_error("글자 크기는 0(자동) 또는 4~72 pt로 입력하세요.");
    style.face=std::stoi(values.at(first+2))-1;style.color=values.at(first+3);
    if(!style.fontFamily.empty()){
        const auto family=ui::wide(style.fontFamily);FontFamily installed(family.c_str());
        if(installed.GetLastStatus()!=Ok)throw std::runtime_error("설치된 글꼴을 선택하거나 빈칸으로 두세요.");
    }
    return style;
}
}
void editEnergyGapStyle(int id,int selectedState){
    const auto* p=plot(id);if(!p||doc.isLocked(id))throw std::runtime_error("에너지 갭을 편집할 그래프를 선택하세요.");
    std::vector<size_t> gaps;std::vector<ui::SearchItem> entries;
    for(size_t i=0;i<p->states.size();++i)if(p->states[i].gapFrom>=0){
        gaps.push_back(i);entries.push_back({ui::wide(p->states[size_t(p->states[i].gapFrom)].label+" → "+p->states[i].label),L"갭 글꼴·크기·굵게·기울임·색상"});
    }
    if(gaps.empty())throw std::runtime_error("먼저 ‘에너지 갭 추가’에서 갭을 배치하세요.");
    size_t chosen=0;if(selectedState>=0){const auto found=std::find(gaps.begin(),gaps.end(),size_t(selectedState));if(found==gaps.end())return;chosen=size_t(found-gaps.begin());}
    else if(gaps.size()>1){const auto result=ui::search(win,L"서식을 바꿀 에너지 갭",entries,L"편집",true);if(!result)return;chosen=*result;}
    const size_t index=gaps[chosen];const Document before=doc;const auto& original=plot(id)->states[index];
    auto fields=energyGapStyleFields(original.gapStyle);
    fields.push_back({L"갭 문구 (빈칸: 자동)",original.gapLabel,true,{},false,2,true});
    std::vector<std::wstring> states,paths;for(const auto& state:plot(id)->states)states.push_back(ui::wide(state.label));for(const auto& path:plot(id)->series)paths.push_back(ui::wide(path.name));if(paths.empty())paths.push_back(L"Path 1");
    fields.push_back({L"출발 상태",std::to_string(original.gapFrom),false,states});
    fields.push_back({L"경로",std::to_string(original.gapPath),false,paths});
    fields.push_back(ui::numericField(L"화살표 X 이동 (pt)",original.gapOffset.x,-10000,10000,1));
    fields.push_back(ui::numericField(L"문구 Y 이동 (pt)",original.gapOffset.y,-10000,10000,1));
    fields.push_back({L"갭 표시","1",false,{L"숨김",L"표시"}});
    auto update=[&](const std::vector<std::string>& values)->std::wstring{try{
        auto candidate=before;auto found=std::find_if(candidate.plots.begin(),candidate.plots.end(),[&](const auto& graph){return graph.id==id;});
        auto& gap=found->states[index];gap.gapStyle=readEnergyGapStyle(values,0);gap.gapLabel=values[4];
        gap.gapFrom=values[9]=="1"?std::stoi(values[5]):-1;gap.gapPath=std::stoi(values[6]);gap.gapOffset={numeric(values[7],-10000,10000),numeric(values[8],-10000,10000)};
        validateEnergyAnnotations(*found);
        doc=std::move(candidate);redraw();UpdateWindow(win);return {};
    }catch(const std::exception& error){return ui::wide(error.what());}};
    const auto result=ui::form(win,L"에너지 갭 — "+entries[chosen].title,fields,update);
    if(result){if(update(*result).empty()&&toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}
    else{doc=before;redraw();}
}
void manageEnergyGaps(int id,int state){
    const auto* graph=plot(id);if(!graph||doc.isLocked(id))throw std::runtime_error("에너지 갭을 편집할 그래프를 선택하세요.");
    if(state>=0){editEnergyGapStyle(id,state);return;}
    std::vector<size_t> gaps;std::vector<ui::SearchItem> entries={{L"새 에너지 갭",L"두 상태·경로·글꼴·색상을 선택해 추가"}};
    for(size_t i=0;i<graph->states.size();++i)if(graph->states[i].gapFrom>=0){gaps.push_back(i);entries.push_back({ui::wide(graph->states[size_t(graph->states[i].gapFrom)].label+" → "+graph->states[i].label),L"갭·위치·글꼴·색상 편집"});}
    if(gaps.empty()){addEnergyGap(id);return;}
    const auto chosen=ui::search(win,L"에너지 갭",entries,L"편집",true);if(!chosen)return;
    if(*chosen==0)addEnergyGap(id);else editEnergyGapStyle(id,int(gaps.at(*chosen-1)));
}
void addEnergyGap(int id){
    const auto* p=plot(id);if(!p||doc.isLocked(id)||p->states.size()<2)throw std::runtime_error("에너지 갭을 넣을 그래프를 선택하세요.");
    std::vector<std::wstring> names,paths;for(size_t i=0;i<p->states.size();++i)names.push_back(std::to_wstring(i+1)+L". "+ui::wide(p->states[i].label));
    for(const auto& path:p->series)paths.push_back(ui::wide(path.name));if(paths.empty())paths.push_back(L"Path 1");
    std::vector<ui::Field> fields={
        {L"출발 상태","0",false,names},{L"도착 상태","1",false,names},{L"경로","0",false,paths},
        {L"갭 문구 (빈칸: "+ui::wide(plotEnergySymbol(*p))+L" 자동 계산)","",true,{},false,2,true}};
    const auto styleFields=energyGapStyleFields(p->states[1].gapStyle);fields.insert(fields.end(),styleFields.begin(),styleFields.end());
    fields.push_back({L"배치 방법","적용 후 캔버스에서 원하는 위치를 클릭하세요."});
    auto validate=[&](const std::vector<std::string>& v)->std::wstring{try{
        const size_t from=std::stoul(v[0]),to=std::stoul(v[1]),path=std::stoul(v[2]);if(from==to)return L"서로 다른 출발·도착 상태를 선택하세요.";
        if(from>=p->states.size()||to>=p->states.size()||path>=std::max(size_t(1),p->series.size()))return L"출발·도착 상태와 경로를 선택하세요.";
        auto candidate=*p;candidate.states[to].gapStyle=readEnergyGapStyle(v,4);validateEnergyAnnotations(candidate);
        if(!p->series.empty()&&(!p->series[path].hasValue(from)||!p->series[path].hasValue(to)))return L"두 상태에 모두 에너지 값이 있어야 합니다.";
        const double a=p->series.empty()?p->states[from].energy:p->series[path].values[from],b=p->series.empty()?p->states[to].energy:p->series[path].values[to];
        if(std::abs(a-b)<1e-10)return L"두 상태의 에너지가 같아 표시할 갭이 없습니다.";
        const auto sc=scene();const auto points=sc.plotSnapPoints.find(id);
        if(points==sc.plotSnapPoints.end()||std::none_of(points->second.begin(),points->second.end(),[&](const auto& point){return point.path==int(path)&&point.state==int(from);})||std::none_of(points->second.begin(),points->second.end(),[&](const auto& point){return point.path==int(path)&&point.state==int(to);}))return L"두 상태가 보이도록 y축 범위를 조정하세요.";
        return {};
    }catch(const std::exception& error){return ui::wide(error.what());}
    };
    auto result=ui::form(win,L"에너지 갭 추가",fields,validate,{{L"갭·배치",{0,1,2,3,8}},{L"글꼴·색상",{4,5,6,7}}});if(!result)return;
    pendingEnergyGap=PendingEnergyGap{id,std::stoi((*result)[0]),std::stoi((*result)[1]),std::stoi((*result)[2]),(*result)[3],readEnergyGapStyle(*result,4)};
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
    history.checkpoint(doc);state.gapFrom=pending.from;state.gapPath=pending.path;state.gapLabel=pending.label;state.gapStyle=pending.style;
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
void editPlotPhotoExcitation(int id){
    const auto* selected=plot(id);
    if(!selected||doc.isLocked(id)||selected->states.size()<2||selected->mode==independentEnergyLevels)
        throw std::runtime_error("두 준위가 연결된 에너지 그래프를 선택하세요.");
    const auto original=*selected;
    const Document before=doc;
    auto stateName=[&](size_t index){
        return std::to_wstring(index+1)+L". "+ui::wide(original.states[index].label);
    };
    auto pathRange=[&](size_t i,size_t k)->std::optional<std::wstring>{
        double a=original.states[i].energy,b=original.states[i+1].energy;
        if(!original.series.empty()){
            const auto& path=original.series[k];
            if(!path.hasValue(i)||!path.hasValue(i+1))return {};
            a=path.values[i];b=path.values[i+1];
        }
        if(std::abs(a-b)<1e-9)return {};
        for(const auto& crossing:original.crossings)
            if(crossing.segment==int(i)&&(crossing.firstPath==int(k)||crossing.secondPath==int(k)))return {};
        return ui::wide(number(std::min(a,b)))+L" → "+ui::wide(number(std::max(a,b)));
    };
    auto update=[&](size_t i,int path)->std::wstring{try{
        auto graph=original;
        auto& from=graph.states[i];
        if(path>=0&&from.breakAfter&&from.breakPath==path)from.breakAfter=false;
        from.photoPath=path;
        validateEnergyAnnotations(graph);
        doc=before;*plot(id)=std::move(graph);redraw();UpdateWindow(win);return {};
    }catch(const std::exception& error){return ui::wide(error.what());}};
    auto finish=[&](bool applied){
        if(applied){if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}
        else{doc=before;redraw();}
    };
    const size_t pathCount=std::max(size_t(1),original.series.size());
    if(pathCount==1){
        std::vector<std::wstring> choices;
        std::vector<std::pair<size_t,int>> actions;
        for(size_t i=0;i+1<original.states.size();++i){
            const bool applied=original.states[i].photoPath>=0;
            const auto range=pathRange(i,0);
            if(!applied&&!range)continue;
            auto label=stateName(i)+L" → "+stateName(i+1);
            if(range)label+=L"  ("+*range+L")";
            choices.push_back((applied?L"해제 · ":L"추가 · ")+label);
            actions.emplace_back(i,applied?-1:0);
        }
        if(actions.empty())throw std::runtime_error("광여기에 사용할 서로 다른 두 에너지 값이 없습니다.");
        const auto applied=ui::form(win,L"광여기 hν",{{L"구간 선택", "0",false,choices}},
            [&](const auto& values){const auto [segment,path]=actions.at(std::stoul(values[0]));return update(segment,path);});
        finish(bool(applied));
        return;
    }

    std::vector<ui::SearchItem> segments;
    std::vector<size_t> indices;
    for(size_t i=0;i+1<original.states.size();++i){
        std::optional<std::wstring> firstRange;
        for(size_t k=0;k<pathCount&&!firstRange;++k)firstRange=pathRange(i,k);
        const bool applied=original.states[i].photoPath>=0;
        if(!firstRange&&!applied)continue;
        auto label=stateName(i)+L" → "+stateName(i+1);
        if(firstRange)label+=L"  ("+*firstRange+L")";
        if(applied)label+=L" · hν";
        segments.push_back({label,L""});
        indices.push_back(i);
    }
    if(segments.empty())throw std::runtime_error("광여기에 사용할 서로 다른 두 에너지 값이 없습니다.");
    const auto segment=ui::search(win,L"광여기 hν · 구간 선택",segments,L"편집");
    if(!segment)return;
    const size_t i=indices[*segment];
    const int current=original.states[i].photoPath;
    std::vector<std::wstring> choices={L"일반 연결"};
    std::vector<int> paths={-1};
    size_t selectedPath=0;
    for(size_t k=0;k<pathCount;++k){
        const auto range=pathRange(i,k);
        if(!range&&int(k)!=current)continue;
        if(int(k)==current)selectedPath=choices.size();
        choices.push_back(ui::wide(original.series[k].name)+L" · "+(range?*range:L"현재 광여기 경로"));
        paths.push_back(int(k));
    }
    if(current<0&&paths.size()==2)selectedPath=1;
    const auto applied=ui::form(win,L"광여기 hν · 경로 선택",
        {{L"광여기를 적용할 경로",std::to_string(selectedPath),false,choices}},
        [&](const auto& values){return update(i,paths.at(std::stoul(values[0])));});
    finish(bool(applied));
}

void editPlotAnnotations(int id,int selectedState){
    auto* p=plot(id);if(!p||doc.isLocked(id)||p->states.empty())throw std::runtime_error("편집할 에너지 그래프를 선택하세요.");
    std::vector<ui::SearchItem> entries;std::vector<std::wstring> states,paths;
    for(size_t i=0;i<p->states.size();++i){auto name=std::to_wstring(i+1)+L". "+ui::wide(p->states[i].label);states.push_back(name);entries.push_back({name,L"이름 위치 · TS [ ]‡ · 다음 구간 // · 에너지 갭"});}
    std::optional<size_t> chosen;if(selectedState>=0&&size_t(selectedState)<p->states.size())chosen=size_t(selectedState);else chosen=ui::search(win,L"곡선·TS·에너지 갭 — 상태 선택",entries,L"편집",true);if(!chosen)return;
    const auto index=*chosen;const auto original=*p;const Document before=doc;const auto& state=original.states[index];
    for(const auto& path:original.series)paths.push_back(ui::wide(path.name));if(paths.empty())paths.push_back(L"Path 1");
    std::vector<std::wstring> gapSources={L"표시 안 함"};gapSources.insert(gapSources.end(),states.begin(),states.end());
    std::vector<ui::Field> fields={
        {L"구조 / 상태 이름",state.label,true,{},false,3,true},
        {L"이름 배치",std::to_string(state.labelPlacement),false,{L"X축",L"곡선 위",L"곡선 아래",L"숨김",L"에너지 데이터 아래"}},
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
    snapLabel=checked.front();snapLabel.captionBelow=label.captionBelow;snapLabel.offset=label.offset;snapLabel.row=label.row;snapPlot=id;snapStage=index;snapStart.reset();selection={id};tool=Select;
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
} // namespace yd::editor

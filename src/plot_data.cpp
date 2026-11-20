#include "plot_data.h"
#include "scene.h"
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cctype>
#include <map>
namespace yd {
EnergyPlot spinMultiplicityPlot(){
    EnergyPlot plot;
    plot.width=460;
    plot.height=285;
    plot.title="Spin-state energies";
    plot.xLabel="Spin multiplicity (2S + 1)";
    plot.unit="kcal mol^{-1}";
    plot.mode=independentEnergyLevels;
    plot.states={{"S = 0",46.3},{"S = 1",21.1},{"S = 2",0.0}};
    EnergySeries levels;
    levels.values={46.3,21.1,0.0};
    levels.levelColors={"#25282d","#b44758","#167d9a"};
    levels.marker=3;
    plot.series.push_back(std::move(levels));
    return plot;
}
EnergyPlot reactionProfilePlot(){
    EnergyPlot p;p.width=920;p.height=490;p.mode=0;p.title="";p.yLabel="ΔG";p.showYTicks=false;
    p.states={{"Reactant",7.7},{"TS1",19.8},{"Int1",8.6},{"TS2",14.2},{"Int2",7.7},{"TS3",15.6},{"Product",-.3}};
    EnergySeries path;path.values={7.7,19.8,8.6,14.2,7.7,15.6,-.3};path.dash=0;path.width=2;path.markerSize=3;p.series={path};
    for(size_t i=0;i<p.states.size();++i){auto& state=p.states[i];state.transitionState=i%2==1;state.labelPlacement=state.transitionState?1:2;state.labelOffset={0,state.transitionState?-24.:12.};}
    p.states[2].breakAfter=true;p.states[2].breakPosition=.35;
    return p;
}
bool isTransitionStateLabel(const std::string& label){
    const auto first=label.find_first_not_of(" \t\r\n");if(first==std::string::npos)return false;
    const auto last=label.find_last_not_of(" \t\r\n");auto name=label.substr(first,last-first+1);
    for(auto& c:name)if(static_cast<unsigned char>(c)<128)c=char(std::toupper(static_cast<unsigned char>(c)));
    if(name=="TRANSITION STATE"||name=="전이상태"||name=="전이 상태")return true;
    if(name.compare(0,2,"TS")!=0)return false;
    return name.size()==2||std::isdigit(static_cast<unsigned char>(name[2]))||std::string(" _-('^{\t\n").find(name[2])!=std::string::npos;
}
void setEnergyStateLabel(EnergyState& state,const std::string& label){
    if(state.label==label)return;const bool previousTS=isTransitionStateLabel(state.label),nextTS=isTransitionStateLabel(label);
    if(previousTS||nextTS)state.transitionState=nextTS;
    state.label=label;
}
std::string plotEnergySymbol(const EnergyPlot& p){return p.energySymbol.empty()?"ΔG":p.energySymbol;}
EnergyPlot newEnergyPlot(int mode,bool freeEnergy){
    if(mode<0||mode>independentEnergyLevels)throw std::runtime_error("Invalid graph mode");
    EnergyPlot p=mode==independentEnergyLevels?spinMultiplicityPlot():EnergyPlot{};
    p.mode=mode;p.title="";p.energySymbol=freeEnergy?"ΔG":"ΔE";p.yLabel=p.energySymbol;
    p.width=640;p.height=370;p.thinLines=true;p.showYTicks=mode>=2;
    if(mode!=independentEnergyLevels){
        p.states={{"R",0},{"TS",20},{"P",-10}};p.states[1].transitionState=true;EnergySeries path;path.values={0,20,-10};path.width=2.4;path.dash=0;path.markerSize=2.8;p.series={path};
        if(mode<2)for(auto& state:p.states){state.labelPlacement=2;state.labelOffset.y=8;}
    }
    return p;
}
Document resizedPlotAxes(const Document& original,int id,double xLength,double yLength,const Measure& measure){
    if(!std::isfinite(xLength)||!std::isfinite(yLength)||xLength<100||yLength<70||xLength>4000||yLength>4000)throw std::runtime_error("축 길이는 가로 100~4000 pt, 세로 70~4000 pt로 입력하세요.");
    if(original.isLocked(id))throw std::runtime_error("잠긴 그래프는 크기를 바꿀 수 없습니다.");
    auto source=std::find_if(original.plots.begin(),original.plots.end(),[&](const auto& p){return p.id==id;});if(source==original.plots.end())throw std::runtime_error("그래프를 선택하세요.");
    const auto oldAxes=makeScene(original,measure).plotAxes.at(id);
    const double oldX=(oldAxes.xEnd-oldAxes.origin).length(),oldY=(oldAxes.yEnd-oldAxes.origin).length();
    if(std::abs(xLength-oldX)<1e-7&&std::abs(yLength-oldY)<1e-7)return original;
    Document result=original;auto& p=result.plots[size_t(source-original.plots.begin())];
    if(!p.axisFontSize)p.axisFontSize=oldAxes.axisFont;if(!p.tickFontSize)p.tickFontSize=oldAxes.tickFont;
    if(!p.valueFontSize)p.valueFontSize=oldAxes.valueFont;if(!p.energyLabelFontSize)p.energyLabelFontSize=oldAxes.energyFont;
    for(int pass=0;pass<6;++pass){
        const auto geometry=makeScene(result,measure).plotAxes.at(id);
        p.p=p.p+oldAxes.origin-geometry.origin;
        const double dx=xLength-(geometry.xEnd-geometry.origin).length(),dy=yLength-(geometry.yEnd-geometry.origin).length();
        if(std::abs(dx)+std::abs(dy)<1e-6)break;
        p.width+=dx;p.height+=dy;
        if(p.width<180||p.width>4000||p.height<140||p.height>4000)throw std::runtime_error("지정한 축 길이가 현재 글자·여백에 맞지 않습니다. 축 길이를 조정하세요.");
    }
    auto geometry=makeScene(result,measure).plotAxes.at(id);p.p=p.p+oldAxes.origin-geometry.origin;
    geometry=makeScene(result,measure).plotAxes.at(id);
    const double sx=(p.width-154)/std::max(1.,source->width-154),sy=geometry.plotHeight/std::max(1.,oldAxes.plotHeight);
    for(auto& state:p.states){state.labelOffset.x*=sx;state.labelOffset.y*=sy;state.gapOffset.x*=sx;state.gapOffset.y*=sy;}
    p.legendOffset.x*=sx;p.legendOffset.y*=sy;
    for(auto& c:p.crossings)if(c.labelOffset){c.labelOffset->x*=sx;c.labelOffset->y*=sy;}
    std::set<int> moved;
    for(size_t i=0;i<p.states.size();++i)for(bool above:{false,true}){
        const auto delta=plotAnchor(p,i,above)-plotAnchor(*source,i,above);std::vector<int> ids;
        for(int member:above?p.states[i].above:p.states[i].below)if(moved.insert(member).second)ids.push_back(member);
        result.translate(ids,delta);
    }
    return result;
}
std::vector<EnergyDisplayBand> energyDisplayBands(const EnergyPlot& p){
    std::vector<EnergyDisplayBand> requested;std::vector<double> stops;
    if(p.mode==independentEnergyLevels)return {};
    for(size_t i=0;i+1<p.states.size();++i){const auto& state=p.states[i];
        if(!state.breakAfter||(std::abs(state.breakScale-1)<1e-10&&state.breakOmit==0&&state.breakUpperScale==1))continue;
        double a=p.states[i].energy,b=p.states[i+1].energy;
        if(!p.series.empty()){
            if(state.breakPath<0||size_t(state.breakPath)>=p.series.size())continue;
            const auto& path=p.series[size_t(state.breakPath)];if(!path.hasValue(i)||!path.hasValue(i+1))continue;a=path.values[i];b=path.values[i+1];
        }
        if(a>b)std::swap(a,b);if(b-a<1e-9)continue;
        if(state.breakLow&&state.breakHigh){a=*state.breakLow;b=*state.breakHigh;}
        else if(state.breakStyle==1){
            // Compress unoccupied energies, preserving the local rise into a TS peak.
            std::vector<double> levels={a,b};
            auto include=[&](double v){if(v>a&&v<b)levels.push_back(v);};
            if(p.series.empty()){for(const auto& point:p.states)include(point.energy);}
            else for(const auto& path:p.series)for(size_t k=0;k<path.values.size();++k)if(path.hasValue(k))include(path.values[k]);
            for(const auto& crossing:p.crossings)include(crossing.energy);
            std::sort(levels.begin(),levels.end());double widest=0,left=a,right=b;
            for(size_t k=1;k<levels.size();++k)if(levels[k]-levels[k-1]>widest){widest=levels[k]-levels[k-1];left=levels[k-1];right=levels[k];}
            const double inset=state.breakOmit>0?.05:.1;
            a=left+widest*inset;b=right-widest*inset;
        }

        const double scale=state.breakOmit>0?std::max(.01,1-state.breakOmit/(b-a)):state.breakScale;
        requested.push_back({a,b,scale});stops.push_back(a);stops.push_back(b);
        if(state.breakUpperScale!=1){requested.push_back({b,1e8,state.breakUpperScale,false});stops.push_back(1e8);}
    }
    std::sort(stops.begin(),stops.end());stops.erase(std::unique(stops.begin(),stops.end()),stops.end());
    std::vector<EnergyDisplayBand> result;
    for(size_t i=1;i<stops.size();++i){double scale=0,middle=(stops[i-1]+stops[i])/2;bool marker=false;
        for(auto band:requested)if(middle>=band.low&&middle<=band.high){scale=scale?std::min(scale,band.scale):band.scale;marker|=band.marker;}
        if(scale&&std::abs(scale-1)>1e-10)result.push_back({stops[i-1],stops[i],scale,marker});
    }
    return result;
}
double displayEnergy(const std::vector<EnergyDisplayBand>& bands,double value){
    double result=value;for(const auto& band:bands)result+=(band.scale-1)*std::clamp(value-band.low,0.,band.high-band.low);return result;
}
double energyFromDisplay(const std::vector<EnergyDisplayBand>& bands,double value){
    double correction=0;
    for(const auto& band:bands){const double start=band.low+correction,end=start+(band.high-band.low)*band.scale;
        if(value<start)return value-correction;
        if(value<=end)return band.low+(value-start)/band.scale;
        correction+=(band.scale-1)*(band.high-band.low);
    }
    return value-correction;
}
void validateEnergyAnnotations(const EnergyPlot& p){
    if(p.fontFamily.empty()||p.fontFamily.size()>128||p.fontFamily.find_first_of("\r\n\t")!=std::string::npos||p.fontFamily.find_first_not_of(" ")==std::string::npos||p.fontFace< -1||p.fontFace>3)throw std::runtime_error("Invalid graph font");
    if(!std::isfinite(p.waveWidth)||p.waveWidth<0||p.waveWidth>20||(p.waveWidth>0&&p.waveWidth<.1)||!std::isfinite(p.waveGap)||p.waveGap>100||(p.waveGap<0&&p.waveGap!=-1))throw std::runtime_error("Invalid wave stroke or clearance");
    const auto& legendFamily=p.legendFontFamily;
    if(p.legendVisibility<0||p.legendVisibility>2||p.legendFace< -1||p.legendFace>3||
       legendFamily.size()>128||legendFamily.find_first_of("\r\n\t")!=std::string::npos||
       (!legendFamily.empty()&&legendFamily.find_first_not_of(" ")==std::string::npos))
        throw std::runtime_error("Invalid legend style");
    if(!std::isfinite(p.legendFontSize)||(p.legendFontSize!=0&&(p.legendFontSize<4||p.legendFontSize>72)))throw std::runtime_error("Invalid legend font size");
    if(!std::isfinite(p.yTitleGap)||p.yTitleGap<0||p.yTitleGap>300)throw std::runtime_error("Invalid Y-axis title gap");
    if(!std::isfinite(p.valueGap)||p.valueGap<0||p.valueGap>200||!std::isfinite(p.yAxisWidth)||p.yAxisWidth<0||p.yAxisWidth>100)throw std::runtime_error("Invalid plot label clearance or axis width");
    for(double size:{p.xAxisHeadLength,p.xAxisHeadWidth,p.yAxisHeadLength,p.yAxisHeadWidth})
        if(!std::isfinite(size)||size<0||size>100)throw std::runtime_error("Invalid axis arrowhead dimension");
    if(!std::isfinite(p.inkScale)||p.inkScale<.25||p.inkScale>4)throw std::runtime_error("Invalid graph ink scale");
    if(!p.energySymbol.empty()&&p.energySymbol!="ΔE"&&p.energySymbol!="ΔG")throw std::runtime_error("Invalid energy symbol");
    const size_t paths=std::max(size_t(1),p.series.size());
    auto offset=[](Vec v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::abs(v.x)<=10000&&std::abs(v.y)<=10000;};
    for(size_t i=0;i<p.states.size();++i){const auto& s=p.states[i];
        if(s.labelPlacement<0||s.labelPlacement>3||s.labelPath<0||size_t(s.labelPath)>=paths||!offset(s.labelOffset)||
           !std::isfinite(s.breakPosition)||s.breakPosition<.05||s.breakPosition>.95||
           !std::isfinite(s.breakOmit)||s.breakOmit<0||s.breakOmit>1e6||!std::isfinite(s.breakUpperScale)||s.breakUpperScale<1||s.breakUpperScale>10||
           s.breakStyle<0||s.breakStyle>1||s.breakLow.has_value()!=s.breakHigh.has_value()||
           (s.breakLow&&(!std::isfinite(*s.breakLow)||!std::isfinite(*s.breakHigh)||std::abs(*s.breakLow)>1e6||std::abs(*s.breakHigh)>1e6||*s.breakHigh-*s.breakLow<1e-6))||
           !std::isfinite(s.breakScale)||s.breakScale<.01||s.breakScale>4||s.breakPath<0||size_t(s.breakPath)>=paths||
           s.gapFrom< -1||(s.gapFrom>=0&&(size_t(s.gapFrom)>=p.states.size()||size_t(s.gapFrom)==i))||
           s.gapPath<0||size_t(s.gapPath)>=paths||!offset(s.gapOffset))throw std::runtime_error("Invalid energy profile annotation");
    }
}
double plotStateFraction(const EnergyPlot& plot,size_t index){
    return plot.states.at(index).position.value_or(double(index)/std::max(size_t(1),plot.states.size()-1));
}
EnergyPlot bilateralPlot(const EnergyPlot& original,size_t sourcePath,const std::string& leftName){
    const size_t n=original.states.size();
    if(n<2||n>50)throw std::runtime_error("양쪽 분기는 상태가 2~50개인 그래프에서 추가하세요 (분기 후 최대 99개).");
    EnergyPlot result=original;
    if(result.series.empty()){
        EnergySeries path;for(auto& state:original.states)path.values.push_back(state.energy);result.series.push_back(path);
    }
    if(sourcePath>=result.series.size())throw std::runtime_error("왼쪽 분기의 원본 경로를 선택하세요.");
    auto left=result.series[sourcePath];
    if(left.values.size()!=n||!left.hasValue(0))throw std::runtime_error("원본 경로의 첫 상태에 공통 반응물 에너지를 입력하세요.");
    bool hasBranch=false;for(size_t i=1;i<n;i++)hasBranch=hasBranch||left.hasValue(i);
    if(!hasBranch)throw std::runtime_error("원본 경로에 분기할 에너지를 한 개 이상 입력하세요.");
    std::vector<EnergyState> states;
    for(size_t i=n-1;i>0;i--){auto state=original.states[i];state.above.clear();state.below.clear();state.energy=left.values[i];states.push_back(state);}
    states.insert(states.end(),original.states.begin(),original.states.end());result.states=std::move(states);
    for(size_t i=0;i<n;i++){
        const auto position=i?plotStateFraction(original,i):0.;
        result.states[n-1+i].position=.5+.5*position;
        result.states[n-1-i].position=.5-.5*position;
    }
    for(auto& path:result.series){
        std::vector<double> values(2*n-1,0);std::vector<bool> present(2*n-1,false);
        for(size_t i=0;i<n;i++)if(path.hasValue(i)){values[n-1+i]=path.values[i];present[n-1+i]=true;}
        std::vector<std::string> levels(2*n-1),segments(2*n-2);
        for(size_t i=0;i<n;i++)if(i<path.levelColors.size())levels[n-1+i]=path.levelColors[i];
        for(size_t i=0;i+1<n;i++)if(i<path.segmentColors.size())segments[n-1+i]=path.segmentColors[i];
        path.levelColors=std::move(levels);path.segmentColors=std::move(segments);path.dash=1;
        if(!path.valueLabels.empty()){
            std::vector<std::string> labels(2*n-1);
            for(size_t i=0;i<n;i++)labels[n-1+i]=energyCellText(path,i,{});
            path.valueLabels=std::move(labels);
        }
        path.values=std::move(values);path.present=std::move(present);
    }
    auto source=left;left.name=leftName;
    for(const char* color:{"#b44758","#167d9a","#7a52a1","#27854a","#b87516"}){
        if(std::none_of(result.series.begin(),result.series.end(),[&](const EnergySeries& path){return path.color==color;})){left.color=color;break;}
    }
    left.values.assign(2*n-1,0);left.present.assign(2*n-1,false);
    for(size_t i=0;i<n;i++)if(source.hasValue(i)){left.values[n-1-i]=source.values[i];left.present[n-1-i]=true;}
    if(!source.valueLabels.empty()){
        left.valueLabels.assign(2*n-1,{});
        for(size_t i=0;i<n;i++)left.valueLabels[n-1-i]=energyCellText(source,i,{});
    }
    left.levelColors.assign(2*n-1,{});left.segmentColors.assign(2*n-2,{});left.dash=1;
    for(size_t i=0;i<n;i++)if(i<source.levelColors.size())left.levelColors[n-1-i]=source.levelColors[i];
    for(size_t i=0;i+1<n;i++)if(i<source.segmentColors.size())left.segmentColors[n-2-i]=source.segmentColors[i];
    result.series.push_back(std::move(left));
    for(size_t i=0;i<result.states.size();++i){auto& state=result.states[i];
        if(i<n-1){state.labelPath=int(result.series.size())-1;state.gapPath=state.labelPath;if(state.gapFrom>=0)state.gapFrom=int(n)-1-state.gapFrom;state.breakAfter=original.states[n-2-i].breakAfter;state.breakPosition=1-original.states[n-2-i].breakPosition;state.breakScale=original.states[n-2-i].breakScale;state.breakOmit=original.states[n-2-i].breakOmit;state.breakUpperScale=original.states[n-2-i].breakUpperScale;state.breakStyle=original.states[n-2-i].breakStyle;state.breakLow=original.states[n-2-i].breakLow;state.breakHigh=original.states[n-2-i].breakHigh;state.breakPath=int(result.series.size())-1;}
        else if(state.gapFrom>=0)state.gapFrom+=int(n)-1;
    }
    for(auto& crossing:result.crossings)crossing.segment+=int(n)-1;
    result.stages.clear();
    auto addStage=[&](MechanismStage stage){
        if(stage.end-stage.start<1){double middle=(stage.start+stage.end)/2;stage.start=std::clamp(middle-.5,0.,99.);stage.end=stage.start+1;}
        if(result.stages.size()>=100)throw std::runtime_error("분기 후 반응 이름·화살표가 100개를 초과합니다. 단계 설명 수를 줄여 주세요.");
        result.stages.push_back(stage);
    };
    for(auto& stage:original.stages){
        auto right=stage;right.start=stage.start==0?50-stage.end/2:50+stage.start/2;right.end=50+stage.end/2;addStage(right);
        if(stage.start>0){auto mirror=stage;mirror.start=50-stage.end/2;mirror.end=50-stage.start/2;addStage(mirror);}
    }
    result.width=std::min(4000.,54+2*(original.width-54));
    return result;
}
std::vector<MechanismStage> defaultMechanismStages(){return {{"Reactant",0,32},{"Transition State",34,66},{"Product",68,100}};}
std::vector<MechanismStage> mechanismStageData(const std::vector<std::vector<std::string>>& cells){
    std::vector<MechanismStage> result;
    for(size_t i=0;i<cells.size();i++){
        const auto& row=cells[i];bool empty=true;for(auto& cell:row)if(cell.find_first_not_of(" \t\r\n")!=std::string::npos)empty=false;if(empty)continue;
        try{
            if(row.size()<3||row[0].find_first_not_of(" \t\r\n")==std::string::npos)throw std::runtime_error("단계 이름과 시작·끝 위치를 모두 입력하세요.");
            auto percent=[](const std::string& value){size_t pos=0;double n=std::stod(value,&pos);if(value.find_first_not_of(" \t\r\n",pos)!=std::string::npos||!std::isfinite(n)||n<0||n>100)throw std::runtime_error("범위는 0~100%의 숫자로 입력하세요.");return n;};
            double start=percent(row[1]),end=percent(row[2]);if(end-start<1)throw std::runtime_error("끝은 시작보다 최소 1% 커야 합니다.");
            double font=0;if(row.size()>3&&row[3].find_first_not_of(" \t\r\n")!=std::string::npos){size_t pos=0;font=std::stod(row[3],&pos);if(row[3].find_first_not_of(" \t\r\n",pos)!=std::string::npos||!std::isfinite(font)||(font!=0&&(font<4||font>72)))throw std::runtime_error("글자 크기는 0(자동) 또는 4~72 pt로 입력하세요.");}
            double width=0,head=0;
            auto optionalSize=[&](size_t column,double maximum){
                if(row.size()<=column||row[column].find_first_not_of(" \t\r\n")==std::string::npos)return 0.;
                size_t end=0;const double value=std::stod(row[column],&end);
                if(row[column].find_first_not_of(" \t\r\n",end)!=std::string::npos||!std::isfinite(value)||value<0||value>maximum||(value>0&&value<.1))throw std::runtime_error(column==4?"화살표 굵기는 0(자동) 또는 0.1~100 pt로 입력하세요.":"화살촉 길이는 0(자동) 또는 0.1~40 pt로 입력하세요.");return value;
            };
            width=optionalSize(4,maxStageArrowWidth);head=optionalSize(5,40);
            result.push_back({row[0],start,end,font,width,head});if(result.size()>100)throw std::runtime_error("단계는 최대 100개입니다.");
        }catch(const std::exception& e){throw std::runtime_error(std::to_string(i+1)+"행: "+e.what());}
    }return result;
}
EnergyPlot publicationPlot(const EnergyPlot& original){
    auto plot=original;
    plot.width=std::clamp(220.+75.*(std::max(size_t(1),plot.states.size())-1),420.,820.);
    plot.height=std::clamp(plot.width*.62,300.,450.);
    plot.axisFontSize=plot.tickFontSize=plot.valueFontSize=plot.energyLabelFontSize=0;
    plot.axisScale=1.6;
    for(auto& path:plot.series)path.width=std::max(path.width,3.5);
    return plot;
}
void validateEnergyCrossings(const EnergyPlot& plot){
    std::set<int> segments;
    for(const auto& crossing:plot.crossings){
        if(!std::isfinite(crossing.leaderWidth)||crossing.leaderWidth<0||crossing.leaderWidth>6)
            throw std::runtime_error("Crossing leader width must be between 0 and 6 pt.");
        if(crossing.labelOffset&&(!std::isfinite(crossing.labelOffset->x)||!std::isfinite(crossing.labelOffset->y)||
           std::abs(crossing.labelOffset->x)>1e4||std::abs(crossing.labelOffset->y)>1e4))
            throw std::runtime_error("Invalid crossing label offset.");
        if(crossing.label.empty()||crossing.segment<0||size_t(crossing.segment)+1>=plot.states.size()||
           crossing.firstPath<0||size_t(crossing.firstPath)>=plot.series.size()||
           crossing.secondPath<0||size_t(crossing.secondPath)>=plot.series.size()||
           crossing.firstPath==crossing.secondPath||!std::isfinite(crossing.position)||
           crossing.position<=0||crossing.position>=1||!std::isfinite(crossing.energy)||
           std::abs(crossing.energy)>1e6||!segments.insert(crossing.segment).second)
            throw std::runtime_error("Invalid crossing point. Use two distinct paths and one point per segment.");
    }
}
bool hasCrossingPaths(const EnergyPlot& plot,const EnergyCrossing& crossing){
    if(crossing.segment<0||size_t(crossing.segment)+1>=plot.states.size())return false;
    for(int path:{crossing.firstPath,crossing.secondPath}){
        if(path<0||size_t(path)>=plot.series.size())return false;
        const auto& series=plot.series[size_t(path)];
        if(series.values.size()!=plot.states.size())return false;
        if(!series.present.empty()&&std::none_of(series.present.begin(),series.present.end(),[](bool value){return value;}))
            return false;
    }
    return true;
}
EnergyAxis energyAxis(const EnergyPlot& plot){
    double low=0,high=0;bool first=true;
    auto include=[&](double v){if(!std::isfinite(v))return;if(first){low=high=v;first=false;}else{low=std::min(low,v);high=std::max(high,v);}};
    if(plot.series.empty()){for(auto& state:plot.states)include(state.energy);}else for(auto& path:plot.series)for(size_t i=0;i<path.values.size();i++)if(path.hasValue(i))include(path.values[i]);
    if(plot.mode!=independentEnergyLevels)
        for(const auto& crossing:plot.crossings)if(hasCrossingPaths(plot,crossing))include(crossing.energy);
    if(high-low<1e-8){high+=1;low-=1;}const auto bands=energyDisplayBands(plot);
    if(bands.empty()){double margin=(high-low)*.22;low-=margin;high+=margin;}
    else{const double a=displayEnergy(bands,low),b=displayEnergy(bands,high),margin=(b-a)*.22;low=energyFromDisplay(bands,a-margin);high=energyFromDisplay(bands,b+margin);}
    double autoSpan=high-low;
    if(plot.yMin){if(!std::isfinite(*plot.yMin)||std::abs(*plot.yMin)>1e8)throw std::runtime_error("y축 최솟값이 올바르지 않습니다.");low=*plot.yMin;}
    if(plot.yMax){if(!std::isfinite(*plot.yMax)||std::abs(*plot.yMax)>1e8)throw std::runtime_error("y축 최댓값이 올바르지 않습니다.");high=*plot.yMax;}
    if(high<=low){
        if(plot.yMin&&plot.yMax)throw std::runtime_error("y축 최댓값은 최솟값보다 커야 합니다.");
        if(plot.yMin)high=low+std::max(1.,autoSpan);else low=high-std::max(1.,autoSpan);
    }
    if(high-low<1e-9)throw std::runtime_error("y축 범위를 더 넓게 입력하세요.");
    double step=plot.yTickStep;
    if(!std::isfinite(step)||step<=0){double rough=(high-low)/5,base=std::pow(10.,std::floor(std::log10(rough)));step=10*base;for(double n:{1.,2.,2.5,5.,10.})if(n*base>=rough){step=n*base;break;}}
    // If data later expands dramatically, keep drawing bounded and use multiples
    // of the requested interval rather than generating millions of labels.
    double count=(high-low)/step;if(count>198)step*=std::ceil(count/198);
    double begin=std::floor(low/step+1e-10),end=std::ceil(high/step-1e-10);
    if(plot.yMin||plot.yMax||!bands.empty()){
        if(bands.empty()){if(!plot.yMin)low=begin*step;if(!plot.yMax)high=end*step;}
        double firstTick=std::ceil(low/step-1e-10)*step;
        return {low,high,step,int(std::floor((high-firstTick)/step+1e-10)),firstTick};
    }
    return {begin*step,end*step,step,std::max(1,int(std::llround(end-begin))),begin*step};
}
std::string energyTickLabel(double value,double step){
    int precision=0;for(;precision<10;precision++){double scaled=step*std::pow(10.,precision);if(std::abs(scaled-std::round(scaled))<1e-7)break;}
    if(std::abs(value)<step*1e-8)value=0;
    while(precision<10){double scaled=value*std::pow(10.,precision);if(std::abs(scaled-std::round(scaled))<1e-7)break;precision++;}
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(precision)<<value;return out.str();
}
static std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r");if(a==std::string::npos)return {};return s.substr(a,s.find_last_not_of(" \t\r")-a+1);}
static std::vector<std::string> cells(const std::string& line){std::vector<std::string> out;std::string s;bool quoted=false;for(size_t i=0;i<line.size();++i){char c=line[i];if(c=='"'){if(quoted&&i+1<line.size()&&line[i+1]=='"'){s+='"';++i;}else quoted=!quoted;}else if((c==','||c=='\t')&&!quoted){out.push_back(trim(s));s.clear();}else s+=c;}if(quoted)throw std::runtime_error("Close the quote in the state name");out.push_back(trim(s));return out;}
std::vector<EnergyRow> parseEnergyTable(const std::string& csv){std::vector<EnergyRow> rows;std::istringstream stream(csv);std::string line;size_t row=0;while(std::getline(stream,line)){++row;if(trim(line).empty())continue;auto v=cells(line);if(v.size()<2||v.size()>3)throw std::runtime_error("Row "+std::to_string(row)+": use State, Energy, Marker");if(rows.empty()&&v[0]=="State"&&v[1]=="Energy")continue;if(v[0].empty())throw std::runtime_error("Row "+std::to_string(row)+": state name is empty");double energy;try{size_t n=0;energy=std::stod(v[1],&n);if(n!=v[1].size()||!std::isfinite(energy)||std::abs(energy)>1e6)throw std::runtime_error("value");}catch(...){throw std::runtime_error("Row "+std::to_string(row)+": energy must be a finite number");}int marker=-1;if(v.size()==3&&!v[2].empty()){std::string name=v[2];for(char& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));std::map<std::string,int> names={{"auto",-1},{"-1",-1},{"none",0},{"0",0},{"circle",1},{"1",1},{"square",2},{"2",2},{"diamond",3},{"3",3},{"triangle",4},{"4",4}};auto i=names.find(name);if(i==names.end())throw std::runtime_error("Row "+std::to_string(row)+": marker is auto/none/circle/square/diamond/triangle");marker=i->second;}rows.push_back({v[0],energy,marker});if(rows.size()>100)throw std::runtime_error("At most 100 states are supported");}if(rows.size()<2)throw std::runtime_error("Enter at least two complete state rows");return rows;}
std::string energyTable(const EnergyPlot& p){std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(10);const char* names[]={"auto","none","circle","square","diamond","triangle"};for(auto& s:p.states){std::string label=s.label;if(label.find_first_of(",\t\"")!=std::string::npos){std::string escaped="\"";for(char c:label){escaped+=c;if(c=='"')escaped+='"';}label=escaped+'"';}out<<label<<", "<<s.energy<<", "<<names[std::clamp(s.marker,-1,4)+1]<<"\r\n";}return out.str();}
void setEnergyData(EnergyPlot& plot,const std::vector<EnergyRow>& rows,bool preserveMarkers){
    if(rows.size()>100)throw std::runtime_error("Use at most 100 energy states");
    auto old=plot;std::vector<int> mapping(rows.size(),-1);std::set<size_t> used;
    // Reserve exact label matches before reusing indices for renamed/new states.
    for(size_t i=0;i<rows.size();i++){
        const auto& r=rows[i];if(!std::isfinite(r.energy)||std::abs(r.energy)>1e6||r.marker<-1||r.marker>4)throw std::runtime_error("Invalid energy row");
        for(size_t j=0;j<old.states.size();j++)if(!used.count(j)&&old.states[j].label==r.label){mapping[i]=int(j);used.insert(j);break;}
    }
    for(size_t i=0;i<rows.size();i++)if(mapping[i]<0&&i<old.states.size()&&!used.count(i)){mapping[i]=int(i);used.insert(i);}
    std::vector<EnergyState> states;for(size_t i=0;i<rows.size();i++){EnergyState s;if(mapping[i]>=0)s=old.states[size_t(mapping[i])];setEnergyStateLabel(s,rows[i].label);s.energy=rows[i].energy;if(!preserveMarkers)s.marker=rows[i].marker;states.push_back(s);}
    auto series=old.series;if(series.empty())series.push_back({});
    for(size_t k=0;k<series.size();k++){std::vector<double> values;for(size_t i=0;i<rows.size();i++){int j=mapping[i];values.push_back(k==0?rows[i].energy:j>=0&&size_t(j)<old.series[k].values.size()?old.series[k].values[size_t(j)]:rows[i].energy);}std::vector<bool> present;for(size_t i=0;i<rows.size();i++){int j=mapping[i];present.push_back(k==0||j<0||old.series.empty()||old.series[k].hasValue(size_t(j)));}std::vector<std::string> levels(rows.size()),segments(rows.empty()?0:rows.size()-1),labels;
        for(size_t i=0;i<rows.size();i++){
            int j=mapping[i];if(j>=0&&size_t(j)<series[k].levelColors.size())levels[i]=series[k].levelColors[size_t(j)];
            if(j>=0&&!series[k].valueLabels.empty()){
                auto text=energyCellText(series[k],size_t(j),{});
                if(!text.empty()&&series[k].values[size_t(j)]==values[i]){labels.resize(i+1);labels[i]=std::move(text);}
            }
            if(i+1<rows.size()&&j>=0&&mapping[i+1]==j+1&&size_t(j)<series[k].segmentColors.size())segments[i]=series[k].segmentColors[size_t(j)];
        }
        series[k].levelColors=std::move(levels);series[k].segmentColors=std::move(segments);
        series[k].present=std::move(present);series[k].values=std::move(values);series[k].valueLabels=std::move(labels);}
    bool sameOrder=rows.size()==old.states.size();
    for(size_t i=0;i<mapping.size();++i)sameOrder&=mapping[i]==int(i);
    if(!sameOrder)for(auto& state:states)state.position.reset();
    for(size_t i=0;i<states.size();++i){auto& state=states[i];
        if(state.gapFrom>=0){auto from=std::find(mapping.begin(),mapping.end(),state.gapFrom);state.gapFrom=from==mapping.end()?-1:int(from-mapping.begin());if(state.gapFrom==int(i))state.gapFrom=-1;}
        if(i+1>=states.size()||mapping[i]<0||mapping[i+1]!=mapping[i]+1)state.breakAfter=false;
    }
    plot.states=std::move(states);plot.series=std::move(series);
    plot.crossings.clear();
    for(auto crossing:old.crossings){
        for(size_t i=0;i+1<mapping.size();i++){
            if(mapping[i]!=crossing.segment||mapping[i+1]!=crossing.segment+1)continue;
            crossing.segment=int(i);
            plot.crossings.push_back(crossing);
            break;
        }
    }
}
// Empty cells represent missing points, including in the first pathway.
static std::optional<double> parseGridEnergy(std::string text){
    if(text.empty())return 0.;
    if(text.find("~{")!=std::string::npos){
        std::string numeric;
        for(const auto& run:scriptRuns(text)){if(run.script)return {};numeric+=run.text;}
        if(numeric.empty())return {};
        text=std::move(numeric);
    }
    try{
        size_t end=0;
        double energy=std::stod(text,&end);
        if(end==text.size()&&std::isfinite(energy)&&std::abs(energy)<=1e6)return energy;
    }catch(const std::logic_error&){}
    return {};
}
bool validEnergyGridValue(const std::string& text){
    return parseGridEnergy(trim(text)).has_value();
}
std::string energyCellText(const EnergySeries& path,size_t index,const std::string& fallback){
    if(index<path.valueLabels.size()&&index<path.values.size()&&!path.valueLabels[index].empty())
        if(auto value=parseGridEnergy(path.valueLabels[index]);value&&*value==path.values[index])return path.valueLabels[index];
    return fallback;
}
EnergyPlot energyGridData(const EnergyPlot& original,const std::vector<std::vector<std::string>>& data){
    if(data.size()<2||data.size()>100)throw std::runtime_error("에너지 그래프에는 2~100개 상태가 필요합니다.");
    size_t paths=std::max(size_t(1),original.series.size());
    std::vector<EnergyRow> states;std::vector<std::vector<double>> energies(paths);std::vector<std::vector<bool>> present(paths);
    std::vector<std::vector<std::string>> valueLabels(paths);
    for(size_t r=0;r<data.size();r++){
        auto prefix=std::to_string(r+1)+"행 · ";
        if(data[r].size()!=paths+1)throw std::runtime_error(prefix+"상태 이름과 모든 경로의 에너지를 입력하세요.");
        auto label=trim(data[r][0]);
        for(size_t k=0;k<paths;k++){
            auto text=trim(data[r][k+1]);
            auto energy=parseGridEnergy(text);
            present[k].push_back(!text.empty());
            if(!energy){auto name=k<original.series.size()?original.series[k].name:"Path 1";throw std::runtime_error(prefix+name+": 에너지를 숫자로 입력하세요 (−1000000~1000000).");}
            energies[k].push_back(*energy);
            if(text.find("~{")!=std::string::npos){
                if(text.size()>256)throw std::runtime_error(prefix+"서식이 지정된 에너지 값이 너무 깁니다.");
                valueLabels[k].resize(r+1);valueLabels[k][r]=std::move(text);}
        }
        states.push_back({label,energies[0].back(),-1});
    }
    EnergyPlot candidate=original;setEnergyData(candidate,states,true);
    for(size_t k=0;k<paths;k++){candidate.series[k].values=std::move(energies[k]);candidate.series[k].present=std::move(present[k]);candidate.series[k].valueLabels=std::move(valueLabels[k]);}
    candidate.width=std::max(candidate.width,114.+(std::max(size_t(1),data.size())-1)*70);
    return candidate;
}

}

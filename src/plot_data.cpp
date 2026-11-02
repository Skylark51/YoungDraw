#include "plot_data.h"
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cctype>
#include <map>
namespace yd {
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
    for(auto& path:result.series){
        std::vector<double> values(2*n-1,0);std::vector<bool> present(2*n-1,false);
        for(size_t i=0;i<n;i++)if(path.hasValue(i)){values[n-1+i]=path.values[i];present[n-1+i]=true;}
        std::vector<std::string> levels(2*n-1),segments(2*n-2);
        for(size_t i=0;i<n;i++)if(i<path.levelColors.size())levels[n-1+i]=path.levelColors[i];
        for(size_t i=0;i+1<n;i++)if(i<path.segmentColors.size())segments[n-1+i]=path.segmentColors[i];
        path.levelColors=std::move(levels);path.segmentColors=std::move(segments);path.dash=1;
        path.values=std::move(values);path.present=std::move(present);
    }
    auto source=left;left.name=leftName;
    for(const char* color:{"#b44758","#167d9a","#7a52a1","#27854a","#b87516"}){
        if(std::none_of(result.series.begin(),result.series.end(),[&](const EnergySeries& path){return path.color==color;})){left.color=color;break;}
    }
    left.values.assign(2*n-1,0);left.present.assign(2*n-1,false);
    for(size_t i=0;i<n;i++)if(source.hasValue(i)){left.values[n-1-i]=source.values[i];left.present[n-1-i]=true;}
    left.levelColors.assign(2*n-1,{});left.segmentColors.assign(2*n-2,{});left.dash=1;
    for(size_t i=0;i<n;i++)if(i<source.levelColors.size())left.levelColors[n-1-i]=source.levelColors[i];
    for(size_t i=0;i+1<n;i++)if(i<source.segmentColors.size())left.segmentColors[n-2-i]=source.segmentColors[i];
    result.series.push_back(std::move(left));
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
            result.push_back({row[0],start,end,font});if(result.size()>100)throw std::runtime_error("단계는 최대 100개입니다.");
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
EnergyAxis energyAxis(const EnergyPlot& plot){
    double low=0,high=0;bool first=true;
    auto include=[&](double v){if(!std::isfinite(v))return;if(first){low=high=v;first=false;}else{low=std::min(low,v);high=std::max(high,v);}};
    if(plot.series.empty()){for(auto& state:plot.states)include(state.energy);}else for(auto& path:plot.series)for(size_t i=0;i<path.values.size();i++)if(path.hasValue(i))include(path.values[i]);
    if(high-low<1e-8){high+=1;low-=1;}double margin=(high-low)*.22;low-=margin;high+=margin;
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
    if(plot.yMin||plot.yMax){
        if(!plot.yMin)low=begin*step;if(!plot.yMax)high=end*step;
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
    std::vector<EnergyState> states;for(size_t i=0;i<rows.size();i++){EnergyState s;if(mapping[i]>=0)s=old.states[size_t(mapping[i])];s.label=rows[i].label;s.energy=rows[i].energy;if(!preserveMarkers)s.marker=rows[i].marker;states.push_back(s);}
    auto series=old.series;if(series.empty())series.push_back({});
    for(size_t k=0;k<series.size();k++){std::vector<double> values;for(size_t i=0;i<rows.size();i++){int j=mapping[i];values.push_back(k==0?rows[i].energy:j>=0&&size_t(j)<old.series[k].values.size()?old.series[k].values[size_t(j)]:rows[i].energy);}std::vector<bool> present;for(size_t i=0;i<rows.size();i++){int j=mapping[i];present.push_back(k==0||j<0||old.series.empty()||old.series[k].hasValue(size_t(j)));}std::vector<std::string> levels(rows.size()),segments(rows.empty()?0:rows.size()-1);
        for(size_t i=0;i<rows.size();i++){
            int j=mapping[i];if(j>=0&&size_t(j)<series[k].levelColors.size())levels[i]=series[k].levelColors[size_t(j)];
            if(i+1<rows.size()&&j>=0&&mapping[i+1]==j+1&&size_t(j)<series[k].segmentColors.size())segments[i]=series[k].segmentColors[size_t(j)];
        }
        series[k].levelColors=std::move(levels);series[k].segmentColors=std::move(segments);
        series[k].present=std::move(present);series[k].values=std::move(values);}
    plot.states=std::move(states);plot.series=std::move(series);
}
EnergyPlot energyGridData(const EnergyPlot& original,const std::vector<std::vector<std::string>>& cells){
    auto data=cells;
    if(data.size()<2||data.size()>100)throw std::runtime_error("에너지 그래프에는 2~100개 상태가 필요합니다.");
    size_t paths=std::max(size_t(1),original.series.size());
    std::vector<EnergyRow> states;std::vector<std::vector<double>> energies(paths);std::vector<std::vector<bool>> present(paths);
    for(size_t r=0;r<data.size();r++){
        auto prefix=std::to_string(r+1)+"행 · ";
        if(data[r].size()!=paths+1)throw std::runtime_error(prefix+"상태 이름과 모든 경로의 에너지를 입력하세요.");
        auto label=trim(data[r][0]);
        for(size_t k=0;k<paths;k++){
            auto text=trim(data[r][k+1]);double energy=0;
            present[k].push_back(!text.empty());
            if(!text.empty())try{size_t end=0;energy=std::stod(text,&end);if(end!=text.size()||!std::isfinite(energy)||std::abs(energy)>1e6)throw std::runtime_error("value");}
            catch(...){auto name=k<original.series.size()?original.series[k].name:"Path 1";throw std::runtime_error(prefix+name+": 에너지를 숫자로 입력하세요 (−1000000~1000000).");}
            energies[k].push_back(energy);
        }
        states.push_back({label,energies[0].back(),-1});
    }
    EnergyPlot candidate=original;setEnergyData(candidate,states,true);
    for(size_t k=0;k<paths;k++){candidate.series[k].values=std::move(energies[k]);candidate.series[k].present=std::move(present[k]);}
    candidate.width=std::max(candidate.width,114.+(std::max(size_t(1),data.size())-1)*70);
    return candidate;
}

}

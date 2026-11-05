#include "model.h"
#include "plot_data.h"
#include "scene.h"
#ifdef TEST_CDXML
#include "cdxml.h"
#endif
#include <fstream>
#include <set>
#include <iostream>
#include <stdexcept>
using namespace yd;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static double measure(const std::string& text,double size,const std::string&,int face){size_t chars=0;for(unsigned char c:text)if((c&0xc0)!=0x80)chars++;return chars*size*(face&1?.62:.56);}
static Rect bounds(const Primitive& text){Vec p=text.points[0];double w=measure(text.text,text.fontSize,text.font,text.face),h=text.fontSize*1.3;return text.rotation==-90?Rect{p.x,p.y-w,h,w}:Rect{p.x,p.y,w,h};}
static bool overlaps(Rect a,Rect b){return a.x<b.x+b.w-0.01&&a.x+a.w>b.x+0.01&&a.y<b.y+b.h-0.01&&a.y+a.h>b.y+0.01;}
static EnergyPlot sample(){EnergyPlot p;p.id=1;p.p={30,90};p.width=760;p.height=400;p.states={{"R",0,{},{}},{"TS1",37.2,{},{}},{"I",17.7,{},{}},{"TS2",27.3,{},{}},{"P",-9.9,{},{}}};p.series={{"Path 1","#167d9a",{0,37.2,17.7,27.3,-9.9},0,2},{"Path 2","#b44758",{0,37.2,17.7,27.3,-9.9},1,2}};return p;}
int main(){try{
    {
        auto p=sample();p.series.resize(1);p.series[0].values={0,0,10,5,5};p.series[0].dash=1;p.thinLines=true;
        Document d;d.plots={p};
        for(int mode:{0,1,2,3}){
            d.plots[0].mode=mode;d.plots[0].boldLevels=false;auto normal=makeScene(d,measure);
            d.plots[0].boldLevels=true;auto emphasis=makeScene(d,measure);
            check(normal.items.size()==emphasis.items.size(),"level emphasis preserves graph elements and layout");
            int changed=0;
            for(size_t i=0;i<normal.items.size();i++){
                auto& a=normal.items[i];auto& b=emphasis.items[i];
                check(a.face==b.face&&a.text==b.text&&a.dash==b.dash&&a.color==b.color&&a.points.size()==b.points.size(),"level emphasis preserves text weight, dashes and colors");
                for(size_t j=0;j<a.points.size();j++)check((a.points[j]-b.points[j]).length()<1e-9,"level emphasis never moves a line or label");
                if(a.width!=b.width){
                    check(b.type==Primitive::Line&&b.dash==0&&b.points.size()==2&&std::abs(b.points[0].y-b.points[1].y)<1e-9&&std::abs(b.width-p.series[0].width)<1e-9,"only plateau strokes regain the original bold width");changed++;
                }
            }
            check(changed==(mode>=2?5:0),"only level graph modes emphasize plateaus; equal-energy connecting segments stay thin");
        }
        check(fromNative(toNative(d)).plots[0].boldLevels,"level emphasis survives native save");
#ifdef TEST_CDXML
        check(fromCdxml(toCdxml(d)).plots[0].boldLevels,"level emphasis survives CDXML save");
#endif
        check(publicationPlot(d.plots[0]).boldLevels,"publication fitting keeps level emphasis");
        d.plots[0].thinLines=false;auto bold=makeScene(d,measure);d.plots[0].boldLevels=false;auto standard=makeScene(d,measure);
        for(size_t i=0;i<bold.items.size();i++)check(bold.items[i].width==standard.items[i].width,"level emphasis never doubles an already bold graph");
    }

    {
        auto p=sample();p.series[0].name="S = 0";p.series[1].name="S=1";p.stages={{"Reaction",0,100}};
        Document d;d.plots={p};auto original=toNative(d);
        for(int mode:{0,1,2,3}){
            d.plots[0].mode=mode;d.plots[0].thinLines=false;auto bold=makeScene(d,measure);
            d.plots[0].thinLines=true;auto thin=makeScene(d,measure);
            int axes=0,profiles=0,values=0,spins=0;
            for(auto& item:thin.items)if(item.owner==p.id){
                if(item.type==Primitive::Text){
                    check(!(item.face&1),"thin plot makes axes, titles, legends, states and data labels regular weight");
                    if(item.textRole==8)values++;
                    if(item.text=="S = 0"||item.text=="S=1"){check(item.face&2,"thin multiplicity legends are italic");spins++;}
                }else if(item.type==Primitive::Line||item.type==Primitive::Bezier){
                    bool matched=false;
                    for(auto& other:bold.items)if(other.type==item.type&&other.color==item.color&&other.textRole==item.textRole&&other.dash==item.dash&&std::abs(other.width-2*item.width)<1e-9){matched=true;break;}
                    check(matched,"thin axes, ticks, legend samples, plateaus and connectors use half the original stroke");
                    if(item.color=="#000000")axes++;else profiles++;
                }
            }
            check(axes&&profiles&&values&&spins==2,"all plot elements participate in the thickness preset");
            for(auto& item:bold.items)if(item.type==Primitive::Text){check(item.face&1,"existing bold plot typography is preserved");if(item.text=="S = 0"||item.text=="S=1")check(item.face&2,"bold multiplicity legends are italic too");}
        }
        auto thinData=toNative(d);check(fromNative(thinData).plots[0].thinLines,"thin preset survives native save");
#ifdef TEST_CDXML
        check(fromCdxml(toCdxml(d)).plots[0].thinLines,"thin preset survives CDXML save");
#endif
        check(publicationPlot(d.plots[0]).thinLines,"publication layout retains the chosen thickness");
        d.plots[0].thinLines=false;d.plots[0].mode=p.mode;check(toNative(d)==original,"switching back restores saved path widths and all original data");
        auto legacy=original;auto flag=legacy.find(" thinLines=\"0\"");check(flag!=std::string::npos,"serialized graph includes thickness selection");legacy.erase(flag,14);
        check(!fromNative(legacy).plots[0].thinLines,"older files default to the existing bold preset");
        d.plots[0].thinLines=true;auto svg=toSvg(makeScene(d,measure));check(svg.find("font-style=\"italic\"")!=std::string::npos&&svg.find("font-weight=\"bold\"")==std::string::npos,"SVG exports regular weight and italic multiplicity");
    }

    {
        auto p=sample();p.series.resize(1);auto& path=p.series[0];
        path.levelColors={"","#ff0000","#ff0000"};path.segmentColors={"","#008800"};
        Document d;d.plots={p};auto sc=makeScene(d,measure);
        check(sc.plotSnapPoints.at(p.id).size()==15,"three snap targets for every visible level");
        for(auto& point:sc.plotSnapPoints.at(p.id)){
            check(std::abs(point.p.x-(p.p.x+50+(p.width-54)*point.percent/100))<1e-8,"snap percent projects to the exact rendered level position");
        }
        bool redLevel=false,greenConnector=false;
        for(auto& part:sc.items)if(part.type==Primitive::Line&&part.points.size()==2){
            redLevel|=part.color=="#ff0000"&&std::abs(part.points[0].y-part.points[1].y)<1e-8;
            greenConnector|=part.color=="#008800"&&std::abs(part.points[0].y-part.points[1].y)>1;
        }
        check(redLevel&&greenConnector,"level and connector colors render independently");
        for(int mode:{0,1,2,3}){d.plots[0].mode=mode;auto svg=toSvg(makeScene(d,measure));check(svg.find("#008800")!=std::string::npos,"segment colors export in all graph modes");}
        auto mirrored=bilateralPlot(p);check(mirrored.series[0].dash==1&&mirrored.series.back().dash==1,"both branches default to dashed connectors");
        check(mirrored.series[0].levelColors[5]=="#ff0000"&&mirrored.series.back().levelColors[3]=="#ff0000"&&mirrored.series.back().segmentColors[2]=="#008800","bilateral branches remap level and connector colors");
        setEnergyData(p,{{"P",-9.9},{"I",17.7},{"TS2",27.3},{"R",0},{"TS1",37.2}},true);
        check(p.series[0].levelColors[1]=="#ff0000"&&p.series[0].levelColors[4]=="#ff0000"&&p.series[0].segmentColors[0].empty(),"state reorder preserves level colors and clears newly created segment overrides");
#ifdef TEST_CDXML
        for(auto restored:{fromNative(toNative(d)),fromCdxml(toCdxml(d))}){
            check(restored.plots[0].series[0].levelColors==d.plots[0].series[0].levelColors&&restored.plots[0].series[0].segmentColors==d.plots[0].series[0].segmentColors,"segment colors survive native and CDXML roundtrips");
        }
#endif
    }

    {
        auto original=sample();original.stages=defaultMechanismStages();original.states[2].above={99};
        original.series[1].values={0,25,8,15,-20};original.series[1].present={true,true,false,true,true};
        auto graph=bilateralPlot(original,1,"Left path");const size_t center=original.states.size()-1;
        check(graph.states.size()==9&&graph.states[center].label=="R","bilateral graph shares the central first state");
        check(graph.states[0].label=="P"&&graph.states.back().label=="P","both branches extend from reactant to product");
        check(graph.states[2].above.empty()&&graph.states[6].above==std::vector<int>{99},"mirrored states do not alias attached structures");
        for(size_t k=0;k<original.series.size();k++)for(size_t i=0;i<original.states.size();i++){
            check(!graph.series[k].hasValue(i==center?0:i),"existing paths are absent to the left");
            check(graph.series[k].hasValue(center+i)==original.series[k].hasValue(i),"right branch preserves missing energies");
            if(original.series[k].hasValue(i))check(graph.series[k].values[center+i]==original.series[k].values[i],"right branch preserves original energies");
        }
        auto& left=graph.series.back();check(left.name=="Left path"&&left.values[0]==-20&&left.hasValue(center),"left branch reverses selected path and includes shared state");
        check(!left.hasValue(2)&&!left.hasValue(center+1),"left branch preserves blanks without connecting to right branch");
        check(graph.stages.size()==5&&graph.stages[0].start==34&&graph.stages[0].end==66,"shared reactant caption spans the center and other stages mirror around it");
        check(left.color!=graph.series[0].color&&left.color!=graph.series[1].color,"new branch has a distinct pathway color");
        Document d;d.nextId=99;d.addAtom({30,20});d.plots={graph};auto drawing=makeScene(d,measure);check(!drawing.items.empty(),"bilateral graph renders");
        std::ofstream("plot-bilateral.svg")<<toSvg(drawing);
#ifdef TEST_CDXML
        std::ofstream("plot-bilateral.ydraw")<<toNative(d);
        for(auto restored:{fromNative(toNative(d)),fromCdxml(toCdxml(d))}){
            auto& p=restored.plots[0];check(p.states.size()==9&&p.series.size()==3&&p.series.back().present==left.present&&p.series.back().values==left.values,"bilateral branches survive native and CDXML roundtrips");
        }
#endif
        auto legacy=original;legacy.series.clear();check(bilateralPlot(legacy).series.size()==2,"legacy state energies support branching");
        auto narrow=original;narrow.stages={{"TS",99,100}};for(auto& stage:bilateralPlot(narrow).stages)check(stage.end-stage.start>=1&&stage.start>=0&&stage.end<=100,"narrow mirrored captions retain valid save ranges");
        for(auto invalid:{EnergyPlot{},sample()}){if(!invalid.series.empty())invalid.series[0].present={false,true,true,true,true};bool rejected=false;try{bilateralPlot(invalid);}catch(...){rejected=true;}check(rejected,"empty graphs and absent shared energies reject branching");}
        auto tooLarge=sample();tooLarge.states.resize(51);bool rejected=false;try{bilateralPlot(tooLarge);}catch(...){rejected=true;}check(rejected,"bilateral expansion respects document state limits");
    }
    {
        auto graph=sample();graph.series.resize(3,graph.series[0]);
        for(const auto& invalid:std::vector<std::vector<std::vector<std::string>>>{{},{{"R","0","1","2"}}}){
            bool rejected=false;try{(void)energyGridData(graph,invalid);}catch(const std::exception&){rejected=true;}
            check(rejected,"energy grid rejects zero and one state");
        }
        for(size_t k=0;k<graph.series.size();k++)graph.series[k].name="S = "+std::to_string(k);
        auto renderLegend=[&](){Document d;d.plots={graph};std::map<int,std::string> entries;for(auto& item:makeScene(d,measure).items)if(item.type==Primitive::Text&&item.textRole==7)entries[item.textIndex]=item.text;return entries;};
        graph=energyGridData(graph,{{"R","0","",""},{"TS","20","",""},{"P","-10","",""}});
        check(renderLegend()==std::map<int,std::string>{{0,"S = 0"}},"only paths with entered energies appear in the legend");
        graph=energyGridData(graph,{{"R","","",""},{"TS","","",""},{"P","","",""}});
        check(renderLegend().empty(),"fully blank energies produce no legend");
        graph=energyGridData(graph,{{"R","","",""},{"TS","","","0"},{"P","","",""}});
        check(renderLegend()==std::map<int,std::string>{{2,"S = 2"}},"a single zero energy is data and retains the original series index for editing");
        graph=energyGridData(graph,{{"R","0","",""},{"TS","","","5"},{"P","","",""}});
        check(renderLegend()==std::map<int,std::string>{{0,"S = 0"},{2,"S = 2"}},"partially populated paths reappear in the legend");
    }
    auto defaults=defaultMechanismStages();check(defaults.size()==3&&defaults[0].label=="Reactant"&&defaults[1].label=="Transition State"&&defaults[2].label=="Product","default mechanism stages");
    auto stageRows=mechanismStageData({{"Oxidation","5","45"},{"Reduction","40","95"},{"","",""}});
    check(stageRows.size()==2&&stageRows[1].start==40,"custom and overlapping ranges are preserved");
    for(auto row:std::vector<std::vector<std::string>>{{"A","-1","30"},{"A","20","20"},{"A","0","101"},{"A","nan","50"},{"A","0x","50"},{"","0","50"}}){bool rejected=false;try{mechanismStageData({row});}catch(...){rejected=true;}check(rejected,"invalid mechanism ranges are rejected");}
    check(mechanismStageData({}).empty(),"all stages can be removed");
    for(double width:{180.,520.,1000.}){
        Document staged;auto graph=sample();graph.width=width;graph.stages=defaults;staged.plots={graph};auto drawing=makeScene(staged,measure);
        std::map<int,Rect> stageBoxes;std::map<int,double> fonts;
        for(auto& item:drawing.items)if(item.textRole==12){auto box=bounds(item);auto it=stageBoxes.find(item.textIndex);if(it==stageBoxes.end())stageBoxes[item.textIndex]=box;else{auto& merged=it->second;double left=std::min(merged.x,box.x),top=std::min(merged.y,box.y);merged={left,top,std::max(merged.x+merged.w,box.x+box.w)-left,std::max(merged.y+merged.h,box.y+box.h)-top};}fonts[item.textIndex]=item.fontSize;}
        check(stageBoxes.size()==3,"each mechanism stage is drawn");
        for(size_t i=0;i<defaults.size();i++){
            auto box=stageBoxes.at(int(i));double cy=box.y+box.h/2,first=graph.p.x+50+(graph.width-54)*defaults[i].start/100,last=graph.p.x+50+(graph.width-54)*defaults[i].end/100;
            if(width>=520&&i==0)first=plotAnchor(graph,0,true).x;
            if(width>=520&&i+1==defaults.size())last=plotAnchor(graph,graph.states.size()-1,true).x;
            check(std::abs(box.x+box.w/2-(first+last)/2)<.01,"stage text is centered on its chosen range");
            int heads=0;for(auto& item:drawing.items)if(item.type==Primitive::Polygon&&!item.points.empty()&&std::abs(item.points[0].y-cy)<.01&&(std::abs(item.points[0].x-first)<.01||std::abs(item.points[0].x-last)<.01))heads++;
            check(heads==2,"arrows point outward through the center of the text block");
            check(fonts.at(int(i))>=9,"long stage names retain readable type on small figures");
            for(auto& item:drawing.items)if(item.type==Primitive::Line&&item.points.size()==2&&std::abs(item.points[0].y-cy)<.01&&std::abs(item.points[1].y-cy)<.01){double left=std::min(item.points[0].x,item.points[1].x),right=std::max(item.points[0].x,item.points[1].x);check(right<=box.x||left>=box.x+box.w,"arrow shafts leave the text unobstructed");}
        }
        if(width==520){std::ofstream("plot-mechanism-stages.svg")<<toSvg(drawing);
#ifdef TEST_CDXML
            std::ofstream("plot-mechanism-stages.ydraw")<<toNative(staged);
            for(auto restored:{fromNative(toNative(staged)),fromCdxml(toCdxml(staged))})check(restored.plots[0].stages.size()==3&&restored.plots[0].stages[1].label=="Transition State"&&restored.plots[0].stages[2].start==68,"mechanism stages survive native and CDXML roundtrips");
#endif
        }
    }
    {Document staged;auto graph=sample();graph.stages=stageRows;staged.plots={graph};std::map<int,double> ys;for(auto& item:makeScene(staged,measure).items)if(item.textRole==12)ys.emplace(item.textIndex,item.points[0].y);check(ys.size()==2&&ys[0]!=ys[1],"overlapping mechanism ranges use separate lanes");}
    {
        Document staged;auto graph=sample();graph.width=1000;graph.tickFontSize=28;
        graph.stages=mechanismStageData({{"Ring opening","5","23","0"},{"C-C bond cleavage","25","44","32"},{"C-OH bond formation","46","65","0"},{"Product formation","67","94","0"}});staged.plots={graph};
        auto drawing=makeScene(staged,measure);std::set<int> labels;int lines=0;
        for(auto& item:drawing.items)if(item.textRole==12){check(item.fontSize==(item.textIndex==1?32:28),"long labels wrap without reducing requested or automatic font size");labels.insert(item.textIndex);lines++;}
        check(labels.size()==4&&lines>4,"long mechanism captions wrap over multiple lines");
        auto arrowDimensions=[&](double size){staged.plots[0].stages={{"Step",0,100,size}};double weight=0,headHeight=0;for(auto& item:makeScene(staged,measure).items){if(item.type==Primitive::Line&&item.width==size*.085)weight=item.width;if(item.type==Primitive::Polygon&&item.points.size()==3&&item.width==size*.085)headHeight=std::max(headHeight,std::abs(item.points[1].y-item.points[2].y));}return Vec{weight,headHeight};};
        auto smallArrow=arrowDimensions(12),largeArrow=arrowDimensions(36);check(largeArrow.x>smallArrow.x*2.9&&largeArrow.y>smallArrow.y*2.9,"arrow stroke and heads scale with the stage font");
        staged.plots={graph};std::ofstream("plot-readable-stages.svg")<<toSvg(drawing);
#ifdef TEST_CDXML
        for(auto restored:{fromNative(toNative(staged)),fromCdxml(toCdxml(staged))})check(restored.plots[0].stages[1].fontSize==32&&restored.plots[0].stages[0].fontSize==0,"stage font settings survive native and CDXML roundtrips");
        std::ofstream("plot-readable-stages.ydraw")<<toNative(staged);
#endif
        for(auto value:{"3","73","nan","18px"}){bool rejected=false;try{mechanismStageData({{"Step","0","100",value}});}catch(...){rejected=true;}check(rejected,"invalid stage font sizes are rejected");}
    }

    auto p=sample();p.yMin=-13;p.yMax=43;p.yTickStep=5;auto axis=energyAxis(p);
    check(axis.low==-13&&axis.high==43&&axis.firstTick==-10,"explicit axis endpoints remain exact without rounding to ticks");
    check(axis.firstTick+axis.intervals*axis.step<=43,"ticks never leave the explicit range");
    check(energyTickLabel(-13.25,5)=="-13.25","fractional custom bounds retain their precision with integer tick steps");
    p.yMin.reset();axis=energyAxis(p);check(axis.high==43&&axis.low<-9.9,"maximum can be fixed independently");
    p.yMin=150;p.yMax.reset();axis=energyAxis(p);check(axis.low==150&&axis.high>150,"single endpoint still yields valid axis when data lies outside");
    p.yMin=10;p.yMax=10;bool invalid=false;try{energyAxis(p);}catch(...){invalid=true;}check(invalid,"inverted or equal bounds are rejected");
    p=sample();p.axisFontSize=20;p.tickFontSize=18;p.valueFontSize=18;
    Document d;d.nextId=2;d.plots={p};auto scene=makeScene(d,measure);int values=0;
    std::vector<Primitive> texts;for(auto& item:scene.items)if(item.owner==1&&item.type==Primitive::Text&&!item.text.empty()){
        texts.push_back(item);if(item.color=="#167d9a"&&item.text.find('.')!=std::string::npos){check(item.fontSize>10&&item.fontSize<=18,"data values enlarge and fit under requested size");values++;}
    }
    check(values==5,"all requested values stay visible");
    for(size_t i=0;i<texts.size();i++)for(size_t j=i+1;j<texts.size();j++)if(overlaps(bounds(texts[i]),bounds(texts[j]))){
        std::cerr<<"Overlap: "<<texts[i].text<<" / "<<texts[j].text<<'\n';check(false,"axis, title, legend, state and data captions must not overlap");
    }
    for(auto size:std::vector<Vec>{{180,140},{300,180},{560,300}}){
        auto crowded=sample();crowded.width=size.x;crowded.height=size.y;crowded.axisFontSize=72;crowded.tickFontSize=72;crowded.valueFontSize=72;
        crowded.states[1].label="Transition state number one";crowded.states[3].label="Transition state number two";
        Document crowdedDoc;crowdedDoc.plots={crowded};auto fitted=makeScene(crowdedDoc,measure);std::vector<Primitive> labels;
        for(auto& item:fitted.items)if(item.type==Primitive::Text&&!item.text.empty())labels.push_back(item);
        for(size_t i=0;i<labels.size();i++)for(size_t j=i+1;j<labels.size();j++)if(overlaps(bounds(labels[i]),bounds(labels[j]))){
            std::cerr<<"Crowded "<<size.x<<"x"<<size.y<<": "<<labels[i].text<<" / "<<labels[j].text<<'\n';check(false,"large requested fonts must fit without caption overlap on small graphs");
        }
    }
    // Lower-energy values must sit below higher-energy values at the same state, even when series are reversed.
    for(int mode:{0,1,2,3})for(bool reversed:{false,true}){
        auto ranked=sample();ranked.mode=mode;ranked.yMin=-20;ranked.yMax=60;
        ranked.series[0].values={0,20,17.7,25,-9.9};ranked.series[1].values={0,20.1,10,25.01,-10};
        ranked.series.push_back({"Path 3","#6655aa",{0,19.9,15,24.99,-9.8},0,2});
        if(reversed)std::reverse(ranked.series.begin(),ranked.series.end());
        Document ordered;ordered.plots={ranked};auto drawing=makeScene(ordered,measure);
        std::map<std::pair<size_t,size_t>,Rect> positions;
        for(auto& text:drawing.items)if(text.type==Primitive::Text&&text.text.find('.')!=std::string::npos&&text.textIndex>=0){
            for(size_t k=0;k<ranked.series.size();k++)if(text.color==ranked.series[k].color)positions[{k,size_t(text.textIndex)}]=bounds(text);
        }
        for(size_t i=0;i<ranked.states.size();i++)for(size_t k=0;k<ranked.series.size();k++)for(size_t j=0;j<ranked.series.size();j++)if(ranked.series[k].values[i]>ranked.series[j].values[i]){
            auto upper=positions.at({k,i}),lower=positions.at({j,i});check(upper.y+upper.h<lower.y,"data label order follows energy, not series order");
        }
    }
    // Reproduce the reported compact A-E plot with three close, crossing pathways.
    for(double height:{180.,240.,400.}){
        EnergyPlot reported;reported.id=1;reported.p={30,90};reported.width=1000;reported.height=height;reported.yMin=-20;reported.yMax=50;reported.yTickStep=20;
        reported.states={{"A",0,{},{}},{"B",42.7,{},{}},{"C",4.4,{},{}},{"D",13.5,{},{}},{"E",6.4,{},{}}};
        reported.series={{"Black","#000000",{0,42.7,4.4,13.5,6.4},1,2},{"Red","#ff0000",{9.9,20.5,-2.2,13.5,8.8},0,2},{"Blue","#0000ff",{8.8,19,-2.2,9.5,9.5},0,2}};
        Document report;report.plots={reported};auto drawing=makeScene(report,measure);int count=0;
        for(auto& text:drawing.items)if(text.type==Primitive::Text&&text.textIndex>=0&&text.text.find('.')!=std::string::npos){
            auto box=bounds(text);double expected=reported.p.x+100+(reported.width-154)*text.textIndex/4.;
            check(std::abs(box.x+box.w/2-expected)<1e-6,"reported layout: every energy value stays centered on its own state");
            check(text.fontSize>=6,"reported layout: values remain readable without a tiny side list");count++;
        }
        check(count==15,"reported layout retains all fifteen labels");
        for(auto& line:drawing.items)if(line.type==Primitive::Line&&line.textRole!=7&&line.points.size()==2&&line.points[0].y>=reported.p.y){
            if(line.width==2&&std::abs(line.points[1].y-line.points[0].y)<1e-6&&line.points[1].x-line.points[0].x>10){
                double center=(line.points[0].x+line.points[1].x)/2;bool state=false;for(size_t i=0;i<5;i++)state|=std::abs(center-(reported.p.x+100+(reported.width-154)*i/4.))<1e-6;
                // Equal adjacent energies also create horizontal connectors; those are not plateaus.
                if(!state)check(line.points[1].x-line.points[0].x>(reported.width-154)/4.*.4,"horizontal connectors retain their own spacing");
            }
            check(line.dash!=2||std::abs(line.points[1].x-line.points[0].x)<reported.width*.5,"reported layout has no long diagonal label leaders");
        }
        int centered=0;for(auto& marker:drawing.items)if(marker.type==Primitive::Ellipse&&marker.textRole!=7&&marker.owner==1){
            Vec mid=(marker.points[0]+marker.points[1])*.5;bool matched=false;
            for(auto& line:drawing.items)if(line.type==Primitive::Line&&line.color==marker.color&&line.points.size()==2&&std::abs(line.points[1].y-line.points[0].y)<1e-6){
                Vec center=(line.points[0]+line.points[1])*.5;if((center-mid).length()<1e-6){matched=true;break;}
            }
            check(matched,"every marker is the exact midpoint of its plateau");centered++;
        }
        check(centered==15,"all fifteen markers have centered plateaus");
        if(height==240){std::ofstream("plot-reported-fixed.svg")<<toSvg(drawing);
            Document compact;compact.plots={publicationPlot(reported)};std::ofstream("plot-publication-reported.svg")<<toSvg(makeScene(compact,measure));
#ifdef TEST_CDXML
            std::ofstream("plot-reported-fixed.ydraw")<<toNative(report);std::ofstream("plot-publication-reported.ydraw")<<toNative(compact);
#endif
        }
    }
    {
        auto before=sample();before.width=1000;before.height=240;before.yMin=-20;before.yMax=50;
        auto fitted=publicationPlot(before);check(fitted.width<before.width&&fitted.height>before.height,"publication layout corrects overly wide shallow plots");
        check(fitted.series[0].values==before.series[0].values&&fitted.series[0].color==before.series[0].color&&fitted.yMin==before.yMin&&fitted.yMax==before.yMax,"publication layout preserves data, styling and explicit bounds");
        Document publication;publication.plots={fitted};auto scene=makeScene(publication,measure);std::vector<Primitive> legend;
        for(auto& item:scene.items)if(item.type==Primitive::Text&&item.textRole==7)legend.push_back(item);
        check(legend.size()==2&&std::abs(legend[0].points[0].y-legend[1].points[0].y)<1e-6,"publication legend is horizontal and compact");
        int defaultTitles=0;for(auto& item:scene.items)if(item.type==Primitive::Text&&item.text==fitted.xLabel)defaultTitles++;
        check(defaultTitles==1,"default reaction-coordinate title is not printed twice");
        double stateSize=0,xCaptionSize=0,yCaptionSize=0,tickSize=0;
        for(auto& item:scene.items)if(item.type==Primitive::Text){
            if(item.textRole==6)stateSize=item.fontSize;
            if(item.text==fitted.xLabel)xCaptionSize=item.fontSize;
            if(item.text==fitted.yLabel+" ( "+fitted.unit+" )")yCaptionSize=item.fontSize;
            if(item.text=="0")tickSize=item.fontSize;
        }
        check(stateSize>0&&std::abs(xCaptionSize-stateSize)<1e-6&&std::abs(yCaptionSize-stateSize)<1e-6&&std::abs(tickSize-stateSize)<1e-6,"automatic axis captions match tick and state-name sizes");
        check(fitted.series[0].width==3.5,"publication paths use a slightly stronger 3.5 point stroke");
        for(auto& item:scene.items)if(item.textRole==7&&item.type!=Primitive::Text)check(item.type==Primitive::Line&&item.dash==1,"legend has dashed lines only, without marker symbols");
        fitted.yTickStep=20;publication.plots={fitted};bool forty=false,fifty=false;
        for(auto& item:makeScene(publication,measure).items)if(item.type==Primitive::Text){forty|=item.text=="40";fifty|=item.text=="50";}
        check(forty&&!fifty&&energyAxis(fitted).high==50,"upper bound 50 remains exact but does not add an irregular tick to a 20-unit axis");

        std::ofstream("plot-publication-verification.svg")<<toSvg(scene);
#ifdef TEST_CDXML
        std::ofstream("plot-publication-verification.ydraw")<<toNative(publication);
#endif
        fitted.title="Catalytic pathway comparison";publication.plots={fitted};bool title=false;for(auto& item:makeScene(publication,measure).items)title|=item.text==fitted.title;
        check(title,"custom scientific titles remain visible");
        for(int i=0;i<6;i++){auto path=fitted.series[0];path.name="Comparison pathway "+std::to_string(i+3);fitted.series.push_back(path);}
        publication.plots={fitted};auto wrapped=makeScene(publication,measure);std::set<double> rows;int entries=0;
        for(auto& item:wrapped.items)if(item.type==Primitive::Text&&item.textRole==7){rows.insert(item.points[0].y);entries++;}
        check(entries==8&&rows.size()>1,"long legends wrap without dropping any pathway");

    }
    {
        auto scripted=publicationPlot(sample());scripted.states[0].label="I_{1}";scripted.states[1].label="TS^{+}_{2}";scripted.states[2].label="Fe^{III}";
        Document labels;labels.nextId=2;labels.plots={scripted};auto drawing=makeScene(labels,measure);
        std::map<int,std::vector<Primitive>> groups;for(auto& run:drawing.items)if(run.type==Primitive::Text&&run.textRole==6)groups[run.textIndex].push_back(run);
        check(groups[0].size()==2&&groups[1].size()==3,"state names render as normal and script runs instead of markup");
        check(std::abs(groups[0][0].points[0].y-groups[1][0].points[0].y)<1e-6&&std::abs(groups[1][0].points[0].y-groups[2][0].points[0].y)<1e-6,"subscript and superscript state names share the same normal-text baseline");
        check(groups[0][1].fontSize<groups[0][0].fontSize&&groups[0][1].points[0].y>groups[0][0].points[0].y,"subscript is smaller and below the baseline");
        check(groups[1][1].fontSize<groups[1][0].fontSize&&groups[1][1].points[0].y<groups[1][0].points[0].y,"superscript is smaller and above the baseline");
        for(int i:{0,1,2}){double left=1e20,right=-1e20;for(auto& run:groups[i]){auto box=bounds(run);left=std::min(left,box.x);right=std::max(right,box.x+box.w);}
            check(std::abs((left+right)/2-(scripted.p.x+100+(scripted.width-154)*i/4.))<1e-6,"scripted state name remains centered on its energy level");}
        double axisWidth=0,headWidth=0;for(auto& part:drawing.items)if(part.owner==1&&part.color=="#000000"){
            if(part.type==Primitive::Line&&part.points.size()==2&&part.points[1].x-part.points[0].x>scripted.width*.8)axisWidth=part.width;
            if(part.type==Primitive::Polygon&&part.points.size()==3)headWidth=std::max(headWidth,std::abs(part.points[1].y-part.points[2].y));
        }
        check(axisWidth>1.8&&headWidth>7,"publication layout increases axis strokes and arrowhead size");
#ifdef TEST_CDXML
        for(auto restored:{fromNative(toNative(labels)),fromCdxml(toCdxml(labels))})check(restored.plots[0].states[1].label=="TS^{+}_{2}"&&restored.plots[0].axisScale==1.6,"state scripts and publication axis scale survive save and restore");
        std::ofstream("plot-scripted-publication.ydraw")<<toNative(labels);
#endif
        std::ofstream("plot-scripted-publication.svg")<<toSvg(drawing);
    }
    auto plateauLength=[&](double width,double font){
        auto levels=sample();levels.width=width;levels.valueFontSize=font;levels.series.resize(1);Document doc;doc.plots={levels};auto scene=makeScene(doc,measure);
        double length=0;for(auto& line:scene.items)if(line.type==Primitive::Line&&line.color==levels.series[0].color&&line.points.size()==2&&line.points[0].y>=levels.p.y&&std::abs(line.points[1].y-line.points[0].y)<1e-6)length=std::max(length,line.points[1].x-line.points[0].x);
        return length;
    };
    check(plateauLength(760,18)>14,"flat levels automatically grow beyond the old fixed length");
    check(plateauLength(1000,18)>plateauLength(560,18),"flat level length grows with state spacing");
    check(plateauLength(560,30)>plateauLength(560,8),"flat level length adapts to energy label size");
    double smallEnergySize=0;
    for(auto size:std::vector<Vec>{{300,180},{900,500}}){
        auto labelPlot=sample();labelPlot.width=size.x;labelPlot.height=size.y;labelPlot.energyLabelFontSize=0;Document doc;doc.plots={labelPlot};double energySize=0;
        for(auto& label:makeScene(doc,measure).items)if(label.rotation==-90)energySize=std::max(energySize,label.fontSize);
        check(energySize>0,"energy axis label has an automatic fitted size");
        if(size.x==300)smallEnergySize=energySize;
        else check(energySize>smallEnergySize,"energy axis label grows with the figure independently of x title");
    }
    auto small=sample();small.width=300;small.height=180;small.series.resize(1);Document sd;sd.plots={small};auto ss=makeScene(sd,measure);double smallFont=0;for(auto& item:ss.items)if(item.textRole==8)smallFont=item.fontSize;
    auto big=sample();big.width=900;big.height=500;big.series.resize(1);Document bd;bd.plots={big};auto bs=makeScene(bd,measure);double bigFont=0;for(auto& item:bs.items)if(item.textRole==8)bigFont=item.fontSize;
    check(bigFont>smallFont,"automatic typography grows with the figure");
    p=sample();p.yMin=-5;p.yMax=30;p.axisFontSize=18;p.tickFontSize=15;p.valueFontSize=16;p.energyLabelFontSize=19;
    for(int mode:{0,1,2,3}){
        p.mode=mode;d.plots={p};scene=makeScene(d,measure);double top=p.p.y+std::max(25.,p.valueFontSize*1.3+12),bottom=p.p.y+p.height-std::max(40.,p.tickFontSize*1.3+p.axisFontSize*1.4+16);
        for(auto& item:scene.items)if(item.owner==1&&item.type!=Primitive::Text&&(item.color=="#167d9a"||item.color=="#b44758")){
            if(item.textRole==7)continue;
            // Legend samples sit above the plot. All profile lines/markers are clipped to the selected scale.
            if(item.type==Primitive::Line&&item.points[0].y<top&&item.points[1].y<top)continue;
            for(auto pt:item.points)check(pt.y>=top-3&&pt.y<=bottom+3,"out-of-range profile is clipped in every graph mode");
        }
        bool minimum=false,maximum=false;for(auto& item:scene.items)if(item.type==Primitive::Text){minimum|=item.text=="-5";maximum|=item.text=="30";}
        check(!minimum&&maximum,"only boundaries aligned with regular ticks are labeled");
        for(auto& item:scene.items)if(item.textRole==8)check(item.text!="37.2"&&item.text!="-9.9","out-of-range values do not print outside the selected scale");
    }
#ifdef TEST_CDXML
    d.plots={p};for(auto restored:{fromNative(toNative(d)),fromCdxml(toCdxml(d))}){
        auto& saved=restored.plots[0];check(saved.yMin==p.yMin&&saved.yMax==p.yMax&&saved.axisFontSize==18&&saved.tickFontSize==15&&saved.valueFontSize==16&&saved.energyLabelFontSize==19,"axis range and font settings survive native and CDXML roundtrips");
    }
    std::ofstream("plot-fit-verification.ydraw")<<toNative(d);
#endif
    auto rankedPreview=sample();rankedPreview.series[0].values={0,20,17.7,25,-9.9};rankedPreview.series[1].values={0,20.1,10,25.01,-10};d.plots={rankedPreview};
    std::ofstream("plot-ranked-verification.svg")<<toSvg(makeScene(d,measure));
    std::ofstream("plot-fit-verification.svg")<<toSvg(makeScene(d,measure));
    std::cout<<"Plot range and typography checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

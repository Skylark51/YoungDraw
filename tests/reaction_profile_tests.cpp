#include "plot_data.h"
#include "scene.h"
#ifdef TEST_CDXML
#include "cdxml.h"
#endif
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace yd;
static void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
static bool textContains(const Scene& s,const std::string& text){for(const auto& p:s.items)if(p.type==Primitive::Text&&p.text.find(text)!=std::string::npos)return true;return false;}
static size_t curves(const Scene& s){return std::count_if(s.items.begin(),s.items.end(),[](const auto& p){return p.type==Primitive::Bezier;});}
static Vec caption(const Scene& s,int i){for(const auto& p:s.items)if(p.type==Primitive::Text&&p.textRole==6&&p.textIndex==i)return p.points.front();throw std::runtime_error("missing caption");}
int main(){try{
    for(int mode=0;mode<=4;++mode)for(bool freeEnergy:{false,true}){
        auto p=newEnergyPlot(mode,freeEnergy);validateEnergyAnnotations(p);check(p.mode==mode&&p.energySymbol==(freeEnergy?"ΔG":"ΔE")&&p.yLabel==p.energySymbol,"new graph applies selected shape and quantity");
        check(std::all_of(p.states.begin(),p.states.end(),[](const auto& state){return state.gapFrom==-1&&!state.breakAfter;}),"new graphs have no unsolicited gaps or breaks");
        Document fresh;p.id=fresh.nextId++;p.p={-390,-120};fresh.plots={p};
        if(mode==0)check(curves(makeScene(fresh))==2,"translated new curve has no accidental break at negative world coordinates");check(makeScene(fresh).gapCaptions.empty(),"new graphs render without gap annotations");
        fresh.plots[0].states[1].gapFrom=0;auto gapScene=makeScene(fresh);check(gapScene.gapCaptions.size()==1&&textContains(gapScene,p.energySymbol),"added gap uses selected energy quantity");
#ifdef TEST_CDXML
        for(const auto& copy:{fromNative(toNative(fresh)),fromCdxml(toCdxml(fresh))})check(toNative(copy)==toNative(fresh),"shape and quantity selections survive native and CDXML saving");
#endif
    }
    {
        Document original;auto graph=reactionProfilePlot();graph.id=original.nextId++;graph.p={10,60};graph.states[1].labelOffset={12,-9};original.plots={graph};
        int a=original.addAtom({150,-40},8),b=original.addAtom({180,-40},7),bond=original.addBond(a,b);original.plots[0].states[1].above={a,b,bond};
        int below=original.addAtom({200,730},8);original.plots[0].states[0].below={below};int outside=original.addAtom({1600,80},8);
        const auto axes=makeScene(original).plotAxes.at(graph.id);const auto old=toSvg(makeScene(original));
        const double x=(axes.xEnd-axes.origin).length(),y=(axes.yEnd-axes.origin).length();
        auto resized=resizedPlotAxes(original,graph.id,x*1.35,y*1.2);auto geometry=makeScene(resized).plotAxes.at(graph.id);
        check(std::abs((geometry.xEnd-geometry.origin).length()-x*1.35)<1e-4&&std::abs((geometry.yEnd-geometry.origin).length()-y*1.2)<1e-4,"numeric axis lengths match physical axis arrow lengths");
        check((geometry.origin-axes.origin).length()<1e-5,"axis intersection remains fixed while resizing");
        const auto delta=plotAnchor(resized.plots[0],1,true)-plotAnchor(original.plots[0],1,true);
        check((resized.atom(a)->p-original.atom(a)->p-delta).length()<1e-5,"above structures follow resized state anchors");
        const auto belowDelta=plotAnchor(resized.plots[0],0,false)-plotAnchor(original.plots[0],0,false);
        check((resized.atom(below)->p-original.atom(below)->p-belowDelta).length()<1e-5,"below structures follow resized state anchors");
        check(std::abs((resized.atom(a)->p-resized.atom(b)->p).length()-30)<1e-6&&resized.atom(outside)->p.x==1600,"resizing preserves structure dimensions and unrelated objects");
        check(resized.plots[0].series[0].values==graph.series[0].values&&old==toSvg(makeScene(original)),"resizing preserves energy data and the input document");
        auto base=makeScene(resized);resized.plots[0].inkScale=2;auto heavy=makeScene(resized);bool structureInk=false,graphInk=false;
        for(size_t i=0;i<base.items.size();++i){const auto& item=heavy.items[i];const auto& previous=base.items[i];
            if(item.type==Primitive::Text&&(item.owner==a||item.owner==graph.id)){check(item.fontSize==previous.fontSize&&item.textStrokeWidth>0,"weight changes text strokes without changing text size");if(item.owner==a)structureInk=true;else graphInk=true;}
            if(item.owner==bond)check(std::abs(item.width-previous.width*2)<1e-6,"inserted structure bonds thicken with the graph");
            if(item.owner==outside)check(item.textStrokeWidth==0,"unrelated structure text weight stays unchanged");
        }
        check(structureInk&&graphInk&&toSvg(heavy).find("paint-order")!=std::string::npos,"graph and inserted structure text both gain stroke weight");
        resized.plots[0].inkScale=.5;check(toSvg(makeScene(resized)).find("feMorphology")!=std::string::npos,"SVG preserves thinning of text strokes");
#ifdef TEST_CDXML
        for(const auto& copy:{fromNative(toNative(resized)),fromCdxml(toCdxml(resized))})check(toNative(copy)==toNative(resized),"axis sizes, relocated structures and ink weight survive saving");
#endif
    }
    for(const std::string label:{"TS"," TS1 ","ts2","TS_{3}","TS-alde","Transition State","전이상태"})check(isTransitionStateLabel(label),"TS state names are recognized");
    for(const std::string label:{"R","Int1","TStart","TST","Products",""})check(!isTransitionStateLabel(label),"ordinary state names are not classified as TS");
    {EnergyState state{"Int1",0};setEnergyStateLabel(state,"TS1");check(state.transitionState,"renaming to TS enables notation");
        state.transitionState=false;setEnergyStateLabel(state,"TS1");check(!state.transitionState,"unchanged names preserve an explicit manual TS override");
        state.transitionState=true;setEnergyStateLabel(state,"Int2");check(!state.transitionState,"renaming TS to an intermediate clears the annotation");
        state.transitionState=true;setEnergyStateLabel(state,"Custom intermediate");check(state.transitionState,"custom TS designation survives ordinary label edits");}
    {auto graph=newEnergyPlot(0,true);check(graph.states[1].transitionState,"default TS node is designated as TS");
        auto edited=energyGridData(graph,{{"TS2","0"},{"Int1","20"},{"P","-10"}});
        check(edited.states[0].transitionState&&!edited.states[1].transitionState,"table renaming updates the TS designation");}
    Document d;auto p=reactionProfilePlot();check(std::all_of(p.states.begin(),p.states.end(),[](const auto& state){return state.gapFrom==-1;}),"profile factory has no automatic gaps");
    for(size_t i=1;i<p.states.size();i+=2)p.states[i].gapFrom=int(i)-1;p.id=d.nextId++;d.plots={p};validateEnergyAnnotations(p);
    auto scene=makeScene(d);check(curves(scene)==7,"six smooth connectors, including two pieces around the break");
    check(textContains(scene,"12.1")&&textContains(scene,"5.6")&&textContains(scene,"7.9"),"gaps reflect actual barriers");
    check(textContains(scene,"‡"),"TS double dagger is rendered");
    auto& edit=d.plots[0];edit.states[2].breakAfter=false;auto unbroken=makeScene(d);check(curves(unbroken)==6,"removing // restores continuous curve");
    edit.states[2].breakAfter=true;
    double holeLeft=1e20,holeRight=-1e20;int slashes=0;
    for(const auto& item:scene.items)if(item.type==Primitive::Line&&item.points.size()==2){auto a=item.points[0],b=item.points[1];if(std::abs(b.x-a.x-6)<1e-6&&std::abs(b.y-a.y+18)<1e-6){++slashes;holeLeft=std::min(holeLeft,(a.x+b.x)/2-4);holeRight=std::max(holeRight,(a.x+b.x)/2+4);}}
    check(slashes==2,"exactly two slashes mark the omitted interval");
    for(const auto& item:scene.items)if(item.type==Primitive::Bezier)check(item.points.back().x<=holeLeft+1e-6||item.points.front().x>=holeRight-1e-6,"curve has a real gap under slashes");
    edit.series[0].values[1]=21.8;check(textContains(makeScene(d),"14.1"),"gap recalculates after energy edit");
    auto before=makeScene(d);edit.states[0].labelOffset=edit.states[0].labelOffset+Vec{31,17};auto shifted=makeScene(d);check((caption(shifted,0)-caption(before,0)-Vec{31,17}).length()<1e-6,"label offsets are exact");
    edit.states[0].labelPlacement=3;
    auto hidden=makeScene(d);check(std::none_of(hidden.items.begin(),hidden.items.end(),[](const auto& p){return p.type==Primitive::Text&&p.textRole==6&&p.textIndex==0;}),"hidden state caption is absent");
    edit.states[0].labelPlacement=0;
    for(bool x:{false,true})for(bool y:{false,true}){edit.showXAxis=x;edit.showYAxis=y;auto s=makeScene(d);check(textContains(s,"Reaction coordinate")==x&&textContains(s,"ΔG (")==y,"axes independently show captions");}
    edit.showXAxis=edit.showYAxis=true;
    // An attached structure is framed by its actual geometry, not a fixed label box.
    int a=d.addAtom({260,-120},6),b=d.addAtom({295,-145},8);int bond=d.addBond(a,b);edit.states[1].above={a,b,bond};
    auto framed=makeScene(d);std::vector<Primitive> brackets;
    for(const auto& item:framed.items)if(item.owner==edit.id&&item.type==Primitive::Line&&item.points.size()==4)brackets.push_back(item);
    check(brackets.size()==2,"TS structure receives two square brackets");
    double top=brackets[0].points[0].y;d.translate({a,b,bond},{0,-40});auto moved=makeScene(d);
    bool follows=false;for(const auto& item:moved.items)if(item.owner==edit.id&&item.type==Primitive::Line&&item.points.size()==4&&std::abs(item.points[0].y-top+40)<1e-6)follows=true;
    check(follows,"TS brackets follow inserted structure movement");
#ifdef TEST_CDXML
    for(const auto& restored:{fromNative(toNative(d)),fromCdxml(toCdxml(d))})check(toNative(restored)==toNative(d),"all annotations round-trip in native and CDXML");
    auto saved=toNative(d);auto key=saved.find("breakPosition=\"");check(key!=std::string::npos,"break position stored");auto end=saved.find('"',key+15);saved.replace(key+15,end-key-15,"nan");bool rejected=false;try{fromNative(saved);}catch(...){rejected=true;}check(rejected,"invalid saved annotations rejected");
#endif
    auto remapped=energyGridData(p,{{"Reactant","7.7"},{"Int1","8.6"},{"TS2","14.2"},{"Int2","7.7"},{"TS3","15.6"},{"Product","-.3"}});
    check(remapped.states[2].gapFrom==1&&remapped.states[4].gapFrom==3,"gap endpoints follow row removal");
    auto removed=energyGridData(p,{{"TS1","19.8"},{"Int1","8.6"},{"TS2","14.2"},{"Int2","7.7"},{"TS3","15.6"},{"Product","-.3"}});check(removed.states[0].gapFrom==-1,"removed gap source hides annotation");
    for(int mode:{0,1,2,3,4}){d.plots[0].mode=mode;auto s=makeScene(d);check(std::isfinite(s.bounds.w)&&std::isfinite(s.bounds.h),"all graph forms render annotations safely");}
    d.plots[0].mode=0;
    for(double position:{.05,.95}){Document edge;edge.plots={p};edge.plots[0].states[2].breakPosition=position;check(curves(makeScene(edge))==7,"breaks near connector endpoints remain visible");}
    auto bilateral=bilateralPlot(p);validateEnergyAnnotations(bilateral);check(bilateral.states[1].labelPath==1&&bilateral.states[1].gapFrom==2,"mirrored state names and gaps follow the new left path");
    {Document multi;multi.plots={p};multi.plots[0].series.push_back(p.series[0]);auto& g=multi.plots[0];g.states[1].labelPath=g.states[1].gapPath=1;g.series[1].present.assign(7,true);g.series[1].present[1]=false;auto absent=makeScene(multi);check(std::none_of(absent.items.begin(),absent.items.end(),[](const auto& item){return item.type==Primitive::Text&&item.textRole==6&&item.textIndex==1;}),"missing path data hides its attached caption");}
    auto invalid=p;invalid.states[0].labelOffset.x=std::numeric_limits<double>::infinity();bool badOffset=false;try{validateEnergyAnnotations(invalid);}catch(...){badOffset=true;}check(badOffset,"nonfinite offsets rejected");
    {
        Document scaled;auto graph=reactionProfilePlot();graph.id=scaled.nextId++;graph.showYTicks=true;graph.states[3].gapFrom=2;scaled.plots={graph};
        auto point=[&](const Scene& scene,int state,int path=0){for(auto snap:scene.plotSnapPoints.at(graph.id))if(snap.state==state&&snap.path==path&&snap.edge==0)return snap.p;throw std::runtime_error("missing energy point");};
        const auto base=makeScene(scaled);const double originalHeight=point(base,2).y-point(base,3).y;
        auto& g=scaled.plots[0];g.states[2].breakScale=.5;validateEnergyAnnotations(g);
        auto compressed=makeScene(scaled);check(point(compressed,2).y-point(compressed,3).y<originalHeight,"50 percent compresses the displayed energy interval");
        check(g.series[0].values==graph.series[0].values&&g.states[3].energy==graph.states[3].energy,"display compression preserves actual energies");
        check(textContains(compressed,"50%")&&textContains(compressed,"5.6"),"compression is disclosed while the gap retains its actual energy");
        g.series.push_back(g.series[0]);auto multi=makeScene(scaled);check(std::abs(point(multi,3).y-point(multi,3,1).y)<1e-8,"all pathways share the transformed energy axis");
        g.states[2].breakScale=2;auto expanded=makeScene(scaled);check(point(expanded,2).y-point(expanded,3).y>originalHeight,"200 percent expands the displayed energy interval");
        g.states[1].breakAfter=true;g.states[1].breakScale=.25;
        auto bands=energyDisplayBands(g);double previous=-1e20;
        for(double value=-30;value<=50;value+=.25){double mapped=displayEnergy(bands,value);check(mapped>previous,"overlapping display bands remain monotonic");check(std::abs(energyFromDisplay(bands,mapped)-value)<1e-8,"display mapping inverts across overlapping intervals");previous=mapped;}
        check(std::abs(displayEnergy(bands,14)-displayEnergy(bands,10)-1)<1e-8,"overlaps use the smaller scale once without multiplying");
#ifdef TEST_CDXML
        for(const auto& restored:{fromNative(toNative(scaled)),fromCdxml(toCdxml(scaled))})check(toNative(restored)==toNative(scaled),"display scales and reference paths survive native and CDXML saving");
#endif
        g.mode=4;check(energyDisplayBands(g).empty(),"independent levels do not apply a hidden connection scale");g.mode=0;
        for(auto& state:g.states)state.breakAfter=false;check(energyDisplayBands(g).empty(),"removing breaks restores the original scale");
        for(double bad:{0.,4.1,std::numeric_limits<double>::quiet_NaN()}){g.states[2].breakScale=bad;bool rejected=false;try{validateEnergyAnnotations(g);}catch(...){rejected=true;}check(rejected,"invalid display scales are rejected");}
        g.states[2].breakScale=.5;g.states[2].breakPath=2;bool rejected=false;try{validateEnergyAnnotations(g);}catch(...){rejected=true;}check(rejected,"invalid scale reference paths are rejected");
        g.states[2].breakPath=1;g.states[2].breakAfter=true;g.series[1].present.assign(g.states.size(),true);g.series[1].present[3]=false;check(energyDisplayBands(g).empty(),"missing reference energy cannot create a spurious band");
        g.states[2].breakPath=0;std::ofstream("reaction-profile-scaled-preview.svg")<<toSvg(makeScene(scaled));
    }
    {
        Document spacing;auto graph=newEnergyPlot(0,true);graph.id=spacing.nextId++;graph.unit="kcal mol^{-1}";graph.energyLabelFontSize=20;graph.yAxisWidth=30;graph.inkScale=3;graph.thinLines=true;graph.showYTicks=false;spacing.plots={graph};
        auto titleRight=[&](const Scene& rendered){double right=-1e20;for(const auto& item:rendered.items)if(item.owner==graph.id&&item.type==Primitive::Text&&item.rotation==-90)right=std::max(right,item.points[0].x+item.fontSize*1.3+std::max(0.,item.textStrokeWidth/2));check(right>-1e19,"Y axis title is rendered");return right;};
        const auto automatic=makeScene(spacing);const auto axes=automatic.plotAxes.at(graph.id);
        check(axes.origin.x-15-titleRight(automatic)>=24-1e-8,"automatic title spacing clears a thick axis and rotated scripts");
        spacing.plots[0].yTitleGap=20;const auto near=makeScene(spacing);spacing.plots[0].yTitleGap=60;const auto far=makeScene(spacing);
        check(std::abs(titleRight(near)-titleRight(far)-40)<1e-8,"Y title clearance changes by the requested amount");
        check((near.plotAxes.at(graph.id).origin-far.plotAxes.at(graph.id).origin).length()<1e-8,"title spacing does not move the graph axes");
        spacing.plots[0].showYTicks=true;const auto withTicks=makeScene(spacing);check(titleRight(withTicks)<titleRight(far),"title spacing also reserves the tick label width");
#ifdef TEST_CDXML
        for(const auto& restored:{fromNative(toNative(spacing)),fromCdxml(toCdxml(spacing))})check(toNative(restored)==toNative(spacing),"Y title spacing survives native and CDXML saving");
        for(double invalid:{-1.,301.,std::numeric_limits<double>::quiet_NaN()}){spacing.plots[0].yTitleGap=invalid;bool rejected=false;try{fromNative(toNative(spacing));}catch(...){rejected=true;}check(rejected,"invalid Y title gaps are rejected on load");}
#endif
        spacing.plots[0].yTitleGap=20;spacing.plots[0].energyAtTop=true;const auto upperNear=makeScene(spacing);spacing.plots[0].yTitleGap=60;const auto upperFar=makeScene(spacing);
        auto headerY=[&](const Scene& rendered){for(const auto& item:rendered.items)if(item.owner==graph.id&&item.type==Primitive::Text&&item.text==graph.energySymbol)return item.points[0].y;throw std::runtime_error("missing upper Y title");};
        check(std::abs(headerY(upperNear)-headerY(upperFar)-40)<1e-8,"spacing also adjusts the title above the axis");
        spacing.plots[0].energyAtTop=false;std::ofstream("y-title-spacing-preview.svg")<<toSvg(makeScene(spacing));
    }
    {
        Document styled;auto graph=newEnergyPlot(0,true);graph.id=styled.nextId++;graph.title="";graph.xLabel="";graph.showYTicks=false;graph.states={{"A",0},{"B",20}};graph.series[0].values={0,20};graph.series[0].color="#137faa";
        graph.fixedValueFontSize=true;graph.stateFontSize=26;graph.valueFontSize=24;graph.xAxisWidth=3;graph.yAxisWidth=5;graph.curveWidth=7;graph.thinLines=true;graph.inkScale=1.5;
        int a=styled.addAtom({40,40},6),b=styled.addAtom({65,40},6);int bond=styled.addBond(a,b);graph.states[0].above={a,b,bond};styled.plots={graph};
        const auto initial=makeScene(styled);styled.plots[0].structureInkScale=2;const auto scene=makeScene(styled);const auto axes=scene.plotAxes.at(graph.id);
        bool x=false,y=false,curve=false,name=false,value=false,structure=false;
        for(const auto& item:scene.items){
            if(item.owner==graph.id&&item.type==Primitive::Line&&item.points.size()==2){
                if(std::abs(item.points[0].y-axes.origin.y)<1e-8&&std::abs(item.points[1].y-axes.origin.y)<1e-8){x=true;check(std::abs(item.width-3)<1e-8,"X axis thickness is independent of global and thin stroke settings");}
                if(std::abs(item.points[0].x-axes.origin.x)<1e-8&&std::abs(item.points[1].x-axes.origin.x)<1e-8){y=true;check(std::abs(item.width-5)<1e-8,"Y axis thickness is independent of curve thickness");}
            }
            if(item.owner==graph.id&&item.type==Primitive::Bezier&&item.color==graph.series[0].color){curve=true;check(std::abs(item.width-7)<1e-8,"curve thickness is independently respected");}
            if(item.owner==graph.id&&item.type==Primitive::Text&&item.textRole==6){name=true;check(std::abs(item.fontSize-26)<1e-8,"state name font is independent of axis ticks");}
            if(item.owner==graph.id&&item.type==Primitive::Text&&item.textRole==8){value=true;check(std::abs(item.fontSize-24)<1e-8,"explicit data value size is not reduced by automatic placement");}
            if(item.owner==bond&&item.type==Primitive::Line){structure=true;for(const auto& old:initial.items)if(old.owner==bond&&old.type==Primitive::Line)check(std::abs(item.width-old.width*2)<1e-8,"inserted structure stroke can change without resizing text or axes");}
        }
        check(x&&y&&curve&&name&&value&&structure,"all independently styled graph elements are rendered");
#ifdef TEST_CDXML
        for(const auto& restored:{fromNative(toNative(styled)),fromCdxml(toCdxml(styled))})check(toNative(restored)==toNative(styled),"independent plot appearance survives native and CDXML saving");
#endif
    }
    {
        Document arrows;auto graph=newEnergyPlot(0,true);graph.id=arrows.nextId++;graph.thinLines=false;graph.xColor="#a12345";
        graph.stages=mechanismStageData({{"Reaction","0","100","14","2.5","8"}});arrows.plots={graph};
        auto arrowItems=[&](){std::vector<Primitive> out;for(auto item:makeScene(arrows).items)if(item.color==graph.xColor&&(item.type==Primitive::Polygon||item.type==Primitive::Line))out.push_back(item);return out;};
        auto headLength=[&](){double length=0;for(auto item:arrowItems())if(item.type==Primitive::Polygon&&item.points.size()==3)length=std::max(length,(item.points[0]-(item.points[1]+item.points[2])*.5).length());return length;};
        check(std::abs(headLength()-8)<1e-8,"explicit arrowhead length is respected");
#ifdef TEST_CDXML
        for(const auto& restored:{fromNative(toNative(arrows)),fromCdxml(toCdxml(arrows))})check(toNative(restored)==toNative(arrows),"explicit reaction arrowhead and thickness survive saving");
#endif

        bool shaft=false;for(auto item:arrowItems())if(item.type==Primitive::Line){shaft=true;check(std::abs(item.width-2.5)<1e-8,"reaction arrow thickness has its own setting");}check(shaft,"reaction shafts are rendered");
        arrows.plots[0].stages[0].arrowWidth=1;check(std::abs(headLength()-8)<1e-8,"shaft thickness changes independently of explicit head length");
        arrows.plots[0].stages[0].arrowHeadSize=0;const double wideHead=headLength();arrows.plots[0].stages[0].start=35;arrows.plots[0].stages[0].end=65;
        check(headLength()>0&&headLength()<wideHead,"automatic arrowheads follow the available reaction span");
#ifdef TEST_CDXML
        for(const auto& restored:{fromNative(toNative(arrows)),fromCdxml(toCdxml(arrows))})check(toNative(restored)==toNative(arrows),"reaction arrow styles survive native and CDXML saving");
#endif
        for(double width:{50.,maxStageArrowWidth}){
            arrows.plots[0].stages=mechanismStageData({{"Reaction","0","100","50",std::to_string(width),"0"}});
            bool rendered=false;for(auto item:arrowItems())if(item.type==Primitive::Line){rendered=true;check(std::abs(item.width-width)<1e-8,"large reaction arrow thickness is applied to the scene");}
            check(rendered,"large reaction arrow shafts are rendered");
#ifdef TEST_CDXML
            for(const auto& restored:{fromNative(toNative(arrows)),fromCdxml(toCdxml(arrows))})check(toNative(restored)==toNative(arrows),"large reaction arrow thickness survives saving and reopening");
#endif
        }
        auto old=mechanismStageData({{"Legacy","0","100","14"}});check(old[0].arrowWidth==0&&old[0].arrowHeadSize==0,"older stage rows retain automatic arrow sizing");
        for(auto row:std::vector<std::vector<std::string>>{{"Bad","0","100","14","101","0"},{"Bad","0","100","14","nan","0"},{"Bad","0","100","14","1","41"}}){bool rejected=false;try{mechanismStageData({row});}catch(...){rejected=true;}check(rejected,"invalid reaction arrow dimensions are rejected");}
        std::ofstream("reaction-arrow-style-preview.svg")<<toSvg(makeScene(arrows));
    }
    {
        Document profile;auto graph=newEnergyPlot(0,true);graph.id=profile.nextId++;graph.states={{"A2",7.7},{"A2-TS",19.8},{"B2",-56.9},{"C2",-60.1},{"D2",-53.3},{"E2",-87.4}};
        graph.series[0].values={7.7,19.8,-56.9,-60.1,-53.3,-87.4};graph.states[1].transitionState=true;profile.plots={graph};
        auto vertical=[&](const Scene& scene,int state){for(auto point:scene.plotSnapPoints.at(graph.id))if(point.state==state&&point.path==0&&point.edge==0)return point.p.y;throw std::runtime_error("missing profile state");};
        const auto before=makeScene(profile);auto& state=profile.plots[0].states[1];state.breakAfter=true;state.breakStyle=1;state.breakScale=.1;
        const auto bands=energyDisplayBands(profile.plots[0]);check(bands.size()==1&&bands[0].low>-53.3&&bands[0].high<7.7,"wave automatically selects the large empty energy interval below the peak");
        check(std::abs(displayEnergy(bands,19.8)-displayEnergy(bands,7.7)-12.1)<1e-8,"compression preserves the complete local activation barrier");
        const auto after=makeScene(profile);check(vertical(after,0)-vertical(after,1)>1.5*(vertical(before,0)-vertical(before,1)),"large negative product energies no longer flatten the first peak");
        check(profile.plots[0].series[0].values==graph.series[0].values,"wave omission preserves the supplied energies");
        const auto gentle=after;state.breakOmit=50;state.breakUpperScale=3;const auto strong=makeScene(profile);const auto strongBands=energyDisplayBands(profile.plots[0]);
        const auto omitted=strongBands.front();check(std::abs((omitted.high-omitted.low)*(1-omitted.scale)-50)<1e-8,"an explicit amount removes fifty energy units from the displayed interval");
        check(std::abs(displayEnergy(strongBands,19.8)-displayEnergy(strongBands,7.7)-12.1*3)<1e-8,"upper panel independently magnifies the local peak threefold");
        check(vertical(strong,0)-vertical(strong,1)>1.5*(vertical(gentle,0)-vertical(gentle,1)),"strong omission makes the local peak substantially higher than gentle compression");
        check(textContains(strong,"19.8")&&textContains(strong,"56.9"),"strong omission still displays the original energy numbers");
#ifdef TEST_CDXML
        for(const auto& restored:{fromNative(toNative(profile)),fromCdxml(toCdxml(profile))})check(toNative(restored)==toNative(profile),"omission amount and upper magnification survive saving");
#endif
        for(double value:{-100.,-53.3,0.,7.7,19.8,35.})check(std::abs(energyFromDisplay(strongBands,displayEnergy(strongBands,value))-value)<1e-8,"upper magnification mapping remains invertible");
        state.breakOmit=0;state.breakUpperScale=1;
        state.breakLow=-50;state.breakHigh=0;validateEnergyAnnotations(profile.plots[0]);auto explicitBands=energyDisplayBands(profile.plots[0]);check(explicitBands.size()==1&&explicitBands[0].low==-50&&explicitBands[0].high==0,"explicit energy bounds control the omitted interval");
#ifdef TEST_CDXML
        for(const auto& restored:{fromNative(toNative(profile)),fromCdxml(toCdxml(profile))})check(toNative(restored)==toNative(profile),"wave and explicit omission bounds survive saving");
#endif
        state.breakHigh=-60;bool rejected=false;try{validateEnergyAnnotations(profile.plots[0]);}catch(...){rejected=true;}check(rejected,"reversed energy omission limits are rejected");
        state.breakHigh=0;std::ofstream("energy-wave-peak-preview.svg")<<toSvg(makeScene(profile));
    }
    {
        Document thick;auto g=newEnergyPlot(0,true);g.id=thick.nextId++;g.thinLines=false;g.inkScale=3;g.series[0].width=10;g.showYAxis=false;g.states[1].breakAfter=true;g.states[1].breakStyle=1;g.valueGap=28;thick.plots={g};
        std::vector<Primitive> waves;for(auto item:makeScene(thick).items)if(item.type==Primitive::Bezier&&item.points.size()==4&&std::abs((item.points.back()-item.points.front()).length()-10)<1e-6)waves.push_back(item);
        check(waves.size()==4&&(waves[0].points[0]-waves[2].points[0]).length()>waves[0].width+10,"thick waves have a real stroke-dependent separation");
        thick.plots[0].showYAxis=true;auto rendered=makeScene(thick);auto axes=rendered.plotAxes.at(g.id);bool axis=false;
        for(auto item:rendered.items)if(item.type==Primitive::Line&&item.points.size()==2&&std::abs(item.points[0].x-axes.origin.x)<1e-8&&std::abs(item.points[1].x-axes.origin.x)<1e-8){axis=true;check(std::abs(item.width-30)<1e-8,"y-axis inherits the curve's displayed width");}check(axis,"y-axis shaft is present");
        thick.plots[0].yAxisWidth=2;auto custom=makeScene(thick);for(auto item:custom.items)if(item.type==Primitive::Line&&item.points.size()==2&&std::abs(item.points[0].x-axes.origin.x)<1e-8&&std::abs(item.points[1].x-axes.origin.x)<1e-8)check(std::abs(item.width-2)<1e-8,"explicit y-axis width remains independent of overall ink scaling");
#ifdef TEST_CDXML
        for(const auto& restored:{fromNative(toNative(thick)),fromCdxml(toCdxml(thick))})check(toNative(restored)==toNative(thick),"label clearance and separate axis width survive saving");
#endif
    }
    std::ofstream("reaction-profile-preview.svg")<<toSvg(makeScene(d));
#ifdef TEST_CDXML
    std::ofstream("reaction-profile-preview.youngdraw")<<toNative(d);
#endif
    std::cout<<"Reaction profile annotations passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

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
    std::ofstream("reaction-profile-preview.svg")<<toSvg(makeScene(d));
#ifdef TEST_CDXML
    std::ofstream("reaction-profile-preview.youngdraw")<<toNative(d);
#endif
    std::cout<<"Reaction profile annotations passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

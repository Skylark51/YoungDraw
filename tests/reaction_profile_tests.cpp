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
    Document d;auto p=reactionProfilePlot();p.id=d.nextId++;d.plots={p};validateEnergyAnnotations(p);
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

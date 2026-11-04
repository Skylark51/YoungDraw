#include "model.h"
#include "scene.h"
#include "mechanism_data.h"
#include "cdxml.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace yd;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static int text(Document& d,Vec p,std::string value){Object o;o.id=d.nextId++;o.kind=ObjectKind::Text;o.points={p};o.text=value;d.objects.push_back(o);return o.id;}
static int arrow(Document& d,Vec a,Vec b){Object o;o.id=d.nextId++;o.points={a,b};o.head=1;d.objects.push_back(o);return o.id;}
static void rigid(const Document& before,const Document& after){for(auto& b:before.bonds){auto* now=after.bond(b.id);check(now&&now->kind==b.kind&&now->a==b.a&&now->b==b.b,"chemical bond identity preserved");check(((before.atom(b.b)->p-before.atom(b.a)->p)-(after.atom(b.b)->p-after.atom(b.a)->p)).length()<1e-7,"bond vectors preserved without rotation or scaling");}for(auto a:before.atoms){auto* b=after.atom(a.id);check(b&&b->charge==a.charge&&b->label==a.label&&b->element==a.element&&b->isotope==a.isotope,"chemical labels and properties preserved");}}
int main(){try{
    {
        Document d;d.ring({50,50},6,true);int anchor=d.atoms[0].id,outside=d.addAtom({100,50});int connector=d.addBond(anchor,outside);auto original=d;d.groups.push_back({d.nextId++,d.allIds()});
        check(collapsePhenyl(d,d.allIds())==1&&d.atoms.size()==2&&d.bonds.size()==1,"monosubstituted benzene becomes Ph inside a selected group");
        check(d.atom(anchor)->label=="Ph"&&d.bond(connector)&&d.bond(connector)->a==anchor,"Ph retains original attachment atom and external bond");
        check((d.atom(outside)->p-original.atom(outside)->p).length()<1e-8,"phenyl abbreviation leaves remainder fixed");
        History history;history.checkpoint(original);check(history.undo(d)&&d.atoms.size()==original.atoms.size(),"phenyl conversion is undoable");
        int other=d.addAtom({0,50});d.addBond(d.atoms[2].id,other);check(collapsePhenyl(d,d.allIds())==0,"disubstituted benzene is not silently abbreviated");
    }
    {
        Document d;int id=d.addAtom({20,20},1);auto* a=d.atom(id);a->label="+H^{+}";a->charge=2;a->color="#ff0000";
        auto picture=makeScene(d);for(auto& p:picture.items)check(p.textRole!=13,"explicit proton label has no synthetic floating 2+ annotation");
        a=d.atom(id);a->label.clear();a->charge=1;picture=makeScene(d);bool plus=false;for(auto& p:picture.items)if(p.textRole==13&&p.text=="+")plus=true;check(plus,"an explicitly set charge remains visible on an automatic atom label");
        a->label="N-C";check(!atomLabelHasCharge(*a),"a hyphen in a custom label does not conceal a real charge");
    }
    {
        Document d;std::vector<int> starts;
        for(Vec p:std::vector<Vec>{{0,0},{280,0},{280,210}}){int a=d.addAtom(p),b=d.addAtom(p+Vec{25,11},8);d.addBond(a,b,BondKind::Wedge);d.atom(b)->isotope=18;starts.push_back(a);}
        arrow(d,{65,0},{220,0});arrow(d,{65,210},{220,210});
        // Both arrows originate at the same structure, including a lower branch.
        d.objects.back().points.front()={65,70};
        text(d,{12,42},"I_{1}");text(d,{12,64},"0.0 / 9.4 / 12.9");text(d,{145,-30},"Ring opening");text(d,{145,28},"16.1 / 13.3 / 23.0");int reagent=text(d,{145,-70},"+HOTf");d.objects.back().color="#ff0000";
        int proton=d.addAtom({135,-55},1);d.atom(proton)->charge=1;d.atom(proton)->label="+H^{+}";
        Object electron;electron.id=d.nextId++;electron.kind=ObjectKind::Curve;electron.head=2;electron.points={{0,0},{4,-15},{20,-15},{25,11}};d.objects.push_back(electron);
        Group all;all.id=d.nextId++;all.members=d.allIds();d.groups.push_back(all);
        auto before=d;int owner=arrangeImportedMechanism(d);rigid(before,d);auto& r=d.reactions[0];
        check(r.id==owner&&r.slots.size()==3&&r.links.size()==2,"imported mechanism becomes editable frames and graph connectors");
        check(r.links[0].from==0&&r.links[0].to==1&&r.links[1].from==0&&r.links[1].to==2,"branch topology is retained");
        check(r.slots[0].label=="I_{1}"&&r.slots[0].energy=="0.0 / 9.4 / 12.9","imported names and slash-separated multiplicities occupy state captions");
        check(r.above[0]=="Ring opening"&&r.below[0]=="16.1 / 13.3 / 23.0","arrow descriptions and spin energies occupy transition captions");
        check(r.links[0].members==std::vector<int>({reagent,proton}),"isolated proton notation belongs to its reaction arrow");
        check(d.objects.size()==2&&d.objects.back().id==electron.id,"electron movement retained and replaced annotations removed exactly once");
        r.crosses.push_back({0,true,6,22,"#00aa88"});auto stable=d;optimizeMechanism(d,{owner});rigid(stable,d);
        for(auto restored:{fromNative(toNative(d)),fromCdxml(toCdxml(d))}){
            check(restored.reactions[0].links.size()==2&&restored.reactions[0].links[1].from==0,"branch links survive native and CDXML roundtrips");
            check(restored.reactions[0].crosses[0].color=="#00aa88"&&restored.reactions[0].crosses[0].width==6,"cross style survives roundtrip");
            check(restored.reactions[0].links[0].members==std::vector<int>({reagent,proton}),"notation ownership survives roundtrip");rigid(d,restored);
        }
        auto branchPath=mechanismLinkPath(r,1,d.style);auto mainCaption=mechanismLinkCaption(r,0,d.style);check(branchPath.size()==4&&branchPath[1].x<mainCaption.x-80,"branch stem stays left of TS caption and energy boxes");
        auto picture=makeScene(d);int crosses=0;for(auto& p:picture.items)if(p.owner==owner&&p.color=="#00aa88"&&p.width==6)crosses++;check(crosses==2,"arrow cross is two styled vector strokes");
        auto copy=d.subset({owner});Document pasted;pasted.append(copy,{90,40});check(pasted.validate().empty(),"copying a branch preserves valid references");
        check(pasted.reactions[0].links[0].members.size()==2&&pasted.atom(pasted.reactions[0].links[0].members[1]),"arrow notation copied with reaction and remapped");
        std::ofstream("mechanism-import-arranged.svg")<<toSvg(picture);std::ofstream("mechanism-import-arranged.ydraw")<<toNative(d);
        auto size=r.slots.size();addMechanismBranch(r,1);check(r.slots.size()==size+1&&r.links.back().from==1,"new branch starts from chosen structure");
        check(r.slots.back().label.empty()&&r.above.back().empty()&&r.below.back().empty(),"new frames contain no invented captions or energies");
    }
    {
        Document d;int id=arrow(d,{0,0},{100,0});d.objects[0].cross={-1,true,5,16,"#cc0033"};auto svg=toSvg(makeScene(d));check(svg.find("#cc0033")!=std::string::npos,"free arrow cross exported as vector ink");
        auto reopened=fromCdxml(toCdxml(d));check(reopened.objects[0].cross.enabled&&reopened.objects[0].id==id,"free arrow cross persists");
        Reaction r;r.id=d.nextId++;r.slots={{"R",{}},{"P",{}}};r.above={""};r.below={""};r.crosses={{0,true,5,16,"#cc0033"}};d.objects.clear();d.reactions={r};
        for(int mode=0;mode<4;mode++){auto& rx=d.reactions[0];rx.cyclic=mode==1;rx.vertical=mode==2;rx.wrapColumns=mode==3?1:0;auto picture=makeScene(d);int n=0;for(auto& p:picture.items)if(p.color=="#cc0033")n++;check(n==2,"X supported on straight, cyclic, vertical and bent connectors");}
    }
    {
        auto imported=fromCdxml("<CDXML><page id=\"1\"><fragment id=\"2\"><n id=\"3\" p=\"0 0\"/><n id=\"4\" p=\"18 9\"/><b id=\"5\" B=\"3\" E=\"4\"/></fragment></page></CDXML>");auto before=imported;check(!upgradeBondGeometry(imported),"opening an external CDXML does not silently normalize bond geometry");rigid(before,imported);
    }
    std::cout<<"Mechanism editing regressions passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

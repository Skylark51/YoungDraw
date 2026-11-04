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
        Document d;Reaction r;r.id=d.nextId++;r.slots={{"R",{}},{"P",{}}};r.spacing=300;r.slotHeight=80;r.slotGrid={{0,0},{1,.06}};r.links={{0,1}};r.above={"O-O bond cleavage"};r.below={"13.5 / 9.5 / 9.5"};d.reactions.push_back(r);
        auto original=toNative(d);
        for(double offset:{.06,-.06,0.}){
            d.reactions[0].slotGrid[1].y=offset;auto path=mechanismLinkPath(d.reactions[0],0,d.style);
            check(path.size()==2&&path.front().y==path.back().y,"small differences in imported frame height produce one straight horizontal arrow");
            auto picture=makeScene(d);int shafts=0;for(auto& item:picture.items)if(item.owner==r.id&&item.reactionArrow==0&&item.type==Primitive::Line){check(item.points.front().y==item.points.back().y,"rendered connector has no short vertical stair step");shafts++;}check(shafts==1,"same-row connector renders as a single shaft");
        }
        d.reactions[0]=r;check(toNative(d)==original,"straightening connector does not move frame positions");
        for(auto restored:{fromNative(toNative(d)),fromCdxml(toCdxml(d))}){auto path=mechanismLinkPath(restored.reactions[0],0,restored.style);check(path.size()==2&&path.front().y==path.back().y,"straight same-row arrows survive native and CDXML reopening");}
        std::ofstream("mechanism-straight-arrow.svg")<<toSvg(makeScene(d));
        d.reactions[0].slots.push_back({"Branch",{}});d.reactions[0].slotGrid.push_back({1,1});d.reactions[0].links.push_back({0,2});
        auto main=mechanismLinkPath(d.reactions[0],0,d.style);for(auto point:main)check(point.y==main.front().y,"same-row arrow remains straight when sharing a branch stem");
        auto branch=mechanismLinkPath(d.reactions[0],1,d.style);check(branch.size()==4&&branch[1].x==branch[2].x&&branch[1].y!=branch[2].y,"actual branches to a different row retain their vertical connecting stem");
    }
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
        auto branchPath=mechanismLinkPath(r,1,d.style);auto mainCaption=mechanismLinkCaption(r,0,d.style);check(branchPath.size()==4&&branchPath[1].x<mainCaption.x-(mechanismArrowLength(r,d.style)/2-mechanismFontSize(r,d.style)*.3),"branch stem stays left of TS caption and energy boxes");
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
    {
        Document d;auto ids=addBranchArrows(d,{0,0},180,120,4);
        check(ids.size()==5&&d.objects.size()==5&&d.groups.size()==1,"four arrows share exactly one grouped spine");
        for(size_t i=1;i<d.objects.size();i++){auto& o=d.objects[i];check(o.head==1&&o.points[0].x==0&&o.points[1].x==120,"every branch starts on the same vertical line");}
        auto original=d;d.translate(ids,{40,20});
        for(size_t i=0;i<d.objects.size();i++)check((d.objects[i].points[0]-original.objects[i].points[0]-Vec{40,20}).length()<1e-8,"fork moves as one group");
        for(auto saved:{fromNative(toNative(d)),fromCdxml(toCdxml(d))})check(saved.groups.size()==1&&saved.objects.size()==5,"standalone fork survives native and CDXML");
        auto svg=toSvg(makeScene(d));check(svg.find("<polygon")!=std::string::npos,"fork arrowheads export as vector geometry");
        auto before=toNative(d);bool rejected=false;try{addBranchArrows(d,{0,0},100,100,1);}catch(...){rejected=true;}
        check(rejected&&toNative(d)==before,"invalid branch count leaves document untouched");
        Document existing;Object stem;stem.id=existing.nextId++;stem.points={{10,20},{10,220}};stem.color="#cc0000";existing.objects.push_back(stem);
        addBranchArrows(existing,{10,20},200,100,3,true,stem.id);
        check(existing.objects.size()==4&&existing.objects[1].points.back().x==-90&&existing.objects[1].color=="#cc0000","selected spine is reused with leftwards arrows and its color");
    }
    {
        Document d;std::vector<std::vector<int>> molecules;
        for(Vec at:std::vector<Vec>{{100,0},{150,0},{340,0},{100,200},{340,200},{100,400}}){int a=d.addAtom(at,6),b=d.addAtom(at+Vec{15,10},8);d.addBond(a,b);molecules.push_back({a,b});}
        // Two disconnected species form the first state, separated from the next by an arrow.
        arrow(d,{200,0},{300,0});arrow(d,{160,200},{300,200});
        Object stem;stem.id=d.nextId++;stem.points={{0,0},{0,400}};d.objects.push_back(stem);
        arrow(d,{0,0},{70,0});arrow(d,{0,200},{70,200});arrow(d,{0,400},{70,400});
        text(d,{240,-30},"CoIII reaction");d.objects.back().fontSize=10;
        auto before=d;int owner=arrangeImportedMechanism(d);auto& r=d.reactions[0];
        check(r.slots.size()==5,"two disconnected molecules between reaction arrows become one state");
        check(r.slots[0].members.size()==4&&r.slots[0].members[0]==molecules[0][0],"compound state retains both molecular fragments");
        int external=0;for(auto& link:r.links)external+=link.from==-1;
        check(external==3&&r.links.size()==5,"three-way external fork is retained without invented reactant structures");
        check(r.captionFontSize==10,"import keeps the source caption font size");
        check(r.above[0]=="Co^{III} reaction","imported reaction captions normalize oxidation numbers");
        auto picture=makeScene(d);int spines=0;
        for(auto& item:picture.items)if(item.owner==owner&&item.type==Primitive::Line&&item.reactionArrow==-1&&item.points.size()==2&&std::abs(item.points[0].x-item.points[1].x)<.01&&std::abs(item.points[0].y-item.points[1].y)>100)spines++;
        check(spines==1,"external fork renders one continuous vertical spine");
        for(auto reopened:{fromNative(toNative(d)),fromCdxml(toCdxml(d))}){
            int n=0;for(auto& link:reopened.reactions[0].links)n+=link.from==-1;
            check(n==3&&reopened.reactions[0].slots[0].members.size()==4,"external fork and compound frames roundtrip");
        }
        check(d.groups.size()==1,"disconnected state is grouped for selection and transport");
        for(auto& b:before.bonds)check(((before.atom(b.a)->p-before.atom(b.b)->p)-(d.atom(b.a)->p-d.atom(b.b)->p)).length()<1e-7,"default import preserves molecular geometry");
        auto members=r.slots[0].members;auto first=d.atom(members[0])->p,second=d.atom(members[2])->p;
        transformStructures(d,members,90);check(std::abs((d.atom(members[2])->p-d.atom(members[0])->p).length()-(second-first).length())<1e-7,"compound state rotates rigidly as one unit");
    }
    {
        Document d;
        for(Vec at:std::vector<Vec>{{-100,0},{280,0},{280,250},{280,500}}){int a=d.addAtom(at),b=d.addAtom(at+Vec{20,10},8);d.addBond(a,b);}
        Object stem;stem.id=d.nextId++;stem.points={{0,0},{0,500}};d.objects.push_back(stem);
        for(double y:{0.,250.,500.})arrow(d,{0,y},{240,y});
        arrangeImportedMechanism(d);auto& r=d.reactions[0];
        check(r.links.size()==3&&std::all_of(r.links.begin(),r.links.end(),[](const MechanismLink& link){return link.from==0;}),"all three spine branches inherit the same upstream reactant even far below it");
        auto first=mechanismLinkPath(r,1,d.style),second=mechanismLinkPath(r,2,d.style);
        check(first.size()>=3&&second.size()>=3&&first[1].x==second[1].x,"internal multiway branches share one fixed vertical spine");
    }
    {
        Document d;int a=d.addAtom({0,0},27);d.atom(a)->label="LCoIII";
        d.atom(a)->labelRuns={{"LCo","#111111","Arial",12,0},{"I","#ff0000","Arial",12,0},{"II","#0000ff","Arial",12,0}};
        int label=text(d,{0,30},"FeII");d.objects.back().runs={{"FeII","#22aa55","Arial",14,1}};
        normalizeChemicalLabels(d);
        check(d.atom(a)->label=="LCo^{III}"&&d.atom(a)->labelRuns[1].face==64&&d.atom(a)->labelRuns[1].color=="#ff0000"&&d.atom(a)->labelRuns[2].color=="#0000ff","split imported atom runs normalize Roman numerals without losing colors");
        check(d.objects.back().text=="Fe^{II}"&&(d.objects.back().runs.back().face&64)&&d.objects.back().runs.back().size<14,"free text oxidation state gains a smaller superscript");
        auto saved=toNative(d);normalizeChemicalLabels(d);check(toNative(d)==saved,"chemical label normalization is idempotent");
        for(auto reopened:{fromNative(saved),fromCdxml(toCdxml(d))})check((reopened.atom(a)->labelRuns.back().face&64)!=0,"oxidation scripts survive output and reopen");
    }
    {
        Reaction r;r.slots={{"A",{}},{"B",{}},{"C",{}}};r.slotGrid={{0,0},{0,1},{0,2}};r.links={{-1,0},{-1,1},{-1,2}};
        auto original=r.slotGrid;appendMechanismStage(r,false,0);
        check(r.links.back().from==0&&r.links.back().to==3,"extend the top branch instead of the last branch");
        appendMechanismStage(r,false,1);check(r.links.back().from==1&&r.links.back().to==4,"middle branch extends independently");
        for(size_t i=0;i<3;i++)check((r.slotGrid[i]-original[i]).length()<1e-8,"other branch frames remain fixed");
        check(r.links[0].from==-1,"extending a rootless branch creates no reactant");
    }
    {
        Document d;
        for(Vec p:std::vector<Vec>{{0,0},{250,0},{250,180}}){int a=d.addAtom(p),b=d.addAtom(p+Vec{20,10},8);d.addBond(a,b);}
        arrow(d,{60,0},{210,0});arrow(d,{60,60},{210,180});auto before=d;
        arrangeImportedMechanism(d,{},true);
        for(auto& a:before.atoms)check((d.atom(a.id)->p-a.p).length()<1e-8,"source layout mode preserves every original atom position");
        auto& r=d.reactions.back();for(size_t i=0;i<r.slots.size();i++){
            auto b=makeScene(d.subset(r.slots[i].members)).bounds;
            check((reactionSlot(r,i)-Vec{b.x+b.w/2,b.y+b.h/2}).length()<1e-6,"frame remains centered on the original structure");
        }
    }
    std::cout<<"Mechanism editing regressions passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

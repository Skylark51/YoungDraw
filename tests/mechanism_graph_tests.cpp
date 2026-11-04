#include "model.h"
#include "mechanism_data.h"
#include "scene.h"
#include "cdxml.h"
#include "mechanism_layout_fixture.h"
#include <iostream>
#include <stdexcept>
#include <fstream>
using namespace yd;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    {
        auto d=parallelMechanismFixture();auto original=d;int owner=d.reactions[0].id;optimizeMechanism(d,{owner});auto& r=d.reactions[0];
        double upper=r.slotGrid[0].y,lower=r.slotGrid[4].y;
        check(upper<lower&&r.slotGrid[10].y>lower,"parallel roots retain globally separate rows");
        for(size_t i=0;i<4;i++)check(r.slotGrid[i].y==upper,"upper pathway stays on its original row");
        for(size_t i=4;i<10;i++)check(r.slotGrid[i].y==lower,"longer lower pathway does not jump to the middle after the upper path ends");
        check(r.slotGrid[11].y>upper&&r.slotGrid[11].y<lower,"short side branch keeps its row between the two pathways");
        check(r.links.size()==original.reactions[0].links.size()&&r.above==original.reactions[0].above&&r.below==original.reactions[0].below,"alignment preserves connection identities and captions");
        auto stable=d;optimizeMechanism(d,{owner});
        for(size_t i=0;i<r.slotGrid.size();i++)check((r.slotGrid[i]-stable.reactions[0].slotGrid[i]).length()<1e-9,"repeated alignment never changes row/column placement");
        for(auto a:stable.atoms)check((d.atom(a.id)->p-a.p).length()<1e-8,"repeated alignment does not drift molecules");
        std::ofstream("mechanism-parallel-preserved.svg")<<toSvg(makeScene(d));
        for(auto restored:{fromNative(toNative(d)),fromCdxml(toCdxml(d))}){optimizeMechanism(restored,{owner});for(size_t i=0;i<r.slotGrid.size();i++)check((restored.reactions[0].slotGrid[i]-r.slotGrid[i]).length()<1e-9,"saved parallel pathways retain rows when realigned");}
    }
    {
        auto d=parallelMechanismFixture();auto& r=d.reactions[0];r.links.push_back({7,5,{}});r.above.push_back("Reversible step");r.below.push_back("1.0");
        optimizeMechanism(d,{r.id});double widest=0;for(auto p:r.slotGrid)widest=std::max(widest,p.x);
        check(widest<=5,"cycle and its downstream stages do not unroll into an arbitrarily long horizontal chain");
        for(size_t i=4;i<10;i++)check(r.slotGrid[i].y==r.slotGrid[4].y,"cycle preserves the lower pathway row");
        auto stable=r.slotGrid;optimizeMechanism(d,{r.id});for(size_t i=0;i<stable.size();i++)check((stable[i]-r.slotGrid[i]).length()<1e-9,"cycle layout is stable on repeated alignment");
    }
    {
        Reaction r;r.manualConnections=true;r.slots.resize(7);r.slotGrid.resize(7);r.links={{0,1},{1,2},{2,3},{0,4},{4,5},{5,6}};
        arrangeMechanismGraph(r);check(r.slotGrid[1].y!=r.slotGrid[4].y,"new overlapping branches get separate lanes");
        check(r.slotGrid[1].y==r.slotGrid[2].y&&r.slotGrid[2].y==r.slotGrid[3].y&&r.slotGrid[4].y==r.slotGrid[5].y&&r.slotGrid[5].y==r.slotGrid[6].y,"every downstream stage inherits its branch lane");
        auto old=r.slotGrid;arrangeMechanismGraph(r);for(size_t i=0;i<old.size();i++)check((old[i]-r.slotGrid[i]).length()<1e-9,"new branch lanes remain stable");
    }
    {
        auto d=parallelMechanismFixture(true);auto groups=d.groups;int outsider=d.addAtom({-400,-500});d.groups[0].members.push_back(outsider);auto before=d;
        int owner=d.reactions[0].id;Vec anchor=d.reactions[0].p;optimizeMechanism(d,{owner});auto& r=d.reactions[0];
        check(d.groups[0].members==before.groups[0].members,"alignment retains cross-stage visual groups");
        check((r.p-anchor).length()<1e-9&&(d.atom(outsider)->p-before.atom(outsider)->p).length()<1e-9,"frame alignment cannot translate the parent mechanism or unrelated grouped atoms");
        check(r.slotWidth<250&&r.slotHeight<250,"frame size is measured from its own structure instead of the whole visual group");
        Document exact=d;exact.groups.clear();exact.reactions.clear();
        for(size_t i=0;i<r.slots.size();i++){auto bounds=makeScene(exact.subset(r.slots[i].members)).bounds;check((reactionSlot(r,i)-Vec{bounds.x+bounds.w/2,bounds.y+bounds.h/2}).length()<1e-7,"every grouped molecule is centered on its own frame");}
        for(auto bond:before.bonds)check(((d.atom(bond.a)->p-d.atom(bond.b)->p)-(before.atom(bond.a)->p-before.atom(bond.b)->p)).length()<1e-7,"alignment keeps every molecular bond vector rigid");
    }
    {
        auto d=mechanismDemo();int owner=d.reactions[0].id;auto original=d;makeMechanismGraph(d.reactions[0]);
        insertMechanismStage(d.reactions[0],1);auto& r=d.reactions[0];check(r.slots.size()==4&&r.links.size()==3,"insert in the middle splits incoming connector");check(r.slots[2].members==original.reactions[0].slots[1].members,"insertion preserves original stage structures by identity");
        removeMechanismStage(d,owner,1);check(d.reactions[0].slots.size()==3&&d.atoms.size()==original.atoms.size(),"stage deletion preserves chemical atoms by default");
        auto previous=d.reactions[0].slots[1].members;removeMechanismStage(d,owner,1,0);check(d.reactions[0].slots.size()==2,"merge removes only duplicate frame");for(int id:previous)check(std::find(d.reactions[0].slots[0].members.begin(),d.reactions[0].slots[0].members.end(),id)!=d.reactions[0].slots[0].members.end(),"merge retains all structure members");
        for(auto l:d.reactions[0].links)check(l.from!=l.to&&l.to<int(d.reactions[0].slots.size()),"merge remaps valid non-self connections");
    }
    {
        auto d=mechanismDemo();auto& r=d.reactions[0];makeMechanismGraph(r);r.crosses={{1,true,4,14,"#ff0000"}};r.captionColors={{2,1,"#123456"}};auto name=r.above[1];removeMechanismConnection(r,0);
        check(r.above[0]==name&&r.crosses[0].index==0&&r.captionColors[0].index==0,"connection removal remaps captions, colors and marks");removeMechanismConnection(r,0);check(r.explicitConnections()&&r.links.empty(),"last connection can be removed without implicit arrows reappearing");
        for(auto restored:{fromNative(toNative(d)),fromCdxml(toCdxml(d))})for(auto item:makeScene(restored).items)check(item.reactionArrow<0,"no deleted connection reappears after native or CDXML save");
        connectMechanismStages(r,0,2);connectMechanismStages(r,0,1);check(r.links.size()==2,"manually create both branches");r.hiddenArrows={0};connectMechanismStages(r,0,2);check(r.links.size()==2&&r.hiddenArrows.empty(),"reconnecting a hidden existing branch restores it without duplicates");
        r.slotGrid={{0,0},{.01,.02},{.02,-.01}};optimizeMechanism(d,{r.id});
        check(r.slotGrid[1].x==r.slotGrid[2].x&&r.slotGrid[1].x>r.slotGrid[0].x&&r.slotGrid[1].y!=r.slotGrid[2].y,"auto layout separates overlapping branch targets into a shared next column");
        int heads=0;for(auto p:makeScene(d).items)if(p.owner==r.id&&p.type==Primitive::Polygon&&p.reactionArrow>=0)heads++;check(heads==2,"upper and lower branch each render an arrowhead");
        r.arrowLength=180;optimizeMechanism(d,{r.id});check(mechanismArrowLength(r,d.style)==180,"manual arrow length is respected during alignment");
        for(auto restored:{fromNative(toNative(d)),fromCdxml(toCdxml(d))})check(restored.reactions[0].arrowLength==180,"manual arrow length survives both file formats");
        std::ofstream("mechanism-manual-branches.svg")<<toSvg(makeScene(d));
    }
    {
        auto d=mechanismDemo();auto r=d.reactions[0];makeMechanismGraph(r);MechanismRows rows;
        rows.push_back({"구조","1","","","R","0"});rows.push_back({"구조","3","","","P","1"});rows.push_back({"구조","4","","","Extra","2"});
        rows.push_back({"화살표","","1","3","Step","13.5"});rows.push_back({"화살표","","1","4","Branch","9.5"});
        auto edited=mechanismGraphData(r,rows);check(edited.slots.size()==3&&edited.slots[1].members==r.slots[2].members&&edited.slots[2].members.empty(),"graph table accepts inserted and removed stage rows without losing surviving structures");
        check(edited.links.size()==2&&edited.links[1].to==2,"table remaps stable structure numbers into new connections");
        bool failed=false;try{connectMechanismStages(edited,1,1);}catch(...){failed=true;}check(failed,"self connections are rejected");
    }
    {
        Document d;int a=d.addAtom({0,0},27);d.atom(a)->label="CoIII";normalizeChemicalLabels(d);auto picture=makeScene(d);double normal=0,roman=0;for(auto p:picture.items)if(p.type==Primitive::Text){if(p.text=="Co")normal=p.fontSize;if(p.text=="III")roman=p.fontSize;}
        check(normal>0&&std::abs(roman/normal-.55)<1e-6,"recognized oxidation Roman numerals are 55 percent of ordinary label size");
        d.atom(a)->label="CoIII";d.atom(a)->labelRuns={{"Co","#000000","Arial",12,1},{"III","#000000","Arial",8.4,65}};normalizeChemicalLabels(d);check(std::abs(d.atom(a)->labelRuns.back().size-6.6)<1e-6,"already-superscripted imported Roman numerals are reduced consistently");
        auto before=toNative(d);normalizeChemicalLabels(d);check(toNative(d)==before,"oxidation normalization is idempotent");
    }
    std::cout<<"Stage insertion/deletion/merging, manual connections/layout, lengths and oxidation sizes passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

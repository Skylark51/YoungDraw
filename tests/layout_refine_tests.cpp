#include "layout_refine.h"
#include "scene.h"
#include <iostream>
#include <stdexcept>
using namespace yd;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static Document foldedChain(){
    Document d;std::vector<Vec> points={{0,0},{30,0},{45,25.9807621135},{30,51.961524227},{0,51.961524227},{-15,25.9807621135},{0,0},{30,0}};
    int previous=0;for(Vec p:points){int id=d.addAtom(p);if(previous)d.addBond(previous,id);previous=id;}
    d.atoms.back().element=7;d.atoms.back().label="NMe_{2}";d.atoms.back().color="#123456";
    return d;
}
static bool lengths(const Document& d){for(const auto& b:d.bonds)if(std::abs((d.atom(b.a)->p-d.atom(b.b)->p).length()-30)>.00001)return false;return true;}
static Document branchedImport(){
    Document d;const std::vector<Vec> points={{23,106},{17,132},{235,14},{37,8},{47,146},{201,132},{211,150},{237,160},{71,82},{177,108},{155,46},{5,176},{239,138},{25,20},{3,110},{237,120},{231,122},{65,76}};
    for(size_t i=0;i<points.size();++i){int id=d.addAtom(points[i],(i==9||i==14||i==17)?7:6);if(i)d.addBond(d.atoms[i<10?i-1:i-9].id,id);}
    d.atoms.back().label="NMe_{2}";return d;
}
static Vec centroid(const Document& d){Vec p;for(const auto& a:d.atoms)p=p+a.p;return p*(1./d.atoms.size());}
static bool samePositions(const Document& a,const Document& b){if(a.atoms.size()!=b.atoms.size())return false;for(size_t i=0;i<a.atoms.size();++i)if(a.atoms[i].p.x!=b.atoms[i].p.x||a.atoms[i].p.y!=b.atoms[i].p.y)return false;return true;}
int main(){try{
    auto d=foldedChain(),original=d;const auto collisions=detail::layoutCollisions(d);
    check(!collisions.empty(),"folded chain reproduces rendered collisions");
    detail::refineLayoutCollisions(d,d.component(d.atoms[0].id),collisions);
    check(detail::layoutCollisions(d).size()<collisions.size(),"bond-flip search reduces collisions");
    check(lengths(d),"reflection preserves every native bond length");
    check((centroid(d)-centroid(original)).length()<1e-8,"refinement preserves molecule position");
    check(d.atoms.back().label==original.atoms.back().label&&d.atoms.back().color==original.atoms.back().color,"rendered labels stay upright with unchanged text and color");
    for(auto kind:{BondKind::Wedge,BondKind::Hash,BondKind::Dative}){
        auto fixed=original;fixed.bonds[1].kind=kind;auto before=fixed;
        detail::refineLayoutCollisions(fixed,fixed.component(fixed.atoms[0].id),collisions);
        check(samePositions(fixed,before),"projection-bearing bond excludes reflection");
    }
    for(bool metal:{false,true}){
        auto fixed=original;if(metal)fixed.atoms[2].element=27;else fixed.atoms[2].protect=true;
        auto before=fixed;detail::refineLayoutCollisions(fixed,fixed.component(fixed.atoms[0].id),collisions);
        check(samePositions(fixed,before),"metal and explicitly protected components remain byte-identical");
    }
    Document ring;ring.ring({0,0},6);ring.atoms[0].label="LongLabel";auto ringBefore=ring;
    detail::refineLayoutCollisions(ring,ring.component(ring.atoms[0].id),{});
    check(samePositions(ring,ringBefore),"ring edges are never used as reflection axes");
    Document alkene;for(Vec p:std::vector<Vec>{{0,0},{30,0},{0,0},{30,0}})alkene.addAtom(p);
    for(size_t i=1;i<alkene.atoms.size();++i)alkene.addBond(alkene.atoms[i-1].id,alkene.atoms[i].id,i==2?BondKind::Double:BondKind::Single);
    const auto alkeneBefore=alkene;detail::refineLayoutCollisions(alkene,alkene.component(alkene.atoms[0].id),{});
    check(samePositions(alkene,alkeneBefore),"double bond and adjacent single bonds never reverse alkene geometry");
    auto settled=d;detail::refineLayoutCollisions(d,d.component(d.atoms[0].id),detail::layoutCollisions(d));
    check(detail::layoutCollisions(d).size()<=detail::layoutCollisions(settled).size(),"repeated refinement never increases collision count");
    auto imported=branchedImport(),beforeImport=imported;const auto beforeCollisions=detail::layoutCollisions(imported);
    auto report=cleanStructure(imported);
    check(report.protectedComponents==0,"public cleanup rescues branched import previously rejected for collisions");
    check(!samePositions(imported,beforeImport),"public cleanup applies the improved coordinates");
    for(const auto& b:imported.bonds){const double expected=30*bondLengthScale(imported.atom(b.a)->element,imported.atom(b.b)->element,b.kind);
        check(std::abs((imported.atom(b.a)->p-imported.atom(b.b)->p).length()-expected)<1e-6,"public cleanup uses YoungDraw bond-specific target lengths");}
    for(const auto& collision:detail::layoutCollisions(imported))check(beforeCollisions.count(collision)>0,"public cleanup introduces no new rendered collisions");
    const auto once=imported;cleanStructure(imported);for(size_t i=0;i<imported.atoms.size();++i)
        check((imported.atoms[i].p-once.atoms[i].p).length()<1e-5,"public cleanup is stable after the first refinement");
    auto unrelated=beforeImport;int freeA=unrelated.addAtom({900,900}),freeB=unrelated.addAtom({940,913});unrelated.addBond(freeA,freeB);
    const Vec freeAPosition=unrelated.atom(freeA)->p,freeBPosition=unrelated.atom(freeB)->p;
    cleanStructure(unrelated,{unrelated.bonds[0].id});
    check((unrelated.atom(freeA)->p-freeAPosition).length()==0&&(unrelated.atom(freeB)->p-freeBPosition).length()==0,"unselected molecules retain exact positions");
    std::cout<<"Layout refinement regression checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}

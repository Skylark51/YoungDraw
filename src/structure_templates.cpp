#include "structure_templates.h"
#include <limits>
#include <map>

namespace yd {
namespace {
struct GroupAtom {
    int element,parent;
    double angle;
    BondKind bond=BondKind::Single;
    int charge=0;
};
StructureTemplate group(const char* name,const char* abbreviation,std::initializer_list<GroupAtom> atoms){
    Document d;
    for(const auto& spec:atoms){
        Vec p;
        if(spec.parent>=0){
            const auto& parent=d.atoms.at(spec.parent);
            const double angle=spec.angle*pi/180;
            p=parent.p+Vec{std::cos(angle),std::sin(angle)}*defaultBondLength(d,parent.id,spec.bond,spec.element);
        }
        int id=d.addAtom(p,spec.element);
        d.atom(id)->charge=spec.charge;
        if(spec.parent>=0)d.addBond(d.atoms.at(spec.parent).id,id,spec.bond);
    }
    const int root=d.atoms.front().id;
    const int marker=d.addAtom({-d.style.bondLength,0});
    d.atom(marker)->label="R";
    d.addBond(root,marker);
    return {name,abbreviation,std::move(d)};
}

StructureTemplate ring(const char* name,int sides,bool alternate,std::initializer_list<std::pair<int,int>> elements={}){
    Document d;
    d.ring({},sides,alternate);
    for(const auto& [index,element]:elements)d.atoms.at(index).element=element;
    if(sides==5&&alternate){
        d.bonds[0].kind=d.bonds[2].kind=d.bonds[4].kind=BondKind::Single;
        d.bonds[1].kind=d.bonds[3].kind=BondKind::Double;
    }
    return {name,"",std::move(d)};
}
}

const std::vector<StructureTemplate>& functionalGroups(){
    using K=BondKind;
    static const std::vector<StructureTemplate> groups={
        group("tert-Butoxycarbonyl","Boc",{{6,-1,0},{8,0,-60,K::Double},{8,0,60},{6,2,0},{6,3,-60},{6,3,60},{6,3,0}}),
        group("Trimethylsilyl","TMS",{{14,-1,0},{6,0,-60},{6,0,0},{6,0,60}}),
        group("tert-Butyl","tBu",{{6,-1,0},{6,0,-60},{6,0,0},{6,0,60}}),
        group("Isopropyl","iPr",{{6,-1,0},{6,0,-60},{6,0,60}}),
        group("Ethyl","Et",{{6,-1,0},{6,0,30}}),
        group("Methyl","Me",{{6,-1,0}}),
        group("Acetoxy","OAc",{{8,-1,0},{6,0,30},{8,1,-60,K::Double},{6,1,50}}),
        group("Acetyl","Ac",{{6,-1,0},{8,0,-60,K::Double},{6,0,60}}),
        group("Methoxycarbonyl","CO2Me",{{6,-1,0},{8,0,-60,K::Double},{8,0,60},{6,2,0}}),
        group("Carboxyl","CO2H",{{6,-1,0},{8,0,-60,K::Double},{8,0,60}}),
        group("Methoxy","OMe",{{8,-1,0},{6,0,30}}),
        group("Cyano","CN",{{6,-1,0},{7,0,0,K::Triple}}),
        group("Nitro","NO2",{{7,-1,0,K::Single,1},{8,0,-60,K::Double},{8,0,60,K::Single,-1}}),
        group("Trifluoromethyl","CF3",{{6,-1,0},{9,0,-60},{9,0,0},{9,0,60}}),
        group("Hydroxy","OH",{{8,-1,0}}),
        group("Amino","NH2",{{7,-1,0}}),
    };
    return groups;
}

static StructureTemplate naphthalene(){
    auto result=ring("Naphthalene",6,true);
    result.drawing.attachRing(0,result.drawing.bonds.front().id,6,true);
    return result;
}

const std::vector<StructureTemplate>& layoutReferences(){
    static const std::vector<StructureTemplate> references={
        ring("Benzene",6,true),ring("Cyclopropane",3,false),ring("Cyclobutane",4,false),
        ring("Cyclopentane",5,false),ring("Cyclohexane",6,false),ring("Cycloheptane",7,false),
        ring("Cyclooctane",8,false),ring("Pyridine",6,true,{{0,7}}),
        ring("Pyrimidine",6,true,{{0,7},{2,7}}),ring("1,3,5-Triazine",6,true,{{0,7},{2,7},{4,7}}),
        ring("Furan",5,true,{{0,8}}),ring("Thiophene",5,true,{{0,16}}),ring("Pyrrole",5,true,{{0,7}}),
        ring("Tetrahydrofuran",5,false,{{0,8}}),ring("1,4-Dioxane",6,false,{{0,8},{3,8}}),
        ring("Piperidine",6,false,{{0,7}}),ring("Morpholine",6,false,{{0,8},{3,7}}),naphthalene()
    };
    return references;
}

void matchTemplate(const Document& d,const std::vector<int>& candidates,const Document& pattern,
                   bool substituent,const std::function<void(const std::vector<int>&)>& match){
    const size_t count=pattern.atoms.size()-size_t(substituent);
    if(candidates.size()<count||(!substituent&&candidates.size()!=count))return;
    using Neighbors=std::map<int,std::map<int,const Bond*>>;
    auto graph=[](const Document& document){
        Neighbors result;
        for(const auto& a:document.atoms)result[a.id];
        for(const auto& b:document.bonds){
            result[b.a][b.b]=&b;
            result[b.b][b.a]=&b;
        }
        return result;
    };
    const auto actual=graph(d), expected=graph(pattern);
    const std::set<int> scope(candidates.begin(),candidates.end());
    std::vector<int> mapped;
    std::function<void()> visit=[&]{
        if(mapped.size()==count){match(mapped);return;}
        const auto& reference=pattern.atoms.at(mapped.size());
        const auto& edges=expected.at(reference.id);
        auto attempt=[&](int id){
            const auto& atom=*d.atom(id);
            const auto& neighbors=actual.at(id);
            if(atom.element!=reference.element||atom.charge!=reference.charge||atom.radical||
               atom.isotope||!atom.label.empty()||!atom.labelRuns.empty()||atom.protect||
               (substituent&&atom.mullikenSpin)||neighbors.size()!=edges.size()||
               d.hydrogens(id)!=pattern.hydrogens(reference.id)||
               std::find(mapped.begin(),mapped.end(),id)!=mapped.end())return;
            for(size_t i=0;i<mapped.size();++i){
                const auto observed=neighbors.find(mapped[i]);
                const auto wanted=edges.find(pattern.atoms[i].id);
                if((observed==neighbors.end())!=(wanted==edges.end()))return;
                if(wanted!=edges.end()&&(observed->second->kind!=wanted->second->kind||
                    !observed->second->customOrder.empty()||!observed->second->customDisplay.empty()))return;
            }
            mapped.push_back(id);
            visit();
            mapped.pop_back();
        };
        if(mapped.empty()){
            for(int id:candidates)attempt(id);
            return;
        }
        for(size_t i=0;i<mapped.size();++i)if(edges.count(pattern.atoms[i].id)){
            for(const auto& [id,bond]:actual.at(mapped[i]))if(scope.count(id))attempt(id);
            return;
        }
    };
    visit();
}

bool applyLayoutReference(Document& d,const std::vector<int>& component){
    double bestError=std::numeric_limits<double>::infinity();
    std::vector<int> bestIds;
    std::vector<Vec> bestPoints;
    Vec center;
    for(int id:component)center=center+d.atom(id)->p;
    center=center*(1./component.size());
    for(const auto& reference:layoutReferences()){
        const auto& pattern=reference.drawing;
        matchTemplate(d,component,pattern,false,[&](const std::vector<int>& ids){
            Vec patternCenter;
            for(const auto& a:pattern.atoms)patternCenter=patternCenter+a.p;
            patternCenter=patternCenter*(1./ids.size());
            const double scale=d.style.bondLength/pattern.style.bondLength;
            double dot=0,cross=0;
            for(size_t i=0;i<ids.size();++i){
                const Vec p=pattern.atoms[i].p-patternCenter,q=d.atom(ids[i])->p-center;
                dot+=p.x*q.x+p.y*q.y;
                cross+=p.x*q.y-p.y*q.x;
            }
            const double angle=std::atan2(cross,dot),c=std::cos(angle),s=std::sin(angle);
            std::vector<Vec> points;
            double error=0;
            for(size_t i=0;i<ids.size();++i){
                const Vec p=(pattern.atoms[i].p-patternCenter)*scale;
                points.push_back(center+Vec{p.x*c-p.y*s,p.x*s+p.y*c});
                const double distance=(d.atom(ids[i])->p-points.back()).length();
                error+=distance*distance;
            }
            if(error<bestError){bestError=error;bestIds=ids;bestPoints=std::move(points);}
        });
    }
    for(size_t i=0;i<bestIds.size();++i)d.atom(bestIds[i])->p=bestPoints[i];
    return !bestIds.empty();
}
}

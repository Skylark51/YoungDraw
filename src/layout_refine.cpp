// Bond-flip collision search adapted from RDKit EmbeddedFrag.cpp.
// Copyright (C) 2004-2025 Greg Landrum and other RDKit contributors.
// All Rights Reserved. BSD-3-Clause; see resources/rdkit-license.txt.
// YoungDraw adaptation: rendered-label collisions, native bond lengths,
// deterministic bounded search, and conservative projection protection.
#include "layout_refine.h"
#include <map>

namespace yd::detail {
namespace {
bool metal(int e){return (e>=21&&e<=30)||(e>=39&&e<=48)||(e>=57&&e<=80)||e>=89;}
struct Flip {int a,b;std::vector<int> side;};
struct Score {size_t added=0,collisions=0;double density=0;};
bool better(const Score& a,const Score& b){
    if(a.added!=b.added)return a.added<b.added;
    if(a.collisions!=b.collisions)return a.collisions<b.collisions;
    return a.density<b.density-1e-7*std::max(1.,b.density);
}
}
void refineLayoutCollisions(Document& d,const std::vector<int>& comp,
                            const LayoutCollisions& originalCollisions){
    // Never reinterpret stereochemical, coordination, or explicitly fixed drawings.
    // The size/evaluation bounds keep cleanup interactive on large imports.
    if(comp.size()<4||comp.size()>100)return;
    std::set<int> ids(comp.begin(),comp.end()),owners=ids;
    std::map<int,std::vector<int>> neighbors;
    std::set<std::pair<int,int>> bonded;
    for(int id:comp)if(d.atom(id)->protect||d.isLocked(id)||metal(d.atom(id)->element))return;
    for(const auto& b:d.bonds)if(ids.count(b.a)){
        if(d.isLocked(b.id)||!b.customOrder.empty()||!b.customDisplay.empty()||
           (b.kind!=BondKind::Single&&b.kind!=BondKind::Double&&
            b.kind!=BondKind::Triple&&b.kind!=BondKind::Aromatic))return;
        neighbors[b.a].push_back(b.b);neighbors[b.b].push_back(b.a);
        owners.insert(b.id);bonded.insert(std::minmax(b.a,b.b));
    }
    auto score=[&](){
        Score result;
        for(const auto& pair:layoutCollisions(d))if(owners.count(pair.first)||owners.count(pair.second)){
            ++result.collisions;if(!originalCollisions.count(pair))++result.added;
        }
        // RDKit's inverse-square density, limited to nonbonded atoms in this molecule.
        const double length2=d.style.bondLength*d.style.bondLength;
        for(size_t i=0;i<comp.size();++i)for(size_t j=0;j<i;++j){
            if(bonded.count(std::minmax(comp[i],comp[j])))continue;
            Vec v=d.atom(comp[i])->p-d.atom(comp[j])->p;
            result.density+=length2/std::max(.001*length2,v.x*v.x+v.y*v.y);
        }
        return result;
    };
    Score current=score();if(!current.collisions)return;
    std::vector<Flip> flips;
    for(const auto& b:d.bonds){
        if(!ids.count(b.a)||b.kind!=BondKind::Single||neighbors[b.a].size()<2||neighbors[b.b].size()<2)continue;
        // A reflection adjacent to a double bond can reverse its drawn E/Z geometry.
        if(std::any_of(d.bonds.begin(),d.bonds.end(),[&](const Bond& other){
            return (other.a==b.a||other.b==b.a||other.a==b.b||other.b==b.b)&&
                   (other.kind==BondKind::Double||other.kind==BondKind::Triple);
        }))continue;
        std::set<int> seen{b.b};std::vector<int> side{b.b};
        for(size_t i=0;i<side.size();++i)for(int next:neighbors[side[i]]){
            if((side[i]==b.a&&next==b.b)||(side[i]==b.b&&next==b.a))continue;
            if(seen.insert(next).second)side.push_back(next);
        }
        if(seen.count(b.a))continue; // A bridge check excludes rings of every size.
        std::vector<int> other;for(int id:comp)if(!seen.count(id))other.push_back(id);
        if(other.size()<side.size())std::swap(side,other);
        flips.push_back({b.a,b.b,side});flips.push_back({b.a,b.b,other});
    }
    std::stable_sort(flips.begin(),flips.end(),[](const Flip& a,const Flip& b){return a.side.size()<b.side.size();});
    int evaluations=0;
    for(int pass=0;pass<8&&current.collisions&&evaluations<96;++pass){
        Score best=current;std::map<int,Vec> bestPoints;
        for(const auto& flip:flips){
            if(evaluations++>=96)break;
            Vec origin=d.atom(flip.a)->p,axis=d.atom(flip.b)->p-origin;
            double length=axis.length();if(length<.001)continue;axis=axis*(1./length);
            std::map<int,Vec> saved;Vec beforeCenter,afterCenter;
            for(int id:comp){saved[id]=d.atom(id)->p;beforeCenter=beforeCenter+saved[id];}
            for(int id:flip.side){
                // Leave both endpoints exactly fixed, avoiding floating point drift.
                if(id==flip.a||id==flip.b)continue;
                auto* atom=d.atom(id);Vec v=atom->p-origin;
                atom->p=origin+axis*(2*(v.x*axis.x+v.y*axis.y))-v;
            }
            // Preserve the molecule's document position on repeated cleanup.
            // Score this translation too, including collisions with nearby molecules.
            for(int id:comp)afterCenter=afterCenter+d.atom(id)->p;
            const Vec shift=(beforeCenter-afterCenter)*(1./comp.size());
            for(int id:comp)d.atom(id)->p=d.atom(id)->p+shift;
            const auto trial=score();
            if(better(trial,best)){best=trial;bestPoints.clear();for(const auto& p:saved)bestPoints[p.first]=d.atom(p.first)->p;}
            for(const auto& p:saved)d.atom(p.first)->p=p.second;
        }
        if(bestPoints.empty())break;
        for(const auto& p:bestPoints)d.atom(p.first)->p=p.second;
        current=best;
    }
}
}

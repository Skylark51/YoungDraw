#pragma once
#include "model.h"
#include "scene.h"
inline yd::Document parallelMechanismFixture(bool grouped=false){
    using namespace yd;
    Document d;Reaction r;r.id=d.nextId++;r.p={65,100};r.manualConnections=true;r.spacing=330;r.rowGap=110;
    r.slotGrid={{0,0},{1,0},{2,0},{3,0},{0,1},{1,1},{2,1},{3,1},{4,1},{5,1},{0,2},{2,.45}};
    r.slots.resize(r.slotGrid.size());r.links={{-1,0},{0,1},{1,2},{2,3},{1,11},{-1,4},{4,5},{5,6},{6,7},{7,8},{8,9},{-1,10}};
    r.above={"Protonation","Ring opening","C-O bond cleavage","Alcohol formation","Side pathway","Protonation","O-O bond cleavage","Rotation","CO bond formation","Alcohol formation","Back transfer","Proton transfer"};
    r.below.resize(r.links.size(),"13.5 / 9.5 / 9.5");
    Document molecule=demo();auto box=makeScene(molecule).bounds;
    for(size_t i=0;i<r.slots.size();i++){
        r.slots[i].label="I_{"+std::to_string(i+1)+"}";r.slots[i].energy="0.0 / 1.2 / 2.4";
        int first=d.nextId;d.append(molecule,reactionSlot(r,i)-Vec{box.x+box.w/2,box.y+box.h/2});
        for(int id:d.allIds())if(id>=first)r.slots[i].members.push_back(id);
    }
    d.reactions.push_back(r);
    if(grouped){Group g;g.id=d.nextId++;g.members=d.allIds();d.groups.push_back(g);}
    return d;
}

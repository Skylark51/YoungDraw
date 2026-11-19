#include "win_editor_canvas.h"
#include "win_editor_chrome.h"
#include "win_editor_clipboard.h"
#include "win_editor_commands.h"
#include "win_editor_plot.h"
#include "win_editor_pointer.h"
#include "win_editor_reaction.h"
#include "win_editor_state.h"
#include "win_editor_structure.h"
#include "win_editor_text.h"
#include "cdxml.h"
#include "mechanism_data.h"
#include "plot_data.h"
#include "win_ui.h"
#include "win_dialogs.h"

namespace yd::editor {

static std::vector<int> dragSelection;
struct RotationDrag {
    Vec pivot;
    std::vector<int> ids;
};
static std::optional<RotationDrag> rotationDrag;
static std::optional<InlineTarget> energyDrag;
static std::optional<CrossingCaption> crossingDrag,gapDrag;
struct PlotLevelDrag { int owner,state,path; PlotDragScale scale; bool horizontal; };
static std::optional<PlotLevelDrag> plotLevelDrag;
struct PlotAxisDrag {int owner;bool horizontal;double xLength,yLength,width,height;};
static std::optional<PlotAxisDrag> plotAxisDrag;
static bool beginPlotAxisDrag(const Scene& sc,Vec at){
    if(selection.size()!=1||doc.isLocked(selection[0]))return false;
    const auto found=sc.plotAxes.find(selection[0]);if(found==sc.plotAxes.end())return false;const auto& a=found->second;
    const double x=(screen(a.xEnd)-at).length(),y=(screen(a.yEnd)-at).length();if(std::min(x,y)>10)return false;
    const auto* p=plot(selection[0]);plotAxisDrag=PlotAxisDrag{p->id,x<=y,(a.xEnd-a.origin).length(),(a.yEnd-a.origin).length(),p->width,p->height};return true;
}
struct StagePositionDrag { int owner; size_t slot; };
static std::optional<StagePositionDrag> stagePositionDrag;

static bool beginPlotLevelDrag(const Scene& sc,Vec cursor,bool horizontal){
    double closest=8;
    for(auto owner=sc.plotSnapPoints.rbegin();owner!=sc.plotSnapPoints.rend();++owner){
        if(doc.isLocked(owner->first))continue;
        for(const auto& point:owner->second){
            const double distance=(screen(point.p)-cursor).length();
            if(distance>=closest)continue;
            closest=distance;
            plotLevelDrag=PlotLevelDrag{owner->first,point.state,point.path,sc.plotDragScales.at(owner->first),horizontal};
        }
    }
    if(!plotLevelDrag)return false;
    selection={plotLevelDrag->owner};
    targetContainer=plotLevelDrag->owner;targetSlot=size_t(plotLevelDrag->state);
    const auto& state=plot(targetContainer)->states[targetSlot];targetAbove=!state.above.empty()||state.below.empty();
    sidebarPage=1;
    status=horizontal?L"준위 가로 간격 조정 · Esc: 취소":
        L"에너지 높이 조정 · Shift: 축 눈금에 맞춤 · Alt+드래그: 가로 이동";
    return true;
}

static bool beginStagePositionDrag(Vec cursor){
    if(!guides)return false;
    double nearest=1e30;
    for(const auto& r:doc.reactions){
        if(r.cyclic||r.circularMerge||doc.isLocked(r.id))continue;
        for(size_t i=0;i<r.slots.size();++i){
            Vec offset=cursor-reactionSlot(r,i);
            if(std::abs(offset.x)>r.slotWidth/2||std::abs(offset.y)>r.slotHeight/2||offset.length()>=nearest)continue;
            nearest=offset.length();
            stagePositionDrag=StagePositionDrag{r.id,i};
        }
    }
    if(!stagePositionDrag)return false;
    targetContainer=stagePositionDrag->owner;
    targetSlot=stagePositionDrag->slot;
    selection={targetContainer};
    sidebarPage=0;
    status=L"단계 자유 배치 · Shift: 수평 또는 수직 · Esc: 취소";
    return true;
}

static void updatePlotLevel(Vec delta){
    const auto& edit=*plotLevelDrag;
    auto& graph=*plot(edit.owner);
    const auto i=size_t(edit.state);
    if(edit.horizontal){
        const double span=graph.width-154;
        if(span<=0)return;
        const double at=plotStateFraction(graph,i);
        const double previous=i?plotStateFraction(graph,i-1):0.;
        const double next=i+1<graph.states.size()?plotStateFraction(graph,i+1):1.;
        const double low=i?previous+std::min(.01,(at-previous)*.5):0.;
        const double high=i+1<graph.states.size()?next-std::min(.01,(next-at)*.5):1.;
        const double position=std::clamp(at+delta.x/span,low,high);
        if(std::abs(position-at)<1e-9)return;
        graph.states[i].position=position;
        auto movement=Vec{(position-at)*span,0};
        doc.translate(graph.states[i].above,movement,false);
        doc.translate(graph.states[i].below,movement,false);
        return;
    }
    auto& energy=graph.series.empty()?graph.states[i].energy:graph.series.at(edit.path).values[i];
    const double axisStep=energyAxis(graph).step;
    const double step=GetKeyState(VK_SHIFT)&0x8000?axisStep:std::min(.1,axisStep/10);
    const double low=std::clamp(edit.scale.low,-1e6,1e6), high=std::clamp(edit.scale.high,-1e6,1e6);
    const auto bands=energyDisplayBands(graph);double proposed=energy-delta.y*edit.scale.unitsPerPoint;
    if(!bands.empty()){
        const double base=displayEnergy(bands,low),span=displayEnergy(bands,high)-base;
        const double target=std::clamp((displayEnergy(bands,energy)-base-delta.y*edit.scale.unitsPerPoint)/span,0.,1.);
        double a=low,b=high;
        for(int pass=0;pass<44;++pass){const double value=(a+b)/2;energy=value;
            const auto candidate=energyDisplayBands(graph);const double start=displayEnergy(candidate,low),end=displayEnergy(candidate,high);
            if((displayEnergy(candidate,value)-start)/(end-start)<target)a=value;else b=value;
        }
        proposed=(a+b)/2;
    }
    const double next=std::round(proposed/step)*step;
    energy=std::clamp(next,low,high);
    if(edit.path==0)graph.states[i].energy=energy;
    status=L"에너지 "+ui::wide(number(energy)+" "+graph.unit)+L" · Esc: 취소";
    graph.yMin=edit.scale.low;
    graph.yMax=edit.scale.high;
}

static void updateStagePosition(Vec delta){
    auto& r=*reaction(stagePositionDrag->owner);
    std::vector<Vec> centers;
    for(size_t i=0;i<r.slots.size();++i)centers.push_back(reactionSlot(r,i));
    makeMechanismGraph(r);
    r.fixedFrames=true;
    const double pitch=r.slotHeight+std::max(r.rowGap,mechanismCaptionHeight(r,Style{})+80);
    const auto index=stagePositionDrag->slot;
    auto offset=centers[index]-r.p;
    delta.x=std::clamp(delta.x,-r.spacing*100-offset.x,r.spacing*100-offset.x);
    delta.y=std::clamp(delta.y,-pitch*100-offset.y,pitch*100-offset.y);
    centers[index]=centers[index]+delta;
    r.slotGrid.clear();
    for(auto center:centers)r.slotGrid.push_back({(center.x-r.p.x)/r.spacing,(center.y-r.p.y)/pitch});
    doc.translate(r.slots[index].members,delta,false);
}

static bool energyDestination(const Reaction& r,const Slot& slot){
    return !slot.members.empty()&&slot.energy.empty()&&!doc.isLocked(r.id)&&
        (!energyDrag->role||r.id==energyDrag->owner);
}
static Rect energyArea(const Reaction& r,size_t i){
    const Vec c=reactionSlot(r,i);
    return {c.x-r.slotWidth/2,c.y-r.slotHeight/2,r.slotWidth,r.slotHeight+4*mechanismFontSize(r,doc.style)};
}
static bool inside(Rect b,Vec p){return p.x>=b.x&&p.x<=b.x+b.w&&p.y>=b.y&&p.y<=b.y+b.h;}

Scene energyDragPreview(const Scene& scene){
    Scene preview;
    if(!dragging||!moved||!energyDrag)return preview;
    Vec offset=(mouse-down)*(1/zoom);
    const Vec cursor=world(mouse);
    bool valid=false;
    for(const auto& r:doc.reactions)for(size_t i=0;i<r.slots.size();++i){
        const auto b=energyArea(r,i);
        const bool hovered=inside(b,cursor),allowed=energyDestination(r,r.slots[i]);
        valid|=hovered&&allowed;
        if(!allowed&&!hovered)continue;
        Primitive outline;
        outline.type=Primitive::Line;
        outline.color=allowed?"#008778":"#d6203b";
        outline.width=(hovered?2.5:1.2)/zoom;
        outline.points={{b.x,b.y},{b.x+b.w,b.y},{b.x+b.w,b.y+b.h},{b.x,b.y+b.h},{b.x,b.y}};
        preview.items.push_back(std::move(outline));
    }
    for(auto item:scene.items)if(item.type==Primitive::Text&&item.owner==energyDrag->owner&&
        item.textRole==energyDrag->role&&item.textIndex==energyDrag->index){
        item.color=valid?"#008778":"#d6203b";
        for(auto& point:item.points)point=point+offset;
        preview.items.push_back(std::move(item));
    }
    return preview;
}

static void dropEnergy(Vec p){
    int owner=0;size_t slot=0;double best=1e30;
    for(auto& r:doc.reactions){
        if(doc.isLocked(r.id)||(energyDrag->role&&r.id!=energyDrag->owner))continue;
        for(size_t i=0;i<r.slots.size();i++){
            if(!energyDestination(r,r.slots[i])||!inside(energyArea(r,i),p))continue;
            Vec c=reactionSlot(r,i);
            double distance=(p-c).length();
            if(distance<best){best=distance;owner=r.id;slot=i;}
        }
    }
    if(owner&&attachMechanismEnergy(doc,energyDrag->owner,energyDrag->role,energyDrag->index,owner,slot)){
        history.checkpoint(dragBefore);
        selection={owner};
        status=L"구조 아래로 에너지를 옮겼습니다. Ctrl+Z: 되돌리기";
        changed();
    }else status=L"에너지 칸이 비어 있는 구조나 그 아래에 놓으세요.";
}

BondKind kind(){return tool==Double?BondKind::Double:tool==Triple?BondKind::Triple:tool==Wedge?BondKind::Wedge:tool==Hash?BondKind::Hash:tool==Delocalized?BondKind::Aromatic:tool==DottedBond?BondKind::Dot:BondKind::Single;}
Vec sproutPoint(int id){return bondEndpoint(doc,id,doc.atom(id)->p,kind());}
Vec endpoint(Vec p){Vec a=startAtom?doc.atom(startAtom)->p:world(down);int target=atomHit(world(p),8);if(target&&target!=startAtom)return doc.atom(target)->p;if((p-down).length()<5)return startAtom?bondEndpoint(doc,startAtom,doc.atom(startAtom)->p,kind(),element):a+Vec{std::cos(-pi/6),std::sin(-pi/6)}*defaultBondLength(doc,0,kind(),element);if(startAtom&&!(GetKeyState(VK_MENU)&0x8000))return bondEndpoint(doc,startAtom,world(p),kind(),element);return snapped(a,world(p),defaultBondLength(doc,startAtom,kind(),element),(GetKeyState(VK_MENU)&0x8000)!=0);}
int objectAt(Vec p){auto sc=scene();for(auto it=doc.objects.rbegin();it!=doc.objects.rend();++it){if(doc.isLocked(it->id))continue;auto box=sc.objectBounds.find(it->id);if(box==sc.objectBounds.end())continue;auto b=box->second;double t=5/zoom;if(p.x<b.x-t||p.x>b.x+b.w+t||p.y<b.y-t||p.y>b.y+b.h+t)continue;if(hitObject(sc,it->id,p,t))return it->id;}
 for(auto& r:doc.reactions){auto b=sc.objectBounds[r.id];if(p.x>=b.x-5&&p.x<=b.x+b.w+5&&p.y>=b.y-5&&p.y<=b.y+b.h+5)return r.id;}for(auto& plot:doc.plots){if(doc.isLocked(plot.id))continue;auto b=sc.objectBounds[plot.id];if(p.x>=b.x-5&&p.x<=b.x+b.w+5&&p.y>=b.y-5&&p.y<=b.y+b.h+5)return plot.id;}return 0;}
int drawingItemAt(Vec p){
    const Vec at=world(p);
    int id=atomHit(at,tool==Benzene?benzenePickRadius:atomPickRadius);
    if(!id)id=doc.bondAt(at,5/zoom);
    return id?id:objectAt(at);
}

static void beginRotation(Vec p){
    auto ids=selection;
    const int hit=drawingItemAt(p);
    if(hit&&!contains(ids,hit))ids=groupedSelection({hit});
    if(ids.empty())return;
    try{
        dragBefore=dragBase=doc;
        Vec pivot;
        selection=transformStructures(dragBase,ids,0,false,false,&pivot);
        rotationDrag=RotationDrag{pivot,std::move(ids)};
        dragging=true;
        moved=copyDrag=false;
        SetCapture(win);
    }catch(const std::exception& e){
        status=ui::wide(e.what());
    }
    redraw();
}

static void updateRotation(){
    const Vec pivot=screen(rotationDrag->pivot);
    const Vec start=down-pivot,current=mouse-pivot;
    double degrees;
    if(start.length()<6){
        // At the pivot there is no starting angle. Use horizontal movement.
        degrees=mouse.x-down.x;
    }else{
        if(current.length()<2)return;
        degrees=std::atan2(start.x*current.y-start.y*current.x,start.x*current.x+start.y*current.y)*180/pi;
    }
    if(GetKeyState(VK_SHIFT)&0x8000)degrees=std::round(degrees/15)*15;
    doc=dragBase;
    transformStructures(doc,rotationDrag->ids,degrees);
    status=L"회전 "+ui::wide(number(degrees))+L"° · Shift: 15° 단위 · Esc: 취소";
}

bool pickSlot(Vec p){if(!guides)return false;for(auto& r:doc.reactions)for(size_t i=0;i<r.slots.size();i++){Vec c=reactionSlot(r,i);if(std::abs(p.x-c.x)<r.slotWidth/2&&std::abs(p.y-c.y)<r.slotHeight/2){targetContainer=r.id;targetSlot=i;targetAbove=false;sidebarPage=0;selection={r.id};status=L"구조 삽입 영역 선택: Ctrl+V로 복사한 구조를 삽입합니다.";redraw();return true;}}
 for(auto& plot:doc.plots)if(!doc.isLocked(plot.id))for(size_t i=0;i<plot.states.size();i++)for(bool above:{false,true}){Vec c=plotAnchor(plot,i,above);if(std::abs(p.x-c.x)<24&&std::abs(p.y-c.y)<27.5){targetContainer=plot.id;targetSlot=i;targetAbove=above;sidebarPage=1;selection={plot.id};status=L"그래프 삽입 영역 선택: Ctrl+V로 복사한 구조를 삽입합니다.";redraw();return true;}}return false;}
bool beginCurveHandle(Vec p){
    if(selection.size()!=1||(GetKeyState(VK_CONTROL)&0x8000))return false;
    auto sc=scene();auto found=sc.arrowPaths.find(selection.front());if(found==sc.arrowPaths.end()||found->second.size()!=4)return false;
    for(int i=0;i<4;i++)if((screen(found->second[i])-p).length()<=8){
        dragBefore=doc;for(auto& o:doc.objects)if(o.id==selection.front()){o.points=found->second;o.autoRoute=false;o.sourceAtom=o.targetAtom=o.sourceBond=o.targetBond=0;}
        dragBase=doc;curveHandle=i;curveOwner=selection.front();dragging=true;moved=false;copyDrag=false;SetCapture(win);status=L"조절점을 드래그하여 곡선과 양끝 위치를 수정합니다. Esc: 취소 / Ctrl+Z: 되돌리기";redraw();return true;
    }return false;
}
void updateDrag(){
    if(!dragging)return;Vec delta=(mouse-down)*(1/zoom);if(GetKeyState(VK_SHIFT)&0x8000)delta=std::abs(delta.x)>std::abs(delta.y)?Vec{delta.x,0}:Vec{0,delta.y};
    if((mouse-down).length()<=2&&!moved)return;moved=true;
    if(rotationDrag){updateRotation();return;}
    if(energyDrag)return;doc=dragBase;
    if(plotAxisDrag){
        const auto& a=*plotAxisDrag;
        const double x=a.horizontal?std::clamp(a.xLength+delta.x,std::max(100.,a.xLength+180-a.width),std::min(4000.,a.xLength+4000-a.width)):a.xLength;
        const double y=a.horizontal?a.yLength:std::clamp(a.yLength-delta.y,std::max(70.,a.yLength+140-a.height),std::min(4000.,a.yLength+4000-a.height));
        try{doc=resizedPlotAxes(dragBase,a.owner,x,y,measure);}catch(const std::exception&){doc=dragBase;}
        return;
    }
    if(plotLevelDrag){updatePlotLevel((mouse-down)*(1/zoom));return;}
    if(stagePositionDrag){updateStagePosition(delta);return;}
    if(gapDrag){
        auto* graph=plot(gapDrag->owner);auto offset=gapDrag->offset+delta;
        graph->states.at(gapDrag->index).gapOffset={std::clamp(offset.x,-1e4,1e4),std::abs(offset.y)<1e-6?1e-6:std::clamp(offset.y,-1e4,1e4)};return;
    }
    if(crossingDrag){
        auto* graph=plot(crossingDrag->owner);
        auto offset=crossingDrag->offset+delta;
        graph->crossings.at(crossingDrag->index).labelOffset=Vec{
            std::clamp(offset.x,-1e4,1e4),std::clamp(offset.y,-1e4,1e4)};
        return;
    }
    if(annotationDrag){auto* a=doc.atom(annotationDrag);a->annotationManual=true;a->annotationOffset=annotationStart+delta-a->p;return;}
    if(curveHandle>=0){for(auto& o:doc.objects)if(o.id==curveOwner){o.points[size_t(curveHandle)]=o.points[size_t(curveHandle)]+delta;}return;}
    if(copyDrag){auto fragment=selectedDocument();int first=doc.nextId;doc.append(fragment);selection.clear();for(int id:doc.allIds())if(id>=first)selection.push_back(id);copyDrag=false;dragBase=doc;}doc.translate(selection,delta);
}
void cancel(){cancelStageCopy();mechanismConnecting=mechanismConnectionDrag=false;connectionEdge=-1;selectedCycleOwner=0;selectedCycleArrow=-1;snapPlot=0;snapStart.reset();pendingEnergyGap.reset();if(dragging){doc=dragBefore;if(rotationDrag||!dragSelection.empty())selection=dragSelection;}rotationDrag.reset();dragSelection.clear();energyDrag.reset();drawing=dragging=boxing=panning=rightPanning=false;curveHandle=-1;annotationDrag=0;crossingDrag.reset();gapDrag.reset();plotLevelDrag.reset();plotAxisDrag.reset();stagePositionDrag.reset();ReleaseCapture();redraw();}
void mouseDown(Vec p){hideTooltip();for(auto& h:hits)if((h.id==150||h.id==156)&&h.box.Contains(float(p.x),float(p.y))){command(h.id);return;}if(!finishInline(true))return;for(auto& h:hits)if(h.box.Contains(float(p.x),float(p.y))){command(h.id);return;}if(!canvas(p))return;SetFocus(win);if(canvas(p)){
    const Vec at=world(p);int hit=atomHit(at);if(!hit)hit=doc.bondAt(at,5/zoom);if(!hit)hit=objectAt(at);
    if(plot(hit)){sidebarPage=1;showLayers=false;}
    else if(reaction(hit)){sidebarPage=0;showLayers=false;}
    else if(hit){
        for(const auto& r:doc.reactions)for(const auto& slot:r.slots)if(contains(slot.members,hit)){sidebarPage=0;showLayers=false;}
        for(const auto& graph:doc.plots)for(const auto& state:graph.states)if(contains(state.above,hit)||contains(state.below,hit)){sidebarPage=1;showLayers=false;}
    }
}if(stageCopyOwner){toggleStageCopy(p);return;}down=mouse=p;dragSelection=selection;annotationDrag=0;energyDrag.reset();crossingDrag.reset();gapDrag.reset();plotLevelDrag.reset();plotAxisDrag.reset();stagePositionDrag.reset();
 if(tool==Rotate){beginRotation(p);return;}
 if(tool==Erase){boxing=true;SetCapture(win);status=L"지우개 · 드래그: 사각형 범위 일괄 삭제 · 클릭: 한 항목 삭제 · Esc: 취소";redraw();return;}
 if(placeEnergyGap(p))return;
 if(tool==Select&&!(GetKeyState(VK_MENU)&0x8000)&&!mechanismConnecting&&!mechanismConnectionDrag&&!isolatedStructureEditing){
    int owner=0;size_t edge=0;double nearest=10;
    for(const auto& r:doc.reactions)for(const auto& [index,at]:mechanismSplitButtons(r)){
        double distance=(at-p).length();
        if(distance<nearest){nearest=distance;owner=r.id;edge=index;}
    }
    if(owner){insertMechanismLinkStage(owner,edge);return;}
 }
 if(beginMechanismConnection(p))return;selectedCycleOwner=0;selectedCycleArrow=-1;
 if(tool==Select&&!(GetKeyState(VK_MENU)&0x8000))if(auto hit=cycleArrowAt(world(p))){selectedCycleOwner=hit->first;selectedCycleArrow=hit->second;selection={hit->first};targetContainer=0;status=L"화살표 선택 · Del: 연결 삭제 · 양끝 원 드래그: 연결 변경·길이 조절 · 우클릭: 추가 편집";redraw();return;}
 if(snapPlot){
    auto* graph=plot(snapPlot);if(!graph||doc.isLocked(snapPlot)){snapPlot=0;snapStart.reset();return;}
    auto hit=nearestPlotSnap(scene(),p);if(!hit){status=L"표시된 준위의 점 가까이를 클릭하세요. Esc: 취소";redraw();return;}
    if(!snapStart){snapStart=hit;status=L"첫 점 고정. 두 번째 끝점·중앙점을 클릭하세요 (좌우 순서 무관). Esc: 취소";redraw();return;}
    auto label=snapLabel;label.start=std::min(snapStart->percent,hit->percent);label.end=std::max(snapStart->percent,hit->percent);
    if(label.end-label.start<1){status=L"두 점을 더 떨어뜨려 선택하세요 (최소 범위 1%). Esc: 취소";redraw();return;}
    history.checkpoint(doc);if(snapStage<0)graph->stages.push_back(label);else graph->stages.at(size_t(snapStage))=label;
    snapPlot=0;snapStart.reset();status=L"준위에 맞춰 반응 이름·양쪽 화살표를 배치했습니다. Ctrl+Z: 되돌리기";changed();return;
 }
 if(tool==Select&&!(GetKeyState(VK_MENU)&0x8000))for(auto& r:doc.reactions)if(!r.cyclic&&!r.circularMerge&&r.slots.size()<40&&r.links.size()<100&&!r.slots.empty()&&!doc.isLocked(r.id) ){for(size_t slot=0;slot<r.slots.size();slot++){bool terminal=!r.explicitConnections()?slot+1==r.slots.size():std::none_of(r.links.begin(),r.links.end(),[&](const MechanismLink& l){return l.from==int(slot);});for(bool below:{false,true}){if((!terminal&&!below)||(below&&r.vertical))continue;if((p-mechanismAddPosition(r,below,slot)).length()<=13){selection={r.id};targetContainer=r.id;targetSlot=slot;command(below?128:122);return;}}}}
 if(tool==Select){
    auto sc=scene();
    const auto w=world(p);
    const bool alt=(GetKeyState(VK_MENU)&0x8000)!=0;
    if(beginPlotAxisDrag(sc,p)){
        dragBefore=dragBase=doc;curveHandle=-1;dragging=true;moved=copyDrag=false;SetCapture(win);redraw();return;
    }
    for(auto i=sc.gapCaptions.rbegin();i!=sc.gapCaptions.rend();++i){
        if(doc.isLocked(i->owner))continue;const auto& b=i->bounds;double pad=4/zoom;
        if(w.x<b.x-pad||w.x>b.x+b.w+pad||w.y<b.y-pad||w.y>b.y+b.h+pad)continue;
        gapDrag=*i;selection={i->owner};dragBefore=dragBase=doc;dragging=true;moved=copyDrag=false;curveHandle=-1;SetCapture(win);redraw();return;
    }
    if((alt&&beginStagePositionDrag(w))||beginPlotLevelDrag(sc,p,alt)){
        dragBefore=dragBase=doc;
        curveHandle=-1;
        dragging=true;moved=copyDrag=false;
        SetCapture(win);redraw();return;
    }
    for(auto i=sc.crossingCaptions.rbegin();i!=sc.crossingCaptions.rend();++i){
        if(doc.isLocked(i->owner))continue;
        const auto& box=i->bounds;
        const double padding=4/zoom;
        if(w.x<box.x-padding||w.x>box.x+box.w+padding||w.y<box.y-padding||w.y>box.y+box.h+padding)continue;
        crossingDrag=*i;
        dragBefore=dragBase=doc;
        selection={i->owner};
        targetContainer=0;curveHandle=-1;
        dragging=true;moved=copyDrag=false;
        SetCapture(win);
        status=L"교차점 라벨 이동 · Esc: 취소 · Ctrl+Z: 되돌리기";
        redraw();
        return;
    }
    for(auto i=sc.items.rbegin();i!=sc.items.rend();++i)if(i->textRole==13&&!groupedObject(i->owner)){Vec at=i->points[0];double width=measure(i->text,i->fontSize,i->font,i->face);if(w.x>=at.x-2&&w.x<=at.x+width+2&&w.y>=at.y-2&&w.y<=at.y+i->fontSize+2){annotationDrag=i->owner;annotationStart=at;dragBefore=dragBase=doc;selection={i->owner};curveHandle=-1;dragging=true;moved=copyDrag=false;SetCapture(win);status=L"전하·라디칼 표시 위치 이동 · Esc: 취소 · Ctrl+Z: 되돌리기";redraw();return;}}}
 if(beginCurveHandle(p))return;curveHandle=-1;Vec w=world(p);int aid=atomHit(w,tool==Benzene?benzenePickRadius:atomPickRadius),bid=doc.bondAt(w,5/zoom),oid=aid||bid?0:objectAt(w);hoverAtom=aid;hoverBond=aid?0:bid;
 if((GetKeyState(VK_CONTROL)&0x8000)&&!aid&&!bid&&!oid){panning=true;panOrigin=origin;SetCapture(win);return;}
 if(tool==Select){
    if(auto target=textAt(w);target&&(target->role==3||target->role==9||
        (target->role==0&&textObject(target->owner)&&!importedEnergy(textObject(target->owner)->text).empty()))){
        energyDrag=target;dragBefore=doc;dragging=true;moved=copyDrag=false;targetContainer=0;
        SetCapture(win);status=L"에너지 값을 구조나 구조 아래로 드래그하세요. Esc: 취소";redraw();return;
    }
    bool imageHit=false;for(auto& o:doc.objects)if(o.id==oid&&o.kind==ObjectKind::Image)imageHit=true;if(!aid&&!bid&&!imageHit&&!groupedObject(oid)&&pickSlot(w))return;targetContainer=0;int id=aid?aid:bid?bid:oid;bool shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;if(id){if(shift){auto members=groupedSelection({id});if(contains(selection,id))selection.erase(std::remove_if(selection.begin(),selection.end(),[&](int member){return contains(members,member);}),selection.end());else{selection.insert(selection.end(),members.begin(),members.end());selection=groupedSelection(selection);}}else if(!contains(selection,id)){selection=groupedSelection({id});}if(!selection.empty()){dragSelection=selection;dragBefore=doc;dragBase=doc;dragging=true;moved=false;copyDrag=(GetKeyState(VK_CONTROL)&0x8000)!=0;SetCapture(win);}}else{if(!shift)selection.clear();boxing=true;SetCapture(win);}redraw();return;}
 if(tool==Text){beginInline(textAt(w),w);return;}
 if(tool==ImageTool){addImage(w);return;}if(tool==Ring5||tool==Ring6||tool==Benzene){history.checkpoint(doc);if(tool==Benzene&&aid)attachPhenyl(aid);else if(aid||bid)doc.attachRing(aid,aid?0:bid,tool==Ring5?5:6,tool==Benzene);else doc.ring(w,tool==Ring5?5:6,tool==Benzene);changed();return;}
 if(tool==Arrow||tool==Curve||tool==Fishhook||tool==Straight||tool==Resonance||tool==Interaction||tool==Orbital){drawing=true;startAtom=aid;startBond=aid?0:bid;SetCapture(win);return;}
 if((GetKeyState(VK_MENU)&0x8000)&&aid){selection={aid};editObject(aid);return;}
 if(!aid&&bid){history.checkpoint(doc);auto b=doc.bond(bid);if(b->kind==BondKind::Aromatic&&kind()==BondKind::Aromatic){Vec a=doc.atom(b->a)->p,v=doc.atom(b->b)->p-a;for(auto& stroke:scene().items)if(stroke.owner==bid&&stroke.dash&&stroke.points.size()==2){Vec offset=(stroke.points[0]+stroke.points[1])*.5-a;b->doubleSide=(v.x*offset.y-v.y*offset.x)>0?-1:1;break;}}else if(b->kind==kind()&&(kind()==BondKind::Wedge||kind()==BondKind::Hash))std::swap(b->a,b->b);else setBondKind(doc,bid,kind());b->customOrder.clear();b->customDisplay.clear();selection={bid};changed();return;}
 startAtom=aid;drawing=true;SetCapture(win);redraw();}
static void finishErase(Vec p){
    mouse=p;boxing=false;ReleaseCapture();
    std::vector<int> ids;
    const bool region=(p-down).length()>4;
    if(region){
        const Vec a=world(down),b=world(p);
        const Rect area{std::min(a.x,b.x),std::min(a.y,b.y),std::abs(a.x-b.x),std::abs(a.y-b.y)};
        for(const auto& atom:doc.atoms)if(!doc.isLocked(atom.id)&&inside(area,atom.p))ids.push_back(atom.id);
        for(const auto& [id,box]:scene().objectBounds)
            if(!doc.atom(id)&&!doc.isLocked(id)&&inside(area,{box.x,box.y})&&inside(area,{box.x+box.w,box.y+box.h}))ids.push_back(id);
    }else{
        if(auto hit=cycleArrowAt(world(down))){
            selectedCycleOwner=hit->first;selectedCycleArrow=hit->second;selection={hit->first};targetContainer=0;
            deleteSelectedCycleArrow();redraw();return;
        }
        const int id=drawingItemAt(down);
        if(id&&!doc.isLocked(id))ids.push_back(id);
    }
    if(!ids.empty()){
        history.checkpoint(doc);
        // A rectangle deletes only its contents, even if a user group spans its edge.
        if(region){
            for(auto& group:doc.groups)group.members.erase(std::remove_if(group.members.begin(),group.members.end(),[&](int id){return contains(ids,id);}),group.members.end());
            doc.groups.erase(std::remove_if(doc.groups.begin(),doc.groups.end(),[](const Group& g){return g.members.empty();}),doc.groups.end());
        }
        doc.erase(ids);selection.clear();targetContainer=0;selectedCycleOwner=0;selectedCycleArrow=-1;
        status=region?L"범위 안의 항목을 일괄 삭제했습니다. Ctrl+Z: 되돌리기":L"항목을 삭제했습니다. Ctrl+Z: 되돌리기";changed();
    }
    redraw();
}
void mouseUp(Vec p){if(tool==Erase&&boxing){finishErase(p);return;}if(mechanismConnectionDrag){finishMechanismConnection(p);return;}if(rightPanning)return;if(panning){panning=false;ReleaseCapture();redraw();return;}if(dragging&&energyDrag){dragging=false;if((p-down).length()>2)dropEnergy(world(p));energyDrag.reset();ReleaseCapture();redraw();return;}if(dragging){mouse=p;updateDrag();crossingDrag.reset();gapDrag.reset();
 if(plotLevelDrag&&!plotLevelDrag->horizontal&&moved){
    auto* graph=plot(plotLevelDrag->owner);
    for(const auto& original:dragBefore.plots)if(original.id==graph->id){graph->yMin=original.yMin;graph->yMax=original.yMax;break;}
    status=L"에너지 값을 변경했습니다. Ctrl+Z: 되돌리기";
 }
 if(plotLevelDrag&&plotLevelDrag->horizontal&&moved)status=L"준위의 가로 위치를 변경했습니다. Ctrl+Z: 되돌리기";
 if(plotAxisDrag&&moved)status=L"축 길이와 연결된 위치를 변경했습니다. Ctrl+Z: 되돌리기";
 if(stagePositionDrag&&moved)status=L"메커니즘 단계의 위치를 변경했습니다. Ctrl+Z: 되돌리기";
 if(rotationDrag){
    moved=moved&&toNative(doc)!=toNative(dragBefore);
    rotationDrag.reset();
    status=L"마우스 회전 · Shift: 15° 단위 · Space: 선택 도구";
    if(moved)status+=L" · Ctrl+Z: 되돌리기";
 }
 plotLevelDrag.reset();plotAxisDrag.reset();stagePositionDrag.reset();dragging=false;if(moved){history.checkpoint(dragBefore);changed();}else doc=dragBefore;curveHandle=-1;annotationDrag=0;ReleaseCapture();}if(boxing){boxing=false;ReleaseCapture();Vec a=world(down),b=world(p);double l=std::min(a.x,b.x),t=std::min(a.y,b.y),r=std::max(a.x,b.x),z=std::max(a.y,b.y);for(auto& atom:doc.atoms)if(atom.p.x>=l&&atom.p.x<=r&&atom.p.y>=t&&atom.p.y<=z&&!contains(selection,atom.id))selection.push_back(atom.id);auto sc=scene();for(auto& box:sc.objectBounds)if(!doc.atom(box.first)&&!doc.bond(box.first)&&!doc.isLocked(box.first)){auto q=box.second;if(q.x>=l&&q.y>=t&&q.x+q.w<=r&&q.y+q.h<=z&&!contains(selection,box.first))selection.push_back(box.first);}selection=groupedSelection(selection);redraw();}
 if(drawing){if(tool==Orbital){auto fragment=draftOrbital(p);drawing=false;ReleaseCapture();history.checkpoint(doc);int first=doc.nextId;doc.append(fragment);selection.clear();for(auto& o:doc.objects)if(o.id>=first)selection.push_back(o.id);changed();return;}if(tool==Arrow||tool==Curve||tool==Fishhook||tool==Straight||tool==Resonance||tool==Interaction){drawing=false;ReleaseCapture();if((p-down).length()<5)return;Object o=draftArrow(p);history.checkpoint(doc);o.id=doc.nextId++;doc.objects.push_back(o);selection={o.id};changed();return;}
 Vec end=endpoint(p),begin=startAtom?doc.atom(startAtom)->p:world(down);int target=atomHit(end,8);drawing=false;ReleaseCapture();if(target==startAtom&&startAtom)return;if((end-begin).length()<1)return;history.checkpoint(doc);int a=startAtom?startAtom:doc.addAtom(begin),b=target?target:doc.addAtom(end,element);if(!target)doc.atom(b)->drawingScale=doc.atom(a)->drawingScale;doc.addBond(a,b,kind());selection={b};changed();}redraw();}

} // namespace yd::editor

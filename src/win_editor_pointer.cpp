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
#include "mechanism_data.h"
#include "plot_data.h"
#include "win_ui.h"
#include "win_dialogs.h"

namespace yd::editor {

static std::optional<InlineTarget> energyDrag;
static std::optional<CrossingCaption> crossingDrag;
struct PlotLevelDrag { int owner,state,path; PlotDragScale scale; bool horizontal; };
static std::optional<PlotLevelDrag> plotLevelDrag;
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
    targetContainer=0;
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
    const double next=std::round((energy-delta.y*edit.scale.unitsPerPoint)/step)*step;
    const double low=std::clamp(edit.scale.low,-1e6,1e6), high=std::clamp(edit.scale.high,-1e6,1e6);
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
Vec endpoint(Vec p){Vec a=startAtom?doc.atom(startAtom)->p:world(down);int target=doc.atomAt(world(p),8/zoom);if(target&&target!=startAtom)return doc.atom(target)->p;if((p-down).length()<5)return startAtom?bondEndpoint(doc,startAtom,doc.atom(startAtom)->p,kind(),element):a+Vec{std::cos(-pi/6),std::sin(-pi/6)}*defaultBondLength(doc,0,kind(),element);if(startAtom&&!(GetKeyState(VK_MENU)&0x8000))return bondEndpoint(doc,startAtom,world(p),kind(),element);return snapped(a,world(p),defaultBondLength(doc,startAtom,kind(),element),(GetKeyState(VK_MENU)&0x8000)!=0);}
int objectAt(Vec p){auto sc=scene();for(auto it=doc.objects.rbegin();it!=doc.objects.rend();++it){if(doc.isLocked(it->id))continue;auto r=sc.objectBounds[it->id];double t=5/zoom;if(p.x>=r.x-t&&p.y>=r.y-t&&p.x<=r.x+r.w+t&&p.y<=r.y+r.h+t){if(it->kind==ObjectKind::Line&&it->points.size()>=2){const auto points=routeArrow(doc,*it);Vec a=points[0],v=points[1]-a;double ll=v.x*v.x+v.y*v.y;if(ll>.001){double u=std::clamp(((p-a).x*v.x+(p-a).y*v.y)/ll,0.,1.);if((p-a-v*u).length()>t)continue;}}return it->id;}}
 for(auto& r:doc.reactions){auto b=sc.objectBounds[r.id];if(p.x>=b.x-5&&p.x<=b.x+b.w+5&&p.y>=b.y-5&&p.y<=b.y+b.h+5)return r.id;}for(auto& plot:doc.plots){if(doc.isLocked(plot.id))continue;auto b=sc.objectBounds[plot.id];if(p.x>=b.x-5&&p.x<=b.x+b.w+5&&p.y>=b.y-5&&p.y<=b.y+b.h+5)return plot.id;}return 0;}
bool pickSlot(Vec p){if(!guides)return false;for(auto& r:doc.reactions)for(size_t i=0;i<r.slots.size();i++){Vec c=reactionSlot(r,i);if(std::abs(p.x-c.x)<r.slotWidth/2&&std::abs(p.y-c.y)<r.slotHeight/2){targetContainer=r.id;targetSlot=i;targetAbove=false;selection={r.id};status=L"구조 삽입 영역 선택: Ctrl+V로 복사한 구조를 삽입합니다.";redraw();return true;}}
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
    if((mouse-down).length()<=2&&!moved)return;moved=true;if(energyDrag)return;doc=dragBase;
    if(plotLevelDrag){updatePlotLevel((mouse-down)*(1/zoom));return;}
    if(stagePositionDrag){updateStagePosition(delta);return;}
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
void cancel(){cancelStageCopy();mechanismConnecting=mechanismConnectionDrag=false;connectionEdge=-1;selectedCycleOwner=0;selectedCycleArrow=-1;snapPlot=0;snapStart.reset();if(dragging)doc=dragBefore;drawing=dragging=boxing=panning=rightPanning=false;curveHandle=-1;annotationDrag=0;crossingDrag.reset();plotLevelDrag.reset();stagePositionDrag.reset();ReleaseCapture();redraw();}
void mouseDown(Vec p){hideTooltip();for(auto& h:hits)if(h.id==156&&h.box.Contains(float(p.x),float(p.y))){command(156);return;}if(!finishInline(true))return;SetFocus(win);for(auto& h:hits)if(h.box.Contains(float(p.x),float(p.y))){command(h.id);return;}if(!canvas(p))return;if(stageCopyOwner){toggleStageCopy(p);return;}down=mouse=p;annotationDrag=0;energyDrag.reset();crossingDrag.reset();plotLevelDrag.reset();stagePositionDrag.reset();
 if(tool==Select&&!(GetKeyState(VK_MENU)&0x8000)&&!mechanismConnecting&&!mechanismConnectionDrag&&!isolatedStructureEditing){
    int owner=0;size_t edge=0;double nearest=10;
    for(const auto& r:doc.reactions)for(const auto& [index,at]:mechanismSplitButtons(r)){
        double distance=(at-p).length();
        if(distance<nearest){nearest=distance;owner=r.id;edge=index;}
    }
    if(owner){insertMechanismLinkStage(owner,edge);return;}
 }
 if(beginMechanismConnection(p))return;selectedCycleOwner=0;selectedCycleArrow=-1;
 if((tool==Select&&!(GetKeyState(VK_MENU)&0x8000))||tool==Erase)if(auto hit=cycleArrowAt(world(p))){selectedCycleOwner=hit->first;selectedCycleArrow=hit->second;selection={hit->first};targetContainer=0;if(tool==Erase)deleteSelectedCycleArrow();else{status=L"화살표 선택 · Del: 연결 삭제 · 양끝 원 드래그: 연결 변경·길이 조절 · 우클릭: 추가 편집";redraw();}return;}
 if(snapPlot){
    auto* graph=plot(snapPlot);if(!graph||doc.isLocked(snapPlot)){snapPlot=0;snapStart.reset();return;}
    auto hit=nearestPlotSnap(scene(),p);if(!hit){status=L"표시된 준위의 점 가까이를 클릭하세요. Esc: 취소";redraw();return;}
    if(!snapStart){snapStart=hit;status=L"첫 점 고정. 두 번째 끝점·중앙점을 클릭하세요 (좌우 순서 무관). Esc: 취소";redraw();return;}
    auto label=snapLabel;label.start=std::min(snapStart->percent,hit->percent);label.end=std::max(snapStart->percent,hit->percent);
    if(label.end-label.start<1){status=L"두 점을 더 떨어뜨려 선택하세요 (최소 범위 1%). Esc: 취소";redraw();return;}
    history.checkpoint(doc);if(snapStage<0)graph->stages.push_back(label);else graph->stages.at(size_t(snapStage))=label;
    snapPlot=0;snapStart.reset();status=L"준위에 맞춰 반응 이름·양쪽 화살표를 배치했습니다. Ctrl+Z: 되돌리기";changed();return;
 }
 if(tool==Select&&!(GetKeyState(VK_MENU)&0x8000))for(auto& r:doc.reactions)if(!r.cyclic&&!r.slots.empty()&&!doc.isLocked(r.id) ){for(size_t slot=0;slot<r.slots.size();slot++){bool terminal=!r.explicitConnections()?slot+1==r.slots.size():std::none_of(r.links.begin(),r.links.end(),[&](const MechanismLink& l){return l.from==int(slot);});for(bool below:{false,true}){if((!terminal&&!below)||(below&&r.vertical))continue;if((p-mechanismAddPosition(r,below,slot)).length()<=13){selection={r.id};targetContainer=r.id;targetSlot=slot;command(below?128:122);return;}}}}
 if(tool==Select){
    auto sc=scene();
    const auto w=world(p);
    const bool alt=(GetKeyState(VK_MENU)&0x8000)!=0;
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
 if(beginCurveHandle(p))return;curveHandle=-1;Vec w=world(p);int aid=doc.atomAt(w,10/zoom),bid=doc.bondAt(w,5/zoom),oid=aid||bid?0:objectAt(w);hoverAtom=aid;hoverBond=aid?0:bid;
 if((GetKeyState(VK_CONTROL)&0x8000)&&!aid&&!bid&&!oid){panning=true;panOrigin=origin;SetCapture(win);return;}
 if(tool==Erase){int id=aid?aid:bid?bid:oid;if(id){history.checkpoint(doc);doc.erase({id});selection.clear();changed();}return;}
 if(tool==Select){
    if(auto target=textAt(w);target&&(target->role==3||target->role==9||
        (target->role==0&&textObject(target->owner)&&!importedEnergy(textObject(target->owner)->text).empty()))){
        energyDrag=target;dragBefore=doc;dragging=true;moved=copyDrag=false;targetContainer=0;
        SetCapture(win);status=L"에너지 값을 구조나 구조 아래로 드래그하세요. Esc: 취소";redraw();return;
    }
    bool imageHit=false;for(auto& o:doc.objects)if(o.id==oid&&o.kind==ObjectKind::Image)imageHit=true;if(!aid&&!bid&&!imageHit&&!groupedObject(oid)&&pickSlot(w))return;targetContainer=0;int id=aid?aid:bid?bid:oid;bool shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;if(id){if(shift){auto members=groupedSelection({id});if(contains(selection,id))selection.erase(std::remove_if(selection.begin(),selection.end(),[&](int member){return contains(members,member);}),selection.end());else{selection.insert(selection.end(),members.begin(),members.end());selection=groupedSelection(selection);}}else if(!contains(selection,id)){selection=groupedSelection({id});}if(!selection.empty()){dragBefore=doc;dragBase=doc;dragging=true;moved=false;copyDrag=(GetKeyState(VK_CONTROL)&0x8000)!=0;SetCapture(win);}}else{if(!shift)selection.clear();boxing=true;SetCapture(win);}redraw();return;}
 if(tool==Text){beginInline(textAt(w),w);return;}
 if(tool==ImageTool){addImage(w);return;}if(tool==Ring5||tool==Ring6||tool==Benzene){history.checkpoint(doc);if(aid||bid)doc.attachRing(aid,bid,tool==Ring5?5:6,tool==Benzene);else doc.ring(w,tool==Ring5?5:6,tool==Benzene);changed();return;}
 if(tool==Arrow||tool==Curve||tool==Fishhook||tool==Straight||tool==Resonance||tool==Interaction||tool==Orbital){drawing=true;startAtom=aid;startBond=aid?0:bid;SetCapture(win);return;}
 if((GetKeyState(VK_MENU)&0x8000)&&aid){selection={aid};editObject(aid);return;}
 if(!aid&&bid){history.checkpoint(doc);auto b=doc.bond(bid);if(b->kind==BondKind::Aromatic&&kind()==BondKind::Aromatic){Vec a=doc.atom(b->a)->p,v=doc.atom(b->b)->p-a;for(auto& stroke:scene().items)if(stroke.owner==bid&&stroke.dash&&stroke.points.size()==2){Vec offset=(stroke.points[0]+stroke.points[1])*.5-a;b->doubleSide=(v.x*offset.y-v.y*offset.x)>0?-1:1;break;}}else if(b->kind==kind()&&(kind()==BondKind::Wedge||kind()==BondKind::Hash))std::swap(b->a,b->b);else setBondKind(doc,bid,kind());b->customOrder.clear();b->customDisplay.clear();selection={bid};changed();return;}
 startAtom=aid;drawing=true;SetCapture(win);redraw();}
void mouseUp(Vec p){if(mechanismConnectionDrag){finishMechanismConnection(p);return;}if(rightPanning)return;if(panning){panning=false;ReleaseCapture();redraw();return;}if(dragging&&energyDrag){dragging=false;if((p-down).length()>2)dropEnergy(world(p));energyDrag.reset();ReleaseCapture();redraw();return;}if(dragging){if(crossingDrag||plotLevelDrag||stagePositionDrag){mouse=p;updateDrag();crossingDrag.reset();}
 if(plotLevelDrag&&!plotLevelDrag->horizontal&&moved){
    auto* graph=plot(plotLevelDrag->owner);
    for(const auto& original:dragBefore.plots)if(original.id==graph->id){graph->yMin=original.yMin;graph->yMax=original.yMax;break;}
    status=L"에너지 값을 변경했습니다. Ctrl+Z: 되돌리기";
 }
 if(plotLevelDrag&&plotLevelDrag->horizontal&&moved)status=L"준위의 가로 위치를 변경했습니다. Ctrl+Z: 되돌리기";
 if(stagePositionDrag&&moved)status=L"메커니즘 단계의 위치를 변경했습니다. Ctrl+Z: 되돌리기";
 plotLevelDrag.reset();stagePositionDrag.reset();dragging=false;if(moved){history.checkpoint(dragBefore);changed();}else doc=dragBefore;curveHandle=-1;annotationDrag=0;ReleaseCapture();}if(boxing){boxing=false;ReleaseCapture();Vec a=world(down),b=world(p);double l=std::min(a.x,b.x),t=std::min(a.y,b.y),r=std::max(a.x,b.x),z=std::max(a.y,b.y);for(auto& atom:doc.atoms)if(atom.p.x>=l&&atom.p.x<=r&&atom.p.y>=t&&atom.p.y<=z&&!contains(selection,atom.id))selection.push_back(atom.id);auto sc=scene();for(auto& box:sc.objectBounds)if(!doc.atom(box.first)&&!doc.bond(box.first)&&!doc.isLocked(box.first)){auto q=box.second;if(q.x>=l&&q.y>=t&&q.x+q.w<=r&&q.y+q.h<=z&&!contains(selection,box.first))selection.push_back(box.first);}selection=groupedSelection(selection);redraw();}
 if(drawing){if(tool==Orbital){auto fragment=draftOrbital(p);drawing=false;ReleaseCapture();history.checkpoint(doc);int first=doc.nextId;doc.append(fragment);selection.clear();for(auto& o:doc.objects)if(o.id>=first)selection.push_back(o.id);changed();return;}if(tool==Arrow||tool==Curve||tool==Fishhook||tool==Straight||tool==Resonance||tool==Interaction){drawing=false;ReleaseCapture();if((p-down).length()<5)return;Object o=draftArrow(p);history.checkpoint(doc);o.id=doc.nextId++;doc.objects.push_back(o);selection={o.id};changed();return;}
 Vec end=endpoint(p),begin=startAtom?doc.atom(startAtom)->p:world(down);int target=doc.atomAt(end,8/zoom);drawing=false;ReleaseCapture();if(target==startAtom&&startAtom)return;if((end-begin).length()<1)return;history.checkpoint(doc);int a=startAtom?startAtom:doc.addAtom(begin),b=target?target:doc.addAtom(end,element);if(!target)doc.atom(b)->drawingScale=doc.atom(a)->drawingScale;doc.addBond(a,b,kind());selection={b};changed();}redraw();}

} // namespace yd::editor

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

namespace yd::editor {

BondKind kind(){return tool==Double?BondKind::Double:tool==Triple?BondKind::Triple:tool==Wedge?BondKind::Wedge:tool==Hash?BondKind::Hash:tool==Delocalized?BondKind::Aromatic:BondKind::Single;}
Vec sproutPoint(int id){return bondEndpoint(doc,id,doc.atom(id)->p,kind());}
Vec endpoint(Vec p){Vec a=startAtom?doc.atom(startAtom)->p:world(down);int target=doc.atomAt(world(p),8/zoom);if(target&&target!=startAtom)return doc.atom(target)->p;if((p-down).length()<5)return startAtom?bondEndpoint(doc,startAtom,doc.atom(startAtom)->p,kind(),element):a+Vec{std::cos(-pi/6),std::sin(-pi/6)}*defaultBondLength(doc,0,kind(),element);if(startAtom&&!(GetKeyState(VK_MENU)&0x8000))return bondEndpoint(doc,startAtom,world(p),kind(),element);return snapped(a,world(p),defaultBondLength(doc,startAtom,kind(),element),(GetKeyState(VK_MENU)&0x8000)!=0);}
int objectAt(Vec p){auto sc=scene();for(auto it=doc.objects.rbegin();it!=doc.objects.rend();++it){if(doc.isLocked(it->id))continue;auto r=sc.objectBounds[it->id];double t=5/zoom;if(p.x>=r.x-t&&p.y>=r.y-t&&p.x<=r.x+r.w+t&&p.y<=r.y+r.h+t){if(it->kind==ObjectKind::Line&&it->points.size()>=2){Vec a=it->points[0],v=it->points[1]-a;double ll=v.x*v.x+v.y*v.y;if(ll>.001){double u=std::clamp(((p-a).x*v.x+(p-a).y*v.y)/ll,0.,1.);if((p-a-v*u).length()>t)continue;}}return it->id;}}
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
    if((mouse-down).length()<=2&&!moved)return;moved=true;doc=dragBase;
    if(annotationDrag){auto* a=doc.atom(annotationDrag);a->annotationManual=true;a->annotationOffset=annotationStart+delta-a->p;return;}
    if(curveHandle>=0){for(auto& o:doc.objects)if(o.id==curveOwner){o.points[size_t(curveHandle)]=o.points[size_t(curveHandle)]+delta;}return;}
    if(copyDrag){auto fragment=selectedDocument();int first=doc.nextId;doc.append(fragment);selection.clear();for(int id:doc.allIds())if(id>=first)selection.push_back(id);copyDrag=false;dragBase=doc;}doc.translate(selection,delta);
}
void cancel(){mechanismConnecting=mechanismConnectionDrag=false;connectionEdge=-1;selectedCycleOwner=0;selectedCycleArrow=-1;snapPlot=0;snapStart.reset();if(dragging)doc=dragBefore;drawing=dragging=boxing=panning=rightPanning=false;curveHandle=-1;annotationDrag=0;ReleaseCapture();redraw();}
void mouseDown(Vec p){hideTooltip();for(auto& h:hits)if(h.id==156&&h.box.Contains(float(p.x),float(p.y))){command(156);return;}if(!finishInline(true))return;SetFocus(win);for(auto& h:hits)if(h.box.Contains(float(p.x),float(p.y))){command(h.id);return;}if(!canvas(p))return;down=mouse=p;annotationDrag=0;if(beginMechanismConnection(p))return;selectedCycleOwner=0;selectedCycleArrow=-1;
 if(tool==Select||tool==Erase)if(auto hit=cycleArrowAt(world(p))){selectedCycleOwner=hit->first;selectedCycleArrow=hit->second;selection={hit->first};targetContainer=0;if(tool==Erase)deleteSelectedCycleArrow();else{status=L"반응 화살표 선택 · 우클릭: 중앙 X 설정 · Ctrl+Z: 되돌리기";redraw();}return;}
 if(snapPlot){
    auto* graph=plot(snapPlot);if(!graph||doc.isLocked(snapPlot)){snapPlot=0;snapStart.reset();return;}
    auto hit=nearestPlotSnap(scene(),p);if(!hit){status=L"표시된 준위의 점 가까이를 클릭하세요. Esc: 취소";redraw();return;}
    if(!snapStart){snapStart=hit;status=L"첫 점 고정. 두 번째 끝점·중앙점을 클릭하세요 (좌우 순서 무관). Esc: 취소";redraw();return;}
    auto label=snapLabel;label.start=std::min(snapStart->percent,hit->percent);label.end=std::max(snapStart->percent,hit->percent);
    if(label.end-label.start<1){status=L"두 점을 더 떨어뜨려 선택하세요 (최소 범위 1%). Esc: 취소";redraw();return;}
    history.checkpoint(doc);if(snapStage<0)graph->stages.push_back(label);else graph->stages.at(size_t(snapStage))=label;
    snapPlot=0;snapStart.reset();status=L"준위에 맞춰 반응 이름·양쪽 화살표를 배치했습니다. Ctrl+Z: 되돌리기";changed();return;
 }
 if(tool==Select)for(auto& r:doc.reactions)if(!r.cyclic&&!r.slots.empty()&&!doc.isLocked(r.id) ){for(size_t slot=0;slot<r.slots.size();slot++){bool terminal=!r.explicitConnections()?slot+1==r.slots.size():std::none_of(r.links.begin(),r.links.end(),[&](const MechanismLink& l){return l.from==int(slot);});if(!terminal)continue;for(bool below:{false,true}){if(below&&r.vertical)continue;if((p-mechanismAddPosition(r,below,slot)).length()<=13){selection={r.id};targetContainer=r.id;targetSlot=slot;command(below?128:122);return;}}}}
 if(tool==Select){auto sc=scene();Vec w=world(p);for(auto i=sc.items.rbegin();i!=sc.items.rend();++i)if(i->textRole==13&&!groupedObject(i->owner)){Vec at=i->points[0];double width=measure(i->text,i->fontSize,i->font,i->face);if(w.x>=at.x-2&&w.x<=at.x+width+2&&w.y>=at.y-2&&w.y<=at.y+i->fontSize+2){annotationDrag=i->owner;annotationStart=at;dragBefore=dragBase=doc;selection={i->owner};curveHandle=-1;dragging=true;moved=copyDrag=false;SetCapture(win);status=L"전하·라디칼 표시 위치 이동 · Esc: 취소 · Ctrl+Z: 되돌리기";redraw();return;}}}
 if(beginCurveHandle(p))return;curveHandle=-1;Vec w=world(p);int aid=doc.atomAt(w,10/zoom),bid=doc.bondAt(w,5/zoom),oid=aid||bid?0:objectAt(w);hoverAtom=aid;hoverBond=aid?0:bid;
 if((GetKeyState(VK_CONTROL)&0x8000)&&!aid&&!bid&&!oid){panning=true;panOrigin=origin;SetCapture(win);return;}
 if(tool==Erase){int id=aid?aid:bid?bid:oid;if(id){history.checkpoint(doc);doc.erase({id});selection.clear();changed();}return;}
 if(tool==Select){if(auto target=textAt(w);target&&contains(selection,target->owner)&&!doc.atom(target->owner)&&!groupedObject(target->owner)){beginInline(target,w);return;}bool imageHit=false;for(auto& o:doc.objects)if(o.id==oid&&o.kind==ObjectKind::Image)imageHit=true;if(!aid&&!bid&&!imageHit&&!groupedObject(oid)&&pickSlot(w))return;targetContainer=0;int id=aid?aid:bid?bid:oid;bool shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;if(id){if(shift){auto members=groupedSelection({id});if(contains(selection,id))selection.erase(std::remove_if(selection.begin(),selection.end(),[&](int member){return contains(members,member);}),selection.end());else{selection.insert(selection.end(),members.begin(),members.end());selection=groupedSelection(selection);}}else if(!contains(selection,id)){selection=groupedSelection({id});}if(!selection.empty()){dragBefore=doc;dragBase=doc;dragging=true;moved=false;copyDrag=(GetKeyState(VK_CONTROL)&0x8000)!=0;SetCapture(win);}}else{if(!shift)selection.clear();boxing=true;SetCapture(win);}redraw();return;}
 if(tool==Text){beginInline(textAt(w),w);return;}
 if(tool==ImageTool){addImage(w);return;}if(tool==Ring5||tool==Ring6||tool==Benzene){history.checkpoint(doc);if(aid||bid)doc.attachRing(aid,bid,tool==Ring5?5:6,tool==Benzene);else doc.ring(w,tool==Ring5?5:6,tool==Benzene);changed();return;}
 if(tool==Arrow||tool==Curve||tool==Fishhook||tool==Straight||tool==Resonance||tool==Orbital){drawing=true;startAtom=aid;startBond=aid?0:bid;SetCapture(win);return;}
 if((GetKeyState(VK_MENU)&0x8000)&&aid){selection={aid};editObject(aid);return;}
 if(!aid&&bid){history.checkpoint(doc);auto b=doc.bond(bid);if(b->kind==BondKind::Aromatic&&kind()==BondKind::Aromatic){Vec a=doc.atom(b->a)->p,v=doc.atom(b->b)->p-a;for(auto& stroke:scene().items)if(stroke.owner==bid&&stroke.dash&&stroke.points.size()==2){Vec offset=(stroke.points[0]+stroke.points[1])*.5-a;b->doubleSide=(v.x*offset.y-v.y*offset.x)>0?-1:1;break;}}else if(b->kind==kind()&&(kind()==BondKind::Wedge||kind()==BondKind::Hash))std::swap(b->a,b->b);else setBondKind(doc,bid,kind());b->customOrder.clear();b->customDisplay.clear();selection={bid};changed();return;}
 startAtom=aid;drawing=true;SetCapture(win);redraw();}
void mouseUp(Vec p){if(mechanismConnectionDrag){finishMechanismConnection(p);return;}if(rightPanning)return;if(panning){panning=false;ReleaseCapture();redraw();return;}if(dragging){dragging=false;if(moved){history.checkpoint(dragBefore);changed();}else doc=dragBefore;curveHandle=-1;annotationDrag=0;ReleaseCapture();}if(boxing){boxing=false;ReleaseCapture();Vec a=world(down),b=world(p);double l=std::min(a.x,b.x),t=std::min(a.y,b.y),r=std::max(a.x,b.x),z=std::max(a.y,b.y);for(auto& atom:doc.atoms)if(atom.p.x>=l&&atom.p.x<=r&&atom.p.y>=t&&atom.p.y<=z&&!contains(selection,atom.id))selection.push_back(atom.id);auto sc=scene();for(auto& box:sc.objectBounds)if(!doc.atom(box.first)&&!doc.bond(box.first)&&!doc.isLocked(box.first)){auto q=box.second;if(q.x>=l&&q.y>=t&&q.x+q.w<=r&&q.y+q.h<=z&&!contains(selection,box.first))selection.push_back(box.first);}selection=groupedSelection(selection);redraw();}
 if(drawing){if(tool==Orbital){auto fragment=draftOrbital(p);drawing=false;ReleaseCapture();history.checkpoint(doc);int first=doc.nextId;doc.append(fragment);selection.clear();for(auto& o:doc.objects)if(o.id>=first)selection.push_back(o.id);changed();return;}if(tool==Arrow||tool==Curve||tool==Fishhook||tool==Straight||tool==Resonance){drawing=false;ReleaseCapture();if((p-down).length()<5)return;Object o=draftArrow(p);history.checkpoint(doc);o.id=doc.nextId++;doc.objects.push_back(o);selection={o.id};changed();return;}
 Vec end=endpoint(p),begin=startAtom?doc.atom(startAtom)->p:world(down);int target=doc.atomAt(end,8/zoom);drawing=false;ReleaseCapture();if(target==startAtom&&startAtom)return;if((end-begin).length()<1)return;history.checkpoint(doc);int a=startAtom?startAtom:doc.addAtom(begin),b=target?target:doc.addAtom(end,element);if(!target)doc.atom(b)->drawingScale=doc.atom(a)->drawingScale;doc.addBond(a,b,kind());selection={b};changed();}redraw();}

} // namespace yd::editor

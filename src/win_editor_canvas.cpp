#include "win_editor_canvas.h"
#include "win_editor_chrome.h"
#include "win_editor_commands.h"
#include "win_editor_images.h"
#include "win_editor_plot.h"
#include "win_editor_pointer.h"
#include "win_editor_reaction.h"
#include "win_editor_render.h"
#include "win_editor_state.h"
#include "win_editor_text.h"
#include "mechanism_data.h"
#include "plot_data.h"
#include "win_dialogs.h"
#include "win_theme.h"
#include "win_ui.h"

namespace yd::editor {
namespace {
constexpr int drawingPanelBottom=830;
int topOffset=0,leftOffset=0,rightOffset=0,topLimit=0;
std::vector<int> favorites;
bool favoritesLoaded=false;

void loadFavorites(){
    if(favoritesLoaded)return;
    favoritesLoaded=true;
    for(int id=200;id<=dottedBondCommand;++id){
        if(id>219&&id!=interactionToolCommand&&id!=dottedBondCommand)continue;
        DWORD value=0,size=sizeof(value);
        const auto key=std::to_wstring(id);
        if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\YoungDraw\\Favorites",key.c_str(),RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS&&value)
            favorites.push_back(id);
    }
}
}
bool pinTool(Vec at){
    for(const auto& hit:hits){
        if((hit.id<200||(hit.id>219&&hit.id!=interactionToolCommand&&hit.id!=dottedBondCommand))||!hit.box.Contains(float(at.x),float(at.y)))continue;
        auto found=std::find(favorites.begin(),favorites.end(),hit.id);
        const bool pinned=found!=favorites.end();
        HMENU menu=CreatePopupMenu();
        AppendMenuW(menu,MF_STRING,1,pinned?L"상단 고정 해제":L"상단 도구 모음에 고정");
        POINT point{LONG(at.x),LONG(at.y)};ClientToScreen(win,&point);
        const auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,win,nullptr);
        DestroyMenu(menu);
        if(choice){
            if(pinned)favorites.erase(found);else favorites.push_back(hit.id);
            const DWORD value=!pinned;
            const auto key=std::to_wstring(hit.id);
            RegSetKeyValueW(HKEY_CURRENT_USER,L"Software\\YoungDraw\\Favorites",key.c_str(),REG_DWORD,&value,sizeof(value));
            redraw();
        }
        return true;
    }
    return false;
}
bool scrollTools(Vec at,int delta){
    int* offset=nullptr,limit=0;
    if(at.y<89){offset=&topOffset;limit=topLimit;}
    else if(at.x<left){offset=&leftOffset;limit=std::max(0,drawingPanelBottom-(wh-bottom));}
    else if(at.x>=ww-right){offset=&rightOffset;limit=std::max(0,684-(wh-bottom));}
    if(!offset)return false;
    *offset=std::clamp(*offset+delta,0,limit);
    hideTooltip();redraw();return true;
}

static void clipHits(size_t first,const RectF& clip,float x,float y){
    for(size_t i=first;i<hits.size();){
        auto& box=hits[i].box;
        box.X+=x;box.Y+=y;
        if(!box.Intersect(clip))hits.erase(hits.begin()+i);
        else ++i;
    }
}


std::unique_ptr<Bitmap> paintBuffer;
double bondFraction(int id,Vec p){auto* b=doc.bond(id);if(!b)return .5;Vec a=doc.atom(b->a)->p,v=doc.atom(b->b)->p-a;double ll=v.x*v.x+v.y*v.y;return ll>1e-9?std::clamp(((p-a).x*v.x+(p-a).y*v.y)/ll,0.,1.):.5;}
Document draftOrbital(Vec pointer){Document preview;preview.style=doc.style;Vec at=startAtom&&doc.atom(startAtom)?doc.atom(startAtom)->p:world(down),tip=world(pointer);if((pointer-down).length()<5||(tip-at).length()<.01)tip=at+Vec{0,-doc.style.bondLength};else if(GetKeyState(VK_SHIFT)&0x8000)tip=snapped(at,tip,(tip-at).length());addOrbital(preview,at,tip,orbitalKind,orbitalPhase);return preview;}
Object draftArrow(Vec pointer){Object o;o.id=-1;Vec a=world(down),b=world(pointer);o.width=doc.style.lineWidth;o.color="#ff0000";
    if(tool==Interaction){
        o.color="#000000";o.dash=2;
        o.sourceAtom=startAtom;
        o.targetAtom=doc.atomAt(b,10/zoom);
        if(o.targetAtom==o.sourceAtom)o.targetAtom=0;
        o.autoRoute=o.sourceAtom||o.targetAtom;
        if(auto* atom=doc.atom(o.sourceAtom))a=atom->p;
        if(auto* atom=doc.atom(o.targetAtom))b=atom->p;
        else if(GetKeyState(VK_SHIFT)&0x8000){Vec v=b-a;b=std::abs(v.x)>std::abs(v.y)?Vec{b.x,a.y}:Vec{a.x,b.y};}
        o.points={a,b};
        return o;
    }
    if(tool==Straight||tool==Resonance){o.head=tool==Resonance?3:0;if(!o.head)o.color="#000000";if(GetKeyState(VK_SHIFT)&0x8000){Vec v=b-a;b=std::abs(v.x)>std::abs(v.y)?Vec{b.x,a.y}:Vec{a.x,b.y};}o.points={a,b};return o;}
    o.head=tool==Fishhook?2:1;o.sourceAtom=startAtom;o.targetAtom=doc.atomAt(b,10/zoom);
    o.sourceBond=o.sourceAtom?0:startBond;o.targetBond=o.targetAtom?0:doc.bondAt(b,5/zoom);
    o.sourceBondT=bondFraction(o.sourceBond,a);o.targetBondT=bondFraction(o.targetBond,b);
    if(o.targetAtom==o.sourceAtom)o.targetAtom=0;
    if(auto* atom=doc.atom(o.sourceAtom))a=atom->p;
    if(auto* atom=doc.atom(o.targetAtom))b=atom->p;else if(!o.targetBond&&(GetKeyState(VK_SHIFT)&0x8000)){Vec v=b-a;b=std::abs(v.x)>std::abs(v.y)?Vec{b.x,a.y}:Vec{a.x,b.y};}
    bool electron=tool==Curve||tool==Fishhook||((o.sourceAtom||o.sourceBond)&&(o.targetAtom||o.targetBond));o.kind=electron?ObjectKind::Curve:ObjectKind::Line;o.autoRoute=electron;
    if(electron){Vec v=b-a;double len=v.length();Vec n=len>.001?Vec{-v.y/len,v.x/len}:Vec{0,1};double bend=std::clamp(len*.3,6.,24.);o.points={a,a+v*.28+n*bend,a+v*.72+n*bend,b};}
    else{o.sourceAtom=o.targetAtom=o.sourceBond=o.targetBond=0;o.points={a,b};}return o;
}

void presentBuffer(Graphics& out,Bitmap& buffer){
    // The point-only DrawImage overload scales by the bitmap/device DPI ratio.
    // UI geometry and native child controls both use client pixels, so copy 1:1.
    out.SetPageUnit(UnitPixel);
    out.DrawImage(&buffer,Gdiplus::Rect(0,0,INT(buffer.GetWidth()),INT(buffer.GetHeight())),0,0,
                  INT(buffer.GetWidth()),INT(buffer.GetHeight()),UnitPixel);
}
void paint(HDC dc,bool reuseScene){const auto& palette=appearance::palette();ink=palette.text;muted=palette.muted;accent=palette.accent;border=palette.edge;
 const auto width=std::max(1,ww),height=std::max(1,wh);
 if(!paintBuffer||paintBuffer->GetWidth()!=UINT(width)||paintBuffer->GetHeight()!=UINT(height))
     paintBuffer=std::make_unique<Bitmap>(width,height,PixelFormat32bppARGB);
 Graphics g(paintBuffer.get());g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);g.Clear(palette.panel);hits.clear();fill(g,{0,0,float(ww),float(top)},palette.chrome);rule(g,0,87,float(ww),87,border);
 // One icon row beneath the native Korean menu; the same hit rectangles drive tooltips and commands.
 float toolbarX=12;
 loadFavorites();
 const int toolbarWidth=isolatedStructureEditing?ww-308:ww;
 topLimit=std::max(0,(isolatedStructureEditing?880:1116+int(favorites.size())*48)-toolbarWidth);
 topOffset=std::min(topOffset,topLimit);
 const auto toolbarClip=RectF(0,0,float(toolbarWidth),63);
 auto toolbarState=g.Save();g.SetClip(toolbarClip);g.TranslateTransform(float(-topOffset),0);
 const auto topHits=hits.size();
 auto group=[&](std::initializer_list<int> ids){
     for(int id:ids){RectF box{toolbarX,18,44,44};hits.push_back({id,box});
         bool hovered=box.Contains(float(mouse.x),float(mouse.y));
         if(hovered){appearance::rounded(g,box,8,palette.hover);}
         Color tint=(id==101||id==102)?palette.accent:ink;
         icon(g,id,{box.X+7,box.Y+7,30,30},tint);toolbarX+=48;
     }
 };
 auto separator=[&](){rule(g,toolbarX+2,22,toolbarX+2,58,border);toolbarX+=16;};
 if(isolatedStructureEditing)group({102});
 else {
     group({100,101,102});
     separator();
     group({185});
 }
 separator();group({104,105});separator();group({107,125,155,156});separator();
 group({170,171,172,173,174});separator();group({160,161,162,163,164});
 if(!isolatedStructureEditing){separator();group({bondAnimationCommand});}
 for(int id:favorites)group({id});
 g.Restore(toolbarState);
 clipHits(topHits,toolbarClip,float(-topOffset),0);
 if(isolatedStructureEditing){
     button(g,186,L"적용 · Ctrl+S",{float(ww-300),23,140,34});
     button(g,187,L"취소",{float(ww-152),23,136,34});
 }else{
     button(g,184,L"배경 · 테마 ◇",{float(ww-168),63,152,22});
 }
 text(g,L"YOUNGDRAW  /  SCIENTIFIC ATELIER",16,70,9,muted,true);
 if(isolatedStructureEditing)text(g,L"이 창에서는 선택한 단계의 구조만 편집합니다. 적용하면 원래 메커니즘에 반영됩니다.",left+12,96,12,ink,true);
 if(topLimit)text(g,L"↔ 휠로 도구 모음 스크롤",float(left+8),69,11,muted);
 const RectF leftClip(0,89,float(left),float(std::max(0,wh-bottom-89)));
 leftOffset=std::min(leftOffset,std::max(0,drawingPanelBottom-(wh-bottom)));
 const auto leftHits=hits.size();auto leftState=g.Save();g.SetClip(leftClip);g.TranslateTransform(0,float(-leftOffset));
 text(g,L"그리기",16,101,11,muted,true);
 const std::vector<int> drawingTools={200,201,202,217,203,204,205,206,207,208,209,210,211,212,213,214,215,216,218,219,interactionToolCommand,dottedBondCommand};
 for(size_t i=0;i<drawingTools.size();i++)toolButton(g,drawingTools[i],{float(12+(i%2)*82),float(122+(i/2)*44),74,40},drawingTools[i]==(tool==DottedBond?dottedBondCommand:tool==Interaction?interactionToolCommand:200+int(tool)));
 text(g,L"원소",16,620,11,muted,true);std::vector<int> elements={6,7,8,16,15,9,17,35,53,1,5,14};
 for(int i=0;i<int(elements.size());i++)button(g,300+elements[i],ui::wide(symbol(elements[i])),{float(12+i%3*53),float(642+i/3*30),49,26},element==elements[i]);
 int shown=int(tool);for(auto& h:hits)if(h.box.Contains(float(mouse.x),float(mouse.y))){
     if(h.id==dottedBondCommand)shown=DottedBond;
     else if(h.id==interactionToolCommand)shown=Interaction;
     else if(h.id>=200&&h.id<=218)shown=h.id-200;
 }
 text(g,toolNames[size_t(shown)],12,772,10,muted);
 button(g,150,L"Arial 굵게 / 2 pt",{12,794,156,29});

 g.Restore(leftState);clipHits(leftHits,leftClip,0,float(-leftOffset));
 if(wh-bottom<drawingPanelBottom)text(g,L"↕ 휠",float(left-46),89,10,muted);

 auto cb=canvasBox();if(appearance::settings().theme==6)SetTimer(win,913,40,nullptr);else KillTimer(win,913);appearance::background(g,cb);auto state=g.Save();g.SetClip(cb);g.TranslateTransform(float(origin.x),float(origin.y));g.ScaleTransform(float(zoom),float(zoom));const Scene& sc=sceneForPaint(reuseScene);animationVisible=false;animationDelay=1000;render(g,sc,true,true);auto draggedEnergy=energyDragPreview(sc);if(!draggedEnergy.items.empty())render(g,draggedEnergy,false,true);if(animationVisible)SetTimer(win,912,animationDelay,nullptr);else KillTimer(win,912);g.Restore(state);
 state=g.Save();g.SetClip(cb);auto guide=[&](Vec c,double w,double h,std::wstring label,int owner,size_t index,bool above){Vec p=screen(c);RectF r(float(p.x-w*zoom/2),float(p.y-h*zoom/2),float(w*zoom),float(h*zoom));bool active=targetContainer==owner&&targetSlot==index&&targetAbove==above;Pen pen(active?accent:Color(255,201,215,223),active?1.6f:1);pen.SetDashStyle(DashStyleDash);g.DrawRectangle(&pen,r);auto clip=g.Save();g.SetClip(RectF(r.X+2,r.Y+2,std::max(1.f,r.Width-4),std::max(1.f,r.Height-4)),CombineModeIntersect);ui::drawLabel(g,label,{r.X+3,r.Y+3,std::max(1.f,r.Width-6),std::max(1.f,r.Height-6)},std::min(10.f,std::max(6.f,r.Width/9)),appearance::palette().canvasInk,false,true);g.Restore(clip);};
 if(snapPlot){
    auto hovered=nearestPlotSnap(sc,mouse);auto found=sc.plotSnapPoints.find(snapPlot);
    if(found!=sc.plotSnapPoints.end())for(auto& pt:found->second){Vec at=screen(pt.p);Pen pen(accent,1.5f);SolidBrush body(white);g.FillEllipse(&body,float(at.x-4),float(at.y-4),8.f,8.f);g.DrawEllipse(&pen,float(at.x-4),float(at.y-4),8.f,8.f);}
    auto highlight=[&](const PlotSnapPoint& pt){Vec at=screen(pt.p);Pen pen(accent,2.5f);g.DrawEllipse(&pen,float(at.x-7),float(at.y-7),14.f,14.f);pen.SetDashStyle(DashStyleDash);g.DrawLine(&pen,float(at.x),float(at.y+8),float(at.x),float(screen({0,sc.plotBaselines.at(snapPlot)}).y));};
    if(snapStart)highlight(*snapStart);
    if(hovered){highlight(*hovered);if(auto* graph=plot(snapPlot)){auto label=ui::wide(graph->states[size_t(hovered->state)].label)+L" · "+(hovered->edge<0?L"왼쪽 끝":hovered->edge>0?L"오른쪽 끝":L"중앙");Vec at=screen(hovered->p);text(g,label,float(at.x+10),float(at.y-25),11,accent,true);}}
 }
 if(auto* r=reaction(stageCopyOwner)){
    Scene retained;
    for(auto item:sc.items)if(item.owner==r->id&&item.reactionArrow>=0&&
        size_t(item.reactionArrow)<r->links.size()&&mechanismLinkSelected(r->links[size_t(item.reactionArrow)],stagesToCopy)){
        item.color="#008778";
        item.width+=1;
        retained.items.push_back(std::move(item));
    }
    auto saved=g.Save();
    g.TranslateTransform(float(origin.x),float(origin.y));
    g.ScaleTransform(float(zoom),float(zoom));
    render(g,retained);
    g.Restore(saved);
    for(size_t i=0;i<r->slots.size();++i){
        const bool selected=stagesToCopy.count(i)>0;
        Vec p=screen(reactionSlot(*r,i)-Vec{r->slotWidth/2,r->slotHeight/2});
        RectF frame(float(p.x),float(p.y),float(r->slotWidth*zoom),float(r->slotHeight*zoom));
        Pen outline(selected?accent:appearance::palette().canvasInk,selected?2.5f:1.f);
        if(!selected)outline.SetDashStyle(DashStyleDash);
        g.DrawRectangle(&outline,frame);
        text(g,std::to_wstring(i+1),frame.X+3,frame.Y-17,11,selected?accent:appearance::palette().canvasInk,true);
    }
 }
 if(guides&&!snapPlot){for(auto& r:doc.reactions)for(size_t i=0;i<r.slots.size();i++)guide(reactionSlot(r,i),r.slotWidth,r.slotHeight,(mechanismConnecting?std::to_wstring(i+1)+L" · ":L"")+(r.slots[i].members.empty()?L"그림 영역":L""),r.id,i,false);for(auto& p:doc.plots)if(!doc.isLocked(p.id))for(size_t i=0;i<p.states.size();i++){guide(plotAnchor(p,i,true),48,55,p.states[i].above.empty()?L"+ 구조 삽입":L"",p.id,i,true);guide(plotAnchor(p,i,false),48,55,p.states[i].below.empty()?L"+ 구조 삽입":L"",p.id,i,false);}}
 if(tool==Select&&!inlineEdit){
    auto plus=[&](Vec p,float radius){
        SolidBrush body(white);Pen outline(accent,1.8f);
        const float x=float(p.x),y=float(p.y);
        g.FillEllipse(&body,x-radius,y-radius,2*radius,2*radius);
        g.DrawEllipse(&outline,x-radius,y-radius,2*radius,2*radius);
        g.DrawLine(&outline,x-5,y,x+5,y);g.DrawLine(&outline,x,y-5,x,y+5);
    };
    for(auto& r:doc.reactions)if(!r.cyclic&&!r.circularMerge&&r.slots.size()<40&&r.links.size()<100&&!r.slots.empty()&&!doc.isLocked(r.id)){
        if(!mechanismConnecting&&!mechanismConnectionDrag&&!stageCopyOwner&&!isolatedStructureEditing)
            for(const auto& button:mechanismSplitButtons(r))plus(button.second,9);
        for(size_t slot=0;slot<r.slots.size();slot++){
            bool terminal=!r.explicitConnections()?slot+1==r.slots.size():std::none_of(r.links.begin(),r.links.end(),[&](const MechanismLink& l){return l.from==int(slot);});
            for(bool below:{false,true})if((terminal||below)&&(!below||!r.vertical))plus(mechanismAddPosition(r,below,slot),12);
        }
    }
 }
 if(selectedCycleArrow>=0&&selection==std::vector<int>{selectedCycleOwner}){Scene highlight;for(auto item:sc.items)if(item.owner==selectedCycleOwner&&item.reactionArrow==selectedCycleArrow){item.color="#008778";item.width+=1;highlight.items.push_back(item);}auto saved=g.Save();g.TranslateTransform(float(origin.x),float(origin.y));g.ScaleTransform(float(zoom),float(zoom));render(g,highlight);g.Restore(saved);}
 if(mechanismConnectionDrag){Vec start=world(down);if(auto* r=reaction(connectionOwner);r&&connectionFrom>=0)start=reactionSlot(*r,size_t(connectionFrom));Object preview;preview.head=1;preview.color="#008778";preview.width=2;preview.points={start,world(mouse)};auto saved=g.Save();g.TranslateTransform(float(origin.x),float(origin.y));g.ScaleTransform(float(zoom),float(zoom));Scene outline;outline.items=arrowPrimitives(preview);render(g,outline);g.Restore(saved);}
 if(selectedCycleArrow>=0&&selection==std::vector<int>{selectedCycleOwner}){auto path=mechanismConnectionPath(selectedCycleOwner,selectedCycleArrow);if(!path.empty()){Pen outline(accent,2);SolidBrush body(white);for(auto at:{path.front(),path.back()}){auto p=screen(at);g.FillEllipse(&body,float(p.x-6),float(p.y-6),12.f,12.f);g.DrawEllipse(&outline,float(p.x-6),float(p.y-6),12.f,12.f);}}}
 if(tool==Select&&selection.size()==1&&!snapPlot&&!inlineEdit){
    auto levels=sc.plotSnapPoints.find(selection.front());
    if(levels!=sc.plotSnapPoints.end()&&!doc.isLocked(levels->first)){
        Pen outline(accent,1.4f);
        for(const auto& point:levels->second)if(point.edge==0){
            Vec at=screen(point.p);
            g.DrawEllipse(&outline,float(at.x-4),float(at.y-4),8.f,8.f);
        }
    }
 }
 if(!inlineEdit)for(int id:selection){auto it=sc.objectBounds.find(id);if(it!=sc.objectBounds.end()){auto r=it->second;Vec p=screen({r.x,r.y});Pen pen(Color(150,0,135,120),1.5);pen.SetDashStyle(DashStyleDash);g.DrawRectangle(&pen,float(p.x-4),float(p.y-4),float(r.w*zoom+8),float(r.h*zoom+8));}else if(auto a=doc.atom(id)){Vec p=screen(a->p);Pen pen(accent,1.5);g.DrawEllipse(&pen,float(p.x-8),float(p.y-8),16.f,16.f);}}
 if(selection.size()==1){auto found=sc.arrowPaths.find(selection.front());if(found!=sc.arrowPaths.end()&&found->second.size()==4){const auto& path=found->second;Pen guidePen(Color(170,0,135,120),1);guidePen.SetDashStyle(DashStyleDash);for(int i:{0,2}){Vec a=screen(path[i]),b=screen(path[i+1]);g.DrawLine(&guidePen,float(a.x),float(a.y),float(b.x),float(b.y));}Pen outline(accent,1.5);SolidBrush body(white),control(accent);for(int i=0;i<4;i++){Vec p=screen(path[i]);g.FillEllipse(i==1||i==2?&control:&body,float(p.x-5),float(p.y-5),10.f,10.f);g.DrawEllipse(&outline,float(p.x-5),float(p.y-5),10.f,10.f);}}}
 if(hoverAtom){Vec p=screen(doc.atom(hoverAtom)->p);Pen pen(accent,1);g.DrawEllipse(&pen,float(p.x-7),float(p.y-7),14.f,14.f);}if(boxing){Pen p(accent,1);p.SetDashStyle(DashStyleDash);g.DrawRectangle(&p,float(std::min(down.x,mouse.x)),float(std::min(down.y,mouse.y)),float(std::abs(down.x-mouse.x)),float(std::abs(down.y-mouse.y)));}if(drawing){if(tool==Orbital){auto transform=g.Save();g.TranslateTransform(float(origin.x),float(origin.y));g.ScaleTransform(float(zoom),float(zoom));render(g,makeScene(draftOrbital(mouse),measure),false,true);g.Restore(transform);}else if(tool==Arrow||tool==Curve||tool==Fishhook||tool==Straight||tool==Resonance||tool==Interaction){Document preview=doc;preview.objects.push_back(draftArrow(mouse));auto all=makeScene(preview,measure);Scene arrow;for(auto& item:all.items)if(item.owner==-1)arrow.items.push_back(item);auto transform=g.Save();g.TranslateTransform(float(origin.x),float(origin.y));g.ScaleTransform(float(zoom),float(zoom));render(g,arrow,false,true);g.Restore(transform);}else{Pen p(accent,1.5);p.SetDashStyle(DashStyleDash);g.DrawLine(&p,float(down.x),float(down.y),float(mouse.x),float(mouse.y));}}g.Restore(state);
 float rx=float(ww-right);fill(g,{rx,float(top),float(right),float(wh-top-bottom)},palette.panel);const RectF rightClip(rx,89,float(right),float(std::max(0,wh-bottom-89)));
 rightOffset=std::min(rightOffset,std::max(0,684-(wh-bottom)));
 const auto rightHits=hits.size();auto rightState=g.Save();g.SetClip(rightClip);g.TranslateTransform(0,float(-rightOffset));
 button(g,175,L"도구",{rx+12,91,114,28},!showLayers);button(g,176,L"레이어",{rx+132,91,116,28},showLayers);
 if(showLayers){
    text(g,L"레이어 잠금",rx+16,142,14,ink,true);
    text(g,L"잠긴 레이어는 클릭되지 않습니다.",rx+16,170,11,muted);
    size_t images=std::count_if(doc.objects.begin(),doc.objects.end(),[](const Object& o){return o.kind==ObjectKind::Image;});
    auto row=[&](float y,const std::wstring& label,size_t count,bool locked,int id){
        appearance::rounded(g,{rx+12,y,236,64},8,palette.raised);text(g,label,rx+24,y+10,13,ink,true);text(g,std::to_wstring(count)+L"개",rx+24,y+34,11,muted);
        button(g,id,locked?L"잠김":L"편집 가능",{rx+142,y+16,96,30},locked);
    };
    row(204,L"이미지",images,doc.imagesLocked,177);row(276,L"그래프",doc.plots.size(),doc.graphsLocked,178);
    text(g,L"빠른 전환",rx+16,372,11,muted,true);
    button(g,179,L"이미지만 편집",{rx+12,398,236,34},doc.graphsLocked&&!doc.imagesLocked);
    button(g,180,L"그래프만 편집",{rx+12,442,236,34},doc.imagesLocked&&!doc.graphsLocked);
    button(g,181,L"모두 잠금 해제",{rx+12,486,236,34});
    text(g,L"그림 편집 중 그래프가 눌리면",rx+16,554,11,muted);
    text(g,L"'이미지만 편집'을 누르세요.",rx+16,575,11,muted);
    text(g,L"이미지와 그래프는 자동 분류됩니다.",rx+16,616,11,muted);
    text(g,L"잠금 상태도 문서에 저장됩니다.",rx+16,637,11,muted);
    text(g,L"Ctrl+Z로 잠금 변경을 되돌립니다.",rx+16,658,11,muted);
 }else if(isolatedStructureEditing){
    button(g,110,L"구조 정리  Ctrl+Shift+K",{rx+12,124,236,30});button(g,111,L"템플릿",{rx+12,164,236,30});button(g,113,L"구조·이미지 파일 삽입",{rx+12,204,236,30});button(g,165,L"크기 조정  Ctrl+K",{rx+12,244,236,30});
    text(g,L"이 단계의 구조만 편집합니다.",rx+16,304,13,ink,true);text(g,L"상단 적용 또는 Ctrl+S: 반영",rx+16,342,12,muted);text(g,L"취소 / 창 닫기: 변경 버리기",rx+16,372,12,muted);
 }else{
    auto tile=[&](int id,const wchar_t* label,RectF box,bool active=false){
        hits.push_back({id,box});
        const bool hovered=box.Contains(float(mouse.x),float(mouse.y));
        const auto fg=active?palette.onAccent:palette.text;
        appearance::rounded(g,box,7,active?palette.accent:hovered?palette.hover:palette.raised);
        const float iconSize=box.Height>=50?26.f:22.f;
        icon(g,id,{box.X+(box.Width-iconSize)*.5f,box.Y+4,iconSize,iconSize},fg);
        ui::drawLabel(g,label,{box.X+2,box.GetBottom()-20,box.Width-4,18},
                      box.Width>100?13.f:12.f,fg,active,true);
    };
    struct SidebarTool {int id;const wchar_t* label;bool active=false;};
    auto row=[&](float y,std::initializer_list<SidebarTool> tools){
        float x=rx+12;
        for(auto item:tools){
            tile(item.id,item.label,{x,y,75,47},item.active);
            x+=81;
        }
    };
    auto heading=[&](const wchar_t* label,float y){text(g,label,rx+16,y,11,muted,true);};
    row(124,{{110,L"구조 정리"},{111,L"템플릿"},{112,L"템플릿 보관"}});
    tile(182,L"메커니즘",{rx+12,180,114,46},sidebarPage==0);
    tile(183,L"그래프",{rx+132,180,116,46},sidebarPage==1);
    tile(recognizeMechanismCommand,L"현재 파일 → 메커니즘 편집",{rx+12,235,237,55},true);
    heading(sidebarPage==0?L"메커니즘 도구":L"그래프 도구",303);
    if(sidebarPage==0){
        row(323,{{120,L"새 메커니즘"},{140,L"예제 열기"},{121,L"메커니즘 편집"}});
        row(376,{{146,L"단계 합치기"},{169,L"분기 추가"}});
        row(429,{{145,L"단계 연결",mechanismConnecting},{147,L"연결 삭제"},{149,L"화살표 길이"}});
        row(482,{{127,L"배치·단위"},{125,L"자동 정렬"}});
    }else{
        row(323,{{130,L"새 그래프"},{141,L"예제 열기"},{131,L"에너지 표"}});
        row(376,{{132,L"경로 추가"},{135,L"왼쪽 분기"},{133,L"경로 스타일"}});
        row(429,{{142,L"구간 색"},{139,L"이름·범위"},{136,L"화살표 배치",snapPlot!=0}});
        row(482,{{137,L"제목·축"},{151,L"스핀 교차점"},{spinMultiplicityPlotCommand,L"다중도 비교"}});
    }
    text(g,sidebarPage==0?L"Alt+드래그: 단계 자유 배치":L"준위 드래그: 에너지 · Alt: 가로",rx+16,531,11,muted);
    heading(L"구조 넣기 · 선택한 그림 영역",550);
    row(567,{{134,L"그림 영역",guides},{123,L"붙여넣기"},{124,L"파일로 채움"}});
    if(sidebarPage==0)row(620,{{186,L"구조 편집"},{113,L"파일 삽입"}});
    else row(620,{{186,L"구조 편집"},{113,L"파일 삽입"},{138,L"출판용 정돈"}});
 }
 g.Restore(rightState);clipHits(rightHits,rightClip,0,float(-rightOffset));
 if(wh-bottom<684)text(g,L"↕ 휠",rx+float(right-44),91,10,muted);
 fill(g,{0,float(wh-bottom),float(ww),float(bottom)},palette.chrome);rule(g,0,float(wh-bottom),float(ww),float(wh-bottom),border);text(g,status,14,float(wh-bottom+9),11,muted);Graphics out(dc);presentBuffer(out,*paintBuffer);}

} // namespace yd::editor

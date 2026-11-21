#include "ligand_spatial.h"
#include "lab_templates_data.h"
#include "cdxml.h"
#include "scene.h"
#include "win_editor_state.h"
#include "win_editor_window.h"
#include "win_editor_chrome.h"
#include "win_editor_commands.h"
#include "win_editor_shortcuts.h"
#include "win_editor_templates.h"
#include "win_editor_render.h"
#include "win_editor_files.h"
#include "win_gallery.h"
#include "win_dialogs.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace yd;
using namespace yd::editor;
static void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
static double segmentDistance(Vec point,Vec a,Vec b){Vec v=b-a;double ll=v.x*v.x+v.y*v.y;if(ll<1e-9)return (point-a).length();double t=std::clamp(((point-a).x*v.x+(point-a).y*v.y)/ll,0.,1.);return (point-a-v*t).length();}
static bool galleryDriven=false;static int favoriteCalls=0;
static void CALLBACK drivePage(HWND owner,UINT,UINT_PTR timer,DWORD){HWND dialog=nullptr;while((dialog=FindWindowExW(nullptr,dialog,L"YoungDrawTemplateGallery",nullptr)))if(GetWindow(dialog,GW_OWNER)==owner)break;if(!dialog)return;KillTimer(owner,timer);UpdateWindow(dialog);SendMessageW(dialog,WM_COMMAND,202,0);PostMessageW(dialog,WM_COMMAND,IDOK,0);}
static int inputMode=0;static bool inputFocusMoved=false;
static void CALLBACK driveGalleryInput(HWND owner,UINT,UINT_PTR timer,DWORD){
    HWND dialog=nullptr;while((dialog=FindWindowExW(nullptr,dialog,L"YoungDrawTemplateGallery",nullptr)))if(GetWindow(dialog,GW_OWNER)==owner)break;
    if(!dialog)return;KillTimer(owner,timer);
    if(inputMode==2){
        SetWindowPos(dialog,nullptr,0,0,800,640,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);UpdateWindow(dialog);
        auto search=GetDlgItem(dialog,500);SendMessageW(search,WM_KEYDOWN,VK_DOWN,0);inputFocusMoved=GetFocus()==dialog;
        SendMessageW(dialog,WM_KEYDOWN,VK_TAB,0);inputFocusMoved=inputFocusMoved&&GetFocus()==search;
        SendMessageW(search,WM_KEYDOWN,VK_DOWN,0);SendMessageW(dialog,WM_KEYDOWN,VK_DOWN,0);
    }else{
        UpdateWindow(dialog);for(int i=0;i<(inputMode==0?3:4);i++)SendMessageW(dialog,WM_MOUSEWHEEL,MAKEWPARAM(0,WORD(short(-30))),0);
    }
    PostMessageW(dialog,WM_COMMAND,IDOK,0);
}
static void CALLBACK driveGallery(HWND owner,UINT,UINT_PTR timer,DWORD){
    HWND dialog=nullptr;while((dialog=FindWindowExW(nullptr,dialog,L"YoungDrawTemplateGallery",nullptr)))if(GetWindow(dialog,GW_OWNER)==owner)break;
    if(!dialog)return;KillTimer(owner,timer);galleryDriven=true;
    SetDlgItemTextW(dialog,500,L"BQCN");SendMessageW(dialog,WM_COMMAND,410,0);
    check(IsWindow(dialog)&&favoriteCalls==1,"favorite does not close or recreate gallery");wchar_t search[100]{};GetDlgItemTextW(dialog,500,search,100);check(std::wstring(search)==L"BQCN","favorite preserves current search");
    SendMessageW(dialog,WM_COMMAND,410,0);check(favoriteCalls==2,"favorite can be toggled back without losing selection");PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);
}
static double rawMeasure(const std::string& s,double size,const std::string& font,int face){
    Bitmap bm(1,1,PixelFormat32bppARGB);Graphics g(&bm);FontFamily ff(ui::wide(font).c_str());Font f(&ff,float(size),(face&1?FontStyleBold:0)|(face&2?FontStyleItalic:0),UnitPixel);StringFormat fmt(StringFormat::GenericTypographic());RectF box;auto w=ui::wide(s);g.MeasureString(w.c_str(),-1,&f,PointF(0,0),&fmt,&box);return box.Width;
}
int main(){try{
    GdiplusStartupInput start;ULONG_PTR token;GdiplusStartup(&token,&start,nullptr);OleInitialize(nullptr);
    Document crossing;int a=crossing.addAtom({-50,0}),b=crossing.addAtom({50,0}),c=crossing.addAtom({-40,-40}),e=crossing.addAtom({-40,40});
    crossing.atom(a)->depth=-20;crossing.atom(b)->depth=80;crossing.atom(c)->depth=crossing.atom(e)->depth=5;
    int foreground=crossing.addBond(a,b),background=crossing.addBond(c,e);crossing.addBond(a,c);crossing.bond(foreground)->depthColor=crossing.bond(background)->depthColor="#000000";
    auto crosses=spatialCrossings(crossing);check(crosses.size()==1&&crosses[0].front==foreground,"crossing interpolates depth instead of comparing midpoint depth");
    shadeSpatial(crossing,crossing.allIds());check(crossing.bond(foreground)->color=="#000000"&&crossing.bond(background)->color=="#000000"&&crossing.bond(background)->depthRearColor!="#000000","visible skeleton remains original ink; gray is stored only as a local crossing accent");
    const auto visual=makeScene(crossing);
    auto stale=crossing;stale.bond(foreground)->color="#999999";const auto restoredVisual=makeScene(stale);for(const auto& p:restoredVisual.items)if(p.owner==foreground)check(p.color=="#000000","old documents with stale foreground gray render in original foreground ink");double nearDistance=1e9,farDistance=1e9;
    for(const auto& p:visual.items)if(p.type==Primitive::Line&&p.points.size()==2){auto distance=segmentDistance(crosses[0].point,p.points[0],p.points[1]);if(p.owner==foreground)nearDistance=std::min(nearDistance,distance);if(p.owner==background)farDistance=std::min(farDistance,distance);}
    check(nearDistance<1e-8&&farDistance>1,"only the rear stroke is cut; the foreground remains continuous");
    bool rearBlack=false,rearGray=false;for(const auto& p:visual.items)if(p.owner==background){rearBlack|=p.color=="#000000";rearGray|=p.color==crossing.bond(background)->depthRearColor;}check(rearBlack&&rearGray,"rear bond has only a short gray crossing accent, with the remaining visible stroke black");
    auto wedge=crossing;wedge.bond(background)->kind=BondKind::Wedge;auto wedgeScene=makeScene(wedge);bool blackWedge=false,grayWedge=false;
    auto inPolygon=[](const std::vector<Vec>& vertices,Vec p){bool inside=false;for(size_t i=0,j=vertices.size()-1;i<vertices.size();j=i++){auto a=vertices[i],b=vertices[j];if(((a.y>p.y)!=(b.y>p.y))&&(p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x))inside=!inside;}return inside;};
    for(const auto& p:wedgeScene.items)if(p.owner==background&&p.type==Primitive::Polygon){check(!inPolygon(p.points,crosses[0].point),"rear wedge cannot cover the foreground crossing");blackWedge|=p.color=="#000000";grayWedge|=p.color==wedge.bond(background)->depthRearColor;}check(blackWedge&&grayWedge,"rear wedge retains black visible regions and local gray accent");
    auto hashed=crossing;hashed.bond(background)->kind=BondKind::Hash;auto hashScene=makeScene(hashed);bool farHash=false;for(const auto& p:hashScene.items)if(p.owner==background&&p.type==Primitive::Line&&p.color=="#000000"&&(p.points[0]-crosses[0].point).length()>15)farHash=true;check(farHash,"rear crossing cuts only nearby hash strokes instead of erasing the whole hashed bond");
    writePreview(L"depth-crossing-corrected.png",crossing);
    auto xml=toCdxml(stale);check(xml.find("CrossingBonds=")!=std::string::npos&&xml.find(" Z=")!=std::string::npos,"ordinary CDXML publishes crossing and front-back order");
    auto annotation=xml.rfind("<annotation");xml.erase(annotation,xml.find("/>",annotation)-annotation+2);auto external=fromCdxml(xml);auto importedCrossings=spatialCrossings(external);check(external.bond(foreground)->color=="#000000","ordinary CDXML also repairs stale foreground gray");check(importedCrossings.size()==1&&importedCrossings[0].front==foreground,"ordinary CDXML keeps the same visible front after external-state removal");
    auto mirror=crossing;rotateSpatial(mirror,mirror.allIds(),0,180,0);check(spatialCrossings(mirror)[0].front==background,"opposite view reverses the actual crossing depth");
    size_t views=0,crossings=0;std::ofstream report("workflow-depth-audit.csv");report<<"template,views,crossings\n";
    for(const auto& item:ligandTemplates())if(item.spatial){size_t count=0;for(int x:{-60,0,60})for(int y=0;y<360;y+=15){auto d=item.drawing;rotateSpatial(d,d.allIds(),x,y,0);auto list=spatialCrossings(d);for(auto pair:list){const auto* front=d.bond(pair.front);check(front->color==front->depthColor,"every foreground crossing stays in original ink at every audited viewpoint");}count+=list.size();views++;}crossings+=count;report<<item.slug<<",72,"<<count<<"\n";}
    for(const auto& item:ligandTemplates())if(std::string(item.slug)=="bqcn-co-peroxo"||std::string(item.slug)=="mesip2-co-carbonyl"){auto d=item.drawing;rotateSpatial(d,d.allIds(),0,45,0);writePreview(ui::wide(std::string(item.slug)+"-paper45.png"),d);}
    size_t oldCount=0;for(const auto& item:labTemplates){auto d=fromNative(item.data);const auto scene=makeScene(d);check(std::isfinite(scene.bounds.w)&&std::isfinite(scene.bounds.h),"every existing template still renders");oldCount++;}check(oldCount==152&&ligandTemplates().size()==14,"all 166 templates retained");
    clearMeasurementCache();double raw=0,cached=0;auto before=std::chrono::steady_clock::now();for(int i=0;i<1200;i++)raw+=rawMeasure("Reaction coordinate",14,"Arial",1);auto middle=std::chrono::steady_clock::now();for(int i=0;i<1200;i++)cached+=measure("Reaction coordinate",14,"Arial",1);auto after=std::chrono::steady_clock::now();check(std::abs(raw-cached)<1e-5,"cached measurements exactly preserve the original GDI widths");auto stats=measurementStats();check(stats.misses==1&&stats.hits==1199,"repeated measurements use one GDI call");
    std::ofstream performance("workflow-performance.json");performance<<"{\"raw_text_ms\":"<<std::chrono::duration<double,std::milli>(middle-before).count()<<",\"cached_text_ms\":"<<std::chrono::duration<double,std::milli>(after-middle).count()<<",\"views\":"<<views<<",\"crossings\":"<<crossings<<"}";
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawWorkflowAudit";RegisterClassW(&wc);auto hwnd=CreateWindowExW(0,wc.lpszClassName,L"Workflow audit",WS_OVERLAPPEDWINDOW,0,0,1440,940,nullptr,nullptr,wc.hInstance,nullptr);win=hwnd;
    doc=ligandTemplates()[2].drawing;selection=doc.allIds();SendMessageW(hwnd,WM_COMMAND,153,0);for(const auto& b:doc.bonds)check(b.color=="#a2a9b2"&&b.depthColor==b.color,"explicit existing gray-color command remains effective on spatial diagrams");SendMessageW(hwnd,WM_COMMAND,154,0);for(const auto& b:doc.bonds)check(b.color=="#000000"&&b.depthColor==b.color,"explicit existing black-color command remains effective on spatial diagrams");doc={};selection.clear();history.clear();
    auto available=availableEditorCommands(hwnd);std::set<int> commands;for(const auto& item:available)commands.insert(item.id);size_t menuCount=0;
    std::function<void(HMENU)> inspect=[&](HMENU menu){for(int i=0;i<GetMenuItemCount(menu);i++){if(auto child=GetSubMenu(menu,i)){inspect(child);continue;}int id=int(GetMenuItemID(menu,i));if(id<=0)continue;menuCount++;check(isEditorCommand(id),"every menu command reaches the native message router");if(id!=230&&!(GetMenuState(menu,i,MF_BYPOSITION)&(MF_GRAYED|MF_DISABLED)))check(commands.count(id)>0,"every enabled menu function remains searchable");}};inspect(GetMenu(hwnd));
    check(commands.count(ligandSpatialCommand)&&commands.count(201)&&commands.count(150),"3D, drawing tools and document formatting are searchable");
    std::vector<ui::TemplateCard> cards;auto fragment=ligandTemplates()[0].drawing;cards.push_back({L"BQCN",L"리간드",fragment,makeScene(fragment),L"",L"",false});SetTimer(hwnd,919,10,driveGallery);
    ui::templateGallery(hwnd,cards,L"전체",[](Graphics&,const Scene&,RectF){},[&](size_t index,bool value){check(index==0,"favorite preserves selected card");favoriteCalls++;cards[index].favorite=value;});KillTimer(hwnd,919);check(galleryDriven&&favoriteCalls==2,"native gallery workflow completed");
    std::vector<ui::TemplateCard> pages;for(int i=0;i<12;i++)pages.push_back({L"Card "+std::to_wstring(i),L"리간드",fragment,makeScene(fragment),L"",L"",false});SetTimer(hwnd,920,10,drivePage);auto next=ui::templateGallery(hwnd,pages,L"전체",[](Graphics&,const Scene&,RectF){});KillTimer(hwnd,920);check(next&&next->index>0,"next page plus Enter inserts the visible page selection, never an off-page card");
    for(inputMode=0;inputMode<3;inputMode++){SetTimer(hwnd,921,10,driveGalleryInput);auto choice=ui::templateGallery(hwnd,pages,L"전체",[](Graphics&,const Scene&,RectF){});KillTimer(hwnd,921);check(choice.has_value(),"gallery input accepts a visible selection");if(inputMode==0)check(choice->index==0,"three quarter-wheel inputs do not prematurely turn a page");if(inputMode==1)check(choice->index>0,"four quarter-wheel inputs turn exactly one page and select a visible card");if(inputMode==2)check(choice->index==2&&inputFocusMoved,"two-column Down navigation and search/card focus transitions match the visible gallery");}
    doc={};selection.clear();history.clear();DestroyWindow(hwnd);win=nullptr;
    std::cout<<"Audited "<<menuCount<<" native menu entries, "<<available.size()<<" searchable commands, 166 templates, "<<views<<" viewpoints and "<<crossings<<" depth crossings.\n";OleUninitialize();GdiplusShutdown(token);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

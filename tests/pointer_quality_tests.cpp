#include "win_editor_state.h"
#include "win_editor_tabs.h"
#include "win_editor_pointer.h"
#include "win_editor_window.h"
#include "win_editor_files.h"
#include "win_editor_images.h"
#include "win_theme.h"
#include "cdxml.h"
#include "plot_data.h"
#include "win_editor_plot.h"
#include "win_editor_text.h"
#include "win_dialogs.h"
#include "win_editor_shortcuts.h"
#include "win_editor_structure.h"
#include "win_editor_commands.h"
#include "win_editor_canvas.h"
#include "win_editor_render.h"
#include <iostream>
#include <stdexcept>
using namespace yd;
using namespace yd::editor;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static bool pickerCancel=false,pickerDriven=false,stylePreviewSeen=false;
static int pickerMode=3;
static void CALLBACK driveGraphPicker(HWND owner,UINT,UINT_PTR timer,DWORD){
    HWND picker=nullptr;while((picker=FindWindowExW(nullptr,picker,L"YoungDrawGraphPicker",nullptr)))if(GetWindow(picker,GW_OWNER)==owner)break;
    if(!picker)return;KillTimer(owner,timer);pickerDriven=true;
    SendMessageW(picker,WM_COMMAND,100+pickerMode,0);SendMessageW(picker,WM_COMMAND,200,0);
    if(!doc.plots.empty())stylePreviewSeen=doc.plots[0].mode==pickerMode;
    SendMessageW(picker,WM_COMMAND,pickerCancel?IDCANCEL:IDOK,0);
}
static void CALLBACK driveRotation(HWND owner,UINT,UINT_PTR timer,DWORD){
    HWND dialog=nullptr;while((dialog=FindWindowExW(nullptr,dialog,L"YoungDrawForm",L"선택 구조 회전")))if(GetWindow(dialog,GW_OWNER)==owner)break;
    if(!dialog)return;KillTimer(owner,timer);SetDlgItemTextW(dialog,100,L"37.5");PostMessageW(dialog,WM_COMMAND,IDOK,0);
}
static void CALLBACK driveArrowColor(HWND owner,UINT,UINT_PTR timer,DWORD){
    HWND dialog=nullptr;
    while((dialog=FindWindowExW(nullptr,dialog,L"YoungDrawColorPicker",nullptr)))if(GetWindow(dialog,GW_OWNER)==owner)break;
    if(!dialog)return;
    KillTimer(owner,timer);
    SetDlgItemTextW(dialog,300,L"#123456");
    PostMessageW(dialog,WM_COMMAND,IDOK,0);
}
static void editorToolRegression(HWND hwnd){
    auto pointMessage=[&](UINT message,Vec p){
        SendMessageW(hwnd,message,0,MAKELPARAM(int(p.x),int(p.y)));
    };
    doc={};selection.clear();history.clear();zoom=2;origin={400,300};
    int keep=doc.addAtom({0,0}),remove=doc.addAtom({40,0});
    doc.addBond(keep,remove);
    changed();
    const auto original=toNative(doc);
    for(int toolCommand:{200,201,208,209,210,211,212,213,rotateToolCommand}){
        tool=Select;
        command(toolCommand);
        if(tool!=Select&&tool!=Rotate)selection={keep};
        pointMessage(WM_MOUSEMOVE,screen({40,0}));
        SendMessageW(hwnd,WM_KEYDOWN,VK_DELETE,0);
        check(!doc.atom(remove)&&doc.atom(keep),"Delete removes the pointed atom in every idle drawing mode");
        check(history.undo(doc)&&toNative(doc)==original&&!history.undo(doc),"Delete is a single undo step");
        changed();
    }
    selection.clear();
    pointMessage(WM_MOUSEMOVE,screen({20,0}));
    SendMessageW(hwnd,WM_KEYDOWN,VK_DELETE,0);
    check(doc.bonds.empty()&&doc.atoms.size()==2&&history.undo(doc),"Delete also targets a bond");
    changed();
    pointMessage(WM_MOUSEMOVE,screen({40,0}));
    SendMessageW(hwnd,WM_MOUSELEAVE,0,0);
    SendMessageW(hwnd,WM_KEYDOWN,VK_DELETE,0);
    check(doc.atom(remove)&&!history.undo(doc),"no hover deletion after the pointer leaves");

    tool=Select;
    SendMessageW(hwnd,WM_COMMAND,rotateToolCommand,0);
    check(tool==Rotate,"rotation button activates the rotation tool");
    const Vec start=screen({40,0}),finish=screen({20,20});
    const Vec viewport=origin;
    pointMessage(WM_LBUTTONDOWN,start);
    pointMessage(WM_MOUSEMOVE,finish);
    check((doc.atom(remove)->p-Vec{20,20}).length()<1e-8,"rotation follows pointer angle during drag");
    SendMessageW(hwnd,WM_KEYDOWN,VK_ESCAPE,0);
    pointMessage(WM_LBUTTONUP,finish);
    check(toNative(doc)==original&&selection.empty()&&!dragging&&!history.undo(doc),"Escape rolls back the in-progress rotation");
    pointMessage(WM_LBUTTONDOWN,start);
    pointMessage(WM_LBUTTONUP,finish);
    check((doc.atom(keep)->p-Vec{20,-20}).length()<1e-8&&zoom==2&&(origin-viewport).length()==0,"rotation moves the connected molecule about its center and keeps the viewport");
    check(history.undo(doc)&&toNative(doc)==original&&!history.undo(doc),"mouse-up commits one undoable rotation");
    SendMessageW(hwnd,WM_COMMAND,rotateToolCommand,0);
    check(tool==Select,"rotation button toggles off");

    doc={};selection.clear();history.clear();changed();
    SetTimer(hwnd,988,10,driveArrowColor);
    SendMessageW(hwnd,WM_COMMAND,arrowColorCommand,0);
    KillTimer(hwnd,988);
    check(arrowColor=="#123456"&&!history.undo(doc),"palette sets the color of new arrows without a document edit");
    command(212);
    const Vec from=screen({-50,40}),to=screen({30,65});
    pointMessage(WM_LBUTTONDOWN,from);
    pointMessage(WM_LBUTTONUP,to);
    check(doc.objects.size()==1&&doc.objects[0].head==2&&doc.objects[0].color=="#123456","new fishhook uses the palette color");
    doc.objects[0].color="#ff0000";
    Object arrow;arrow.id=doc.nextId++;arrow.head=1;arrow.color="#ff0000";arrow.points={{0,90},{40,90}};
    doc.objects.push_back(arrow);selection.push_back(arrow.id);
    history.clear();
    SetTimer(hwnd,988,10,driveArrowColor);
    SendMessageW(hwnd,WM_COMMAND,arrowColorCommand,0);
    KillTimer(hwnd,988);
    check(doc.objects[0].color=="#123456"&&doc.objects[1].color=="#123456","palette recolors both selected arrowhead types");
    const auto loaded=fromNative(toNative(doc));
    check(loaded.objects[0].head==2&&loaded.objects[0].color=="#123456"&&loaded.objects[1].color=="#123456","arrow colors and fishhook survive native serialization");
    check(history.undo(doc)&&doc.objects[0].color=="#ff0000"&&doc.objects[1].color=="#ff0000"&&!history.undo(doc),"batch arrow recolor is one undo step");
    arrowColor="#ff0000";
    selection.clear();tool=Select;
}
static void partialRotationRegression(HWND hwnd){
    auto point=[&](UINT message,Vec p){SendMessageW(hwnd,message,0,MAKELPARAM(int(p.x),int(p.y)));};
    BYTE keys[256]{};SetKeyboardState(keys);
    for(bool grouped:{false,true})for(int pick:{0,1,2}){
        doc={};selection.clear();history.clear();tool=Select;zoom=2;origin={400,300};
        int a=doc.addAtom({0,0}),b=doc.addAtom({40,0}),c=doc.addAtom({80,0}),d=doc.addAtom({120,30});
        int ab=doc.addBond(a,b);doc.addBond(b,c);doc.addBond(c,d,BondKind::Wedge);
        if(grouped)doc.groups.push_back({doc.nextId++,{a,b,c,d,ab}});
        const auto original=toNative(doc);const auto viewport=origin;
        selection=pick==1?std::vector<int>{ab}:std::vector<int>{a,b};
        const auto originalSelection=selection;
        changed();dirty=false;SendMessageW(hwnd,WM_COMMAND,rotateToolCommand,0);
        const Vec start=screen(pick==2?Vec{30,0}:Vec{40,0});
        const Vec finish=screen(pick==2?Vec{20,10}:Vec{20,20});
        point(WM_LBUTTONDOWN,start);point(WM_MOUSEMOVE,finish);
        check((doc.atom(a)->p-Vec{20,-20}).length()<1e-8&&(doc.atom(b)->p-Vec{20,20}).length()<1e-8,"direct rotation uses the selected fragment center");
        check((doc.atom(c)->p-Vec{80,0}).length()==0&&(doc.atom(d)->p-Vec{120,30}).length()==0,"direct rotation leaves connected unselected atoms fixed, including group members");
        check(!contains(selection,c)&&!contains(selection,d),"direct rotation does not expand the explicit selection to the molecule or group");
        SendMessageW(hwnd,WM_KEYDOWN,VK_ESCAPE,0);point(WM_LBUTTONUP,finish);
        check(toNative(doc)==original&&selection==originalSelection&&!history.undo(doc)&&!dirty,"partial rotation Escape restores the exact document and selection without an undo entry");
        point(WM_LBUTTONDOWN,start);point(WM_LBUTTONUP,finish);
        auto expected=fromNative(original);expected.atom(a)->p={20,-20};expected.atom(b)->p={20,20};
        check(toNative(doc)==toNative(expected),"partial rotation changes only selected atom coordinates; bonds, groups and other chemical data remain intact");
        check((origin-viewport).length()==0&&zoom==2,"partial rotation preserves the viewport");
        check(history.undo(doc)&&toNative(doc)==original&&!history.undo(doc),"partial rotation commits exactly one undo step");
        check(history.redo(doc)&&toNative(doc)==toNative(expected),"partial rotation supports redo");
        doc=fromNative(original);history.clear();selection=originalSelection;changed();dirty=false;
        point(WM_LBUTTONDOWN,start);point(WM_LBUTTONUP,start);
        check(toNative(doc)==original&&!dirty&&!history.undo(doc),"partial rotation without movement is not an edit");
        selection=originalSelection;
        point(WM_LBUTTONDOWN,start);point(WM_MOUSEMOVE,finish);SendMessageW(hwnd,WM_CAPTURECHANGED,0,0);point(WM_LBUTTONUP,finish);
        check(toNative(doc)==original&&selection==originalSelection&&!history.undo(doc),"capture loss cancels partial rotation");
    }
    selection.clear();history.clear();tool=Select;hoverAtom=hoverBond=0;
}
static void editingRegression(HWND hwnd){
    BYTE keys[256]{};SetKeyboardState(keys);showLayers=false;targetContainer=0;origin={400,300};zoom=2;
    Bitmap image(1440,940);Graphics graphics(&image);auto repaint=[&](){HDC dc=graphics.GetHDC();paint(dc);graphics.ReleaseHDC(dc);};
    doc={};int corner=doc.addAtom({0,0});
    for(double scale:{.5,1.,2.,4.}){zoom=scale;check(atomHit(world(screen({0,0})+Vec{13,13}))==corner,"square atom target includes corners outside the old circular area");check(!atomHit(world(screen({0,0})+Vec{15,0})),"square atom hit stops at its visible boundary independent of zoom");}
    zoom=1;int nearby=doc.addAtom({20,0});check(atomHit({11,0})==nearby&&atomHit({9,0})==corner,"overlapping square targets choose the nearest atom");
    zoom=2;selection.clear();tool=Select;mouseDown(screen({0,0})+Vec{13,13});check(contains(selection,corner),"actual mouse selection accepts the expanded square corner");cancel();
    for(int pass=0;pass<50;++pass){
        doc={};int a=doc.addAtom({0,0}),b=doc.addAtom({30,0});doc.addBond(a,b);tool=Erase;selection.clear();history.clear();
        const auto original=toNative(doc);mouseDown(screen({0,0}));
        check(toNative(doc)==original,"eraser waits for release so dragging can define a region");mouseUp(screen({0,0}));
        check(!doc.atom(a)&&!hoverAtom&&!hoverBond,"eraser clears deleted hover references before repaint");repaint();hotkey('1');
        check(history.undo(doc)&&toNative(doc)==original,"erase remains one undoable edit");changed();hoverAtom=a;doc.erase({a});repaint();changed();check(!hoverAtom,"renderer tolerates stale hover from direct document replacement too");
    }
    for(double scale:{.5,1.,2.,4.})for(bool reverse:{false,true}){
        zoom=scale;doc={};history.clear();dirty=false;selection.clear();tool=Erase;
        int a=doc.addAtom({0,0}),b=doc.addAtom({30,0}),outside=doc.addAtom({100,0});doc.addBond(a,b);
        doc.groups.push_back({doc.nextId++,{a,outside}});
        Object line;line.id=doc.nextId++;line.kind=ObjectKind::Line;line.points={{0,25},{30,25}};doc.objects.push_back(line);
        Object image;image.id=doc.nextId++;image.kind=ObjectKind::Image;image.points={{0,35},{20,50}};doc.objects.push_back(image);doc.imagesLocked=true;
        const auto original=toNative(doc);changed();dirty=false;
        Vec first=screen({-20,-20}),last=screen({50,70});if(reverse)std::swap(first,last);
        mouseDown(first);check(boxing&&toNative(doc)==original,"region erase starts without changing the document");
        mouse=last;repaint();check(toNative(doc)==original,"region preview does not delete contents");
        mouseUp(last);
        check(!doc.atom(a)&&!doc.atom(b)&&doc.atom(outside)&&doc.bonds.empty(),"region erases enclosed atoms and bonds without expanding a group beyond the rectangle");
        check(doc.objects.size()==1&&doc.objects[0].id==image.id,"region erases enclosed objects and preserves locked images");
        check(dirty&&!boxing&&selection.empty(),"region erase commits and clears interaction state");
        check(history.undo(doc)&&toNative(doc)==original&&!history.undo(doc),"entire region erasure has exactly one undo checkpoint");
        check(history.redo(doc)&&!doc.atom(a)&&doc.atom(outside),"region erasure supports redo");
        history.undo(doc);history.clear();dirty=false;changed();dirty=false;
        mouseDown(first);mouse=last;key(VK_ESCAPE);mouseUp(last);
        check(toNative(doc)==original&&!dirty&&!history.undo(doc),"Escape cancels region erasure without dirtying or undo history");
        mouseDown(first);SendMessageW(hwnd,WM_CAPTURECHANGED,0,0);mouseUp(last);
        check(toNative(doc)==original&&!boxing&&!history.undo(doc),"capture loss cancels region erasure");
        mouseDown(screen({150,80}));mouseUp(screen({200,110}));
        check(toNative(doc)==original&&!dirty&&!history.undo(doc),"empty region leaves the document and history unchanged");
    }
    zoom=2;doc={};history.clear();selection.clear();tool=Erase;
    int eraseStart=doc.addAtom({0,0}),eraseEnd=doc.addAtom({30,30});changed();
    mouseDown(screen({0,0}));mouseUp(screen({50,50}));
    check(!doc.atom(eraseStart)&&!doc.atom(eraseEnd),"region erasure can start directly on an atom");
    doc={};history.clear();Object partial;partial.id=doc.nextId++;partial.kind=ObjectKind::Line;partial.points={{0,0},{100,0}};doc.objects.push_back(partial);changed();dirty=false;
    mouseDown(screen({20,-20}));mouseUp(screen({70,20}));
    check(doc.objects.size()==1&&!dirty&&!history.undo(doc),"partially enclosed objects stay outside the deletion batch");
    mouseDown(screen({50,0}));mouseUp(screen({50,0})+Vec{2,1});
    check(doc.objects.empty()&&history.undo(doc)&&doc.objects.size()==1&&!history.undo(doc),"click erasure tolerates small pointer jitter and remains one undoable edit");
    doc={};int root=doc.addAtom({0,0}),end=doc.addAtom({30,0});doc.addBond(root,end);selection={end};hoverAtom=end;tool=Select;
    auto viewport=origin;double scale=zoom;command(163);check((origin-viewport).length()==0&&zoom==scale,"toolbar rotation preserves viewport");
    selection={end};SetTimer(hwnd,989,10,driveRotation);command(164);KillTimer(hwnd,989);check((origin-viewport).length()==0&&zoom==scale,"angle preview and apply preserve viewport");
    keys[VK_MENU]=0x80;SetKeyboardState(keys);key(VK_UP);keys[VK_MENU]=0;SetKeyboardState(keys);check((origin-viewport).length()==0&&zoom==scale,"keyboard rotation preserves viewport");
    doc={};root=doc.addAtom({0,0});end=doc.addAtom({30,0},8);doc.addBond(root,end);hoverAtom=end;selection={end};
    check(doc.hydrogens(end)==1,"terminal oxygen defaults to OH");history.clear();hotkey('o');check(doc.hydrogens(end)==0&&doc.atom(end)->explicitH==0,"O shortcut toggles OH to bare O");
    auto bare=toNative(doc);check(fromNative(bare).hydrogens(end)==0&&fromCdxml(toCdxml(doc)).hydrogens(end)==0,"bare oxygen survives saving");
    check(history.undo(doc)&&doc.hydrogens(end)==1,"bare oxygen toggle supports undo");command(308);check(doc.hydrogens(end)==0,"oxygen palette toggles OH to O too");command(308);check(doc.hydrogens(end)==1,"repeating oxygen restores automatic H");
    doc={};hoverAtom=hoverBond=0;tool=Select;hotkey('a');check(tool==Benzene,"a in blank space selects benzene tool");
    root=doc.addAtom({0,0});mouseDown(screen({0,0})+Vec{14,0});check(doc.atoms.size()==7&&doc.bonds.size()==7,"benzene tool attaches a substituent within expanded atom hit area");
    check(doc.component(root).size()==7,"expanded hit creates one connected phenyl substituent");
    for(double scale:{.5,1.,2.}){zoom=scale;doc={};root=doc.addAtom({0,0});end=doc.addAtom({30,0});doc.addBond(root,end);tool=Benzene;mouseDown(screen({15,0}));check(doc.atoms.size()==6&&doc.bonds.size()==6,"bond center keeps fused benzene accessible when enlarged atom targets overlap");}zoom=2;
    doc={};hoverAtom=hoverBond=0;tool=Benzene;mouseDown(screen({100,100}));check(doc.atoms.size()==6&&doc.bonds.size()==6,"blank canvas still draws standalone benzene");
    selection.clear();hoverAtom=hoverBond=0;tool=Select;
}
int main(int argc,char** argv){
    OleInitialize(nullptr);GdiplusStartupInput startup;ULONG_PTR token;GdiplusStartup(&token,&startup,nullptr);
    try{
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawPointerQuality";
        check(RegisterClassW(&wc)!=0,"register hidden test window");
        auto hwnd=CreateWindowExW(0,wc.lpszClassName,L"Quality",WS_OVERLAPPEDWINDOW,0,0,1440,940,nullptr,nullptr,wc.hInstance,nullptr);
        check(hwnd!=nullptr,"create hidden editor");
        if(argc==2&&std::string(argv[1])=="--partial-rotation-only"){
            partialRotationRegression(hwnd);
            DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();
            std::cout<<"PASS: selected-fragment direct rotation\n";return 0;
        }
        if(argc==4&&std::string(argv[1])=="--fit-profile-preview"){
            doc=fromNative(readFile(ui::wide(argv[2])));for(const auto& graph:doc.plots){size_t index=0;for(size_t i=0;i+1<graph.states.size();++i)if(graph.states[i].breakAfter){index=i;break;}compressPlotEnergyInterval(graph.id,index);break;}
            const auto prefix=ui::wide(argv[3]);writePreview(prefix+L".png",doc);writeFile(prefix+L".ydraw",toNative(doc));writeFile(prefix+L".cdxml",toCdxml(doc));writeFile(prefix+L".svg",toSvg(scene()));
            DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return 0;
        }

        BYTE initialKeys[256]{};check(SetKeyboardState(initialKeys)!=FALSE,"initialize the isolated pointer test keyboard state");
        doc={};int a=doc.addAtom({50,50}),b=doc.addAtom({80,50});doc.addBond(a,b);history.clear();dirty=false;selection={a};zoom=2;origin={300,200};tool=Select;
        auto before=toNative(doc);Vec start=screen(doc.atom(a)->p);
        mouseDown(start);mouseUp(start+Vec{24,18});
        check(doc.atom(a)->p.x==62&&doc.atom(a)->p.y==59&&doc.atom(b)->p.x==80,"mouse-up applies final coordinates without a preceding move event");
        check(history.undo(doc)&&toNative(doc)==before,"one drag creates exactly one reversible edit");
        check(!history.undo(doc),"drag does not create extra undo entries");
        dirty=false;selection={a};mouseDown(start);mouse=start+Vec{15,20};updateDrag();
        newDocumentTab();
        check(!dragging&&documents[0].document.atom(a)->p.x==50&&!documents[0].dirty,"new tab cancels uncommitted old-tab drag");
        check(switchDocumentTab(0),"switch back to original tab");
        selection={a};start=screen(doc.atom(a)->p);mouseDown(start);copyDrag=true;mouse=start+Vec{20,20};updateDrag();
        check(doc.atoms.size()==3,"Ctrl drag previews a copy");
        SendMessageW(hwnd,WM_CANCELMODE,0,0);
        check(doc.atoms.size()==2&&selection==std::vector<int>{a}&&!dragging,"cancel restores copy-drag selection and document");
        selection={a};mouseDown(start);mouse=start+Vec{20,20};updateDrag();SendMessageW(hwnd,WM_CAPTURECHANGED,0,0);
        check(toNative(doc)==before&&!dragging,"capture loss rolls back uncommitted edits");
        const auto path=std::filesystem::temp_directory_path()/(L"YoungDraw-quality-"+std::to_wstring(GetCurrentProcessId())+L".ydraw");
        filePath=path.wstring();dirty=true;check(save()&&!dirty&&fromNative(readFile(filePath)).atoms.size()==2,"legacy native document saves in place without Save As");std::filesystem::remove(path);
        zoom=2;Vec at{500,400},anchor=world(at);POINT screenPoint{500,400};ClientToScreen(hwnd,&screenPoint);
        SendMessageW(hwnd,WM_MOUSEWHEEL,MAKEWPARAM(0,240),MAKELPARAM(screenPoint.x,screenPoint.y));
        check(std::abs(zoom-2*1.15*1.15)<1e-8&&(world(at)-anchor).length()<1e-8,"multi-notch wheel keeps cursor anchor and applies full delta");
        double beforeZoom=zoom;SendMessageW(hwnd,WM_MOUSEWHEEL,0,MAKELPARAM(screenPoint.x,screenPoint.y));
        check(zoom==beforeZoom,"zero wheel delta does not zoom out");
        auto pickerBefore=toNative(doc);SetTimer(hwnd,990,10,driveGraphPicker);auto choice=ui::graphChoice(hwnd);
        check(pickerDriven&&choice&&choice->mode==3&&!choice->freeEnergy,"icon selection dialog returns selected shape and energy quantity");
        pickerDriven=false;pickerCancel=true;SetTimer(hwnd,990,10,driveGraphPicker);check(!ui::graphChoice(hwnd)&&pickerDriven&&toNative(doc)==pickerBefore,"canceling graph selection does not modify the document");
        {
            doc={};history.clear();zoom=2;origin={300,200};tool=Select;selection.clear();
            Object arrow;arrow.id=doc.nextId++;arrow.head=1;arrow.points={{50,50},{110,50}};
            doc.objects.push_back(arrow);
            const auto original=toNative(doc);
            const auto shaft=screen({80,50});
            mouseDown(shaft);mouseUp(shaft);
            check(selection==std::vector<int>{arrow.id},"click selects the straight arrow on the canvas");
            BYTE keys[256]{},shifted[256]{};GetKeyboardState(keys);
            memcpy(shifted,keys,sizeof(keys));shifted[VK_SHIFT]=0x80;
            SetKeyboardState(shifted);
            SendMessageW(hwnd,WM_CHAR,'+',0);
            check(doc.objects[0].width>arrow.width,"Shift+plus increases straight arrow stroke width");
            SendMessageW(hwnd,WM_CHAR,'_',0);
            SetKeyboardState(keys);
            check(std::abs(doc.objects[0].width-arrow.width)<1e-9,"Shift+minus decreases straight arrow stroke width");
            check(history.undo(doc)&&doc.objects[0].width>arrow.width&&history.undo(doc)&&toNative(doc)==original,
                  "both arrow width edits can be undone");
            const auto tip=screen(arrow.points.back());
            mouseDown(tip);mouseUp(tip+Vec{40,0});
            check((doc.objects[0].points.front()-arrow.points.front()).length()<1e-9&&
                  (doc.objects[0].points.back()-arrow.points.back()-Vec{20,0}).length()<1e-9,
                  "dragging the straight arrow tip lengthens the arrow with the tail fixed");
            check(history.undo(doc)&&toNative(doc)==original&&!history.undo(doc),"arrow length drag is a single reversible edit");
            mouseDown(tip);mouse=tip+Vec{-40,0};updateDrag();cancel();
            check(toNative(doc)==original&&!history.undo(doc),"cancelling an arrow handle drag restores its length");
        }
        doc={};auto graph=newEnergyPlot(0,false);graph.id=doc.nextId++;graph.p={100,100};doc.plots={graph};history.clear();zoom=1;origin={200,150};tool=Select;selection={graph.id};
        auto cleanGraph=toNative(doc);pendingEnergyGap=PendingEnergyGap{graph.id,0,1,0,""};cancel();
        check(!pendingEnergyGap&&toNative(doc)==cleanGraph,"canceling gap placement leaves the graph unchanged");
        pendingEnergyGap=PendingEnergyGap{graph.id,0,1,0,""};check(placeEnergyGap({700,500}),"click places pending gap");
        check(!pendingEnergyGap&&doc.plots[0].states[1].gapFrom==0&&scene().gapCaptions.size()==1,"gap exists only after placement click");
        auto placedOffset=doc.plots[0].states[1].gapOffset;auto caption=scene().gapCaptions.front();
        Vec captionCenter=screen({caption.bounds.x+caption.bounds.w/2,caption.bounds.y+caption.bounds.h/2});
        mouseDown(captionCenter);mouseUp(captionCenter+Vec{30,20});
        check((doc.plots[0].states[1].gapOffset-placedOffset-Vec{30,20}).length()<1e-5,"drag moves gap arrow horizontally and caption vertically");
        check(history.undo(doc)&&(doc.plots[0].states[1].gapOffset-placedOffset).length()<1e-5,"gap drag is one reversible edit");
        check(history.undo(doc)&&toNative(doc)==cleanGraph,"gap creation is one reversible edit");
        {const auto id=doc.plots[0].id;doc.plots[0].states[1].transitionState=false;history.clear();
            int oxygen=doc.addAtom({220,35},8);doc.plots[0].states[1].above={oxygen};
            auto target=plotStateAt(screen(doc.atom(oxygen)->p));check(target&&target->first==id&&target->second==1,"graph structure hit identifies its attached state");
            const auto original=toNative(doc);setPlotTransitionState(id,1,true);int brackets=0;bool dagger=false;
            for(const auto& item:scene().items)if(item.owner==id){brackets+=item.type==Primitive::Line&&item.points.size()==4;dagger|=item.type==Primitive::Text&&item.text=="‡";}
            check(brackets==2&&dagger,"designating a graph structure as TS immediately draws both brackets and dagger");
            check(history.undo(doc)&&toNative(doc)==original&&!history.undo(doc),"canvas TS designation is exactly one reversible edit");
            setInlineValue({id,6,0},"TS2");check(doc.plots[0].states[0].transitionState,"canvas name editing automatically marks TS names");
            setInlineValue({id,6,0},"R");check(!doc.plots[0].states[0].transitionState,"canvas renaming removes inferred TS designation");
            doc.plots[0].states[1].above.clear();doc.erase({oxygen});history.clear();dirty=false;
        }
        // A style edit may only change mode, including during preview and cancellation.
        auto& styled=doc.plots[0];styled.states[1].gapFrom=0;styled.states[1].transitionState=true;styled.states[0].breakAfter=true;
        auto second=styled.series[0];second.name="Alternative";second.color="#b44758";second.values={0,18,-8};styled.series.push_back(second);
        styled.crossings.push_back({"Crossing",0,0,1,.5,12});styled.legendOffset={21,35};styled.showYAxis=false;
        int structure=doc.addAtom({200,20},8);styled.states[1].above={structure};selection={styled.id};dirty=false;history.clear();
        const auto originalStyle=toNative(doc);
        pickerCancel=true;pickerMode=4;stylePreviewSeen=false;SetTimer(hwnd,990,10,driveGraphPicker);changePlotStyle(styled.id);
        check(stylePreviewSeen&&toNative(doc)==originalStyle&&!dirty&&!history.undo(doc),"style preview cancels without changing data, annotations, dirty state or undo history");
        const int plotId=doc.plots[0].id;
        for(int mode:{1,2,3,4,0}){
            auto expected=fromNative(originalStyle);expected.plots[0].mode=mode;pickerCancel=false;pickerMode=mode;stylePreviewSeen=false;
            SetTimer(hwnd,990,10,driveGraphPicker);changePlotStyle(plotId);
            check(stylePreviewSeen&&toNative(doc)==toNative(expected),"all style choices preserve energies, quantity, paths, colors, structures and annotations");
            if(mode!=0){check(history.undo(doc)&&toNative(doc)==originalStyle&&!history.undo(doc),"style change is exactly one undoable edit");}
            else check(!history.undo(doc),"applying the original style adds no undo entry");
            dirty=false;
        }
        {
            // Hosted Windows runners cap the native window to their small desktop.
            // These synthetic mouse coordinates target a fixed logical editor viewport.
            const int nativeWidth=ww,nativeHeight=wh;ww=std::max(1440,ww);wh=std::max(940,wh);
            doc={};auto graph=newEnergyPlot(0,false);graph.id=doc.nextId++;graph.p={100,100};doc.plots={graph};
            int atom=doc.addAtom({180,0},8);doc.plots[0].states[1].above={atom};int unrelated=doc.addAtom({1000,20},7);
            selection={graph.id};tool=Select;zoom=1;origin={200,150};history.clear();dirty=false;hoverAtom=unrelated;
            hotkey('+');check(std::abs(doc.plots[0].inkScale-1.15)<1e-9&&doc.atom(unrelated)->charge==0,"plus adjusts a selected graph before atom charge shortcuts");
            hotkey('-');check(std::abs(doc.plots[0].inkScale-1)<1e-9,"minus reverses graph weight without requiring Shift");
            check(history.undo(doc)&&std::abs(doc.plots[0].inkScale-1.15)<1e-9&&history.undo(doc)&&doc.plots[0].inkScale==1,"each graph weight step is independently undoable");
            hoverAtom=0;history.clear();const auto beforeResize=toNative(doc);auto axes=scene().plotAxes.at(graph.id);
            Vec handle=screen(axes.xEnd);mouseDown(handle);mouseUp(handle+Vec{80,0});auto grown=scene().plotAxes.at(graph.id);
            check(std::abs((grown.xEnd-grown.origin).length()-(axes.xEnd-axes.origin).length()-80)<1e-5,"horizontal axis handle resizes only the horizontal axis");
            check((grown.origin-axes.origin).length()<1e-5&&std::abs((grown.yEnd-grown.origin).length()-(axes.yEnd-axes.origin).length())<1e-5,"axis drag preserves origin and the other axis");
            check(history.undo(doc)&&toNative(doc)==beforeResize,"axis drag including structure movement undoes as one edit");
            axes=scene().plotAxes.at(graph.id);handle=screen(axes.yEnd);mouseDown(handle);mouse=handle+Vec{0,-65};updateDrag();cancel();
            check(toNative(doc)==beforeResize&&!history.undo(doc),"canceling an axis drag restores the graph and inserted structures");
            mouseDown(handle);mouseUp(handle+Vec{0,-65});grown=scene().plotAxes.at(graph.id);
            check(std::abs((grown.yEnd-grown.origin).length()-(axes.yEnd-axes.origin).length()-65)<1e-5,"vertical axis handle independently changes vertical axis length");
            ww=nativeWidth;wh=nativeHeight;
        }
        {
            auto darkness=[&](double scale){
                doc.plots[0].inkScale=scale;const auto s=scene();auto text=std::find_if(s.items.begin(),s.items.end(),[](const auto& item){return item.type==Primitive::Text&&item.textRole==6;});check(text!=s.items.end(),"graph has a text primitive for weight rendering");
                auto glyph=*text;glyph.fontSize=32;glyph.textStrokeWidth=32*.05*(scale-1);glyph.text="Axis";glyph.points={{10,10}};glyph.color="#000000";Scene isolated;isolated.items={glyph};
                Bitmap bitmap(140,70,PixelFormat32bppARGB);{Graphics graphics(&bitmap);graphics.Clear(Color::White);render(graphics,isolated);}
                long ink=0;for(int y=0;y<70;++y)for(int x=0;x<140;++x){Color pixel;bitmap.GetPixel(x,y,&pixel);ink+=255-pixel.GetR();}return ink;
            };
            const auto thin=darkness(.5),normal=darkness(1),thick=darkness(2);
            check(thin>0&&thin<normal&&normal<thick,"actual glyph ink decreases and increases independently of font size");
            doc.plots[0].inkScale=.5;writePreview(L"graph-weight-thin.png",doc);doc.plots[0].inkScale=1;writePreview(L"graph-weight-normal.png",doc);doc.plots[0].inkScale=2;writePreview(L"graph-weight-thick.png",doc);
        }
        {
            const int nativeWidth=ww,nativeHeight=wh;ww=std::max(1440,ww);wh=std::max(940,wh);
            doc={};auto graph=newEnergyPlot(0,true);graph.id=doc.nextId++;graph.p={50,30};graph.states[0].breakAfter=true;graph.states[0].breakScale=.5;graph.yMin=-20;graph.yMax=30;
            graph.stages=mechanismStageData({{"Reaction","0","100","14","1.4","0"}});doc.plots={graph};selection={graph.id};history.clear();zoom=1;origin={150,150};tool=Select;
            auto peak=[&](){const auto snapshot=scene();for(auto point:snapshot.plotSnapPoints.at(graph.id))if(point.state==1&&point.path==0&&point.edge==0)return screen(point.p);throw std::runtime_error("missing scaled peak");};
            BYTE neutralKeys[256]{};SetKeyboardState(neutralKeys);
            auto original=toNative(doc);auto start=peak();mouseDown(start);check(dragging,"scaled energy peak can be selected for dragging");mouse=start+Vec{0,-15};SetKeyboardState(neutralKeys);updateDrag();
            check(std::abs(peak().y-start.y+15)<1.5&&doc.plots[0].series[0].values[1]>20,"scaled peak follows the pointer while its interval endpoint changes");
            mouseUp(start+Vec{0,-15});check(history.undo(doc)&&toNative(doc)==original,"scaled energy drag is one reversible edit");
            start=peak();mouseDown(start);mouse=start+Vec{0,15};updateDrag();cancel();check(toNative(doc)==original&&!history.undo(doc),"cancel restores scaled peak data and display settings");
            writePreview(L"energy-break-scale-and-arrows.png",doc);
            ww=nativeWidth;wh=nativeHeight;
        }
        {
            doc={};auto graph=newEnergyPlot(0,true);graph.id=doc.nextId++;graph.width=1534.8;graph.height=398.28567;graph.yMin=-90;graph.yMax=50;graph.valueFontSize=23;graph.stages={{"Homolytic Cleavage Rearrangement",0,38,20},{"C-O bond Formation",40,77,20},{"Product Formation",78,100,20}};graph.states={{"A2",7.7},{"A2-TS",19.8},{"B2",-56.9},{"C2",-60.1},{"D2",-53.3},{"E2",-87.4}};graph.states[1].transitionState=true;
            graph.series[0].values={7.7,19.8,-56.9,-60.1,-53.3,-87.4};doc.plots={graph};
            Vec initialPoint;const auto initialScene=scene();for(auto point:initialScene.plotSnapPoints.at(graph.id))if(point.state==0&&point.edge==0)initialPoint=point.p;
            const int overlappingStructure=doc.addAtom(initialPoint,8);doc.plots[0].states[0].below={overlappingStructure};history.clear();const auto original=toNative(doc);
            writePreview(L"energy-wave-before.png",doc);compressPlotEnergyInterval(graph.id,1);
            check(doc.plots[0].states[1].breakStyle==1&&doc.plots[0].states[1].breakOmit==50&&doc.plots[0].states[1].breakUpperScale==3,"canvas command inserts a wave and compresses the empty energy band");
            const auto finalScene=scene();for(auto point:finalScene.plotSnapPoints.at(graph.id))if(point.state==0&&point.edge==0)check(doc.atom(overlappingStructure)->p.y>point.p.y+18,"attached structures are cleared away from the energy curve after fitting");
            check(!doc.plots[0].yMin&&!doc.plots[0].yMax,"compression clears stale manual bounds that flatten the actual peak");
            const auto fittedAxes=scene().plotAxes.at(graph.id);check((fittedAxes.yEnd-fittedAxes.origin).length()>=(fittedAxes.xEnd-fittedAxes.origin).length()*.45-1e-4,"wide energy figures gain enough vertical space for a visible peak");
            check(fittedAxes.valueFont>15,"explicit energy numbers remain readable after compression");
            writePreview(L"energy-wave-after.png",doc);check(history.undo(doc)&&toNative(doc)==original&&!history.undo(doc),"wave compression is one reversible edit");
        }
        editingRegression(hwnd);
        editorToolRegression(hwnd);
        partialRotationRegression(hwnd);
        DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();
        std::cout<<"PASS: hidden editor pointer, tab, save and zoom checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}

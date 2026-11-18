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
static void editingRegression(HWND hwnd){
    BYTE keys[256]{};SetKeyboardState(keys);showLayers=false;targetContainer=0;origin={400,300};zoom=2;
    Bitmap image(1440,940);Graphics graphics(&image);auto repaint=[&](){HDC dc=graphics.GetHDC();paint(dc);graphics.ReleaseHDC(dc);};
    for(int pass=0;pass<50;++pass){
        doc={};int a=doc.addAtom({0,0}),b=doc.addAtom({30,0});doc.addBond(a,b);tool=Erase;selection.clear();history.clear();
        const auto original=toNative(doc);mouseDown(screen({0,0}));
        check(!doc.atom(a)&&!hoverAtom&&!hoverBond,"eraser clears deleted hover references before repaint");repaint();hotkey('1');
        check(history.undo(doc)&&toNative(doc)==original,"erase remains one undoable edit");changed();hoverAtom=a;doc.erase({a});repaint();changed();check(!hoverAtom,"renderer tolerates stale hover from direct document replacement too");
    }
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
    doc={};root=doc.addAtom({0,0});end=doc.addAtom({30,0});doc.addBond(root,end);tool=Benzene;mouseDown(screen({15,0}));check(doc.atoms.size()==6&&doc.bonds.size()==6,"middle of bond still fuses benzene to existing edge");
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
        if(argc==4&&std::string(argv[1])=="--fit-profile-preview"){
            doc=fromNative(readFile(ui::wide(argv[2])));for(const auto& graph:doc.plots){size_t index=0;for(size_t i=0;i+1<graph.states.size();++i)if(graph.states[i].breakAfter){index=i;break;}compressPlotEnergyInterval(graph.id,index);break;}
            const auto prefix=ui::wide(argv[3]);writePreview(prefix+L".png",doc);writeFile(prefix+L".ydraw",toNative(doc));writeFile(prefix+L".cdxml",toCdxml(doc));writeFile(prefix+L".svg",toSvg(scene()));
            DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return 0;
        }

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
        DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();
        std::cout<<"PASS: hidden editor pointer, tab, save and zoom checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}

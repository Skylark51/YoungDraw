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
int main(){
    OleInitialize(nullptr);GdiplusStartupInput startup;ULONG_PTR token;GdiplusStartup(&token,&startup,nullptr);
    try{
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawPointerQuality";
        check(RegisterClassW(&wc)!=0,"register hidden test window");
        auto hwnd=CreateWindowExW(0,wc.lpszClassName,L"Quality",WS_OVERLAPPEDWINDOW,0,0,1440,940,nullptr,nullptr,wc.hInstance,nullptr);
        check(hwnd!=nullptr,"create hidden editor");
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
        DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();
        std::cout<<"PASS: hidden editor pointer, tab, save and zoom checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}

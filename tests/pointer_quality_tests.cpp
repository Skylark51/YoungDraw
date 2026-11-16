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
#include "win_dialogs.h"
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
        DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();
        std::cout<<"PASS: hidden editor pointer, tab, save and zoom checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}

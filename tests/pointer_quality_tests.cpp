#include "win_editor_state.h"
#include "win_editor_tabs.h"
#include "win_editor_pointer.h"
#include "win_editor_window.h"
#include "win_editor_files.h"
#include "win_editor_images.h"
#include "win_theme.h"
#include "cdxml.h"
#include <iostream>
#include <stdexcept>
using namespace yd;
using namespace yd::editor;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
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
        DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();
        std::cout<<"PASS: hidden editor pointer, tab, save and zoom checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}

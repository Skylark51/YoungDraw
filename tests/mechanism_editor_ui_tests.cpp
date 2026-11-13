#include "win_mechanism_editor.h"
#include "win_editor_state.h"
#include "mechanism_data.h"
#include "cdxml.h"
#include <commctrl.h>
#include <iostream>
#include <stdexcept>
using namespace yd;using namespace yd::editor;
namespace {
int phase=0,tick=0;std::string failure;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void command(HWND window,int id){SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),0);}
void CALLBACK drive(HWND owner,UINT,UINT_PTR timer,DWORD){
    HWND editor=nullptr;
    while((editor=FindWindowExW(nullptr,editor,L"YoungDrawMechanismEditor",nullptr))){DWORD pid=0;GetWindowThreadProcessId(editor,&pid);if(pid==GetCurrentProcessId()&&GetWindow(editor,GW_OWNER)==owner)break;}
    if(!editor)return;
    try{
        auto list=GetDlgItem(editor,20);
        if(tick==0){
            check(SendMessageW(list,LB_GETCOUNT,0,0)==3,"draft initially lists all three stages");
            SendMessageW(list,LB_SETCURSEL,0,0);SendMessageW(editor,WM_COMMAND,MAKEWPARAM(20,LBN_SELCHANGE),0);command(editor,33);
            check(SendMessageW(list,LB_GETCOUNT,0,0)==4,"first intermediate can branch in the editor");
            if(phase==1){KillTimer(owner,timer);PostMessageW(editor,WM_COMMAND,IDCANCEL,0);return;}
            SendDlgItemMessageW(editor,17,TBM_SETPOS,TRUE,220);SendMessageW(editor,WM_HSCROLL,TB_LINEUP,(LPARAM)GetDlgItem(editor,17));SendMessageW(editor,WM_HSCROLL,TB_ENDTRACK,(LPARAM)GetDlgItem(editor,17));
            SetDlgItemTextW(editor,13,L"eV");SendMessageW(editor,WM_COMMAND,MAKEWPARAM(13,CBN_EDITCHANGE),0);
            command(editor,42);command(editor,43);command(editor,40);command(editor,40);
            SendMessageW(list,LB_SETCURSEL,3,0);SendMessageW(editor,WM_COMMAND,MAKEWPARAM(20,LBN_SELCHANGE),0);
            PostMessageW(editor,WM_KEYDOWN,VK_DELETE,0);tick=1;return;
        }
        check(SendMessageW(list,LB_GETCOUNT,0,0)==3,"Delete removes just the selected stage");command(editor,42);
        check(SendMessageW(list,LB_GETCOUNT,0,0)==4,"local undo restores a removed stage");
        KillTimer(owner,timer);PostMessageW(editor,WM_COMMAND,IDOK,0);
    }catch(const std::exception& e){failure=e.what();KillTimer(owner,timer);PostMessageW(editor,WM_CLOSE,0,0);}
}
}
int main(){
    ULONG_PTR token=0;GdiplusStartupInput start;GdiplusStartup(&token,&start,nullptr);OleInitialize(nullptr);
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_BAR_CLASSES};InitCommonControlsEx(&common);
    HWND owner=CreateWindowExW(0,L"STATIC",L"Mechanism editor regression",WS_OVERLAPPEDWINDOW,0,0,900,750,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    int result=0;
    try{
        auto source=mechanismDemo();auto before=toNative(source);
        SetTimer(owner,1,20,drive);auto applied=mechanismEditor(owner,source,source.reactions[0].id,&source);
        check(failure.empty(),failure.c_str());check(applied.has_value(),"draft can be applied");
        auto& r=applied->reactions[0];check(r.slots.size()==4&&r.links.size()==3&&r.links.back().from==0,"branch targets first stage and keeps original connections");
        check(r.arrowLength==220&&r.unit=="eV","slider and unit settings survive apply");check(toNative(source)==before,"editor never mutates its source document");
        phase=1;tick=0;SetTimer(owner,1,20,drive);auto cancelled=mechanismEditor(owner,source,source.reactions[0].id,&source);
        check(!cancelled&&toNative(source)==before,"Cancel discards all draft mutations");
        std::cout<<"PASS: isolated mechanism draft, early branch, slider, units, Delete, undo/redo and cancellation\n";
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';result=1;}
    KillTimer(owner,1);DestroyWindow(owner);GdiplusShutdown(token);OleUninitialize();return result;
}

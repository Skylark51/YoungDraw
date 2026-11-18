#include "win_editor_state.h"
#include "win_editor_tabs.h"
#include "win_editor_text.h"
#include "win_editor_commands.h"
#include "win_editor_window.h"
#include "win_editor_images.h"
#include "win_theme.h"
#include "cdxml.h"
#include <iostream>

using namespace yd;
using namespace yd::editor;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void shortcut(HWND hwnd,WPARAM key,bool shift=false,bool alt=false){
    BYTE previous[256]{},keys[256]{};GetKeyboardState(previous);
    keys[VK_CONTROL]=0x80;keys[VK_SHIFT]=shift?0x80:0;keys[VK_MENU]=alt?0x80:0;
    SetKeyboardState(keys);SendMessageW(hwnd,alt?WM_SYSKEYDOWN:WM_KEYDOWN,key,0);SetKeyboardState(previous);
}
static int closeAnswer=IDNO;
static void CALLBACK answerClose(HWND owner,UINT,UINT_PTR timer,DWORD){
    auto popup=GetLastActivePopup(owner);
    if(popup==owner)return;
    KillTimer(owner,timer);PostMessageW(popup,WM_COMMAND,closeAnswer,0);
}
int main(){
    OleInitialize(nullptr);Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;
    Gdiplus::GdiplusStartup(&token,&input,nullptr);int result=0;HWND hwnd=nullptr;
    try{
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawShortcutRegression";
        check(RegisterClassW(&wc)!=0,"register shortcut regression window");
        hwnd=CreateWindowExW(0,wc.lpszClassName,L"Shortcuts",WS_OVERLAPPEDWINDOW,0,0,1440,940,nullptr,nullptr,wc.hInstance,nullptr);
        check(hwnd&&documentTabs,"create document tabs");
        const auto initial=toNative(doc);
        shortcut(hwnd,'T',true);
        check(documents.size()==1&&toNative(doc)==initial,"empty reopen history leaves document intact");
        int atom=doc.addAtom({30,40},8);history.checkpoint(doc);doc.atom(atom)->charge=1;
        dirty=true;selection={atom};zoom=2.25;origin={240,180};tool=Double;
        const auto unsaved=toNative(doc);
        shortcut(hwnd,'T');
        check(documents.size()==2&&doc.atoms.empty()&&!dirty,"Ctrl+T opens a clean new tab");
        check(documents[0].dirty&&toNative(documents[0].document)==unsaved,"Ctrl+T preserves unsaved document");
        doc.addAtom({0,0},7);filePath=L"shortcut-saved.cdxml";dirty=false;
        const auto saved=toNative(doc);
        shortcut(hwnd,'W');
        check(documents.size()==1&&toNative(doc)==unsaved,"Ctrl+W closes current tab");
        closeAnswer=IDCANCEL;SetTimer(hwnd,990,10,answerClose);shortcut(hwnd,'W');KillTimer(hwnd,990);
        check(documents.size()==1&&dirty&&toNative(doc)==unsaved,"cancelled close preserves document");
        shortcut(hwnd,'T',true);
        check(documents.size()==2&&toNative(doc)==saved&&filePath==L"shortcut-saved.cdxml"&&!dirty,
              "Ctrl+Shift+T restores last closed tab, not a cancelled close");
        shortcut(hwnd,'T',true);
        check(documents.size()==2,"reopening consumes history without duplicating tabs");
        // Close a saved tab, then the unsaved final tab: restore them in reverse order.
        shortcut(hwnd,'W');closeAnswer=IDNO;SetTimer(hwnd,990,10,answerClose);shortcut(hwnd,'W');KillTimer(hwnd,990);
        check(documents.size()==1&&doc.atoms.empty()&&!dirty,"closing final tab leaves clean document");
        shortcut(hwnd,'T',true);
        check(documents.size()==2&&toNative(doc)==unsaved&&dirty&&filePath.empty(),"reopen restores discarded unsaved contents");
        check(selection==std::vector<int>{atom}&&zoom==2.25&&origin.x==240&&origin.y==180&&tool==Double,
              "reopen restores selection, view and drawing tool");
        check(history.undo(doc)&&doc.atom(atom)->charge==0,"reopen restores undo history");
        check(history.redo(doc)&&toNative(doc)==unsaved,"reopen restores redo history");
        shortcut(hwnd,'T',true);
        check(documents.size()==3&&toNative(doc)==saved,"repeated reopen follows reverse close order");
        shortcut(hwnd,'T',false,true);shortcut(hwnd,'T',true,true);
        check(documents.size()==3&&toNative(doc)==saved,"Ctrl+Alt+T combinations do not open tabs");
        isolatedStructureEditing=true;shortcut(hwnd,'T');shortcut(hwnd,'T',true);isolatedStructureEditing=false;
        check(documents.size()==3&&toNative(doc)==saved,"independent structure editor cannot switch documents");
        // Text editing must commit before either document shortcut changes tabs.
        tool=Text;beginInline({}, {400,300});check(inlineEdit!=nullptr,"start inline text");
        SetWindowTextW(inlineEdit,L"Committed text");shortcut(inlineEdit,'T');
        check(!inlineEdit&&documents.size()==4&&documents[2].document.objects.back().text=="Committed text",
              "Ctrl+T commits inline text and opens new tab");
        shortcut(hwnd,'W');
        tool=Text;beginInline({}, {500,350});check(inlineEdit!=nullptr,"start second inline text");
        SetWindowTextW(inlineEdit,L"Before reopening");shortcut(inlineEdit,'T',true);
        check(!inlineEdit&&documents.size()==4&&documents[2].document.objects.back().text=="Before reopening",
              "Ctrl+Shift+T commits inline text and restores closed tab");
        check(GetMenuState(GetMenu(hwnd),reopenClosedDocumentCommand,MF_BYCOMMAND)!=UINT(-1),"file menu exposes reopen command");
        shortcut(hwnd,'W');SendMessageW(hwnd,WM_COMMAND,reopenClosedDocumentCommand,0);
        check(documents.size()==4&&doc.atoms.empty(),"reopen menu uses the same command as the shortcut");
        for(int i=0;i<21;++i){
            newDocumentTab();int marker=doc.addAtom({0,0});doc.atom(marker)->charge=i;dirty=false;
            shortcut(hwnd,'W');
        }
        for(int i=20;i>=1;--i){
            shortcut(hwnd,'T',true);
            check(doc.atoms.size()==1&&doc.atoms[0].charge==i,"bounded history preserves reverse close order");
        }
        auto count=documents.size();shortcut(hwnd,'T',true);
        check(documents.size()==count,"only twenty most recent closed documents are retained");
        std::cout<<"Document shortcut regression tests passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    if(hwnd)DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();
    Gdiplus::GdiplusShutdown(token);OleUninitialize();return result;
}

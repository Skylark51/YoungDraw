#include "win_editor_state.h"
#include "win_editor_window.h"
#include "win_editor_shortcuts.h"
#include "win_editor_canvas.h"
#include "win_editor_chrome.h"
#include "win_editor_commands.h"
#include "win_editor_reaction.h"
#include "win_editor_pointer.h"
#include "win_editor_render.h"
#include "win_theme.h"
#include "win_dialogs.h"
#include "cdxml.h"
#include <iostream>
#include <stdexcept>

using namespace yd;
using namespace yd::editor;
namespace {
void check(bool result,const char* text){if(!result)throw std::runtime_error(text);}
int themeClose=IDCANCEL,phase=0;
bool formSeen=false,customPreview=false;
bool toolMenuSeen=false;
void CALLBACK dismissToolMenu(HWND owner,UINT,UINT_PTR timer,DWORD){
 if(!FindWindowW(L"#32768",nullptr))return;
 toolMenuSeen=true;KillTimer(owner,timer);EndMenu();
}
HWND owned(const wchar_t* type,HWND owner){
 HWND found=nullptr;
 while((found=FindWindowExW(nullptr,found,type,nullptr))){
  DWORD process=0;GetWindowThreadProcessId(found,&process);
  if(process==GetCurrentProcessId()&&GetWindow(found,GW_OWNER)==owner)return found;
 }
 return nullptr;
}
void CALLBACK themeDriver(HWND owner,UINT,UINT_PTR timer,DWORD){try{
 auto picker=owned(L"YoungDrawAppearance",owner);if(!picker)return;
 if(phase==0){
  phase=1;
  SendMessageW(picker,WM_COMMAND,400,0);
  customPreview=appearance::settings().theme==7&&appearance::settings().colors[9]==0x123456;
  SendMessageW(picker,WM_COMMAND,themeClose,0);
 }else if(phase==1){
  auto form=owned(L"YoungDrawForm",picker);if(!form)return;
  KillTimer(owner,timer);phase=2;formSeen=true;
  SetDlgItemTextW(form,100,L"My workspace");
  SetDlgItemTextW(form,101,L"#123456");
  SetDlgItemTextW(form,102,L"#eef4ff");
  SetDlgItemTextW(form,112,L"34");
  SetDlgItemTextW(form,113,L"27");
  SetDlgItemTextW(form,114,L"65");
  SetDlgItemTextW(form,115,L"editor-background.png");
  PostMessageW(form,WM_COMMAND,IDOK,0);
 }
}catch(const std::exception& e){std::cerr<<"Theme driver exception: "<<e.what()<<std::endl;ExitProcess(10);}
}
void editTheme(HWND hwnd,int close){
 phase=0;formSeen=customPreview=false;themeClose=close;
 SetTimer(hwnd,998,10,themeDriver);
 command(184);
 KillTimer(hwnd,998);check(formSeen&&customPreview,"custom theme editor previews actual entered colors");
}
RectF hit(int id){for(const auto& h:hits)if(h.id==id)return h.box;throw std::runtime_error("missing sidebar tool");}
void snapshot(HWND hwnd){
 HDC dc=GetDC(hwnd);paint(dc);ReleaseDC(hwnd,dc);
 CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
 check(paintBuffer->Save(L"editor-polish.png",&png,nullptr)==Ok,"save editor screenshot");
}
}
int main(){std::cerr<<std::unitbuf;std::cout<<std::unitbuf;
 OleInitialize(nullptr);GdiplusStartupInput start;ULONG_PTR token;GdiplusStartup(&token,&start,nullptr);
 HKEY sandbox=nullptr;
 const auto key=L"Software\\YoungDraw-EditorPolish-"+std::to_wstring(GetCurrentProcessId());
 RegCreateKeyExW(HKEY_CURRENT_USER,key.c_str(),0,nullptr,REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,nullptr,&sandbox,nullptr);
 check(sandbox&&RegOverridePredefKey(HKEY_CURRENT_USER,sandbox)==ERROR_SUCCESS,"isolate appearance settings");
 int result=0;HWND testWindow=nullptr;
 try{
  WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawPolishTest";RegisterClassW(&wc);
  auto hwnd=CreateWindowW(wc.lpszClassName,L"Editor",WS_OVERLAPPEDWINDOW,0,0,1440,940,nullptr,nullptr,wc.hInstance,nullptr);
  check(hwnd!=nullptr,"create isolated editor");testWindow=hwnd;
  doc={};const int a=doc.addAtom({0,0}),b=doc.addAtom({30,0}),c=doc.addAtom({60,0}),d=doc.addAtom({90,0});
  doc.addBond(a,b);doc.addBond(b,c);doc.addBond(c,d);selection={a,b};
  const auto original=toNative(doc);const auto selected=selection;
  for(int commandId:{160,161,162,163}){
   doc=fromNative(original);selection=selected;history.clear();
   command(commandId);
   check(doc.atom(c)->p.x==60&&doc.atom(d)->p.x==90&&doc.atom(c)->p.y==0&&doc.atom(d)->p.y==0,"toolbar transformations leave connected unselected atoms fixed");
   check(!contains(selection,c)&&!contains(selection,d),"toolbar does not expand explicit selection");
   check(history.undo(doc)&&toNative(doc)==original,"toolbar edit can be undone exactly");
  }
  doc=fromNative(original);selection=selected;history.clear();
  BYTE keys[256]{};keys[VK_MENU]=0x80;SetKeyboardState(keys);yd::editor::key(VK_UP);keys[VK_MENU]=0;SetKeyboardState(keys);
  check(doc.atom(c)->p.x==60&&doc.atom(c)->p.y==0&&doc.atom(d)->p.x==90,"keyboard rotation uses the same selected fragment");
  check(history.undo(doc)&&toNative(doc)==original,"keyboard rotation preserves one undo step");
  doc=fromNative(original);selection=selected;history.clear();origin={left+200.,top+200.};zoom=2;
  command(rotateToolCommand);mouseDown(screen(doc.atom(c)->p));mouse=screen({15,45});mouseUp(mouse);
  check(doc.atom(c)->p.x==60&&doc.atom(c)->p.y==0&&doc.atom(d)->p.x==90&&std::abs(doc.atom(a)->p.y)>1,"mouse rotation over a connected unselected atom rotates only the explicit selection");
  check(history.undo(doc)&&toNative(doc)==original,"direct rotation is exactly undoable");
  {
   doc={};doc.ring({},6,true);const int root=doc.atoms[0].id,outside=doc.addAtom({100,0}),connector=doc.addBond(root,outside);
   Reaction reaction;reaction.id=doc.nextId++;reaction.slots={{"Reactant",doc.allIds()}};doc.reactions.push_back(reaction);
   selection={reaction.id};targetContainer=reaction.id;targetSlot=0;history.clear();const auto before=toNative(doc);
   keys[VK_CONTROL]=keys[VK_SHIFT]=0x80;SetKeyboardState(keys);yd::editor::key('P');keys[VK_CONTROL]=keys[VK_SHIFT]=0;SetKeyboardState(keys);
   check(doc.atoms.size()==2&&doc.atom(root)->label=="Ph"&&doc.bond(connector),"Ctrl Shift P resolves a selected mechanism structure slot");
   check(history.undo(doc)&&toNative(doc)==before,"phenyl shortcut has one exact undo step");
   selection={connector};targetContainer=0;command(166);check(doc.atoms.size()==2,"connector-only selection invokes phenyl conversion");
  }
  doc=fromNative(original);selection=selected;history.clear();targetContainer=0;
  selection={a};auto menu=canvasContextMenu(a);
  check(GetMenuState(menu,molecularAnnotationsCommand,MF_BYCOMMAND)!=UINT(-1),"selected atom context menu offers molecular annotations");DestroyMenu(menu);
  snapshot(hwnd);
  check(hit(201).X==hit(202).X&&hit(202).X==hit(203).X,"single double triple bonds share a column");
  check(hit(204).X==hit(205).X,"wedge and hash share a column");
  check(hit(184).X>hit(copyPngCommand).X&&hit(184).Y==hit(copyPngCommand).Y,"theme icon follows image copy in the same toolbar row");
  for(int removed:{206,219,interactionToolCommand})check(std::none_of(hits.begin(),hits.end(),[&](const Hit& h){return h.id==removed;}),"redundant tools are absent from the sidebar");
  check(beginToolHold(218,{20,20})&&finishToolHold({20,20})&&tool==Orbital,"short orbital press selects drawing");
  check(beginToolHold(dottedBondCommand,{20,20})&&finishToolHold({20,20})&&tool==DottedBond,"short dotted tool press selects dotted bond");
  const auto beforeMenu=toNative(doc);ShowWindow(hwnd,SW_SHOWNOACTIVATE);
  SetTimer(hwnd,997,10,dismissToolMenu);beginToolHold(218,{20,20});showHeldToolOptions();KillTimer(hwnd,997);
  check(toolMenuSeen&&toNative(doc)==beforeMenu&&!drawing,"long orbital press opens options and cancelling does not draw");
  {Bitmap image(8,8);Graphics g(&image);g.Clear(Color(255,24,39,58));CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};check(image.Save(L"editor-background.png",&png,nullptr)==Ok,"create custom background fixture");}
  const auto before=appearance::settings();const auto native=toNative(doc);
  editTheme(hwnd,IDCANCEL);
  auto after=appearance::settings();
  check(after.theme==before.theme&&after.colors==before.colors&&after.customName==before.customName&&after.patternSpacing==before.patternSpacing,"cancel restores the entire custom palette");
  editTheme(hwnd,IDOK);appearance::load();after=appearance::settings();
  check(after.theme==7&&after.colors[9]==0x123456&&after.colors[10]==0xeef4ff&&after.customName==L"My workspace"&&after.patternSpacing==34&&after.patternOpacity==27&&after.imageOpacity==65&&after.backgroundImage==L"editor-background.png","custom palette round trips through settings");
  check(toNative(doc)==native,"theme editing preserves molecular data");
  check(appearance::palette().darkCanvas,"custom canvas brightness controls preview ink");
  snapshot(hwnd);DestroyWindow(hwnd);
  std::cout<<"PASS: sidebar, selected-fragment transforms, annotation context menu, custom theme preview/cancel/save/reload\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
 if(testWindow&&IsWindow(testWindow))DestroyWindow(testWindow);
 RegOverridePredefKey(HKEY_CURRENT_USER,nullptr);RegCloseKey(sandbox);RegDeleteTreeW(HKEY_CURRENT_USER,key.c_str());
 appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return result;
}

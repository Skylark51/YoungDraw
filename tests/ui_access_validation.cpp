// Real native windows, rendered only on scripts/test-isolated.ps1's desktop.
// Requested DPI is a synthetic WM_DPICHANGED probe, NEVER a physical monitor claim.
#include "win_editor_state.h"
#include "win_editor_window.h"
#include "win_editor_chrome.h"
#include "win_editor_canvas.h"
#include "win_editor_shortcuts.h"
#include "win_editor_commands.h"
#include "win_editor_files.h"
#include "win_editor_templates.h"
#include "win_editor_render.h"
#include "win_mechanism_editor.h"
#include "win_plot_manager.h"
#include "win_projects.h"
#include "win_theme.h"
#include "win_dialogs.h"
#include "mechanism_data.h"
#include "plot_data.h"
#include "cdxml.h"
#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <windowsx.h>
using namespace yd;
using namespace yd::editor;
namespace {
namespace fs=std::filesystem;
fs::path output;
std::ofstream manifest,menus,commands,hitsFile,controls;
int viewportWidth=1440,viewportHeight=900,requestedDpi=96;
bool after=true,capturing=false,modalSeen=false;
std::string modalName,failure;
std::wstring modalClass;
int modalTicks=0;
HWND pendingDialog=nullptr;
int readyTicks=0;
struct RegistrySandbox{
    HKEY key=nullptr;std::wstring path;
    RegistrySandbox(){
        path=L"Software\\YoungDraw-UI-validation-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
        checkStatus(RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&key,nullptr));
        const auto result=RegOverridePredefKey(HKEY_CURRENT_USER,key);if(result!=ERROR_SUCCESS){RegCloseKey(key);key=nullptr;RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str());checkStatus(result);}
    }
    static void checkStatus(LSTATUS result){if(result!=ERROR_SUCCESS)throw std::runtime_error("Cannot isolate test registry; refusing real user preferences");}
    ~RegistrySandbox(){if(key){RegOverridePredefKey(HKEY_CURRENT_USER,nullptr);RegCloseKey(key);RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str());}}
};
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
std::wstring env(const wchar_t* key){wchar_t value[32768]{};GetEnvironmentVariableW(key,value,DWORD(std::size(value)));return value;}
std::string csv(const std::string& s){std::string r="\"";for(char c:s){if(c=='\"')r+='\"';r+=c;}return r+'\"';}
void isolated(){wchar_t name[256]{};DWORD needed=0;check(GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,name,sizeof(name),&needed)!=FALSE,"cannot verify desktop");check(std::wstring(name).find(L"YoungDrawTests-")==0,"UI validation requires scripts/test-isolated.ps1; refusing user desktop");}
void pump(){MSG m{};int n=0;while(n++<200&&PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message!=WM_QUIT){TranslateMessage(&m);DispatchMessageW(&m);}}}
void click(HWND h,int id){SendMessageW(h,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),LPARAM(GetDlgItem(h,id)));}
void resize(HWND h){
    SetWindowPos(h,nullptr,0,0,viewportWidth,viewportHeight,SWP_NOZORDER|SWP_NOACTIVATE);
    RECT suggestion{};GetWindowRect(h,&suggestion);
    SendMessageW(h,WM_DPICHANGED,MAKEWPARAM(requestedDpi,requestedDpi),LPARAM(&suggestion));
    RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
}
std::string key(const std::string& surface){return surface+"-"+std::to_string(viewportWidth)+"x"+std::to_string(viewportHeight)+"-requested"+std::to_string(requestedDpi);}
void capture(HWND h,const std::string& surface){
    check(h&&IsWindow(h),"capture target exists");
    RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
    RECT rect{};GetClientRect(h,&rect);int w=rect.right,hh=rect.bottom;
    check(w>0&&hh>0,"capture has nonempty bounds");
    Gdiplus::Bitmap bitmap(w,hh,PixelFormat32bppARGB);
    std::string method="PrintWindow-client";
    {Gdiplus::Graphics graphics(&bitmap);graphics.Clear(Gdiplus::Color::Magenta);auto dc=graphics.GetHDC();
     // The editor's paint handler uses a backbuffer; capture that actual paint route.
     if(h==win){paint(dc,true);method="editor-paint-handler";}
     else check(PrintWindow(h,dc,PW_CLIENTONLY)!=FALSE,"PrintWindow failed");
     graphics.ReleaseHDC(dc);}
    // PrintWindow cannot guarantee nonblank output; retain an objective pixel check.
    Gdiplus::Color first;bitmap.GetPixel(0,0,&first);int distinct=0;
    for(int y=5;y<hh;y+=17)for(int x=5;x<w;x+=17){Gdiplus::Color pixel;bitmap.GetPixel(x,y,&pixel);if(pixel.GetValue()!=first.GetValue())++distinct;}
    auto name=key(surface)+".png";const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    check(bitmap.Save((output/ui::wide(name)).c_str(),&png)==Gdiplus::Ok,"cannot save captured UI");
    const auto actualDpi=GetDpiForWindow(h);
    manifest<<csv(name)<<','<<csv(surface)<<','<<viewportWidth<<','<<viewportHeight<<','<<w<<','<<hh<<','<<requestedDpi<<','<<actualDpi<<','<<csv(method)<<",synthetic-WM_DPICHANGED,"<<distinct<<'\n';manifest.flush();
    check(distinct>20,"capture is blank or uniform (diagnostic PNG retained)");
    for(HWND child=GetWindow(h,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
        if(!IsWindowVisible(child))continue;
        RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,h,reinterpret_cast<POINT*>(&r),2);
        wchar_t klass[100]{},label[512]{};GetClassNameW(child,klass,100);GetWindowTextW(child,label,512);
        const bool outside=r.left<0||r.top<0||r.right>w||r.bottom>hh;
        controls<<csv(key(surface))<<','<<GetDlgCtrlID(child)<<','<<csv(ui::utf8(klass))<<','<<csv(ui::utf8(label))<<','<<r.left<<','<<r.top<<','<<r.right<<','<<r.bottom<<','<<outside<<'\n';
    }
    if(h==win)for(const auto& hit:hits)hitsFile<<csv(key(surface))<<','<<hit.id<<','<<hit.box.X<<','<<hit.box.Y<<','<<hit.box.Width<<','<<hit.box.Height<<'\n';
}
void inventory(HMENU menu,const std::string& parent,int depth){
    for(int i=0;i<GetMenuItemCount(menu);++i){wchar_t label[512]{};GetMenuStringW(menu,UINT(i),label,512,MF_BYPOSITION);std::string route=parent+"/"+ui::utf8(label);
        if(auto child=GetSubMenu(menu,i))inventory(child,route,depth+1);
        else{int id=int(GetMenuItemID(menu,i));if(id>0){menus<<id<<','<<depth<<','<<csv(route)<<'\n';check(isEditorCommand(id),"menu command lost native routing");}}
    }
}
void CALLBACK modalDriver(HWND owner,UINT,UINT_PTR timer,DWORD){
    HWND dialog=nullptr;
    while((dialog=FindWindowExW(nullptr,dialog,modalClass.c_str(),nullptr))){DWORD pid=0;GetWindowThreadProcessId(dialog,&pid);if(pid==GetCurrentProcessId()&&GetWindow(dialog,GW_OWNER)==owner&&IsWindowVisible(dialog))break;}
    if(!dialog){if(++modalTicks>300){failure="modal capture timed out: "+modalName;KillTimer(owner,timer);EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM)->BOOL{if(GetWindow(h,GW_OWNER))PostMessageW(h,WM_CLOSE,0,0);return TRUE;},0);}return;}
    if(dialog!=pendingDialog){pendingDialog=dialog;readyTicks=0;}
    if(++readyTicks<(modalName=="save-svg"?15:3))return;
    KillTimer(owner,timer);modalSeen=true;
    try{
        resize(dialog);capture(dialog,modalName);
        if(modalName=="mechanism")for(int page:{70,71,72}){click(dialog,page);capture(dialog,"mechanism-page"+std::to_string(page));}
        if(modalName=="mechanism"&&after){
            auto edit=GetDlgItem(dialog,80);check(edit!=nullptr,"inline mechanism name inspector exists");
            SendDlgItemMessageW(dialog,20,LB_SETCURSEL,0,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(20,LBN_SELCHANGE),0);
            wchar_t before[256]{};GetWindowTextW(edit,before,256);SetFocus(edit);
            SetWindowTextW(edit,L"Intermediate A");SetWindowTextW(edit,L"Intermediate AB");
            check(SendDlgItemMessageW(dialog,20,LB_GETCOUNT,0,0)>=3,"live name edit retains stages");
            wchar_t live[512]{};SendDlgItemMessageW(dialog,20,LB_GETTEXT,0,LPARAM(live));
            check(std::wstring(live).find(L"Intermediate AB")!=std::wstring::npos,"inline name edits update actual stage list immediately");
            capture(dialog,"mechanism-inline-name");
            click(dialog,42);
            SendDlgItemMessageW(dialog,20,LB_SETCURSEL,0,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(20,LBN_SELCHANGE),0);
            wchar_t restored[256]{};GetWindowTextW(edit,restored,256);
            check(std::wstring(before)==restored,"one mechanism Undo restores all changes within focused name field");
        }
    }catch(const std::exception& e){failure=e.what();}
    // Only close this test-owned dialog; never enumerate or close user processes.
    if(modalName=="projects")PostMessageW(dialog,WM_CLOSE,0,0);
    else PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);
}
template<class Open>void modal(HWND owner,const wchar_t* klass,const std::string& name,Open open){
    modalClass=klass;modalName=name;modalTicks=0;modalSeen=false;pendingDialog=nullptr;readyTicks=0;SetTimer(owner,992,20,modalDriver);open();KillTimer(owner,992);
    check(modalSeen,("dialog did not open: "+name).c_str());check(failure.empty(),failure.c_str());pump();
}
void quickAssertions(HWND h){
    if(!after)return;
    std::set<int> available;for(const auto& c:availableEditorCommands(h))available.insert(c.id);
    for(int id=263;id<=269;++id)check(available.count(id)!=0,"selection formatting command remains searchable");
    doc={};selection.clear();history.clear();int a=doc.addAtom({0,0},8),b=doc.addAtom({30,0},7);int bond=doc.addBond(a,b);selection={a,b,bond};
    auto before=toNative(doc);SendMessageW(h,WM_COMMAND,267,0);check(toNative(doc)!=before,"font-plus applies to real selected atoms");
    SendMessageW(h,WM_COMMAND,104,0);check(toNative(doc)==before,"one Undo restores selected atom font increment");
    selection={a,b,bond};
    SendMessageW(h,WM_COMMAND,269,0);check(toNative(doc)!=before,"stroke-plus applies to selected bond");
    SendMessageW(h,WM_COMMAND,104,0);check(toNative(doc)==before,"one Undo restores selected bond thickness");
    selection.clear();SendMessageW(h,WM_COMMAND,267,0);check(toNative(doc)==before,"empty selection increment is a no-op");
    selection={a,b,bond};resize(h);capture(h,"selection-formatting");
    bool found=false;for(const auto& hit:hits)if(hit.id==267){found=true;auto at=MAKELPARAM(int(hit.box.X+hit.box.Width/2),int(hit.box.Y+hit.box.Height/2));SendMessageW(h,WM_LBUTTONDOWN,MK_LBUTTON,at);SendMessageW(h,WM_LBUTTONUP,0,at);break;}
    check(found&&toNative(doc)!=before,"actual selection icon hit route changes selected font in one mouse press");
    SendMessageW(h,WM_COMMAND,104,0);check(toNative(doc)==before,"icon route also commits exactly one Undo");
    Object text;text.id=doc.nextId++;text.kind=ObjectKind::Text;text.text="Label";text.fontSize=12;text.face=0;text.color="#123456";text.points={{10,40}};
    text.runs={{"La","#123456","Arial",12,0},{"bel","#654321","Courier New",6,0}};doc.objects.push_back(text);
    Object image;image.id=doc.nextId++;image.kind=ObjectKind::Image;image.points={{80,40},{100,60}};doc.objects.push_back(image);
    auto graph=reactionProfilePlot();graph.id=doc.nextId++;doc.plots.push_back(graph);doc.imagesLocked=doc.graphsLocked=true;
    selection={a,b,bond,text.id,image.id,graph.id};history.clear();const auto mixedBefore=toNative(doc);
    SendMessageW(h,WM_COMMAND,267,0);
    auto formatted=std::find_if(doc.objects.begin(),doc.objects.end(),[&](const auto& o){return o.id==text.id;});
    check(formatted->fontSize==13&&std::abs(formatted->runs[1].size-6.*13./12.)<1e-10,"mixed selection font increment preserves rich-run size ratios");
    check(formatted->runs[1].font=="Courier New"&&formatted->runs[1].color=="#654321"&&formatted->color=="#123456","quick font command preserves text families and colors");
    auto expected=doc;expected.plots[0]=graph;expected.objects.back()=image;
    check(toNative(expected)==toNative(doc),"mixed formatting preserves locked graph and image data");
    check(history.undo(doc)&&toNative(doc)==mixedBefore&&!history.undo(doc),"mixed selection font change is one complete undo transaction");
    selection={image.id,graph.id};const auto lockedBefore=toNative(doc);history.clear();
    for(int id=263;id<=269;++id)SendMessageW(h,WM_COMMAND,id,0);
    check(toNative(doc)==lockedBefore&&!history.undo(doc),"locked and unsupported-only selection creates no changes or undo entries");
    selection={text.id};auto& textObject=*std::find_if(doc.objects.begin(),doc.objects.end(),[&](const auto& o){return o.id==text.id;});textObject.fontSize=144;history.clear();const auto boundBefore=toNative(doc);
    SendMessageW(h,WM_COMMAND,267,0);check(toNative(doc)==boundBefore&&!history.undo(doc),"font upper-bound increment is a true no-op");
    SendMessageW(h,WM_COMMAND,264,0);check((doc.objects.front().face&2)!=0,"italic icon command applies to supported selected text");
    SendMessageW(h,WM_COMMAND,104,0);check(toNative(doc)==boundBefore,"text style command undoes in one step");
}
void surfaces(HWND h,const fs::path& project){
    doc=mechanismDemo();selection.clear();fit();resize(h);capture(h,"editor");
    doc=coordinateDemo();selection={doc.plots.front().id};fit();managePlot(doc.plots.front().id);resize(plotManagerWindow);
    for(int page=0;page<5;++page){click(plotManagerWindow,4000+page);capture(plotManagerWindow,"graph-page"+std::to_string(page));}
    closePlotManager();pump();
    doc=mechanismDemo();selection={doc.reactions.front().id};const auto before=toNative(doc);
    modal(h,L"YoungDrawMechanismEditor","mechanism",[&]{auto result=mechanismEditor(h,doc,doc.reactions.front().id);check(!result,"outer Cancel discards mechanism draft");});
    check(toNative(doc)==before,"mechanism inline edits and Cancel leave source data untouched");
    modal(h,L"YoungDrawTemplateGallery","templates",[&]{library();});
    modal(h,L"YoungDrawForm","document-style",[&]{SendMessageW(h,WM_COMMAND,150,0);});
    projects::Callbacks callbacks;callbacks.blank=toNative(doc);callbacks.close=[](const auto&){return true;};callbacks.preview=[](HDC,const RECT&,const std::wstring&){};
    modal(h,L"YoungDrawProjectManager","projects",[&]{projects::manage(h,callbacks,project);});
    modal(h,L"#32770","save-svg",[&]{auto result=fileDialog(true,1);check(!result,"save dialog Cancel is non-destructive");});
}
}
int main(int argc,char** argv){
    ULONG_PTR token=0;GdiplusStartupInput start;HWND window=nullptr;
    try{
        isolated();SetProcessDPIAware();after=env(L"YOUNGDRAW_UI_STAGE")!=L"baseline";
        RegistrySandbox registry;
        const DWORD theme=0;RegSetKeyValueW(HKEY_CURRENT_USER,L"Software\\YoungDraw\\Appearance",L"Theme",REG_DWORD,&theme,sizeof(theme));
        capturing=argc>1&&std::string(argv[1])=="--capture";
        output=env(L"YOUNGDRAW_UI_ARTIFACT_DIR");if(output.empty())output=fs::current_path()/"ui-access-validation";
        fs::create_directories(output);output=fs::absolute(output);
        auto sandbox=output/"sandbox";fs::create_directories(sandbox);SetEnvironmentVariableW(L"LOCALAPPDATA",sandbox.c_str());SetEnvironmentVariableW(L"APPDATA",sandbox.c_str());
        GdiplusStartup(&token,&start,nullptr);OleInitialize(nullptr);INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_WIN95_CLASSES};InitCommonControlsEx(&cc);
        manifest.open(output/"captures.csv");manifest<<"file,surface,requested_width,requested_height,client_width,client_height,requested_dpi,effective_dpi,method,dpi_probe,nonuniform_samples\n";
        menus.open(output/"menus.csv");menus<<"id,depth,route\n";commands.open(output/"commands.csv");commands<<"id,title,detail\n";
        hitsFile.open(output/"hits.csv");hitsFile<<"surface,id,x,y,width,height\n";controls.open(output/"controls.csv");controls<<"surface,id,class,label,left,top,right,bottom,outside_parent\n";
        WNDCLASSW wc{};wc.style=CS_DBLCLKS;wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawAccessValidation";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
        window=CreateWindowExW(0,wc.lpszClassName,L"YoungDraw UI validation",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,0,0,1440,900,nullptr,nullptr,wc.hInstance,nullptr);check(window!=nullptr,"editor created");ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);
        inventory(GetMenu(window),"",1);for(const auto& item:availableEditorCommands(window))commands<<item.id<<','<<csv(ui::utf8(item.title))<<','<<csv(ui::utf8(item.detail))<<'\n';
        menus.flush();commands.flush();
        auto project=projects::Project::create(projects::root(),L"Validation project");project.addDrawing(L"Example",toNative(mechanismDemo()));project.save();
        if(capturing){for(auto size:std::vector<std::pair<int,int>>{{800,600},{1440,900},{2560,1440}})for(int dpi:{96,120,144,192}){viewportWidth=size.first;viewportHeight=size.second;requestedDpi=dpi;surfaces(window,project.folder);std::cout<<"Captured "<<viewportWidth<<'x'<<viewportHeight<<" requested DPI "<<dpi<<"; effective editor DPI "<<GetDpiForWindow(window)<<'\n';}}
        else surfaces(window,project.folder);
        quickAssertions(window);doc={};selection.clear();dirty=false;DestroyWindow(window);window=nullptr;win=nullptr;appearance::shutdown();OleUninitialize();GdiplusShutdown(token);
        std::cout<<"PASS: isolated UI validation ("<<(after?"after assertions":"explicit baseline")<<"). DPI probes are synthetic; see effective_dpi.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';if(window)DestroyWindow(window);return 1;}
}

#include "win_button_help.h"
#include "win_projects.h"
#include "win_theme.h"
#include <commctrl.h>
#include <iostream>
#include <stdexcept>
using namespace yd::projects;
namespace fs=std::filesystem;
namespace {
std::string failure;fs::path projectFolder;int phase=0,openCalls=0;bool fullEditorSeen=false;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void capture(HWND window,const wchar_t* filename){RECT r{};GetClientRect(window,&r);Gdiplus::Bitmap bitmap(r.right,r.bottom,PixelFormat32bppARGB);{Gdiplus::Graphics graphics(&bitmap);auto dc=graphics.GetHDC();PrintWindow(window,dc,PW_CLIENTONLY);graphics.ReleaseHDC(dc);}const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};bitmap.Save(filename,&png);}
void command(HWND h,int id){SendMessageW(h,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),0);}
void CALLBACK cancelFullEditor(HWND manager,UINT,UINT_PTR timer,DWORD){
    HWND form=nullptr;while((form=FindWindowExW(nullptr,form,L"YoungDrawForm",L"파일 이름·노트")))if(GetWindow(form,GW_OWNER)==manager)break;
    if(!form)return;KillTimer(manager,timer);
    fullEditorSeen=GetDlgItem(form,100)&&GetDlgItem(form,101);
    PostMessageW(form,WM_COMMAND,IDCANCEL,0);
}
void CALLBACK drive(HWND owner,UINT,UINT_PTR timer,DWORD){
    HWND manager=nullptr;
    while((manager=FindWindowExW(nullptr,manager,L"YoungDrawProjectManager",nullptr))){DWORD pid=0;GetWindowThreadProcessId(manager,&pid);if(pid==GetCurrentProcessId()&&GetWindow(manager,GW_OWNER)==owner)break;}
    if(!manager)return;KillTimer(owner,timer);
    try{
        ui::attachWindowHelp(manager);for(int id=10;id<=35;id++){auto control=GetDlgItem(manager,id);if(control)check(ui::hasButtonHelp(control),"every project action has tooltip help");}
        auto list=GetDlgItem(manager,41),search=GetDlgItem(manager,44),note=GetDlgItem(manager,42);
        int iconWidth=0,iconHeight=0;ImageList_GetIconSize(ListView_GetImageList(list,LVSIL_NORMAL),&iconWidth,&iconHeight);
        check((GetWindowLongPtrW(list,GWL_STYLE)&LVS_TYPEMASK)==LVS_ICON&&iconWidth>=48&&iconHeight>=48,"project files use large folder and drawing icons");
        if(phase==1){
            auto saved=Project::load(projectFolder);
            check(!ListView_GetEditControl(list)&&IsWindow(manager)&&openCalls==0,"inline rename Enter finishes without opening the drawing or closing the manager");
            check(saved.items[2].name==L"Nested renamed"&&saved.items[2].note==L"needle"&&saved.items[0].name==L"폴더"&&saved.items[1].name==L"Root","filtered inline rename persists the underlying nested item and preserves siblings and note");
            check(ListView_GetItemCount(list)==1&&ListView_GetSelectedCount(list)==1,"rename refresh preserves selection in filtered results");
            command(manager,38);auto edit=ListView_GetEditControl(list);check(edit!=nullptr,"quick rename reopens the native label editor");
            SetWindowTextW(edit,L"Discarded rename");
            check(PostMessageW(edit,WM_KEYDOWN,VK_ESCAPE,0)!=FALSE,"queue native label cancellation");
            phase=2;SetTimer(owner,1,20,drive);return;
        }
        if(phase==2){
            check(!ListView_GetEditControl(list)&&IsWindow(manager)&&openCalls==0,"inline rename Escape cancels only the label editor");
            check(Project::load(projectFolder).items[2].name==L"Nested renamed","cancelled inline rename never persists its draft");
            BYTE keys[256]{},neutral[256]{};GetKeyboardState(keys);SetKeyboardState(neutral);
            SetTimer(manager,903,20,cancelFullEditor);SendMessageW(list,WM_KEYDOWN,VK_F2,0);KillTimer(manager,903);SetKeyboardState(keys);
            check(fullEditorSeen&&!ListView_GetEditControl(list),"existing F2 still opens the full name and note dialog");
            check(Project::load(projectFolder).items[2].name==L"Nested renamed","cancelling the original F2 dialog preserves the renamed item");
        }else{
        check(ListView_GetItemCount(list)==2,"root displays folder and file");capture(manager,L"project-large-icons.png");
        SetWindowPos(manager,nullptr,0,0,640,480,SWP_NOMOVE|SWP_NOZORDER);
        RECT bounds{};GetClientRect(manager,&bounds);
        for(int id=10;id<=45;++id){auto c=GetDlgItem(manager,id);if(!c||!IsWindowVisible(c))continue;RECT r{};GetWindowRect(c,&r);MapWindowPoints(nullptr,manager,reinterpret_cast<POINT*>(&r),2);check(r.left>=0&&r.top>=0&&r.right<=bounds.right&&r.bottom<=bounds.bottom,"controls fit compact monitor layout");}
        capture(manager,L"project-large-icons-compact.png");SetWindowTextW(search,L"needle");check(ListView_GetItemCount(list)==1,"notes search finds nested item");
        ListView_SetItemState(list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        const auto nested=Project::load(projectFolder).items[2];
        command(manager,38);auto edit=ListView_GetEditControl(list);check(edit!=nullptr,"quick rename opens a native editor on the filtered row");
        SetWindowTextW(edit,L"Nested renamed");
        const auto onDisk=Project::load(projectFolder);
        check(onDisk.items[2].name==nested.name,"typing a rename does not save before Enter");
        // Post through the production message loop: IsDialogMessage must not turn Enter into Open.
        check(PostMessageW(edit,WM_KEYDOWN,VK_RETURN,0)!=FALSE,"queue native label commit");phase=1;SetTimer(owner,1,20,drive);return;
        }
        SetWindowTextW(note,L"자동 저장 검증");SendMessageW(manager,WM_COMMAND,MAKEWPARAM(42,EN_CHANGE),(LPARAM)note);SendMessageW(manager,WM_TIMER,2,0);
        auto saved=Project::load(projectFolder);check(saved.items[2].note==L"자동 저장 검증","filtered note edits target underlying item");
        SetWindowTextW(search,L"");
        ListView_SetItemState(list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);command(manager,23);
        check(ListView_GetItemCount(list)==1,"folder opens to nested contents");command(manager,30);check(ListView_GetItemCount(list)==2,"parent navigation returns to root");
        ListView_SetItemState(list,-1,LVIS_SELECTED,LVIS_SELECTED);check(ListView_GetSelectedCount(list)==2,"multiple items can be selected");
        command(manager,32);check(!IsWindowVisible(list)&&!IsWindowVisible(note),"expanded preview hides side panels");command(manager,33);command(manager,34);command(manager,35);command(manager,32);check(IsWindowVisible(list)&&IsWindowVisible(note),"expanded preview restores panels");
        phase=3;
    }catch(const std::exception& e){failure=e.what();}
    PostMessageW(manager,WM_CLOSE,0,0);
}
}
int main(){
    auto temp=fs::temp_directory_path()/(L"YoungDrawManagerTest-"+std::to_wstring(GetCurrentProcessId()));
    ULONG_PTR token=0;Gdiplus::GdiplusStartupInput start;Gdiplus::GdiplusStartup(&token,&start,nullptr);OleInitialize(nullptr);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&controls);
    DWORD widths[2]{};DWORD bytes=sizeof(widths);bool hadWidths=RegGetValueW(HKEY_CURRENT_USER,L"Software\\YoungDraw\\ProjectManager",L"PaneWidths",RRF_RT_REG_BINARY,nullptr,widths,&bytes)==ERROR_SUCCESS;
    int result=0;
    try{
        fs::create_directories(temp);SetEnvironmentVariableW(L"LOCALAPPDATA",temp.c_str());
        auto p=Project::create(root(),L"UI 회귀");auto f=p.addFolder(L"폴더");p.addDrawing(L"Root","root");p.addDrawing(L"Nested","nested",p.items[f].file);p.items[2].note=L"needle";p.save();projectFolder=p.folder;
        HWND owner=CreateWindowExW(0,L"STATIC",L"Project manager regression",WS_OVERLAPPEDWINDOW,0,0,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Callbacks callbacks;callbacks.close=[](const auto&){return true;};callbacks.preview=[](HDC,const RECT&,const std::wstring&){};callbacks.open=[](const auto&){++openCalls;};
        SetTimer(owner,1,20,drive);manage(owner,callbacks,p.folder);DestroyWindow(owner);
        check(failure.empty(),failure.c_str());
        check(phase==3,("all project manager interaction phases completed (phase="+std::to_string(phase)+", openCalls="+std::to_string(openCalls)+")").c_str());
        std::cout<<"PASS: filtered inline rename Enter/Escape, original F2 route, compact layout, folders, search mapping, autosave, multi selection and preview\n";
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';result=1;}
    HKEY key{};if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\YoungDraw\\ProjectManager",0,KEY_WRITE,&key)==ERROR_SUCCESS){if(hadWidths)RegSetValueExW(key,L"PaneWidths",0,REG_BINARY,reinterpret_cast<BYTE*>(widths),sizeof(widths));else RegDeleteValueW(key,L"PaneWidths");RegCloseKey(key);}
    fs::remove_all(temp);appearance::shutdown();Gdiplus::GdiplusShutdown(token);OleUninitialize();return result;
}

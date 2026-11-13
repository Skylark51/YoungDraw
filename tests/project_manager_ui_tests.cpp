#include "win_projects.h"
#include "win_theme.h"
#include <commctrl.h>
#include <iostream>
#include <stdexcept>
using namespace yd::projects;
namespace fs=std::filesystem;
namespace {
std::string failure;fs::path projectFolder;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void command(HWND h,int id){SendMessageW(h,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),0);}
void CALLBACK drive(HWND owner,UINT,UINT_PTR timer,DWORD){
    HWND manager=nullptr;
    while((manager=FindWindowExW(nullptr,manager,L"YoungDrawProjectManager",nullptr))){DWORD pid=0;GetWindowThreadProcessId(manager,&pid);if(pid==GetCurrentProcessId()&&GetWindow(manager,GW_OWNER)==owner)break;}
    if(!manager)return;KillTimer(owner,timer);
    try{
        auto list=GetDlgItem(manager,41),search=GetDlgItem(manager,44),note=GetDlgItem(manager,42);
        check(ListView_GetItemCount(list)==2,"root displays folder and file");
        SetWindowPos(manager,nullptr,0,0,640,480,SWP_NOMOVE|SWP_NOZORDER);
        RECT bounds{};GetClientRect(manager,&bounds);
        for(int id=10;id<=45;++id){auto c=GetDlgItem(manager,id);if(!c||!IsWindowVisible(c))continue;RECT r{};GetWindowRect(c,&r);MapWindowPoints(nullptr,manager,reinterpret_cast<POINT*>(&r),2);check(r.left>=0&&r.top>=0&&r.right<=bounds.right&&r.bottom<=bounds.bottom,"controls fit compact monitor layout");}
        SetWindowTextW(search,L"needle");check(ListView_GetItemCount(list)==1,"notes search finds nested item");
        ListView_SetItemState(list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        SetWindowTextW(note,L"자동 저장 검증");SendMessageW(manager,WM_COMMAND,MAKEWPARAM(42,EN_CHANGE),(LPARAM)note);SendMessageW(manager,WM_TIMER,2,0);
        auto saved=Project::load(projectFolder);check(saved.items[2].note==L"자동 저장 검증","filtered note edits target underlying item");
        SetWindowTextW(search,L"");
        ListView_SetItemState(list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);command(manager,23);
        check(ListView_GetItemCount(list)==1,"folder opens to nested contents");command(manager,30);check(ListView_GetItemCount(list)==2,"parent navigation returns to root");
        ListView_SetItemState(list,-1,LVIS_SELECTED,LVIS_SELECTED);check(ListView_GetSelectedCount(list)==2,"multiple items can be selected");
        command(manager,32);check(!IsWindowVisible(list)&&!IsWindowVisible(note),"expanded preview hides side panels");command(manager,33);command(manager,34);command(manager,35);command(manager,32);check(IsWindowVisible(list)&&IsWindowVisible(note),"expanded preview restores panels");
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
        Callbacks callbacks;callbacks.close=[](const auto&){return true;};callbacks.preview=[](HDC,const RECT&,const std::wstring&){};
        SetTimer(owner,1,20,drive);manage(owner,callbacks,p.folder);DestroyWindow(owner);
        check(failure.empty(),failure.c_str());std::cout<<"PASS: compact layout, folders, search mapping, autosave, multi selection and preview\n";
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';result=1;}
    HKEY key{};if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\YoungDraw\\ProjectManager",0,KEY_WRITE,&key)==ERROR_SUCCESS){if(hadWidths)RegSetValueExW(key,L"PaneWidths",0,REG_BINARY,reinterpret_cast<BYTE*>(widths),sizeof(widths));else RegDeleteValueW(key,L"PaneWidths");RegCloseKey(key);}
    fs::remove_all(temp);appearance::shutdown();Gdiplus::GdiplusShutdown(token);OleUninitialize();return result;
}

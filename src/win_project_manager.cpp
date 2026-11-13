#include "win_dialog_style.h"
#include "win_projects.h"
#include "win_dialogs.h"
#include "win_theme.h"
#include "win_ui.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <algorithm>
#include <memory>

#include <wincrypt.h>
#pragma comment(lib,"crypt32.lib")

namespace yd::projects {
namespace fs=std::filesystem;
namespace {
COLORREF color(Gdiplus::Color value){return RGB(value.GetR(),value.GetG(),value.GetB());}
void fill(HDC dc,const RECT& area,Gdiplus::Color value){
    auto previous=SetDCBrushColor(dc,color(value));
    FillRect(dc,&area,(HBRUSH)GetStockObject(DC_BRUSH));
    SetDCBrushColor(dc,previous);
}
void frame(HDC dc,const RECT& area,Gdiplus::Color value){
    auto previous=SetDCBrushColor(dc,color(value));
    FrameRect(dc,&area,(HBRUSH)GetStockObject(DC_BRUSH));
    SetDCBrushColor(dc,previous);
}
LRESULT CALLBACK itemListProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(msg==WM_NOTIFY){
        auto* draw=reinterpret_cast<NMCUSTOMDRAW*>(lp);
        if(draw->hdr.hwndFrom==ListView_GetHeader(hwnd)&&draw->hdr.code==NM_CUSTOMDRAW){
            const auto& palette=appearance::palette();
            if(draw->dwDrawStage==CDDS_PREPAINT){
                RECT area{};GetClientRect(draw->hdr.hwndFrom,&area);
                fill(draw->hdc,area,palette.panel);
                return CDRF_NOTIFYITEMDRAW;
            }
            if(draw->dwDrawStage==CDDS_ITEMPREPAINT){
                wchar_t label[256]{};HDITEMW item{};
                item.mask=HDI_TEXT;item.pszText=label;item.cchTextMax=256;
                Header_GetItem(draw->hdr.hwndFrom,int(draw->dwItemSpec),&item);
                auto area=draw->rc;
                fill(draw->hdc,area,palette.raised);frame(draw->hdc,area,palette.edge);
                area.left+=6;area.right-=4;
                SetBkMode(draw->hdc,TRANSPARENT);SetTextColor(draw->hdc,color(palette.text));
                DrawTextW(draw->hdc,label,-1,&area,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
                return CDRF_SKIPDEFAULT;
            }
        }
    }
    return DefSubclassProc(hwnd,msg,wp,lp);
}
// Keep extracted data isolated until the existing project importer validates its manifest.
struct ZipWorkspace {
    fs::path folder;
    ZipWorkspace(){
        GUID id{};
        if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("임시 작업 폴더를 만들 수 없습니다.");
        wchar_t name[40]{};StringFromGUID2(id,name,40);
        folder=fs::temp_directory_path()/(std::wstring(L"YoungDraw-")+name);
        if(!fs::create_directory(folder))throw std::runtime_error("임시 작업 폴더를 만들 수 없습니다.");
    }
    ~ZipWorkspace(){std::error_code error;fs::remove_all(folder,error);}
};

std::wstring zipBase64(const std::wstring& value){
    DWORD length=0;
    const auto data=reinterpret_cast<const BYTE*>(value.data());
    const auto size=DWORD(value.size()*sizeof(wchar_t));
    constexpr DWORD flags=CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF;
    if(!CryptBinaryToStringW(data,size,flags,nullptr,&length))throw std::runtime_error("ZIP 명령을 준비할 수 없습니다.");
    std::wstring encoded(length,L'\0');
    if(!CryptBinaryToStringW(data,size,flags,encoded.data(),&length))throw std::runtime_error("ZIP 명령을 준비할 수 없습니다.");
    encoded.resize(length);
    return encoded;
}
std::wstring zipLiteral(const fs::path& path){
    return L"[Text.Encoding]::Unicode.GetString([Convert]::FromBase64String('"+
        zipBase64(fs::absolute(path).wstring())+L"'))";
}
void zipCommand(const std::wstring& operation){
    const auto script=L"$ErrorActionPreference='Stop'; Add-Type -AssemblyName System.IO.Compression.FileSystem; "+operation;
    const auto encoded=zipBase64(script);
    wchar_t system[MAX_PATH]{};
    if(!GetSystemDirectoryW(system,MAX_PATH))throw std::runtime_error("Windows ZIP 구성 요소를 찾을 수 없습니다.");
    auto exe=fs::path(system)/L"WindowsPowerShell/v1.0/powershell.exe";
    auto command=L"\""+exe.wstring()+L"\" -NoProfile -NonInteractive -EncodedCommand "+encoded;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))
        throw std::runtime_error("Windows ZIP 도구를 시작할 수 없습니다.");
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess,INFINITE);
    DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);
    if(code!=0)throw std::runtime_error("ZIP 파일을 처리할 수 없습니다. 파일 형식과 저장 공간을 확인하세요.");
}

Project importZip(const fs::path& path){
    ZipWorkspace temp;
    // ExtractToDirectory rejects entries that escape the destination.
    zipCommand(L"[IO.Compression.ZipFile]::ExtractToDirectory("+zipLiteral(path)+L","+zipLiteral(temp.folder)+L")");
    return Project::importFrom(temp.folder/L"project.ydproject",root());
}

void exportZip(const Project& project,const fs::path& path,const PngWriter& png){
    ZipWorkspace temp;
    const auto exported=project.exportTo(temp.folder,png);
    const auto archive=temp.folder/L"project.zip";
    zipCommand(L"[IO.Compression.ZipFile]::CreateFromDirectory("+zipLiteral(exported.folder)+L","+zipLiteral(archive)+L")");
    if(!MoveFileExW(archive.c_str(),path.c_str(),MOVEFILE_COPY_ALLOWED|MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("ZIP 파일을 저장할 수 없습니다.");
}

struct Manager {
    HWND window=nullptr, projects=nullptr, items=nullptr, note=nullptr, heading=nullptr, hint=nullptr;
    HWND search=nullptr, breadcrumb=nullptr, tooltip=nullptr;
    HFONT iconFont=nullptr;
    HIMAGELIST images=nullptr;
    std::vector<int> visible;
    std::wstring currentFolder;
    int leftWidth=210,rightWidth=360,splitter=0,dropFolder=-1;
    bool expanded=false;
    double zoom=1.0;
    RECT leftGrip{},rightGrip{};
    HFONT font=nullptr;
    HBRUSH chromeBrush=nullptr,panelBrush=nullptr,raisedBrush=nullptr;
    ~Manager(){if(iconFont)DeleteObject(iconFont);if(images)ImageList_Destroy(images);if(font)DeleteObject(font);if(chromeBrush)DeleteObject(chromeBrush);if(panelBrush)DeleteObject(panelBrush);if(raisedBrush)DeleteObject(raisedBrush);}
    Callbacks callbacks;
    fs::path initial;
    std::vector<Project> all;
    int selected=-1, item=-1, dragging=-1, insertion=-1;
    bool refreshing=false, noteDirty=false;
    RECT preview{};
    HWND control(const wchar_t* cls,const wchar_t* label,int id,DWORD style){
        HWND c=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,(HMENU)INT_PTR(id),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;
    }
    Project* project(){return selected>=0&&selected<int(all.size())?&all[selected]:nullptr;}
    int modelIndex(int row) const{return row>=0&&row<int(visible.size())?visible[row]:-1;}
    int rowIndex(int index) const{auto it=std::find(visible.begin(),visible.end(),index);return it==visible.end()?-1:int(it-visible.begin());}
    std::vector<size_t> selection(){std::vector<size_t> result;for(int row=-1;(row=ListView_GetNextItem(items,row,LVNI_SELECTED))>=0;)result.push_back(size_t(modelIndex(row)));return result;}
    std::vector<std::wstring> selectedFiles(){std::vector<std::wstring> result;auto p=project();if(p)for(auto i:selection())result.push_back(p->items[i].file);return result;}
    void restoreSelection(const std::vector<std::wstring>& files){
        auto p=project();if(!p)return;refreshing=true;int first=-1;
        for(int row=0;row<int(visible.size());++row)if(std::find(files.begin(),files.end(),p->items[visible[row]].file)!=files.end()){ListView_SetItemState(items,row,LVIS_SELECTED,LVIS_SELECTED);if(first<0)first=row;}
        refreshing=false;if(first>=0){ListView_SetItemState(items,first,LVIS_FOCUSED,LVIS_FOCUSED);selectItem(modelIndex(first));ListView_EnsureVisible(items,first,FALSE);}
    }
    std::wstring folderPath(std::wstring id){
        std::wstring result;auto p=project();if(!p)return result;
        while(!id.empty()){auto it=std::find_if(p->items.begin(),p->items.end(),[&](const auto& x){return x.file==id;});if(it==p->items.end())break;result=it->name+(result.empty()?L"":L" / "+result);id=it->parent;}
        return result;
    }
    void rememberWidths(){DWORD widths[]={DWORD(leftWidth),DWORD(rightWidth)};HKEY key{};if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\YoungDraw\\ProjectManager",0,nullptr,0,KEY_WRITE,nullptr,&key,nullptr)==ERROR_SUCCESS){RegSetValueExW(key,L"PaneWidths",0,REG_BINARY,reinterpret_cast<BYTE*>(widths),sizeof(widths));RegCloseKey(key);}}
    void layout(){
        RECT r;GetClientRect(window,&r);int w=r.right,h=r.bottom;
        int left=std::clamp(leftWidth,120,std::max(120,w/3));
        int side=std::clamp(rightWidth,180,std::max(180,w/2));
        int x=left+22,px=std::max(x+180,w-side-10);side=w-px-10;
        auto place=[&](int id,int xx,int yy,int ww,int hh){MoveWindow(GetDlgItem(window,id),xx,yy,std::max(1,ww),std::max(1,hh),TRUE);};
        place(1,10,10,left-34,24);place(16,left-24,6,34,32);
        for(int i=0;i<6;++i)place(10+i,10+(i%3)*36,42+(i/3)*36,32,32);
        MoveWindow(projects,10,118,left,std::max(1,h-152),TRUE);
        place(3,x,10,std::max(1,px-x-10),24);
        const int actionIds[]={20,21,22,23,24,25,26,27,29,30,31,17,18};
        int cols=std::max(1,(px-x-10)/36),rows=(13+cols-1)/cols;
        for(int i=0;i<13;++i)place(actionIds[i],x+(i%cols)*36,42+(i/cols)*36,32,32);
        int sy=46+rows*36;place(46,x,sy,px-x-10,22);MoveWindow(search,x,sy+24,std::max(1,px-x-10),28,TRUE);
        MoveWindow(breadcrumb,x,sy+58,std::max(1,px-x-10),24,TRUE);
        int top=sy+86;MoveWindow(items,x,top,std::max(1,px-x-10),std::max(1,h-top-34),TRUE);
        ListView_SetColumnWidth(items,0,std::max(90,px-x-115));ListView_SetColumnWidth(items,1,90);
        leftGrip={left+10,118,x,h-34};rightGrip={px-9,42,px,h-34};
        for(int i=32;i<=35;++i)place(i,px+(i-32)*36,6,32,32);
        preview={px,42,w-10,std::max(110,h/2)};
        place(2,px,preview.bottom+8,side,24);MoveWindow(note,px,preview.bottom+36,std::max(1,side),std::max(1,h-int(preview.bottom)-104),TRUE);
        place(28,px,h-60,32,28);place(36,px+40,h-58,side-40,24);
        MoveWindow(hint,10,h-26,std::max(1,w-20),22,TRUE);
        for(int id:{1,3,16,40,41,42,43,2,28,36,44,45,46})ShowWindow(GetDlgItem(window,id),expanded?SW_HIDE:SW_SHOW);
        for(int id=10;id<=31;++id)if(GetDlgItem(window,id))ShowWindow(GetDlgItem(window,id),expanded?SW_HIDE:SW_SHOW);
        if(expanded){preview={10,46,w-10,h-10};for(int i=32;i<=35;++i)place(i,10+(i-32)*36,6,32,32);}
        InvalidateRect(window,nullptr,TRUE);
    }
    void saveNote(){
        auto p=project();if(!p||item<0||item>=int(p->items.size())||!noteDirty)return;
        int n=GetWindowTextLengthW(note);std::wstring value(size_t(n)+1,0);GetWindowTextW(note,value.data(),n+1);value.resize(n);
        auto before=p->items[item].note;p->items[item].note=value;
        try{p->save();noteDirty=false;KillTimer(window,2);SetWindowTextW(GetDlgItem(window,36),L"저장됨");}catch(...){p->items[item].note=before;SetWindowTextW(GetDlgItem(window,36),L"저장 실패 · 다시 시도하세요");throw;}
    }
    void selectItem(int index){
        saveNote();item=index;auto p=project();refreshing=true;
        SetWindowTextW(note,p&&index>=0&&index<int(p->items.size())?p->items[index].note.c_str():L"");
        EnableWindow(note,p&&index>=0);EnableWindow(GetDlgItem(window,28),p&&index>=0);SetWindowTextW(GetDlgItem(window,36),p&&index>=0?L"저장됨":L"항목을 선택하세요");noteDirty=false;refreshing=false;InvalidateRect(window,&preview,TRUE);
    }
    void refreshItems(int index=-1){
        refreshing=true;ListView_DeleteAllItems(items);visible.clear();auto p=project();
        SetWindowTextW(heading,p?p->name.c_str():L"프로젝트를 만들거나 선택하세요");
        wchar_t query[512]{};GetWindowTextW(search,query,512);std::wstring filter=query;
        auto lower=[](std::wstring text){for(auto& c:text)c=wchar_t(towlower(c));return text;};filter=lower(filter);
        SetWindowTextW(breadcrumb,(L"⌂ / "+folderPath(currentFolder)+(filter.empty()?L"":L"  · 검색 결과")).c_str());
        if(p)for(size_t i=0;i<p->items.size();++i){auto& entry=p->items[i];
            if(filter.empty()?entry.parent!=currentFolder:lower(entry.name+L" "+entry.note+L" "+folderPath(entry.parent)).find(filter)==std::wstring::npos)continue;
            int rowIndex=int(visible.size());visible.push_back(int(i));LVITEMW row{};row.mask=LVIF_TEXT|LVIF_IMAGE;row.iItem=rowIndex;row.pszText=entry.name.data();row.iImage=entry.isFolder?0:1;ListView_InsertItem(items,&row);
            auto ext=entry.isFolder?L"폴더":fs::path(entry.file).extension().wstring();ListView_SetItemText(items,rowIndex,1,ext.data());
        }
        for(int id=20;id<=31;id++)if(GetDlgItem(window,id))EnableWindow(GetDlgItem(window,id),p!=nullptr);
        EnableWindow(GetDlgItem(window,14),p!=nullptr);
        item=-1;noteDirty=false;refreshing=false;int row=rowIndex(index);selectItem(row>=0?index:-1);
        if(row>=0){ListView_SetItemState(items,row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);ListView_EnsureVisible(items,row,FALSE);}
        SetWindowTextW(hint,(std::to_wstring(visible.size())+L"개 항목  ·  Ctrl+F 검색  ·  Ctrl/Shift 다중 선택  ·  끌어놓기로 추가·이동").c_str());
    }
    void refreshProjects(int index){
        refreshing=true;SendMessageW(projects,LB_RESETCONTENT,0,0);for(auto& p:all)SendMessageW(projects,LB_ADDSTRING,0,(LPARAM)p.name.c_str());
        currentFolder.clear();SetWindowTextW(search,L"");selected=index;SendMessageW(projects,LB_SETCURSEL,index,0);refreshing=false;refreshItems();
    }
    void load(){
        fs::create_directories(root());std::wstring errors;
        for(auto& dir:fs::directory_iterator(root()))if(dir.is_directory()&&fs::exists(dir.path()/L"project.ydproject")){
            try{all.push_back(Project::load(dir.path()));}catch(const std::exception& e){errors+=dir.path().filename().wstring()+L": "+ui::wide(e.what())+L"\n";}
        }
        std::sort(all.begin(),all.end(),[](auto& a,auto& b){return a.name<b.name;});int index=all.empty()?-1:0;
        for(size_t i=0;i<all.size();++i)if(all[i].folder==initial){index=int(i);break;}
        refreshProjects(index);
        if(!errors.empty())MessageBoxW(window,(L"일부 프로젝트를 읽지 못했습니다. 원본은 유지됩니다.\n\n"+errors).c_str(),L"내 프로젝트",MB_OK|MB_ICONWARNING);
    }
    std::optional<std::vector<std::string>> edit(const std::wstring& title,const std::wstring& name,const std::wstring& text=L""){
        return ui::form(window,title,{{L"이름 (최대 200자)",ui::utf8(name)},{L"노트",ui::utf8(text),true,{},false,7}});
    }
    void open(){auto p=project();if(!p||item<0)return;saveNote();if(p->items[item].isFolder){currentFolder=p->items[item].file;SetWindowTextW(search,L"");refreshItems();return;}callbacks.open((p->folder/p->items[item].file).wstring());DestroyWindow(window);}
    void addFiles(const std::vector<fs::path>& files){
        auto p=project();if(!p)return;saveNote();int last=item;std::wstring errors;
        for(auto& path:files){try{last=int(p->add(path,currentFolder));}catch(const std::exception& e){errors+=path.filename().wstring()+L": "+ui::wide(e.what())+L"\n";}}
        refreshItems(last);if(!errors.empty())MessageBoxW(window,errors.c_str(),L"추가하지 못한 파일",MB_OK|MB_ICONWARNING);
    }
    void chooseFiles(){
        std::vector<wchar_t> buffer(65536);OPENFILENAMEW f{};f.lStructSize=sizeof(f);f.hwndOwner=window;f.lpstrFile=buffer.data();f.nMaxFile=DWORD(buffer.size());
        f.lpstrFilter=L"구조 및 이미지\0*.ydraw;*.cdxml;*.png;*.jpg;*.jpeg;*.gif\0\0";f.Flags=OFN_EXPLORER|OFN_ALLOWMULTISELECT|OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
        if(!GetOpenFileNameW(&f)){if(CommDlgExtendedError())throw std::runtime_error("파일을 선택하지 못했습니다. 파일 수를 줄여 다시 시도하세요.");return;}
        fs::path first(buffer.data());auto next=buffer.data()+wcslen(buffer.data())+1;std::vector<fs::path> files;
        if(!*next)files.push_back(first);else while(*next){files.push_back(first/next);next+=wcslen(next)+1;}addFiles(files);
    }
    void searchProjects(){
        std::vector<ui::SearchItem> entries;
        std::vector<std::pair<int,int>> positions;
        for(int j=0;j<int(all.size());++j){
            const auto& p=all[j];
            entries.push_back({p.name,p.note});positions.emplace_back(j,-1);
            for(int k=0;k<int(p.items.size());++k){
                const auto& entry=p.items[k];
                entries.push_back({p.name+L"  /  "+entry.name,p.note+L"\r\n"+entry.note});
                positions.emplace_back(j,k);
            }
        }
        if(auto match=ui::search(window,L"프로젝트 · 구조 · 노트 검색",entries,L"선택",true)){
            auto [projectIndex,itemIndex]=positions[*match];
            refreshProjects(projectIndex);if(itemIndex>=0)currentFolder=all[projectIndex].items[itemIndex].parent;refreshItems(itemIndex);
        }
    }
    void action(int id){
        saveNote();auto p=project();
        if(id==32){expanded=!expanded;zoom=1;layout();return;}
        if(id==33||id==34||id==35){zoom=id==35?1.0:std::clamp(zoom*(id==33?1.25:0.8),0.25,4.0);InvalidateRect(window,&preview,TRUE);return;}
        if(id==30){if(p&&!currentFolder.empty()){auto it=std::find_if(p->items.begin(),p->items.end(),[&](const auto& e){return e.file==currentFolder;});currentFolder=it==p->items.end()?L"":it->parent;}SetWindowTextW(search,L"");refreshItems();return;}
        if(id==29){if(!p)return;auto value=edit(L"새 폴더",L"새 폴더");if(value){auto i=p->addFolder(ui::wide((*value)[0]),currentFolder);p->items[i].note=ui::wide((*value)[1]);p->save();refreshItems(int(i));}return;}
        if(id==31){if(!p)return;auto chosen=selection();if(chosen.empty())return;std::vector<ui::SearchItem> choices{{L"⌂ 프로젝트 루트",L""}};std::vector<std::wstring> targets{L""};for(auto& entry:p->items)if(entry.isFolder){choices.push_back({folderPath(entry.file),L"폴더"});targets.push_back(entry.file);}if(auto match=ui::search(window,L"선택한 항목 이동",choices,L"이동",true)){p->relocate(chosen,targets[*match],p->items.size());refreshItems();}return;}
        if(id==17){if(!p)return;auto duplicated=p->duplicate(selection());std::vector<std::wstring> files;for(auto i:duplicated)files.push_back(p->items[i].file);refreshItems();restoreSelection(files);return;}
        if(id==18){if(!p)return;auto chosen=selection();if(chosen.empty())return;wchar_t path[32768]{};OPENFILENAMEW f{};f.lStructSize=sizeof(f);f.hwndOwner=window;f.lpstrFile=path;f.nMaxFile=32768;f.lpstrTitle=L"선택 항목 ZIP 내보내기";f.lpstrDefExt=L"zip";f.lpstrFilter=L"프로젝트 ZIP\0*.zip\0\0";f.Flags=OFN_EXPLORER|OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
            if(!GetSaveFileNameW(&f)){if(CommDlgExtendedError())throw std::runtime_error("ZIP 저장 창을 열 수 없습니다.");return;}
            if(callbacks.saveForExport&&!callbacks.saveForExport(p->folder))return;ZipWorkspace temp;auto subset=p->copySelectionTo(temp.folder,chosen);exportZip(subset,path,callbacks.writePng);SetWindowTextW(hint,L"선택한 항목을 ZIP 파일로 내보냈습니다.");return;}
        if(id==37){if(!p||item<0||p->items[item].isFolder)return;ShellExecuteW(window,L"open",p->folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return;}

        if(id==16){searchProjects();return;}
        if(id==10){auto value=edit(L"새 프로젝트",L"새 프로젝트");if(!value)return;auto created=Project::create(root(),ui::wide((*value)[0]));created.note=ui::wide((*value)[1]);created.save();all.push_back(created);refreshProjects(int(all.size()-1));return;}
        if(id==13){
            wchar_t path[32768]{};OPENFILENAMEW f{};f.lStructSize=sizeof(f);f.hwndOwner=window;f.lpstrFile=path;f.nMaxFile=32768;
            f.lpstrTitle=L"프로젝트 ZIP 또는 기존 프로젝트 정보를 선택하세요";
            f.lpstrFilter=L"프로젝트 ZIP\0*.zip\0기존 프로젝트 정보\0*.ydproject\0\0";f.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
            if(!GetOpenFileNameW(&f)){if(CommDlgExtendedError())throw std::runtime_error("프로젝트 선택 창을 열 수 없습니다.");return;}
            auto imported=_wcsicmp(fs::path(path).extension().c_str(),L".zip")==0?importZip(path):Project::importFrom(path,root());all.push_back(std::move(imported));refreshProjects(int(all.size()-1));
            SetWindowTextW(hint,L"이전 프로젝트를 이 PC에 복사했습니다. 항목을 선택하고 열기 / 편집을 누르세요.");return;
        }
        if(id==15){fs::create_directories(root());auto folder=p?p->folder:root();if(INT_PTR(ShellExecuteW(window,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)throw std::runtime_error("저장 폴더를 열 수 없습니다.");return;}
        if(!p)return;
        if(id==14){
            wchar_t path[32768]{};
            OPENFILENAMEW f{};f.lStructSize=sizeof(f);f.hwndOwner=window;f.lpstrFile=path;f.nMaxFile=32768;
            f.lpstrTitle=L"프로젝트 ZIP 내보내기";f.lpstrDefExt=L"zip";
            f.lpstrFilter=L"프로젝트 ZIP\0*.zip\0\0";f.Flags=OFN_EXPLORER|OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
            if(!GetSaveFileNameW(&f)){if(CommDlgExtendedError())throw std::runtime_error("ZIP 저장 창을 열 수 없습니다.");return;}
            if(callbacks.saveForExport&&!callbacks.saveForExport(p->folder))return;
            exportZip(*p,path,callbacks.writePng);
            SetWindowTextW(hint,L"프로젝트를 ZIP 파일로 내보냈습니다.");return;
        }
        if(id==11){auto value=edit(L"프로젝트 이름·노트",p->name,p->note);if(!value)return;auto before=*p;p->name=ui::wide((*value)[0]);p->note=ui::wide((*value)[1]);try{p->save();}catch(...){*p=before;throw;}refreshProjects(selected);return;}
        if(id==12){if(MessageBoxW(window,L"이 프로젝트를 삭제하시겠습니까?\n파일은 프로젝트 휴지통에 보관됩니다.",L"프로젝트 삭제",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
            std::vector<std::wstring> paths;for(auto& entry:p->items)if(!entry.isFolder)paths.push_back((p->folder/entry.file).wstring());if(!callbacks.close(paths))return;
            auto trash=root().parent_path()/L"ProjectTrash";fs::create_directories(trash);auto dest=trash/p->folder.filename();while(fs::exists(dest))dest+=L"-deleted";fs::rename(p->folder,dest);all.erase(all.begin()+selected);refreshProjects(all.empty()?-1:std::min(selected,int(all.size()-1)));return;}
        if(id==20){auto value=edit(L"새 구조 문서",L"새 구조");if(!value)return;size_t i=p->addDrawing(ui::wide((*value)[0]),callbacks.blank,currentFolder);p->items[i].note=ui::wide((*value)[1]);p->save();refreshItems(int(i));open();return;}
        if(id==21){chooseFiles();return;}
        if(id==22){auto current=callbacks.current();if(!current)return;auto value=edit(L"현재 문서를 프로젝트에 복사",current->first);if(!value)return;auto i=p->addDrawing(ui::wide((*value)[0]),current->second,currentFolder);p->items[i].note=ui::wide((*value)[1]);p->save();refreshItems(int(i));return;}
        if(id==28){SetWindowTextW(hint,L"노트를 저장했습니다.");return;}
        if(item<0||item>=int(p->items.size()))return;
        if(id==23){open();return;}
        if(id==24){auto& entry=p->items[item];auto value=edit(L"파일 이름·노트",entry.name,entry.note);if(!value)return;auto before=entry;entry.name=ui::wide((*value)[0]);entry.note=ui::wide((*value)[1]);try{p->save();}catch(...){entry=before;throw;}refreshItems(item);return;}
        if(id==25){auto chosen=selection();if(chosen.empty())return;
            if(MessageBoxW(window,L"선택한 항목과 하위 항목을 프로젝트에서 제외하시겠습니까?\n원본 파일은 복구를 위해 보관됩니다.",L"항목 삭제",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
            std::vector<std::wstring> removed,paths;for(auto i:chosen)removed.push_back(p->items[i].file);
            bool changed=true;while(changed){changed=false;for(auto& e:p->items)if(std::find(removed.begin(),removed.end(),e.parent)!=removed.end()&&std::find(removed.begin(),removed.end(),e.file)==removed.end()){removed.push_back(e.file);changed=true;}}
            for(auto& e:p->items)if(!e.isFolder&&std::find(removed.begin(),removed.end(),e.file)!=removed.end())paths.push_back((p->folder/e.file).wstring());
            if(!callbacks.close(paths))return;auto before=p->items;
            p->items.erase(std::remove_if(p->items.begin(),p->items.end(),[&](auto& e){return std::find(removed.begin(),removed.end(),e.file)!=removed.end();}),p->items.end());
            try{p->save();}catch(...){p->items=before;throw;}refreshItems();return;
        }
        if(id==26||id==27){if(GetWindowTextLengthW(search)>0){SetWindowTextW(hint,L"순서를 바꾸려면 검색을 해제하세요. 폴더 이동은 검색 중에도 가능합니다.");return;}auto chosen=selection();if(chosen.empty())return;int row=rowIndex(int(id==26?chosen.front():chosen.back()));int target=row+(id==26?-1:1);if(target<0||target>=int(visible.size()))return;auto files=selectedFiles();p->relocate(chosen,currentFolder,size_t(modelIndex(target)+(id==27?1:0)));refreshItems();restoreSelection(files);return;}
    }

    void stopDrag(){dragging=insertion=dropFolder=-1;ListView_SetItemState(items,-1,0,LVIS_DROPHILITED);KillTimer(window,1);LVINSERTMARK mark{sizeof(mark)};mark.iItem=-1;ListView_SetInsertMark(items,&mark);if(GetCapture()==window)ReleaseCapture();}
    void dragPosition(){
        POINT pt;GetCursorPos(&pt);ScreenToClient(items,&pt);RECT bounds;GetClientRect(items,&bounds);auto p=project();if(!p)return;
        if(pt.y<20)ListView_Scroll(items,0,-24);else if(pt.y>bounds.bottom-20)ListView_Scroll(items,0,24);
        LVHITTESTINFO hit{};hit.pt=pt;int row=ListView_HitTest(items,&hit);insertion=-1;dropFolder=-1;
        ListView_SetItemState(items,-1,0,LVIS_DROPHILITED);LVINSERTMARK mark{sizeof(mark)};mark.iItem=-1;
        if(pt.x>=0&&pt.x<bounds.right&&pt.y>=0&&pt.y<bounds.bottom){
            if(row>=0){int model=modelIndex(row);RECT r;ListView_GetItemRect(items,row,&r,LVIR_BOUNDS);
                if(p->items[model].isFolder&&pt.y>r.top+4&&pt.y<r.bottom-4){dropFolder=model;ListView_SetItemState(items,row,LVIS_DROPHILITED,LVIS_DROPHILITED);}
                else{bool after=pt.y>(r.top+r.bottom)/2;insertion=model+(after?1:0);mark.iItem=row;mark.dwFlags=after?LVIM_AFTER:0;}
            }else insertion=int(p->items.size());
        }
        ListView_SetInsertMark(items,&mark);
    }
    void contextMenu(HWND source,POINT point){
        if(point.x!=-1&&source==items){POINT local=point;ScreenToClient(items,&local);LVHITTESTINFO hit{};hit.pt=local;int row=ListView_HitTest(items,&hit);if(row>=0){if(!(ListView_GetItemState(items,row,LVIS_SELECTED)&LVIS_SELECTED)){ListView_SetItemState(items,-1,0,LVIS_SELECTED);ListView_SetItemState(items,row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}selectItem(modelIndex(row));}}
        if(point.x!=-1&&source==projects){POINT local=point;ScreenToClient(projects,&local);DWORD hit=DWORD(SendMessageW(projects,LB_ITEMFROMPOINT,0,MAKELPARAM(local.x,local.y)));if(!HIWORD(hit)&&int(LOWORD(hit))!=selected){saveNote();refreshProjects(LOWORD(hit));}}
        if(point.x==-1){RECT r;GetWindowRect(source,&r);point={r.left+24,r.top+24};}
        HMENU menu=CreatePopupMenu();
        auto add=[&](int id,const wchar_t* label){AppendMenuW(menu,MF_STRING,id,label);};
        if(source==projects){add(10,L"새 프로젝트");add(11,L"프로젝트 정보");add(13,L"불러오기");add(14,L"ZIP 내보내기");add(15,L"프로젝트 폴더 열기");add(12,L"프로젝트 삭제");}
        else{add(23,L"열기 / 편집\tEnter");add(24,L"이름·노트\tF2");add(17,L"선택 항목 복제");add(31,L"선택 항목 이동…");add(18,L"선택 항목 ZIP 내보내기…");add(37,L"원본 위치 열기");add(25,L"선택 항목 제외\tDelete");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);add(29,L"새 폴더");add(20,L"새 구조");add(21,L"파일 추가…");add(22,L"현재 문서 추가");}
        int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);DestroyMenu(menu);if(command)action(command);
    }
    void paint(){
        const auto& palette=appearance::palette();
        PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);fill(dc,preview,palette.canvas);auto p=project();
        for(HWND control:{projects,items,note}){
            if(expanded)break;
            RECT area{};GetWindowRect(control,&area);
            MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&area),2);
            InflateRect(&area,1,1);frame(dc,area,palette.edge);
        }
        auto previous=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color(palette.canvasInk));
        try{if(p&&item>=0&&!p->items[item].isFolder){int saved=SaveDC(dc);IntersectClipRect(dc,preview.left,preview.top,preview.right,preview.bottom);RECT target=preview;int dx=int((preview.right-preview.left)*(zoom-1)/2),dy=int((preview.bottom-preview.top)*(zoom-1)/2);InflateRect(&target,dx,dy);try{callbacks.preview(dc,target,(p->folder/p->items[item].file).wstring());}catch(...){RestoreDC(dc,saved);throw;}RestoreDC(dc,saved);}else{SetBkMode(dc,TRANSPARENT);auto r=preview;DrawTextW(dc,L"항목을 선택하면 미리보기가 표시됩니다", -1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}}
        catch(const std::exception& e){auto r=preview;auto text=ui::wide(e.what());DrawTextW(dc,text.c_str(),-1,&r,DT_WORDBREAK);}
        frame(dc,preview,palette.edge);SelectObject(dc,previous);EndPaint(window,&ps);
    }
    void drawControl(const DRAWITEMSTRUCT& draw){
        const auto& palette=appearance::palette();
        const bool list=draw.CtlType==ODT_LISTBOX;
        const bool active=(draw.itemState&(list?ODS_SELECTED:ODS_SELECTED|ODS_HOTLIGHT))!=0;
        const bool disabled=(draw.itemState&ODS_DISABLED)!=0;
        const bool primary=!list&&(draw.CtlID==14||draw.CtlID==23);
        const bool accent=!disabled&&(list?active:primary&&!active);
        auto background=accent?palette.accent:active?palette.hover:list||disabled?palette.panel:palette.raised;
        auto area=draw.rcItem;auto dc=draw.hDC;
        fill(dc,area,background);
        if(!list)frame(dc,area,draw.itemState&ODS_FOCUS?palette.accent:palette.edge);
        SetTextColor(dc,color(disabled?palette.muted:accent?palette.onAccent:palette.text));SetBkMode(dc,TRANSPARENT);
        auto previous=SelectObject(dc,list?font:iconFont);
        if(list)area.left+=8;
        wchar_t label[256]{};
        if(list&&draw.itemID<all.size())SendMessageW(draw.hwndItem,LB_GETTEXT,draw.itemID,(LPARAM)label);
        if(!list){
            const wchar_t* glyph=L"\xE10F";
            switch(draw.CtlID){case 10:glyph=L"\xE109";break;case 11:glyph=L"\xE946";break;case 12:case 25:glyph=L"\xE74D";break;case 13:glyph=L"\xE8B5";break;case 14:glyph=L"\xE898";break;case 15:glyph=L"\xE838";break;case 16:glyph=L"\xE721";break;case 17:glyph=L"\xE8C8";break;case 18:glyph=L"\xE898";break;case 20:glyph=L"\xE8A5";break;case 21:glyph=L"\xE8E5";break;case 22:glyph=L"\xE8C8";break;case 23:glyph=L"\xE768";break;case 24:glyph=L"\xE70F";break;case 26:case 30:glyph=L"\xE74A";break;case 27:glyph=L"\xE74B";break;case 28:glyph=L"\xE74E";break;case 29:glyph=L"\xE8F4";break;case 31:glyph=L"\xE8DE";break;case 32:glyph=expanded?L"\xE73F":L"\xE740";break;case 33:glyph=L"\xE8A3";break;case 34:glyph=L"\xE71F";break;case 35:glyph=L"\xE72C";break;}
            lstrcpynW(label,glyph,256);
        }
        DrawTextW(dc,label,-1,&area,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX|(list?DT_LEFT:DT_CENTER));
        if(draw.itemState&ODS_FOCUS){area=draw.rcItem;InflateRect(&area,-3,-3);DrawFocusRect(dc,&area);}
        SelectObject(dc,previous);
    }
    static LRESULT CALLBACK procedure(HWND h,UINT msg,WPARAM wp,LPARAM lp){
        auto self=reinterpret_cast<Manager*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<Manager*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)self);}
        if(!self)return DefWindowProcW(h,msg,wp,lp);
        try{switch(msg){
            case WM_CREATE:{
                const auto& palette=appearance::palette();
                self->chromeBrush=CreateSolidBrush(color(palette.chrome));
                self->panelBrush=CreateSolidBrush(color(palette.panel));
                self->raisedBrush=CreateSolidBrush(color(palette.raised));
                self->font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,ui::interfaceFont());
                self->iconFont=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe MDL2 Assets");
                DWORD widths[2]{},bytes=sizeof(widths);if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\YoungDraw\\ProjectManager",L"PaneWidths",RRF_RT_REG_BINARY,nullptr,widths,&bytes)==ERROR_SUCCESS&&bytes==sizeof(widths)){self->leftWidth=int(widths[0]);self->rightWidth=int(widths[1]);}
                self->control(L"STATIC",L"내 프로젝트",1,0);self->control(L"BUTTON",L"검색…",16,WS_TABSTOP|BS_OWNERDRAW);self->control(L"STATIC",L"노트 · 자동 저장",2,0);
                self->heading=self->control(L"STATIC",L"",3,SS_ENDELLIPSIS);
                const wchar_t* labels[]={L"새로",L"정보",L"삭제",L"불러오기",L"내보내기",L"폴더"};
                for(int i=0;i<6;i++)self->control(L"BUTTON",labels[i],10+i,WS_TABSTOP|BS_OWNERDRAW);
                const wchar_t* actions[]={L"새 구조",L"파일 추가",L"현재 문서 추가",L"열기 / 편집",L"이름·노트",L"항목 삭제",L"위로 ↑",L"아래로 ↓",L"노트 저장"};
                for(int i=0;i<9;i++)self->control(L"BUTTON",actions[i],20+i,WS_TABSTOP|BS_OWNERDRAW);
                self->control(L"BUTTON",L"선택 항목 복제",17,WS_TABSTOP|BS_OWNERDRAW);self->control(L"BUTTON",L"선택 항목 ZIP 내보내기",18,WS_TABSTOP|BS_OWNERDRAW);
                self->control(L"BUTTON",L"새 폴더",29,WS_TABSTOP|BS_OWNERDRAW);self->control(L"BUTTON",L"상위 폴더",30,WS_TABSTOP|BS_OWNERDRAW);self->control(L"BUTTON",L"선택 항목 이동",31,WS_TABSTOP|BS_OWNERDRAW);
                const wchar_t* previewActions[]={L"미리보기 확대 / 돌아가기",L"확대",L"축소",L"배율 초기화"};for(int i=0;i<4;++i)self->control(L"BUTTON",previewActions[i],32+i,WS_TABSTOP|BS_OWNERDRAW);
                self->control(L"STATIC",L"항목을 선택하세요",36,SS_ENDELLIPSIS);
                self->control(L"STATIC",L"파일·폴더·노트 검색 (Ctrl+F)",46,SS_ENDELLIPSIS);
                self->search=self->control(L"EDIT",L"",44,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);SendMessageW(self->search,EM_SETCUEBANNER,TRUE,(LPARAM)L"파일·폴더·노트 검색 (Ctrl+F)");
                self->breadcrumb=self->control(L"STATIC",L"",45,SS_ENDELLIPSIS);
                self->tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,h,nullptr,GetModuleHandleW(nullptr),nullptr);
                SendMessageW(self->tooltip,TTM_SETMAXTIPWIDTH,0,300);
                for(int id=10;id<=35;++id){HWND button=GetDlgItem(h,id);if(!button)continue;wchar_t label[256]{};GetWindowTextW(button,label,256);TOOLINFOW info{sizeof(info)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=h;info.uId=UINT_PTR(button);info.lpszText=label;SendMessageW(self->tooltip,TTM_ADDTOOLW,0,(LPARAM)&info);}
                self->projects=self->control(L"LISTBOX",L"",40,WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|WS_TABSTOP);
                SendMessageW(self->projects,LB_SETITEMHEIGHT,0,24);
                self->items=self->control(WC_LISTVIEWW,L"",41,LVS_REPORT|LVS_SHOWSELALWAYS|WS_TABSTOP);
                self->images=ImageList_Create(20,20,ILC_COLOR32|ILC_MASK,2,0);
                for(auto stock:{SIID_FOLDER,SIID_DOCNOASSOC}){SHSTOCKICONINFO icon{sizeof(icon)};if(SUCCEEDED(SHGetStockIconInfo(stock,SHGSI_ICON|SHGSI_SMALLICON,&icon))){ImageList_AddIcon(self->images,icon.hIcon);DestroyIcon(icon.hIcon);}}
                ListView_SetImageList(self->items,self->images,LVSIL_SMALL);
                SetWindowSubclass(self->items,itemListProc,1,0);
                ListView_SetBkColor(self->items,color(palette.panel));
                ListView_SetTextBkColor(self->items,color(palette.panel));
                ListView_SetTextColor(self->items,color(palette.text));
                ListView_SetInsertMarkColor(self->items,color(palette.accent));
                ListView_SetExtendedListViewStyle(self->items,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_INFOTIP);
                LVCOLUMNW col{};col.mask=LVCF_TEXT|LVCF_WIDTH;col.cx=260;col.pszText=const_cast<wchar_t*>(L"이름 · 드래그로 순서 변경");ListView_InsertColumn(self->items,0,&col);col.cx=90;col.pszText=const_cast<wchar_t*>(L"형식");ListView_InsertColumn(self->items,1,&col);
                self->note=self->control(L"EDIT",L"",42,WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_TABSTOP);SendMessageW(self->note,EM_SETLIMITTEXT,1024*1024,0);
                self->hint=self->control(L"STATIC",L"다른 PC의 프로젝트: 폴더 전체를 옮긴 뒤 불러오기에서 project.ydproject 선택",43,SS_ENDELLIPSIS);
                DragAcceptFiles(h,TRUE);self->layout();self->load();return 0;}
            case WM_ERASEBKGND:{
                RECT area{};GetClientRect(h,&area);fill((HDC)wp,area,appearance::palette().chrome);return TRUE;}
            case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:{
                const auto& palette=appearance::palette();
                auto dc=(HDC)wp;auto control=(HWND)lp;
                SetTextColor(dc,color(IsWindowEnabled(control)?palette.text:palette.muted));
                if(control==self->note||control==self->projects||control==self->search){
                    bool raised=(control==self->note||control==self->search)&&IsWindowEnabled(control);
                    SetBkColor(dc,color(raised?palette.raised:palette.panel));
                    return (LRESULT)(raised?self->raisedBrush:self->panelBrush);
                }
                SetBkColor(dc,color(palette.chrome));return (LRESULT)self->chromeBrush;}
            case WM_DRAWITEM:self->drawControl(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
            case WM_GETMINMAXINFO:{MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&monitor);reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={std::min(640L,monitor.rcWork.right-monitor.rcWork.left),std::min(480L,monitor.rcWork.bottom-monitor.rcWork.top)};return 0;}
            case WM_SIZE:self->layout();return 0;
            case WM_COMMAND:
                if(LOWORD(wp)==IDCANCEL){SendMessageW(h,WM_CLOSE,0,0);return 0;}
                if(LOWORD(wp)==IDOK){self->open();return 0;}
                if(LOWORD(wp)==40&&HIWORD(wp)==LBN_SELCHANGE&&!self->refreshing){self->saveNote();self->selected=int(SendMessageW(self->projects,LB_GETCURSEL,0,0));self->currentFolder.clear();SetWindowTextW(self->search,L"");self->refreshItems();return 0;}
                if(LOWORD(wp)==42&&HIWORD(wp)==EN_CHANGE&&!self->refreshing){self->noteDirty=true;SetWindowTextW(GetDlgItem(h,36),L"저장 중…");SetTimer(h,2,650,nullptr);return 0;}
                if(LOWORD(wp)==44&&HIWORD(wp)==EN_CHANGE&&!self->refreshing){self->saveNote();self->refreshItems();return 0;}
                if(HIWORD(wp)==BN_CLICKED){self->action(LOWORD(wp));return 0;}break;
            case WM_NOTIFY:{auto hdr=reinterpret_cast<NMHDR*>(lp);if(hdr->hwndFrom!=self->items)break;
                if(hdr->code==NM_CUSTOMDRAW){
                    auto* draw=reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                    if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
                    if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT){
                        const auto& palette=appearance::palette();
                        bool selected=(ListView_GetItemState(self->items,int(draw->nmcd.dwItemSpec),LVIS_SELECTED)&LVIS_SELECTED)!=0;
                        draw->nmcd.uItemState&=~CDIS_SELECTED;
                        draw->clrText=color(selected?palette.onAccent:palette.text);
                        draw->clrTextBk=color(selected?palette.accent:palette.panel);
                        return CDRF_NEWFONT;
                    }
                }
                if(hdr->code==LVN_ITEMCHANGING&&!self->refreshing){auto n=reinterpret_cast<NMLISTVIEW*>(lp);if((n->uChanged&LVIF_STATE)&&(n->uOldState&LVIS_SELECTED)&&!(n->uNewState&LVIS_SELECTED))self->saveNote();}
                if(hdr->code==LVN_ITEMCHANGED&&!self->refreshing){auto n=reinterpret_cast<NMLISTVIEW*>(lp);if((n->uNewState&LVIS_SELECTED)&&!(n->uOldState&LVIS_SELECTED))self->selectItem(self->modelIndex(n->iItem));}
                if(hdr->code==NM_DBLCLK){auto n=reinterpret_cast<NMITEMACTIVATE*>(lp);if(n->iItem>=0){self->selectItem(self->modelIndex(n->iItem));self->open();}}
                if(hdr->code==LVN_GETINFOTIPW){auto tip=reinterpret_cast<NMLVGETINFOTIPW*>(lp);int index=self->modelIndex(tip->iItem);if(index>=0){auto& entry=self->project()->items[index];auto text=entry.name+L"\r\n"+self->folderPath(entry.parent)+L"\r\n"+entry.note;lstrcpynW(tip->pszText,text.c_str(),tip->cchTextMax);}}
                if(hdr->code==LVN_BEGINDRAG){self->saveNote();self->dragging=reinterpret_cast<NMLISTVIEW*>(lp)->iItem;SetCapture(h);SetTimer(h,1,90,nullptr);self->dragPosition();}
                if(hdr->code==LVN_KEYDOWN){auto key=reinterpret_cast<NMLVKEYDOWN*>(lp)->wVKey;if(key==VK_RETURN)self->open();else if(key==VK_F2)self->action(24);else if(key==VK_DELETE)self->action(25);else if(key==VK_BACK)self->action(30);else if(key=='A'&&(GetKeyState(VK_CONTROL)&0x8000))ListView_SetItemState(self->items,-1,LVIS_SELECTED,LVIS_SELECTED);}
                return 0;}
            case WM_CONTEXTMENU:{self->contextMenu((HWND)wp,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;}
            case WM_LBUTTONDBLCLK:{POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(PtInRect(&self->preview,pt))self->action(32);return 0;}
            case WM_MOUSEWHEEL:{POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(h,&pt);if(PtInRect(&self->preview,pt))self->action(GET_WHEEL_DELTA_WPARAM(wp)>0?33:34);return 0;}
            case WM_LBUTTONDOWN:{POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(!self->expanded){self->splitter=PtInRect(&self->leftGrip,pt)?1:PtInRect(&self->rightGrip,pt)?2:0;if(self->splitter)SetCapture(h);}return 0;}
            case WM_SETCURSOR:{POINT pt;GetCursorPos(&pt);ScreenToClient(h,&pt);if(!self->expanded&&(PtInRect(&self->leftGrip,pt)||PtInRect(&self->rightGrip,pt))){SetCursor(LoadCursorW(nullptr,IDC_SIZEWE));return TRUE;}break;}
            case WM_MOUSEMOVE:if(self->splitter){RECT r;GetClientRect(h,&r);int x=GET_X_LPARAM(lp);if(self->splitter==1)self->leftWidth=std::clamp(x-10,120,std::max(120,int(r.right)/3));else self->rightWidth=std::clamp(int(r.right)-x-10,180,std::max(180,int(r.right)/2));self->layout();}else if(self->dragging>=0)self->dragPosition();return 0;
            case WM_TIMER:if(wp==2){KillTimer(h,2);self->saveNote();}else if(self->dragging>=0)self->dragPosition();return 0;
            case WM_LBUTTONUP:if(self->splitter){self->splitter=0;ReleaseCapture();self->rememberWidths();return 0;}if(self->dragging>=0){int to=self->insertion,folder=self->dropFolder;auto chosen=self->selection();auto parent=folder>=0?self->project()->items[folder].file:self->currentFolder;self->stopDrag();if((to>=0||folder>=0)&&self->project()&&!chosen.empty()){if(folder<0&&GetWindowTextLengthW(self->search)>0){SetWindowTextW(self->hint,L"검색 중에는 폴더 위에 놓거나 이동 버튼을 사용하세요.");return 0;}auto files=self->selectedFiles();self->project()->relocate(chosen,parent,folder>=0?self->project()->items.size():size_t(to));self->refreshItems();self->restoreSelection(files);}}return 0;
            case WM_CAPTURECHANGED:self->splitter=0;if(self->dragging>=0)self->stopDrag();return 0;
            case WM_KEYDOWN:if(wp==VK_ESCAPE){if(self->dragging>=0)self->stopDrag();else if(self->expanded)self->action(32);else SendMessageW(h,WM_CLOSE,0,0);}return 0;
            case WM_DROPFILES:{HDROP drop=(HDROP)wp;POINT point{};DragQueryPoint(drop,&point);MapWindowPoints(h,self->items,&point,1);LVHITTESTINFO hit{};hit.pt=point;int row=ListView_HitTest(self->items,&hit);int target=self->modelIndex(row);if(target>=0&&self->project()&&self->project()->items[target].isFolder){self->saveNote();self->currentFolder=self->project()->items[target].file;}std::vector<fs::path> files;UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0);for(UINT i=0;i<count;i++){UINT n=DragQueryFileW(drop,i,nullptr,0);std::wstring path(size_t(n)+1,0);DragQueryFileW(drop,i,path.data(),n+1);path.resize(n);files.emplace_back(path);}DragFinish(drop);SetWindowTextW(self->search,L"");self->addFiles(files);return 0;}
            case WM_PAINT:self->paint();return 0;
            case WM_CLOSE:self->saveNote();DestroyWindow(h);return 0;
            case WM_DESTROY:self->stopDrag();KillTimer(h,2);self->rememberWidths();return 0;
        }}catch(const std::exception& e){MessageBoxW(h,ui::wide(e.what()).c_str(),L"내 프로젝트",MB_OK|MB_ICONWARNING);if(msg==WM_CREATE)return -1;if(msg==WM_NOTIFY)return TRUE;}
        return DefWindowProcW(h,msg,wp,lp);
    }
};
}
void manage(HWND owner,const Callbacks& callbacks,const fs::path& initial){
    WNDCLASSW wc{};wc.lpfnWndProc=Manager::procedure;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawProjectManager";wc.style=CS_DBLCLKS;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    Manager manager;manager.callbacks=callbacks;manager.initial=initial;
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
    const int w=std::min(1140,int(monitor.rcWork.right-monitor.rcWork.left));
    const int h=std::min(760,int(monitor.rcWork.bottom-monitor.rcWork.top));
    const RECT r=ui::ownedWindowRect(owner,w,h);
    HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,L"내 프로젝트 — YoungDraw",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,r.left,r.top,w,h,owner,nullptr,wc.hInstance,&manager);
    if(!window)throw std::runtime_error("프로젝트 창을 열 수 없습니다.");
    appearance::titlebar(window);
    EnableWindow(owner,FALSE);ShowWindow(window,SW_SHOW);MSG msg{};
    while(IsWindow(window)){BOOL result=GetMessageW(&msg,nullptr,0,0);if(result<=0){if(result==0)PostQuitMessage(int(msg.wParam));break;}if(msg.message==WM_KEYDOWN&&(GetKeyState(VK_CONTROL)&0x8000)&&msg.wParam=='F'){SetFocus(manager.search);SendMessageW(manager.search,EM_SETSEL,0,-1);continue;}if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    if(IsWindow(window))DestroyWindow(window);EnableWindow(owner,TRUE);SetActiveWindow(owner);
}
}

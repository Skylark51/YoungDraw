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
struct Manager {
    HWND window=nullptr, projects=nullptr, items=nullptr, note=nullptr, heading=nullptr, hint=nullptr;
    HFONT font=nullptr;
    HBRUSH chromeBrush=nullptr,panelBrush=nullptr,raisedBrush=nullptr;
    ~Manager(){if(font)DeleteObject(font);if(chromeBrush)DeleteObject(chromeBrush);if(panelBrush)DeleteObject(panelBrush);if(raisedBrush)DeleteObject(raisedBrush);}
    Callbacks callbacks;
    std::vector<Project> all;
    int selected=-1, item=-1, dragging=-1, insertion=-1;
    bool refreshing=false, noteDirty=false;
    RECT preview{};
    HWND control(const wchar_t* cls,const wchar_t* label,int id,DWORD style){
        HWND c=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,(HMENU)INT_PTR(id),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;
    }
    Project* project(){return selected>=0&&selected<int(all.size())?&all[selected]:nullptr;}
    void layout(){
        RECT r;GetClientRect(window,&r);int w=r.right,h=r.bottom,side=std::max(280,w-750);
        MoveWindow(GetDlgItem(window,1),16,12,210,24,TRUE);
        MoveWindow(GetDlgItem(window,10),16,44,64,30,TRUE);MoveWindow(GetDlgItem(window,11),84,44,68,30,TRUE);MoveWindow(GetDlgItem(window,12),156,44,70,30,TRUE);
        MoveWindow(GetDlgItem(window,13),16,80,76,30,TRUE);MoveWindow(GetDlgItem(window,14),96,80,76,30,TRUE);MoveWindow(GetDlgItem(window,15),176,80,50,30,TRUE);
        MoveWindow(projects,16,120,210,h-168,TRUE);
        int x=242;MoveWindow(heading,x,12,w-x-16,24,TRUE);
        for(int i=0;i<4;i++)MoveWindow(GetDlgItem(window,20+i),x+i*112,44,106,30,TRUE);
        for(int i=0;i<4;i++)MoveWindow(GetDlgItem(window,24+i),x+i*112,80,106,30,TRUE);
        int listWidth=w-x-side-32;MoveWindow(items,x,120,listWidth,h-168,TRUE);
        ListView_SetColumnWidth(items,0,std::max(110,listWidth-115));ListView_SetColumnWidth(items,1,90);
        int px=w-side-16;preview={px,120,w-16,std::max(240,h/2)};
        MoveWindow(GetDlgItem(window,2),px,preview.bottom+12,side,24,TRUE);
        MoveWindow(note,px,preview.bottom+40,side,std::max(60,h-int(preview.bottom)-130),TRUE);
        MoveWindow(GetDlgItem(window,28),px,h-80,side,30,TRUE);
        MoveWindow(hint,16,h-36,w-32,24,TRUE);InvalidateRect(window,nullptr,TRUE);
    }
    void saveNote(){
        auto p=project();if(!p||item<0||item>=int(p->items.size())||!noteDirty)return;
        int n=GetWindowTextLengthW(note);std::wstring value(size_t(n)+1,0);GetWindowTextW(note,value.data(),n+1);value.resize(n);
        auto before=p->items[item].note;p->items[item].note=value;
        try{p->save();noteDirty=false;}catch(...){p->items[item].note=before;throw;}
    }
    void selectItem(int index){
        saveNote();item=index;auto p=project();refreshing=true;
        SetWindowTextW(note,p&&index>=0&&index<int(p->items.size())?p->items[index].note.c_str():L"");
        EnableWindow(note,p&&index>=0);EnableWindow(GetDlgItem(window,28),p&&index>=0);noteDirty=false;refreshing=false;InvalidateRect(window,&preview,TRUE);
    }
    void refreshItems(int index=-1){
        refreshing=true;ListView_DeleteAllItems(items);auto p=project();
        SetWindowTextW(heading,p?p->name.c_str():L"프로젝트를 만들거나 선택하세요");
        if(p)for(size_t i=0;i<p->items.size();i++){auto& entry=p->items[i];LVITEMW row{};row.mask=LVIF_TEXT;row.iItem=int(i);row.pszText=entry.name.data();ListView_InsertItem(items,&row);auto ext=fs::path(entry.file).extension().wstring();ListView_SetItemText(items,int(i),1,ext.data());}
        for(int id=20;id<=27;id++)EnableWindow(GetDlgItem(window,id),p!=nullptr);
        EnableWindow(GetDlgItem(window,14),p!=nullptr);
        item=-1;noteDirty=false;refreshing=false;selectItem(index);
        if(p&&index>=0&&index<int(p->items.size())){ListView_SetItemState(items,index,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);ListView_EnsureVisible(items,index,FALSE);}
    }
    void refreshProjects(int index){
        refreshing=true;SendMessageW(projects,LB_RESETCONTENT,0,0);for(auto& p:all)SendMessageW(projects,LB_ADDSTRING,0,(LPARAM)p.name.c_str());
        selected=index;SendMessageW(projects,LB_SETCURSEL,index,0);refreshing=false;refreshItems();
    }
    void load(){
        fs::create_directories(root());std::wstring errors;
        for(auto& dir:fs::directory_iterator(root()))if(dir.is_directory()&&fs::exists(dir.path()/L"project.ydproject")){
            try{all.push_back(Project::load(dir.path()));}catch(const std::exception& e){errors+=dir.path().filename().wstring()+L": "+ui::wide(e.what())+L"\n";}
        }
        std::sort(all.begin(),all.end(),[](auto& a,auto& b){return a.name<b.name;});refreshProjects(all.empty()?-1:0);
        if(!errors.empty())MessageBoxW(window,(L"일부 프로젝트를 읽지 못했습니다. 원본은 유지됩니다.\n\n"+errors).c_str(),L"내 프로젝트",MB_OK|MB_ICONWARNING);
    }
    std::optional<std::vector<std::string>> edit(const std::wstring& title,const std::wstring& name,const std::wstring& text=L""){
        return ui::form(window,title,{{L"이름 (최대 200자)",ui::utf8(name)},{L"노트",ui::utf8(text),true,{},false,7}});
    }
    void open(){auto p=project();if(!p||item<0)return;saveNote();callbacks.open((p->folder/p->items[item].file).wstring());DestroyWindow(window);}
    void addFiles(const std::vector<fs::path>& files){
        auto p=project();if(!p)return;saveNote();int last=item;std::wstring errors;
        for(auto& path:files){try{last=int(p->add(path));}catch(const std::exception& e){errors+=path.filename().wstring()+L": "+ui::wide(e.what())+L"\n";}}
        refreshItems(last);if(!errors.empty())MessageBoxW(window,errors.c_str(),L"추가하지 못한 파일",MB_OK|MB_ICONWARNING);
    }
    void chooseFiles(){
        std::vector<wchar_t> buffer(65536);OPENFILENAMEW f{};f.lStructSize=sizeof(f);f.hwndOwner=window;f.lpstrFile=buffer.data();f.nMaxFile=DWORD(buffer.size());
        f.lpstrFilter=L"구조 및 이미지\0*.ydraw;*.cdxml;*.png;*.jpg;*.jpeg;*.gif\0\0";f.Flags=OFN_EXPLORER|OFN_ALLOWMULTISELECT|OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
        if(!GetOpenFileNameW(&f)){if(CommDlgExtendedError())throw std::runtime_error("파일을 선택하지 못했습니다. 파일 수를 줄여 다시 시도하세요.");return;}
        fs::path first(buffer.data());auto next=buffer.data()+wcslen(buffer.data())+1;std::vector<fs::path> files;
        if(!*next)files.push_back(first);else while(*next){files.push_back(first/next);next+=wcslen(next)+1;}addFiles(files);
    }
    void action(int id){
        saveNote();auto p=project();
        if(id==10){auto value=edit(L"새 프로젝트",L"새 프로젝트");if(!value)return;auto created=Project::create(root(),ui::wide((*value)[0]));created.note=ui::wide((*value)[1]);created.save();all.push_back(created);refreshProjects(int(all.size()-1));return;}
        if(id==13){
            wchar_t path[32768]{};OPENFILENAMEW f{};f.lStructSize=sizeof(f);f.hwndOwner=window;f.lpstrFile=path;f.nMaxFile=32768;
            f.lpstrTitle=L"이전 프로젝트 폴더 안의 project.ydproject를 선택하세요";
            f.lpstrFilter=L"YoungDraw 프로젝트 정보\0*.ydproject\0\0";f.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
            if(!GetOpenFileNameW(&f)){if(CommDlgExtendedError())throw std::runtime_error("프로젝트 선택 창을 열 수 없습니다.");return;}
            auto imported=Project::importFrom(path,root());all.push_back(std::move(imported));refreshProjects(int(all.size()-1));
            SetWindowTextW(hint,L"이전 프로젝트를 이 PC에 복사했습니다. 항목을 선택하고 열기 / 편집을 누르세요.");return;
        }
        if(id==15){fs::create_directories(root());auto folder=p?p->folder:root();if(INT_PTR(ShellExecuteW(window,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)throw std::runtime_error("저장 폴더를 열 수 없습니다.");return;}
        if(!p)return;
        if(id==14){
            BROWSEINFOW info{};info.hwndOwner=window;info.lpszTitle=L"프로젝트를 복사할 폴더를 선택하세요 (USB 또는 공유 폴더)";info.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
            auto chosen=SHBrowseForFolderW(&info);if(!chosen)return;wchar_t path[MAX_PATH]{};BOOL ok=SHGetPathFromIDListW(chosen,path);CoTaskMemFree(chosen);if(!ok)throw std::runtime_error("파일 시스템 폴더를 선택하세요.");
            if(callbacks.saveForExport&&!callbacks.saveForExport(p->folder))return;
            auto exported=p->exportTo(path,callbacks.writePng);
            MessageBoxW(window,(L"프로젝트를 내보냈습니다. 구조 문서와 투명 PNG가 같은 폴더에 저장됩니다.\n\n"+exported.folder.wstring()+L"\n\n다른 PC에서는 내 프로젝트 → 불러오기에서 project.ydproject를 선택하세요.").c_str(),L"프로젝트 내보내기",MB_OK|MB_ICONINFORMATION);return;
        }
        if(id==11){auto value=edit(L"프로젝트 이름·노트",p->name,p->note);if(!value)return;auto before=*p;p->name=ui::wide((*value)[0]);p->note=ui::wide((*value)[1]);try{p->save();}catch(...){*p=before;throw;}refreshProjects(selected);return;}
        if(id==12){if(MessageBoxW(window,L"이 프로젝트를 삭제하시겠습니까?\n파일은 프로젝트 휴지통에 보관됩니다.",L"프로젝트 삭제",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
            std::vector<std::wstring> paths;for(auto& entry:p->items)paths.push_back((p->folder/entry.file).wstring());if(!callbacks.close(paths))return;
            auto trash=root().parent_path()/L"ProjectTrash";fs::create_directories(trash);auto dest=trash/p->folder.filename();while(fs::exists(dest))dest+=L"-deleted";fs::rename(p->folder,dest);all.erase(all.begin()+selected);refreshProjects(all.empty()?-1:std::min(selected,int(all.size()-1)));return;}
        if(id==20){auto value=edit(L"새 구조 문서",L"새 구조");if(!value)return;size_t i=p->addDrawing(ui::wide((*value)[0]),callbacks.blank);p->items[i].note=ui::wide((*value)[1]);p->save();refreshItems(int(i));open();return;}
        if(id==21){chooseFiles();return;}
        if(id==22){auto current=callbacks.current();if(!current)return;auto value=edit(L"현재 문서를 프로젝트에 복사",current->first);if(!value)return;auto i=p->addDrawing(ui::wide((*value)[0]),current->second);p->items[i].note=ui::wide((*value)[1]);p->save();refreshItems(int(i));return;}
        if(id==28){SetWindowTextW(hint,L"노트를 저장했습니다.");return;}
        if(item<0||item>=int(p->items.size()))return;
        if(id==23){open();return;}
        if(id==24){auto& entry=p->items[item];auto value=edit(L"파일 이름·노트",entry.name,entry.note);if(!value)return;auto before=entry;entry.name=ui::wide((*value)[0]);entry.note=ui::wide((*value)[1]);try{p->save();}catch(...){entry=before;throw;}refreshItems(item);return;}
        if(id==25){if(MessageBoxW(window,L"선택한 항목을 프로젝트에서 삭제하시겠습니까?",L"항목 삭제",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
            if(!callbacks.close({(p->folder/p->items[item].file).wstring()}))return;auto before=p->items;p->items.erase(p->items.begin()+item);try{p->save();}catch(...){p->items=before;throw;}
            // Keep the unlisted asset for recovery; never destroy the imported original.
            refreshItems(p->items.empty()?-1:std::min(item,int(p->items.size()-1)));return;}
        if(id==26&&item>0){p->move(size_t(item),size_t(item-1));refreshItems(item-1);return;}
        if(id==27&&item+1<int(p->items.size())){p->move(size_t(item),size_t(item+1));refreshItems(item+1);return;}
    }
    void stopDrag(){dragging=insertion=-1;KillTimer(window,1);LVINSERTMARK mark{sizeof(mark)};mark.iItem=-1;ListView_SetInsertMark(items,&mark);if(GetCapture()==window)ReleaseCapture();}
    void dragPosition(){
        POINT pt;GetCursorPos(&pt);ScreenToClient(items,&pt);RECT bounds;GetClientRect(items,&bounds);auto p=project();if(!p)return;
        if(pt.y<20)ListView_Scroll(items,0,-24);else if(pt.y>bounds.bottom-20)ListView_Scroll(items,0,24);
        LVHITTESTINFO hit{};hit.pt=pt;int row=ListView_HitTest(items,&hit);insertion=-1;
        LVINSERTMARK mark{sizeof(mark)};mark.iItem=-1;
        if(pt.x>=0&&pt.x<bounds.right){if(row>=0){RECT r;ListView_GetItemRect(items,row,&r,LVIR_BOUNDS);bool after=pt.y>(r.top+r.bottom)/2;insertion=row+(after?1:0);mark.iItem=row;mark.dwFlags=after?LVIM_AFTER:0;}else if(pt.y>=0&&pt.y<=bounds.bottom&&!p->items.empty()){insertion=pt.y<20?0:int(p->items.size());mark.iItem=insertion?int(p->items.size()-1):0;mark.dwFlags=insertion?LVIM_AFTER:0;}}
        ListView_SetInsertMark(items,&mark);
    }
    void paint(){
        const auto& palette=appearance::palette();
        PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);fill(dc,preview,palette.canvas);auto p=project();
        for(HWND control:{projects,items,note}){
            RECT area{};GetWindowRect(control,&area);
            MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&area),2);
            InflateRect(&area,1,1);frame(dc,area,palette.edge);
        }
        auto previous=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color(palette.canvasInk));
        try{if(p&&item>=0)callbacks.preview(dc,preview,(p->folder/p->items[item].file).wstring());else{SetBkMode(dc,TRANSPARENT);auto r=preview;DrawTextW(dc,L"항목을 선택하면 미리보기가 표시됩니다", -1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}}
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
        auto previous=SelectObject(dc,font);
        if(list)area.left+=8;
        wchar_t label[256]{};
        if(list&&draw.itemID<all.size())SendMessageW(draw.hwndItem,LB_GETTEXT,draw.itemID,(LPARAM)label);
        if(!list)GetWindowTextW(draw.hwndItem,label,256);
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
                self->control(L"STATIC",L"내 프로젝트",1,0);self->control(L"STATIC",L"파일 노트 (항목 전환·닫을 때 자동 저장)",2,0);
                self->heading=self->control(L"STATIC",L"",3,SS_ENDELLIPSIS);
                const wchar_t* labels[]={L"새로",L"정보",L"삭제",L"불러오기",L"내보내기",L"폴더"};
                for(int i=0;i<6;i++)self->control(L"BUTTON",labels[i],10+i,WS_TABSTOP|BS_OWNERDRAW);
                const wchar_t* actions[]={L"새 구조",L"파일 추가",L"현재 문서 추가",L"열기 / 편집",L"이름·노트",L"항목 삭제",L"위로 ↑",L"아래로 ↓",L"노트 저장"};
                for(int i=0;i<9;i++)self->control(L"BUTTON",actions[i],20+i,WS_TABSTOP|BS_OWNERDRAW);
                self->projects=self->control(L"LISTBOX",L"",40,WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|WS_TABSTOP);
                SendMessageW(self->projects,LB_SETITEMHEIGHT,0,24);
                self->items=self->control(WC_LISTVIEWW,L"",41,LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP);
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
                if(control==self->note||control==self->projects){
                    bool raised=control==self->note&&IsWindowEnabled(control);
                    SetBkColor(dc,color(raised?palette.raised:palette.panel));
                    return (LRESULT)(raised?self->raisedBrush:self->panelBrush);
                }
                SetBkColor(dc,color(palette.chrome));return (LRESULT)self->chromeBrush;}
            case WM_DRAWITEM:self->drawControl(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
            case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={1100,700};return 0;
            case WM_SIZE:self->layout();return 0;
            case WM_COMMAND:
                if(LOWORD(wp)==IDCANCEL){SendMessageW(h,WM_CLOSE,0,0);return 0;}
                if(LOWORD(wp)==IDOK){self->open();return 0;}
                if(LOWORD(wp)==40&&HIWORD(wp)==LBN_SELCHANGE&&!self->refreshing){self->saveNote();self->selected=int(SendMessageW(self->projects,LB_GETCURSEL,0,0));self->refreshItems();return 0;}
                if(LOWORD(wp)==42&&HIWORD(wp)==EN_CHANGE&&!self->refreshing){self->noteDirty=true;return 0;}
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
                if(hdr->code==LVN_ITEMCHANGED&&!self->refreshing){auto n=reinterpret_cast<NMLISTVIEW*>(lp);if((n->uNewState&LVIS_SELECTED)&&!(n->uOldState&LVIS_SELECTED))self->selectItem(n->iItem);}
                if(hdr->code==NM_DBLCLK)self->open();
                if(hdr->code==LVN_BEGINDRAG){self->saveNote();self->dragging=reinterpret_cast<NMLISTVIEW*>(lp)->iItem;SetCapture(h);SetTimer(h,1,90,nullptr);self->dragPosition();}
                if(hdr->code==LVN_KEYDOWN){auto key=reinterpret_cast<NMLVKEYDOWN*>(lp)->wVKey;if(key==VK_RETURN)self->open();else if(key==VK_F2)self->action(24);else if(key==VK_DELETE)self->action(25);}
                return 0;}
            case WM_MOUSEMOVE:case WM_TIMER:if(self->dragging>=0)self->dragPosition();return 0;
            case WM_LBUTTONUP:if(self->dragging>=0){int from=self->dragging,to=self->insertion;if(to>from)--to;self->stopDrag();if(to>=0&&self->project()&&to<int(self->project()->items.size())){self->project()->move(size_t(from),size_t(to));self->refreshItems(to);}}return 0;
            case WM_CAPTURECHANGED:if(self->dragging>=0)self->stopDrag();return 0;
            case WM_KEYDOWN:if(wp==VK_ESCAPE){if(self->dragging>=0)self->stopDrag();else SendMessageW(h,WM_CLOSE,0,0);}return 0;
            case WM_DROPFILES:{HDROP drop=(HDROP)wp;std::vector<fs::path> files;UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0);for(UINT i=0;i<count;i++){UINT n=DragQueryFileW(drop,i,nullptr,0);std::wstring path(size_t(n)+1,0);DragQueryFileW(drop,i,path.data(),n+1);path.resize(n);files.emplace_back(path);}DragFinish(drop);self->addFiles(files);return 0;}
            case WM_PAINT:self->paint();return 0;
            case WM_CLOSE:self->saveNote();DestroyWindow(h);return 0;
            case WM_DESTROY:self->stopDrag();return 0;
        }}catch(const std::exception& e){MessageBoxW(h,ui::wide(e.what()).c_str(),L"내 프로젝트",MB_OK|MB_ICONWARNING);if(msg==WM_CREATE)return -1;if(msg==WM_NOTIFY)return TRUE;}
        return DefWindowProcW(h,msg,wp,lp);
    }
};
}
void manage(HWND owner,const Callbacks& callbacks){
    WNDCLASSW wc{};wc.lpfnWndProc=Manager::procedure;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawProjectManager";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    Manager manager;manager.callbacks=callbacks;RECT r;GetWindowRect(owner,&r);int w=1140,h=760;
    HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,L"내 프로젝트 — YoungDraw",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,std::max(0L,r.left+(r.right-r.left-w)/2),std::max(0L,r.top+(r.bottom-r.top-h)/2),w,h,owner,nullptr,wc.hInstance,&manager);
    if(!window)throw std::runtime_error("프로젝트 창을 열 수 없습니다.");
    appearance::titlebar(window);
    EnableWindow(owner,FALSE);ShowWindow(window,SW_SHOW);MSG msg{};
    while(IsWindow(window)){BOOL result=GetMessageW(&msg,nullptr,0,0);if(result<=0){if(result==0)PostQuitMessage(int(msg.wParam));break;}if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    if(IsWindow(window))DestroyWindow(window);EnableWindow(owner,TRUE);SetActiveWindow(owner);
}
}

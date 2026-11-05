#include "win_dialogs.h"
#include "win_ui.h"
#include "win_scripts.h"
#include <commctrl.h>
#include <algorithm>
#include <set>
namespace ui {
namespace {
constexpr UINT CommitFocus=WM_APP+41;
struct Grid {
    Table rows,original; std::vector<std::wstring> columns; TablePreview preview;std::wstring hint;
    HWND win=nullptr,list=nullptr,edit=nullptr,error=nullptr,apply=nullptr; HFONT font=nullptr;
    int row=0,col=0,anchorRow=0,anchorCol=0,scriptColumn=-1;
    HIMAGELIST rowHeight=nullptr;
    bool done=false,ok=false,loading=false,composing=false,scripts=false,stages=false;
};
void finish(Grid& g,bool accepted){
    g.ok=accepted;g.done=true;
    // A synchronous button/close message can arrive while GetMessage is waiting.
    // Wake the modal loop so completion never needs another click or key press.
    PostMessageW(g.win,WM_NULL,0,0);
}
std::wstring value(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n+1,0);GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
Table populatedRows(const Grid& g){
    auto rows=g.rows;
    while(!rows.empty()&&std::all_of(rows.back().begin(),rows.back().end(),[](const std::string& cell){return cell.find_first_not_of(" \t\r\n")==std::string::npos;}))rows.pop_back();
    return rows;
}
bool validate(Grid& g){
    std::wstring error;try{if(g.preview)error=g.preview(g.stages?populatedRows(g):g.rows);}catch(const std::exception& e){error=wide(e.what());}
    if(error.empty()&&!g.hint.empty()){SetWindowTextW(g.error,(g.hint+L"\nAlt+Enter: 셀 안 줄바꿈 · Enter: 다음 행 · Tab: 다음 셀 · Ctrl+C/V: 복사/붙여넣기 · Ctrl+D: 행 복제 · 적용: 현재 입력까지 저장").c_str());return true;}
    SetWindowTextW(g.error,error.empty()?L"입력: 바로 타이핑 / F2 / 더블클릭 · Shift+방향키: 범위 선택 · Ctrl+C/V: 복사/붙여넣기\nAlt+Enter: 셀 안 줄바꿈 · Tab: 다음 셀 · Enter: 아래 셀 · Esc: 입력 취소 · Ctrl+D: 행 복제":error.c_str());
    // Never disable Apply from stale validation: the active editor may fix the error.
    return error.empty();
}
void resizeRows(Grid& g){
    size_t lines=1;for(auto& row:g.rows)for(auto& cell:row)lines=std::max(lines,size_t(1+std::count(cell.begin(),cell.end(),'\n')));
    int height=int(lines)*20+6;
    if(g.rowHeight){int w=0,h=0;ImageList_GetIconSize(g.rowHeight,&w,&h);if(h==height)return;}
    auto images=ImageList_Create(1,height,ILC_COLOR32,1,1);
    if(images){ListView_SetImageList(g.list,images,LVSIL_SMALL);if(g.rowHeight)ImageList_Destroy(g.rowHeight);g.rowHeight=images;}
}
void sizeEditor(Grid& g){
    if(!g.edit)return;auto text=value(g.edit);RECT cell{},client{};
    ListView_GetSubItemRect(g.list,g.row,g.col+1,LVIR_BOUNDS,&cell);GetClientRect(g.list,&client);
    int height=std::max(int(cell.bottom-cell.top)-2,26+20*int(std::count(text.begin(),text.end(),L'\n')));
    height=std::min(height,std::max(26,int(client.bottom-cell.top-2)));
    SetWindowPos(g.edit,HWND_TOP,cell.left+1,cell.top+1,cell.right-cell.left-2,height,SWP_NOACTIVATE);
}
void refresh(Grid& g){
    resizeRows(g);
    g.loading=true;SendMessageW(g.list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(g.list);
    for(size_t r=0;r<g.rows.size();r++){
        g.rows[r].resize(g.columns.size());std::wstring index=std::to_wstring(r+1);
        LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=int(r);item.pszText=index.data();ListView_InsertItem(g.list,&item);
        for(size_t c=0;c<g.columns.size();c++){auto text=wide(g.rows[r][c]);ListView_SetItemText(g.list,int(r),int(c+1),text.data());}
    }
    SendMessageW(g.list,WM_SETREDRAW,TRUE,0);InvalidateRect(g.list,nullptr,FALSE);g.loading=false;
}
void commit(Grid& g){
    if(!g.edit)return;
    HWND editor=g.edit;auto text=value(editor);g.edit=nullptr;g.composing=false;DestroyWindow(editor);
    if(g.rows[g.row][g.col]!=utf8(text)){
        g.rows[g.row][g.col]=utf8(text);ListView_SetItemText(g.list,g.row,g.col+1,text.data());resizeRows(g);InvalidateRect(g.list,nullptr,FALSE);validate(g);
    }
}
void selectCell(Grid& g,int row,int col,bool extend=false){
    if(g.rows.empty())return;
    commit(g);g.row=std::clamp(row,0,int(g.rows.size())-1);g.col=std::clamp(col,0,int(g.columns.size())-1);
    if(!extend){g.anchorRow=g.row;g.anchorCol=g.col;}
    ListView_SetItemState(g.list,g.row,LVIS_FOCUSED,LVIS_FOCUSED);ListView_EnsureVisible(g.list,g.row,FALSE);
    // Keep the active column visible even with many pathways.
    RECT cell{},client{};ListView_GetSubItemRect(g.list,g.row,g.col+1,LVIR_BOUNDS,&cell);GetClientRect(g.list,&client);
    if(cell.left<45)ListView_Scroll(g.list,cell.left-45,0);else if(cell.right>client.right)ListView_Scroll(g.list,cell.right-client.right,0);
    SetFocus(g.list);InvalidateRect(g.list,nullptr,FALSE);
}
std::wstring clipboard(Grid& g){
    std::wstring data;if(!OpenClipboard(g.win))return data;HANDLE h=GetClipboardData(CF_UNICODETEXT);
    if(h){auto p=static_cast<const wchar_t*>(GlobalLock(h));if(p){data=p;GlobalUnlock(h);}}CloseClipboard();return data;
}
void copy(Grid& g){
    if(g.rows.empty())return;commit(g);std::wstring data;
    for(int r=std::min(g.row,g.anchorRow);r<=std::max(g.row,g.anchorRow);r++){
        if(!data.empty())data+=L"\r\n";
        for(int c=std::min(g.col,g.anchorCol);c<=std::max(g.col,g.anchorCol);c++){if(c>std::min(g.col,g.anchorCol))data+=L'\t';auto cell=wide(g.rows[r][c]);
            if(cell.find_first_of(L"\t\r\n\"")!=std::wstring::npos){data+=L'"';for(auto ch:cell){data+=ch;if(ch==L'"')data+=ch;}data+=L'"';}else data+=cell;}
    }
    if(!OpenClipboard(g.win))return;HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE,(data.size()+1)*sizeof(wchar_t));
    if(h){auto p=GlobalLock(h);if(p){memcpy(p,data.c_str(),(data.size()+1)*sizeof(wchar_t));GlobalUnlock(h);EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,h))GlobalFree(h);}else GlobalFree(h);}CloseClipboard();
}
void paste(Grid& g){
    auto data=clipboard(g);if(data.empty())return;commit(g);
    int startRow=g.row,startCol=g.col,r=startRow,lastCol=startCol;
    // Excel quotes cells containing line breaks and doubles embedded quotes.
    std::vector<std::vector<std::wstring>> rows;std::vector<std::wstring> row;std::wstring cell;bool quoted=false,atStart=true;
    for(size_t pos=0;pos<data.size();pos++){
        wchar_t ch=data[pos];
        if(quoted){if(ch==L'"'){if(pos+1<data.size()&&data[pos+1]==L'"'){cell+=ch;pos++;}else quoted=false;}else cell+=ch;continue;}
        if(atStart&&ch==L'"'){quoted=true;atStart=false;continue;}
        if(ch==L'\t'||ch==L'\r'||ch==L'\n'){
            row.push_back(cell);cell.clear();atStart=true;
            if(ch!=L'\t'){rows.push_back(row);row.clear();if(ch==L'\r'&&pos+1<data.size()&&data[pos+1]==L'\n')pos++;}
        }else{cell+=ch;atStart=false;}
    }
    if(!cell.empty()||!row.empty()||data.back()==L'\t'){row.push_back(cell);rows.push_back(row);}
    for(auto& cells:rows){
        if(r>=100)break;if(size_t(r)>=g.rows.size())g.rows.resize(r+1,std::vector<std::string>(g.columns.size()));
        int c=startCol;for(auto& text:cells){if(size_t(c)>=g.columns.size())break;g.rows[r][c++]=utf8(text);}
        lastCol=std::max(lastCol,c-1);r++;
    }
    refresh(g);validate(g);selectCell(g,r-1,lastCol);g.anchorRow=startRow;g.anchorCol=startCol;InvalidateRect(g.list,nullptr,FALSE);
}
void insertStage(Grid& g,bool copy){
    commit(g);if(g.rows.empty()||g.rows.size()>57)return;
    int begin=g.row/2*2;bool hasTs=begin+1<int(g.rows.size());
    std::vector<std::string> state={"중간체","","",""},ts={"TS","","",""};
    if(copy){state=g.rows[begin];if(hasTs)ts=g.rows[begin+1];std::set<std::string> names;for(size_t i=0;i<g.rows.size();i+=2)names.insert(g.rows[i][1]);
        auto base=state[1].empty()?"Intermediate":state[1];int suffix=2;do{state[1]=base+" ("+std::to_string(suffix++)+")";}while(names.count(state[1]));}
    if(!hasTs)g.rows.push_back({"TS","","",""});
    int target=begin+2;g.rows.insert(g.rows.begin()+target,state);if(hasTs)g.rows.insert(g.rows.begin()+target+1,ts);
    refresh(g);validate(g);selectCell(g,target,1);
}
void deleteStage(Grid& g){
    commit(g);if(g.rows.empty())return;int begin=g.row/2*2,end=std::min(begin+2,int(g.rows.size()));
    g.rows.erase(g.rows.begin()+begin,g.rows.begin()+end);if(g.rows.empty())g.rows.push_back({"중간체","","",""});
    refresh(g);validate(g);selectCell(g,std::min(begin,int(g.rows.size())-1),1);
}
void duplicate(Grid& g){
    if(g.stages){insertStage(g,true);return;}
    commit(g);if(g.rows.empty()||g.rows.size()>=100)return;
    auto row=g.rows[g.row];std::set<std::string> names;for(auto& item:g.rows)names.insert(item[0]);
    auto base=row[0];int suffix=2;do{row[0]=base+" ("+std::to_string(suffix++)+")";}while(names.count(row[0]));
    int target=g.row+1;g.rows.insert(g.rows.begin()+target,row);refresh(g);validate(g);selectCell(g,target,0);
}
void editCell(Grid& g,bool replace=false);
void moveCell(Grid& g,WPARAM key,bool shift){
    int r=g.row,c=g.col;commit(g);
    if(key==VK_TAB){c+=shift?-1:1;if(c<0){c=int(g.columns.size())-1;r--;}if(c==int(g.columns.size())){c=0;r++;}}
    else if(key==VK_RETURN)r+=shift?-1:1;
    else if(key==VK_UP)r--;else if(key==VK_DOWN)r++;else if(key==VK_LEFT)c--;else if(key==VK_RIGHT)c++;
    if(g.stages&&r==int(g.rows.size())&&(key==VK_RETURN||key==VK_TAB)){
        selectCell(g,int(g.rows.size())-1,key==VK_TAB?int(g.columns.size())-1:g.col);
        SetFocus(g.apply);return;
    }
    if(r==int(g.rows.size())&&r<100&&(key==VK_RETURN||key==VK_TAB)){
        g.rows.push_back(std::vector<std::string>(g.columns.size()));refresh(g);validate(g);
    }
    selectCell(g,r,c,shift&&key!=VK_TAB&&key!=VK_RETURN);
}
LRESULT CALLBACK editProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR ref){
    auto& g=*reinterpret_cast<Grid*>(ref);
    if(msg==WM_IME_STARTCOMPOSITION)g.composing=true;
    if(msg==WM_IME_ENDCOMPOSITION)g.composing=false;
    if(msg==WM_KILLFOCUS)PostMessageW(g.win,CommitFocus,0,0);
    if(!g.composing&&multilineShortcut(hwnd,msg,wp))return 0;
    if(!g.composing&&g.scripts&&g.col==g.scriptColumn&&scriptShortcut(hwnd,msg,wp))return 0;
    if(msg==WM_KEYDOWN&&!g.composing&&wp!=VK_PROCESSKEY){
        bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        if(ctrl&&wp=='A'){SendMessageW(hwnd,EM_SETSEL,0,-1);return 0;}
        if(ctrl&&wp=='V'){auto data=clipboard(g);if(data.find_first_of(L"\t\r\n")!=std::wstring::npos){paste(g);return 0;}}
        if((wp==VK_UP||wp==VK_DOWN)&&SendMessageW(hwnd,EM_GETLINECOUNT,0,0)>1)return DefSubclassProc(hwnd,msg,wp,lp);
        if(wp==VK_TAB||wp==VK_RETURN||wp==VK_UP||wp==VK_DOWN){moveCell(g,wp,shift);return 0;}
        if(wp==VK_ESCAPE){auto editor=g.edit;g.edit=nullptr;DestroyWindow(editor);SetFocus(g.list);return 0;}
    }
    if(msg==WM_GETDLGCODE)return DLGC_WANTALLKEYS;
    return DefSubclassProc(hwnd,msg,wp,lp);
}
void editCell(Grid& g,bool replace){
    if(g.rows.empty())return;if(g.edit){SetFocus(g.edit);return;}
    g.anchorRow=g.row;g.anchorCol=g.col;RECT r{};ListView_GetSubItemRect(g.list,g.row,g.col+1,LVIR_BOUNDS,&r);
    g.edit=CreateWindowExW(0,L"EDIT",replace?L"":wide(g.rows[g.row][g.col]).c_str(),WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|ES_AUTOHSCROLL|ES_WANTRETURN|ES_NOHIDESEL,r.left+1,r.top+1,r.right-r.left-2,r.bottom-r.top-2,g.list,nullptr,nullptr,nullptr);
    SendMessageW(g.edit,WM_SETFONT,(WPARAM)g.font,TRUE);SetWindowSubclass(g.edit,editProc,1,(DWORD_PTR)&g);
    sizeEditor(g);SetFocus(g.edit);int caret=GetWindowTextLengthW(g.edit);SendMessageW(g.edit,EM_SETSEL,caret,caret);
}
LRESULT CALLBACK listProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR ref){
    auto& g=*reinterpret_cast<Grid*>(ref);bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
    if(msg==WM_GETDLGCODE)return DLGC_WANTALLKEYS;
    if(msg==WM_COMMAND&&HIWORD(wp)==EN_CHANGE&&(HWND)lp==g.edit){sizeEditor(g);return 0;}
    if((msg==WM_SYSKEYDOWN||msg==WM_KEYDOWN)&&wp==VK_RETURN&&(GetKeyState(VK_MENU)&0x8000)){editCell(g);if(g.edit)multilineShortcut(g.edit,msg,wp);return 0;}
    if(msg==WM_SYSCHAR&&wp==VK_RETURN)return 0;
    if(msg==WM_MOUSEWHEEL||msg==WM_HSCROLL||msg==WM_VSCROLL)commit(g);
    // Select on mouse-down before focus loss or native ListView notifications can swallow the click.
    if(msg==WM_LBUTTONUP)return 0;
    if(msg==WM_LBUTTONDOWN||msg==WM_LBUTTONDBLCLK){
        LVHITTESTINFO hit{};hit.pt={int(short(LOWORD(lp))),int(short(HIWORD(lp)))};
        ListView_SubItemHitTest(hwnd,&hit);
        if(hit.iItem>=0){selectCell(g,hit.iItem,std::max(0,hit.iSubItem-1),shift&&msg==WM_LBUTTONDOWN);if(msg==WM_LBUTTONDBLCLK)editCell(g);}return 0;
    }
    if(msg==WM_KEYDOWN){
        if(ctrl&&g.scripts&&g.col==g.scriptColumn&&(wp==VK_OEM_PLUS||wp==VK_ADD)){editCell(g);scriptShortcut(g.edit,msg,wp);return 0;}
        if(ctrl&&wp=='C'){copy(g);return 0;}if(ctrl&&wp=='V'){paste(g);return 0;}if(ctrl&&wp=='D'){duplicate(g);return 0;}
        if(ctrl&&wp=='A'&&!g.rows.empty()){g.anchorRow=0;g.anchorCol=0;g.row=int(g.rows.size())-1;g.col=int(g.columns.size())-1;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        if(wp==VK_F2){editCell(g);return 0;}
        if(wp==VK_TAB||wp==VK_RETURN||wp==VK_UP||wp==VK_DOWN||wp==VK_LEFT||wp==VK_RIGHT){moveCell(g,wp,shift);return 0;}
        if(wp==VK_DELETE&&!g.rows.empty()){
            for(int r=std::min(g.row,g.anchorRow);r<=std::max(g.row,g.anchorRow);r++)for(int c=std::min(g.col,g.anchorCol);c<=std::max(g.col,g.anchorCol);c++)g.rows[r][c].clear();
            refresh(g);validate(g);return 0;
        }
        if(wp==VK_ESCAPE){selectCell(g,g.row,g.col);return 0;}
        // Start the native editor before IME gets its first key.
        if(!ctrl&&!g.rows.empty()&&((wp>='0'&&wp<='Z')||wp==VK_PROCESSKEY||wp==VK_SPACE||(wp>=VK_NUMPAD0&&wp<=VK_DIVIDE)||(wp>=VK_OEM_1&&wp<=VK_OEM_7))){editCell(g,true);SendMessageW(g.edit,msg,wp,lp);return 0;}
    }
    if(msg==WM_CHAR){if(wp>=32&&!ctrl){editCell(g,true);SendMessageW(g.edit,msg,wp,lp);}return 0;}
    return DefSubclassProc(hwnd,msg,wp,lp);
}
bool apply(Grid& g){
    commit(g);
    if(!validate(g))return false;
    if(g.stages)g.rows=populatedRows(g);
    finish(g,true);return true;
}
void requestClose(Grid& g){
    commit(g);
    if(populatedRows(g)==g.original){finish(g,false);return;}
    int choice=MessageBoxW(g.win,L"표에 적용하지 않은 입력이 있습니다. 적용하고 닫을까요?\n\n예: 적용 후 닫기\n아니요: 입력 버리기\n취소: 표로 돌아가기",L"입력 내용 보관",MB_YESNOCANCEL|MB_ICONQUESTION|MB_DEFBUTTON3);
    if(choice==IDYES)apply(g);else if(choice==IDNO)finish(g,false);
}
LRESULT CALLBACK gridProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    auto* g=reinterpret_cast<Grid*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_CREATE){
        g=static_cast<Grid*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);g->win=hwnd;if(g->scriptColumn<0)g->scriptColumn=g->stages?1:0;SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)g);
        g->font=CreateFontW(-16,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,ui::interfaceFont());
        g->list=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,16,16,940,450,hwnd,(HMENU)10,nullptr,nullptr);
        SendMessageW(g->list,WM_SETFONT,(WPARAM)g->font,TRUE);ListView_SetExtendedListViewStyle(g->list,LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
        SetWindowSubclass(g->list,listProc,1,(DWORD_PTR)g);
        for(size_t c=0;c<=g->columns.size();c++){LVCOLUMNW col{};col.mask=LVCF_TEXT|LVCF_WIDTH;col.cx=c?std::max(120,870/int(g->columns.size())):45;auto name=c?g->columns[c-1]:L"행";col.pszText=name.data();ListView_InsertColumn(g->list,int(c),&col);}
        g->error=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,16,480,940,48,hwnd,nullptr,nullptr,nullptr);SendMessageW(g->error,WM_SETFONT,(WPARAM)g->font,TRUE);
        const wchar_t* labels[]={g->stages?L"단계 추가":L"행 추가",g->stages?L"단계 삭제":L"행 삭제",L"적용",L"취소",g->stages?L"단계 복제":L"행 복제"};const int positions[]={16,356,526,696,186};
        for(int i=0;i<5;i++){HWND b=CreateWindowW(L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,positions[i],540,155,34,hwnd,(HMENU)(INT_PTR)(20+i),nullptr,nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)g->font,TRUE);if(i==2)g->apply=b;}
        if(g->scripts)for(int i=0;i<2;i++){HWND b=CreateWindowW(L"BUTTON",i?L"x²":L"x₂",WS_CHILD|WS_VISIBLE|WS_TABSTOP,860+i*50,540,46,34,hwnd,(HMENU)(INT_PTR)(25+i),nullptr,nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)g->font,TRUE);}
        refresh(*g);validate(*g);return 0;
    }
    if(!g)return DefWindowProcW(hwnd,msg,wp,lp);
    if(msg==CommitFocus){int focus=GetDlgCtrlID(GetFocus());if(g->edit&&GetFocus()!=g->edit&&focus!=25&&focus!=26)commit(*g);return 0;}
    if(msg==WM_NOTIFY){
        auto* h=(NMHDR*)lp;
        if(h->hwndFrom==g->list&&(h->code==NM_CLICK||h->code==NM_DBLCLK)){
            auto* click=(NMITEMACTIVATE*)lp;if(click->iItem>=0){selectCell(*g,click->iItem,std::max(0,click->iSubItem-1),(GetKeyState(VK_SHIFT)&0x8000)!=0);if(h->code==NM_DBLCLK)editCell(*g);}return 0;
        }
        if(h->hwndFrom==g->list&&h->code==NM_CUSTOMDRAW){
            auto* draw=(NMLVCUSTOMDRAW*)lp;
            if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
            if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT)return CDRF_NOTIFYSUBITEMDRAW;
            if(draw->nmcd.dwDrawStage==(CDDS_ITEMPREPAINT|CDDS_SUBITEM)){
                int r=int(draw->nmcd.dwItemSpec),c=draw->iSubItem-1;
                bool selected=c>=0&&r>=std::min(g->row,g->anchorRow)&&r<=std::max(g->row,g->anchorRow)&&c>=std::min(g->col,g->anchorCol)&&c<=std::max(g->col,g->anchorCol);
                if(c>=0&&g->rows[r][c].find('\n')!=std::string::npos){
                    RECT cell{};ListView_GetSubItemRect(g->list,r,c+1,LVIR_BOUNDS,&cell);
                    auto brush=CreateSolidBrush(selected?RGB(218,238,250):RGB(255,255,255));FillRect(draw->nmcd.hdc,&cell,brush);DeleteObject(brush);
                    cell.left+=4;cell.top+=3;SetBkMode(draw->nmcd.hdc,TRANSPARENT);SetTextColor(draw->nmcd.hdc,RGB(0,0,0));
                    auto old=SelectObject(draw->nmcd.hdc,g->font);auto text=wide(g->rows[r][c]);
                    DrawTextW(draw->nmcd.hdc,text.c_str(),int(text.size()),&cell,DT_LEFT|DT_TOP|DT_NOPREFIX);SelectObject(draw->nmcd.hdc,old);
                    return CDRF_SKIPDEFAULT;
                }
                draw->clrTextBk=selected?RGB(218,238,250):RGB(255,255,255);draw->clrText=RGB(0,0,0);
                return CDRF_NEWFONT;
            }
        }
    }
    if(msg==WM_COMMAND){int id=LOWORD(wp);
        if((id==25||id==26)&&g->scripts){if(g->col!=g->scriptColumn){commit(*g);selectCell(*g,g->row,g->scriptColumn);}if(!g->edit)editCell(*g);if(g->edit)toggleScript(g->edit,id==26);return 0;}
        if(id>=20&&id<=24){
        commit(*g);
        if(g->stages&&id==20){insertStage(*g,false);return 0;}if(g->stages&&id==21){deleteStage(*g);return 0;}
        if(id==20&&g->rows.size()<100){int target=g->rows.empty()?0:g->row+1;g->rows.insert(g->rows.begin()+target,std::vector<std::string>(g->columns.size()));refresh(*g);validate(*g);selectCell(*g,target,0);editCell(*g);}
        if(id==21&&!g->rows.empty()){g->rows.erase(g->rows.begin()+g->row);refresh(*g);validate(*g);selectCell(*g,g->row,0);}
        if(id==24)duplicate(*g);
        if(id==22)apply(*g);if(id==23)requestClose(*g);return 0;
    }}
    if(msg==WM_DESTROY&&g->rowHeight){ListView_SetImageList(g->list,nullptr,LVSIL_SMALL);ImageList_Destroy(g->rowHeight);g->rowHeight=nullptr;}
    if(msg==WM_CLOSE){requestClose(*g);return 0;}return DefWindowProcW(hwnd,msg,wp,lp);
}
}
std::optional<Table> table(HWND parent,const std::wstring& title,const std::vector<std::wstring>& columns,const Table& rows,const TablePreview& preview,bool scripts,const std::wstring& hint,bool stages,int scriptColumn){
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&controls);static bool registered=false;
    if(!registered){WNDCLASSW wc{};wc.lpfnWndProc=gridProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawGrid";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&wc);registered=true;}
    Grid g;g.columns=columns;g.rows=rows;g.original=rows;g.preview=preview;g.scripts=scripts;g.hint=hint;g.stages=stages;g.scriptColumn=scriptColumn<0?(stages?1:0):scriptColumn;if(g.rows.empty())g.rows.push_back(std::vector<std::string>(columns.size()));RECT r;GetWindowRect(parent,&r);
    HWND hwnd=CreateWindowExW(WS_EX_DLGMODALFRAME,L"YoungDrawGrid",title.c_str(),WS_CAPTION|WS_SYSMENU|WS_POPUP,std::max(0L,(r.left+r.right-990)/2),std::max(0L,(r.top+r.bottom-640)/2),990,640,parent,nullptr,GetModuleHandleW(nullptr),&g);
    EnableWindow(parent,FALSE);ShowWindow(hwnd,SW_SHOW);selectCell(g,0,0);
    MSG msg;while(!g.done&&GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    EnableWindow(parent,TRUE);DestroyWindow(hwnd);DeleteObject(g.font);SetActiveWindow(parent);if(g.ok)return g.rows;return {};
}
}

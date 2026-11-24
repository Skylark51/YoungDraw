#include "win_button_help.h"
#include "win_dialogs.h"
#include "win_ui.h"
#include "win_dialog_style.h"
#include "win_scripts.h"
#include "scene.h"
#include <commctrl.h>
#include <algorithm>
#include <set>
namespace ui {
namespace {
constexpr UINT CommitFocus=WM_APP+41;
constexpr int DeleteColumns=29,ClearCells=30,SelectRow=31,SelectColumn=32,SelectAll=33;
struct Grid {
    DialogColors colors;
    struct Snapshot {Table rows;std::vector<std::wstring> columns;std::vector<size_t> sourceColumns;int row,col,anchorRow,anchorCol;size_t revision;};
    std::vector<Snapshot> undo,redo;
    size_t revision=0,nextRevision=1;
    bool dragging=false;
    Table rows,original; std::vector<std::wstring> columns; TablePreview preview;std::wstring hint;
    TableCellError cellError;
    TableColumnEdit columnEdit;
    TableReadback readback;
    TableStructurePreview structurePreview;
    std::vector<size_t> sourceColumns;
    std::vector<std::wstring> originalColumns;
    std::set<std::pair<int,int>> invalidCells;
    HWND win=nullptr,list=nullptr,edit=nullptr,error=nullptr,apply=nullptr; HFONT font=nullptr,italicFont=nullptr,interfaceFont=nullptr;
    int row=0,col=0,anchorRow=0,anchorCol=0,scriptColumn=-1,linePixels=20;
    HIMAGELIST rowHeight=nullptr;
    bool embedded=false,reactionStages=false,selectingRows=false;
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
void refresh(Grid& g);
TableHistory tableHistory(const Grid& g){
    TableHistory history{g.revision,{g.revision}};
    for(const auto& state:g.undo)history.retained.push_back(state.revision);
    for(const auto& state:g.redo)history.retained.push_back(state.revision);
    std::sort(history.retained.begin(),history.retained.end());
    history.retained.erase(std::unique(history.retained.begin(),history.retained.end()),history.retained.end());
    return history;
}
bool validate(Grid& g){
    std::wstring error;
    g.invalidCells.clear();
    if(g.cellError){
        for(int r=0;r<int(g.rows.size());r++)for(int c=0;c<int(g.columns.size());c++){
            auto message=g.cellError(c,g.rows[r][c]);
            if(message.empty())continue;
            g.invalidCells.emplace(r,c);
            if(error.empty())error=std::to_wstring(r+1)+L"행 · "+g.columns[c]+L": "+message;
        }
        if(g.invalidCells.size()>1)error+=L" ("+std::to_wstring(g.invalidCells.size())+L"개 셀)";
        InvalidateRect(g.list,nullptr,FALSE);
    }
    if(error.empty())try{
        if(g.structurePreview)error=g.structurePreview(g.rows,g.sourceColumns,g.columns,tableHistory(g));
        else if(g.preview)error=g.preview(g.stages||g.reactionStages?populatedRows(g):g.rows);
    }catch(const std::exception& e){error=wide(e.what());}
    if(error.empty()&&g.readback){auto rows=g.readback();if(rows!=g.rows&&std::all_of(rows.begin(),rows.end(),[&](const auto& row){return row.size()==g.columns.size();})){g.rows=std::move(rows);refresh(g);}}
    if(error.empty()&&g.structurePreview){SetWindowTextW(g.error,L"행 번호 / 열 제목: 전체 행·열 선택 · Shift+클릭 / 드래그: 범위 선택 · 우클릭: 편집 메뉴\nDelete: 셀 내용 지우기 · Ctrl+Delete: 선택 경로 삭제 · Ctrl+Z / Ctrl+Y: 되돌리기 / 다시 실행 · F2: 입력");return true;}
    if(error.empty()&&!g.hint.empty()){SetWindowTextW(g.error,(g.hint+L"\n드래그+Delete: 범위 지우기 · Ctrl+Z: 되돌리기 · Ctrl+= / Ctrl+Shift+=: 첨자 · Alt+Enter: 줄바꿈").c_str());return true;}
    SetWindowTextW(g.error,error.empty()?L"입력: 바로 타이핑 / F2 / 더블클릭 · Shift+방향키: 범위 선택 · Ctrl+C/V: 복사/붙여넣기\nAlt+Enter: 셀 안 줄바꿈 · Tab: 다음 셀 · Enter: 아래 셀 · Esc: 입력 취소 · Ctrl+D: 행 복제 · 드래그+Delete: 범위 지우기 · Ctrl+Z: 되돌리기 · Ctrl+= / Ctrl+Shift+=: 첨자":error.c_str());
    // Never disable Apply from stale validation: the active editor may fix the error.
    return error.empty();
}
void resizeRows(Grid& g){
    size_t lines=1;for(auto& row:g.rows)for(auto& cell:row)lines=std::max(lines,size_t(1+std::count(cell.begin(),cell.end(),'\n')));
    int height=int(lines)*g.linePixels+6;
    if(g.rowHeight){int w=0,h=0;ImageList_GetIconSize(g.rowHeight,&w,&h);if(h==height)return;}
    auto images=ImageList_Create(1,height,ILC_COLOR32,1,1);
    if(images){ListView_SetImageList(g.list,images,LVSIL_SMALL);if(g.rowHeight)ImageList_Destroy(g.rowHeight);g.rowHeight=images;}
}
void sizeEditor(Grid& g){
    if(!g.edit)return;auto text=value(g.edit);RECT cell{},client{};
    ListView_GetSubItemRect(g.list,g.row,g.col+1,LVIR_BOUNDS,&cell);GetClientRect(g.list,&client);
    int height=std::max(int(cell.bottom-cell.top)-2,g.linePixels+6+g.linePixels*int(std::count(text.begin(),text.end(),L'\n')));
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
void refreshColumns(Grid& g){
    while(ListView_DeleteColumn(g.list,0)){}
    constexpr int reactionWidths[]={35,140,80,80,57,57,73,69,61,61,62,84};
    for(size_t c=0;c<=g.columns.size();++c){
        LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;
        column.cx=g.reactionStages?reactionWidths[c]:(c?std::max(120,870/int(g.columns.size())):45);
        auto name=c?g.columns[c-1]:L"행";column.pszText=name.data();ListView_InsertColumn(g.list,int(c),&column);
    }
}
void checkpoint(Grid& g){
    g.undo.push_back({g.rows,g.columns,g.sourceColumns,g.row,g.col,g.anchorRow,g.anchorCol,g.revision});
    if(g.undo.size()>100)g.undo.erase(g.undo.begin());
    g.redo.clear();
    g.revision=g.nextRevision++;
}
void commit(Grid& g){
    if(!g.edit)return;
    HWND editor=g.edit;auto text=value(editor);g.edit=nullptr;g.composing=false;DestroyWindow(editor);
    if(g.rows[g.row][g.col]!=utf8(text)){
        checkpoint(g);g.rows[g.row][g.col]=utf8(text);ListView_SetItemText(g.list,g.row,g.col+1,text.data());resizeRows(g);InvalidateRect(g.list,nullptr,FALSE);validate(g);
    }
}
void selectCell(Grid& g,int row,int col,bool extend=false){
    if(g.rows.empty()){g.row=g.col=g.anchorRow=g.anchorCol=0;return;}
    commit(g);g.row=std::clamp(row,0,int(g.rows.size())-1);g.col=std::clamp(col,0,int(g.columns.size())-1);
    if(!extend){g.anchorRow=g.row;g.anchorCol=g.col;}
    ListView_SetItemState(g.list,g.row,LVIS_FOCUSED,LVIS_FOCUSED);ListView_EnsureVisible(g.list,g.row,FALSE);
    // Keep the active column visible even with many pathways.
    RECT cell{},client{};ListView_GetSubItemRect(g.list,g.row,g.col+1,LVIR_BOUNDS,&cell);GetClientRect(g.list,&client);
    if(cell.left<45)ListView_Scroll(g.list,cell.left-45,0);else if(cell.right>client.right)ListView_Scroll(g.list,cell.right-client.right,0);
    SetFocus(g.list);InvalidateRect(g.list,nullptr,FALSE);
}
void undo(Grid& g,bool redo=false){
    commit(g);
    auto& source=redo?g.redo:g.undo;auto& target=redo?g.undo:g.redo;
    if(source.empty())return;
    target.push_back({g.rows,g.columns,g.sourceColumns,g.row,g.col,g.anchorRow,g.anchorCol,g.revision});
    auto state=std::move(source.back());source.pop_back();g.rows=std::move(state.rows);
    if(g.columns!=state.columns){g.columns=std::move(state.columns);refreshColumns(g);}
    g.sourceColumns=std::move(state.sourceColumns);
    g.revision=state.revision;
    refresh(g);selectCell(g,state.row,state.col);g.anchorRow=state.anchorRow;g.anchorCol=state.anchorCol;
    validate(g);InvalidateRect(g.list,nullptr,FALSE);
}
void selectRow(Grid& g,int row,bool extend=false){
    if(g.rows.empty())return;
    const int anchor=extend?g.anchorRow:row;
    selectCell(g,row,int(g.columns.size())-1,true);g.anchorRow=anchor;g.anchorCol=0;
    InvalidateRect(g.list,nullptr,FALSE);
}
void selectColumn(Grid& g,int column,bool extend=false){
    if(g.rows.empty())return;
    const int anchor=extend?g.anchorCol:column;
    selectCell(g,int(g.rows.size())-1,column,true);g.anchorRow=0;g.anchorCol=anchor;
    InvalidateRect(g.list,nullptr,FALSE);
}
void selectAll(Grid& g){
    if(g.rows.empty())return;
    selectCell(g,int(g.rows.size())-1,int(g.columns.size())-1);g.anchorRow=g.anchorCol=0;
    InvalidateRect(g.list,nullptr,FALSE);
}
void clearCells(Grid& g){
    commit(g);if(g.rows.empty())return;
    bool changed=false;
    for(int r=std::min(g.row,g.anchorRow);r<=std::max(g.row,g.anchorRow);++r)
        for(int c=std::min(g.col,g.anchorCol);c<=std::max(g.col,g.anchorCol);++c)changed|=!g.rows[r][c].empty();
    if(!changed)return;
    checkpoint(g);
    for(int r=std::min(g.row,g.anchorRow);r<=std::max(g.row,g.anchorRow);++r)
        for(int c=std::min(g.col,g.anchorCol);c<=std::max(g.col,g.anchorCol);++c)g.rows[r][c].clear();
    refresh(g);validate(g);
}
void deleteColumns(Grid& g){
    commit(g);if(!g.structurePreview)return;
    // Structural edits need a validated model for the current revision so
    // undo can restore exact path identities, annotations and energy values.
    if(!validate(g))return;
    const int first=std::max(1,std::min(g.col,g.anchorCol)),last=std::max(g.col,g.anchorCol);
    if(last<first){SetWindowTextW(g.error,L"삭제할 경로의 셀이나 열 제목을 선택하세요. 단계명 열은 삭제할 수 없습니다.");return;}
    if(int(g.columns.size())-(last-first+1)<2){SetWindowTextW(g.error,L"그래프에는 최소 한 개의 경로가 필요합니다. 남길 경로를 제외하고 선택하세요.");return;}
    checkpoint(g);
    for(auto& row:g.rows)row.erase(row.begin()+first,row.begin()+last+1);
    g.columns.erase(g.columns.begin()+first,g.columns.begin()+last+1);
    g.sourceColumns.erase(g.sourceColumns.begin()+first,g.sourceColumns.begin()+last+1);
    refreshColumns(g);refresh(g);selectCell(g,g.row,std::min(first,int(g.columns.size())-1));validate(g);
}
std::wstring clipboard(Grid& g){
    std::wstring data;if(!OpenClipboard(g.win))return data;HANDLE h=GetClipboardData(CF_UNICODETEXT);
    if(h){auto p=static_cast<const wchar_t*>(GlobalLock(h));if(p){data=p;GlobalUnlock(h);}}CloseClipboard();return data;
}
void copy(Grid& g){
    if(g.rows.empty())return;commit(g);std::wstring data;
    for(int r=std::min(g.row,g.anchorRow);r<=std::max(g.row,g.anchorRow);r++){
        if(r>std::min(g.row,g.anchorRow))data+=L"\r\n";
        for(int c=std::min(g.col,g.anchorCol);c<=std::max(g.col,g.anchorCol);c++){if(c>std::min(g.col,g.anchorCol))data+=L'\t';auto cell=wide(g.rows[r][c]);
            if(cell.find_first_of(L"\t\r\n\"")!=std::wstring::npos){data+=L'"';for(auto ch:cell){data+=ch;if(ch==L'"')data+=ch;}data+=L'"';}else data+=cell;}
    }
    if(!OpenClipboard(g.win))return;HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE,(data.size()+1)*sizeof(wchar_t));
    if(h){auto p=GlobalLock(h);if(p){memcpy(p,data.c_str(),(data.size()+1)*sizeof(wchar_t));GlobalUnlock(h);EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,h))GlobalFree(h);}else GlobalFree(h);}CloseClipboard();
}
void paste(Grid& g,const std::wstring& data){
    if(data.empty())return;
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
    if(!atStart||!row.empty()){row.push_back(cell);rows.push_back(row);}
    if(quoted){SetWindowTextW(g.error,L"붙여넣을 범위에 닫히지 않은 따옴표가 있습니다.");return;}
    if(rows.size()>size_t(100-startRow)||std::any_of(rows.begin(),rows.end(),[&](const auto& cells){
        return cells.size()>g.columns.size()-startCol;
    })){
        SetWindowTextW(g.error,L"붙여넣을 범위가 표를 벗어납니다. 시작 셀과 열 개수를 확인하세요. (최대 100행)");
        return;
    }
    commit(g);checkpoint(g);
    for(auto& cells:rows){
        if(size_t(r)>=g.rows.size())g.rows.resize(r+1,std::vector<std::string>(g.columns.size()));
        int c=startCol;for(auto& text:cells)g.rows[r][c++]=utf8(text);
        lastCol=std::max(lastCol,c-1);r++;
    }
    refresh(g);validate(g);selectCell(g,r-1,lastCol);g.anchorRow=startRow;g.anchorCol=startCol;InvalidateRect(g.list,nullptr,FALSE);
}
void paste(Grid& g){paste(g,clipboard(g));}
void insertStage(Grid& g,bool copy){
    commit(g);if(g.rows.empty()||g.rows.size()>57)return;
    checkpoint(g);int begin=g.row/2*2;bool hasTs=begin+1<int(g.rows.size());
    std::vector<std::string> state={"중간체","","",""},ts={"TS","","",""};
    if(copy){state=g.rows[begin];if(hasTs)ts=g.rows[begin+1];std::set<std::string> names;for(size_t i=0;i<g.rows.size();i+=2)names.insert(g.rows[i][1]);
        auto base=state[1].empty()?"Intermediate":state[1];int suffix=2;do{state[1]=base+" ("+std::to_string(suffix++)+")";}while(names.count(state[1]));}
    if(!hasTs)g.rows.push_back({"TS","","",""});
    int target=begin+2;g.rows.insert(g.rows.begin()+target,state);if(hasTs)g.rows.insert(g.rows.begin()+target+1,ts);
    refresh(g);validate(g);selectCell(g,target,1);
}
void deleteStage(Grid& g){
    commit(g);if(g.rows.empty())return;checkpoint(g);int begin=g.row/2*2,end=std::min(begin+2,int(g.rows.size()));
    g.rows.erase(g.rows.begin()+begin,g.rows.begin()+end);if(g.rows.empty())g.rows.push_back({"중간체","","",""});
    refresh(g);validate(g);selectCell(g,std::min(begin,int(g.rows.size())-1),1);
}
void duplicate(Grid& g){
    if(g.stages){insertStage(g,true);return;}
    commit(g);if(g.rows.empty()||g.rows.size()>=100)return;
    checkpoint(g);auto row=g.rows[g.row];std::set<std::string> names;for(auto& item:g.rows)names.insert(item[0]);
    auto base=row[0];int suffix=2;do{row[0]=base+" ("+std::to_string(suffix++)+")";}while(names.count(row[0]));
    int target=g.row+1;g.rows.insert(g.rows.begin()+target,row);refresh(g);validate(g);selectCell(g,target,0);
}
void editCell(Grid& g,bool replace=false);
void moveCell(Grid& g,WPARAM key,bool shift){
    int r=g.row,c=g.col;commit(g);
    if(key==VK_TAB){
        c+=shift?-1:1;
        if(c<0){if(r==0)return;c=int(g.columns.size())-1;r--;}
        if(c==int(g.columns.size())){if(r+1>=100)return;c=0;r++;}
    }
    else if(key==VK_RETURN)r+=shift?-1:1;
    else if(key==VK_UP)r--;else if(key==VK_DOWN)r++;else if(key==VK_LEFT)c--;else if(key==VK_RIGHT)c++;
    if(g.stages&&r==int(g.rows.size())&&(key==VK_RETURN||key==VK_TAB)){
        selectCell(g,int(g.rows.size())-1,key==VK_TAB?int(g.columns.size())-1:g.col);
        SetFocus(g.apply);return;
    }
    if(r==int(g.rows.size())&&r<100&&(key==VK_RETURN||key==VK_TAB)){
        checkpoint(g);g.rows.push_back(std::vector<std::string>(g.columns.size()));refresh(g);validate(g);
    }
    selectCell(g,r,c,shift&&key!=VK_TAB&&key!=VK_RETURN);
}
LRESULT CALLBACK editProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR ref){
    auto& g=*reinterpret_cast<Grid*>(ref);
    if(msg==WM_IME_STARTCOMPOSITION)g.composing=true;
    if(msg==WM_IME_ENDCOMPOSITION)g.composing=false;
    if(msg==WM_KILLFOCUS)PostMessageW(g.win,CommitFocus,0,0);
    if(!g.composing&&multilineShortcut(hwnd,msg,wp))return 0;
    if(!g.composing&&g.embedded&&(!g.reactionStages||g.col==0)&&italicShortcut(hwnd,msg,wp))return 0;
    if(!g.composing&&g.scripts&&g.col==g.scriptColumn&&scriptShortcut(hwnd,msg,wp))return 0;
    if(msg==WM_KEYDOWN&&!g.composing&&wp!=VK_PROCESSKEY){
        bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        if(ctrl&&wp=='Z'&&(!SendMessageW(hwnd,EM_CANUNDO,0,0)||shift)){undo(g,shift);return 0;}
        if(ctrl&&wp=='A'){SendMessageW(hwnd,EM_SETSEL,0,-1);return 0;}
        if(ctrl&&wp=='V'){auto data=clipboard(g);if(data.find_first_of(L"\t\r\n")!=std::wstring::npos){paste(g,data);return 0;}}
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
void drawGridText(const Grid& grid,HDC dc,const std::wstring& value,RECT cell){
    const auto input=utf8(value);
    if(input.find("~{")==std::string::npos){
        DrawTextW(dc,value.c_str(),-1,&cell,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        return;
    }
    const int saved=SaveDC(dc);
    IntersectClipRect(dc,cell.left,cell.top,cell.right,cell.bottom);
    for(const auto& run:yd::scriptRuns(input)){
        const auto text=wide(run.text);
        SelectObject(dc,run.italic?grid.italicFont:grid.font);
        SIZE size{};GetTextExtentPoint32W(dc,text.c_str(),int(text.size()),&size);
        DrawTextW(dc,text.c_str(),int(text.size()),&cell,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_NOCLIP);
        cell.left+=size.cx;
        if(cell.left>=cell.right)break;
    }
    RestoreDC(dc,saved);
}
void contextMenu(Grid& g,LPARAM position){
    commit(g);
    POINT screen{int(short(LOWORD(position))),int(short(HIWORD(position)))};
    if(screen.x==-1&&screen.y==-1){
        RECT cell{};ListView_GetSubItemRect(g.list,g.row,g.col+1,LVIR_BOUNDS,&cell);
        screen={cell.left,cell.bottom};ClientToScreen(g.list,&screen);
    }else{
        RECT headerRect{};auto header=ListView_GetHeader(g.list);GetWindowRect(header,&headerRect);
        POINT point=screen;
        if(PtInRect(&headerRect,screen)){
            ScreenToClient(header,&point);HDHITTESTINFO hit{};hit.pt=point;
            const int column=int(SendMessageW(header,HDM_HITTEST,0,reinterpret_cast<LPARAM>(&hit)))-1;
            if(column<0)selectAll(g);
            else if(column<std::min(g.col,g.anchorCol)||column>std::max(g.col,g.anchorCol))selectColumn(g,column);
        }else{
            ScreenToClient(g.list,&point);LVHITTESTINFO hit{};hit.pt=point;ListView_SubItemHitTest(g.list,&hit);
            if(hit.iItem>=0){
                const int column=hit.iSubItem-1;
                if(column<0)selectRow(g,hit.iItem);
                else if(hit.iItem<std::min(g.row,g.anchorRow)||hit.iItem>std::max(g.row,g.anchorRow)||
                    column<std::min(g.col,g.anchorCol)||column>std::max(g.col,g.anchorCol))selectCell(g,hit.iItem,column);
            }
        }
    }
    HMENU menu=CreatePopupMenu();if(!menu)return;
    const UINT cellFlags=MF_STRING|(g.rows.empty()?MF_GRAYED:0);
    AppendMenuW(menu,cellFlags,ClearCells,L"선택 셀 비우기\tDelete");
    AppendMenuW(menu,cellFlags,21,g.stages||g.reactionStages?L"선택 단계 삭제":L"선택 행 삭제");
    if(g.structurePreview){
        const int first=std::max(1,std::min(g.col,g.anchorCol)),last=std::max(g.col,g.anchorCol);
        const bool allowed=last>=first&&int(g.columns.size())-(last-first+1)>=2;
        AppendMenuW(menu,MF_STRING|(allowed?0:MF_GRAYED),DeleteColumns,L"선택 열(경로) 삭제");
    }
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,cellFlags,SelectRow,L"행 전체 선택");AppendMenuW(menu,cellFlags,SelectColumn,L"열 전체 선택");
    AppendMenuW(menu,cellFlags,SelectAll,L"표 전체 선택\tCtrl+A");
    const UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,screen.x,screen.y,0,g.win,nullptr);
    DestroyMenu(menu);if(command)SendMessageW(g.win,WM_COMMAND,command,0);
}
LRESULT CALLBACK listProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR ref){
    auto& g=*reinterpret_cast<Grid*>(ref);bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
    if(msg==WM_NOTIFY){
        const auto& header=*reinterpret_cast<NMHDR*>(lp);
        if(header.hwndFrom==ListView_GetHeader(hwnd)&&(header.code==HDN_ITEMCLICKW||header.code==HDN_ITEMCLICKA)){
            const auto& item=*reinterpret_cast<const NMHEADERW*>(lp);
            if(item.iButton==0){if(item.iItem==0)selectAll(g);else selectColumn(g,item.iItem-1,shift);}
            return 0;
        }
        if(header.hwndFrom==ListView_GetHeader(hwnd)&&g.columnEdit&&
           (header.code==HDN_ITEMDBLCLICKW||header.code==HDN_ITEMDBLCLICKA)){
            const int column=reinterpret_cast<const NMHEADERW*>(lp)->iItem-1;
            if(column<0||size_t(column)>=g.columns.size())return 0;
            commit(g);
            if(auto title=g.columnEdit(g.win,size_t(column));title&&*title!=g.columns[column]){
                if(g.structurePreview)checkpoint(g);
                g.columns[column]=std::move(*title);
                LVCOLUMNW item{};item.mask=LVCF_TEXT;item.pszText=g.columns[column].data();
                ListView_SetColumn(hwnd,column+1,&item);
                validate(g);
            }
            SetFocus(hwnd);
            return 0;
        }
        if(header.hwndFrom==ListView_GetHeader(hwnd)&&header.code==NM_CUSTOMDRAW){
            auto& draw=*reinterpret_cast<NMCUSTOMDRAW*>(lp);
            if(draw.dwDrawStage==CDDS_PREPAINT){
                if(g.embedded)FillRect(draw.hdc,&draw.rc,g.colors.panel);
                return CDRF_NOTIFYITEMDRAW|(g.embedded?CDRF_NOTIFYPOSTPAINT:0);
            }
            if(draw.dwDrawStage==CDDS_POSTPAINT&&g.embedded){
                RECT remainder{};
                GetClientRect(header.hwndFrom,&remainder);
                RECT last{};
                if(const int count=Header_GetItemCount(header.hwndFrom);count>0&&Header_GetItemRect(header.hwndFrom,count-1,&last))
                    remainder.left=last.right;
                FillRect(draw.hdc,&remainder,g.colors.panel);
                return CDRF_SKIPDEFAULT;
            }
            if(draw.dwDrawStage==CDDS_ITEMPREPAINT){
                wchar_t title[512]{};
                HDITEMW column{};column.mask=HDI_TEXT;column.pszText=title;column.cchTextMax=512;
                SendMessageW(header.hwndFrom,HDM_GETITEMW,draw.dwItemSpec,reinterpret_cast<LPARAM>(&column));
                RECT cell=draw.rc;
                FillRect(draw.hdc,&cell,g.colors.panel);
                SetDCBrushColor(draw.hdc,colorRef(g.colors.palette.edge));
                FrameRect(draw.hdc,&cell,(HBRUSH)GetStockObject(DC_BRUSH));
                cell.left+=6;cell.right-=6;
                SetTextColor(draw.hdc,colorRef(g.colors.palette.text));SetBkMode(draw.hdc,TRANSPARENT);
                drawGridText(g,draw.hdc,title,cell);
                return CDRF_SKIPDEFAULT;
            }
        }
    }
    if(msg==WM_CTLCOLOREDIT)return g.colors.control(msg,(HDC)wp);
    if(msg==WM_GETDLGCODE)return DLGC_WANTALLKEYS;
    if(msg==WM_COMMAND&&HIWORD(wp)==EN_CHANGE&&(HWND)lp==g.edit){sizeEditor(g);return 0;}
    if(msg==WM_CONTEXTMENU){contextMenu(g,lp);return 0;}
    if((msg==WM_SYSKEYDOWN||msg==WM_KEYDOWN)&&wp==VK_RETURN&&(GetKeyState(VK_MENU)&0x8000)){editCell(g);if(g.edit)multilineShortcut(g.edit,msg,wp);return 0;}
    if(msg==WM_SYSCHAR&&wp==VK_RETURN)return 0;
    if(msg==WM_MOUSEWHEEL||msg==WM_HSCROLL||msg==WM_VSCROLL)commit(g);
    // Select on mouse-down before focus loss or native ListView notifications can swallow the click.
    if(msg==WM_LBUTTONUP){g.dragging=false;if(GetCapture()==hwnd)ReleaseCapture();return 0;}
    if(msg==WM_CAPTURECHANGED){g.dragging=false;}
    if(msg==WM_MOUSEMOVE&&g.dragging&&(wp&MK_LBUTTON)){
        LVHITTESTINFO hit{};hit.pt={int(short(LOWORD(lp))),int(short(HIWORD(lp)))};
        ListView_SubItemHitTest(hwnd,&hit);
        if(hit.iItem>=0){if(g.selectingRows)selectRow(g,hit.iItem,true);else selectCell(g,hit.iItem,std::max(0,hit.iSubItem-1),true);}
        return 0;
    }
    if(msg==WM_LBUTTONDOWN||msg==WM_LBUTTONDBLCLK){
        LVHITTESTINFO hit{};hit.pt={int(short(LOWORD(lp))),int(short(HIWORD(lp)))};
        ListView_SubItemHitTest(hwnd,&hit);
        if(hit.iItem>=0){
            g.selectingRows=hit.iSubItem==0;
            if(hit.iSubItem==0)selectRow(g,hit.iItem,shift&&msg==WM_LBUTTONDOWN);
            else selectCell(g,hit.iItem,std::max(0,hit.iSubItem-1),shift&&msg==WM_LBUTTONDOWN);
            if(msg==WM_LBUTTONDBLCLK&&hit.iSubItem>0)editCell(g);else{g.dragging=true;SetCapture(hwnd);}
        }return 0;
    }
    if(msg==WM_KEYDOWN){
        if(ctrl&&g.embedded&&wp=='I'&&(!g.reactionStages||g.col==0)){editCell(g);if(g.edit)italicShortcut(g.edit,msg,wp);return 0;}
        if(ctrl&&g.scripts&&g.col==g.scriptColumn&&(wp==VK_OEM_PLUS||wp==VK_ADD)){editCell(g);scriptShortcut(g.edit,msg,wp);return 0;}
        if(ctrl&&wp=='Z'){undo(g,shift);return 0;}if(ctrl&&wp=='Y'){undo(g,true);return 0;}
        if(ctrl&&wp=='C'){copy(g);return 0;}if(ctrl&&wp=='V'){paste(g);return 0;}if(ctrl&&wp=='D'){duplicate(g);return 0;}
        if(ctrl&&wp=='A'){selectAll(g);return 0;}
        if(ctrl&&wp==VK_SPACE){selectColumn(g,g.col,shift);return 0;}
        if(shift&&wp==VK_SPACE){selectRow(g,g.row);return 0;}
        if(wp==VK_F2){editCell(g);return 0;}
        if(wp==VK_TAB||wp==VK_RETURN||wp==VK_UP||wp==VK_DOWN||wp==VK_LEFT||wp==VK_RIGHT){moveCell(g,wp,shift);return 0;}
        if(wp==VK_DELETE){if(ctrl&&g.structurePreview)deleteColumns(g);else clearCells(g);return 0;}
        if(wp==VK_ESCAPE){selectCell(g,g.row,g.col);return 0;}
        // Start the native editor before IME gets its first key.
        if(!ctrl&&!g.rows.empty()&&((wp>='0'&&wp<='Z')||wp==VK_PROCESSKEY||wp==VK_SPACE||(wp>=VK_NUMPAD0&&wp<=VK_DIVIDE)||(wp>=VK_OEM_1&&wp<=VK_OEM_7))){editCell(g,true);SendMessageW(g.edit,msg,wp,lp);return 0;}
    }
    if(msg==WM_CHAR){if(wp>=32&&!ctrl){editCell(g,true);SendMessageW(g.edit,msg,wp,lp);}return 0;}
    return DefSubclassProc(hwnd,msg,wp,lp);
}
bool apply(Grid& g){
    commit(g);
    if(!validate(g)){
        if(!g.invalidCells.empty()){
            auto [row,col]=*g.invalidCells.begin();
            selectCell(g,row,col);
        }
        return false;
    }
    if(g.stages)g.rows=populatedRows(g);
    finish(g,true);return true;
}
void requestClose(Grid& g){
    commit(g);
    if(((!g.columnEdit&&!g.structurePreview)||g.columns==g.originalColumns)&&(g.stages?populatedRows(g):g.rows)==g.original){finish(g,false);return;}
    int choice=appearance::messageBox(g.win,L"표에 적용하지 않은 입력이 있습니다. 적용하고 닫을까요?\n\n예: 적용 후 닫기\n아니요: 입력 버리기\n취소: 표로 돌아가기",L"입력 내용 보관",MB_YESNOCANCEL|MB_ICONQUESTION|MB_DEFBUTTON3);
    if(choice==IDYES)apply(g);else if(choice==IDNO)finish(g,false);
}
LRESULT CALLBACK gridProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    auto* g=reinterpret_cast<Grid*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_CREATE){ui::enableButtonHelp();
        g=static_cast<Grid*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);g->win=hwnd;if(g->scriptColumn<0)g->scriptColumn=g->stages?1:0;SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)g);
        if(g->originalColumns.empty())g->originalColumns=g->columns;
        if(g->sourceColumns.empty())for(size_t c=0;c<g->columns.size();++c)g->sourceColumns.push_back(c);
        g->font=CreateFontW(-16,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,ui::interfaceFont());
        g->list=CreateWindowExW(g->embedded?0:WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,16,16,940,450,hwnd,(HMENU)10,nullptr,nullptr);
        SendMessageW(g->list,WM_SETFONT,(WPARAM)g->font,TRUE);ListView_SetExtendedListViewStyle(g->list,LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
        LOGFONTW italic{};GetObjectW(g->font,sizeof(italic),&italic);italic.lfItalic=TRUE;g->italicFont=CreateFontIndirectW(&italic);
        if(g->embedded)appearance::nativeControl(g->list);
        ListView_SetBkColor(g->list,colorRef(g->colors.palette.raised));
        ListView_SetTextBkColor(g->list,colorRef(g->colors.palette.raised));
        ListView_SetTextColor(g->list,colorRef(g->colors.palette.text));
        SetWindowSubclass(g->list,listProc,1,(DWORD_PTR)g);
        constexpr int reactionWidths[]={35,140,80,80,57,57,73,69,61,61,62,84};
        for(size_t c=0;c<=g->columns.size();c++){LVCOLUMNW col{};col.mask=LVCF_TEXT|LVCF_WIDTH;col.cx=g->reactionStages?reactionWidths[c]:(c?std::max(120,870/int(g->columns.size())):45);auto name=c?g->columns[c-1]:L"행";col.pszText=name.data();ListView_InsertColumn(g->list,int(c),&col);}
        g->error=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,16,480,940,48,hwnd,nullptr,nullptr,nullptr);SendMessageW(g->error,WM_SETFONT,(WPARAM)g->font,TRUE);
        const wchar_t* labels[]={g->stages||g->reactionStages?L"단계 추가":L"행 추가",g->stages||g->reactionStages?L"단계 삭제":L"행 삭제",L"적용",L"취소",g->stages||g->reactionStages?L"단계 복제":L"행 복제"};const int positions[]={16,356,526,696,186};
        for(int i=0;i<5;i++){HWND b=CreateWindowW(L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,positions[i],540,155,34,hwnd,(HMENU)(INT_PTR)(20+i),nullptr,nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)g->font,TRUE);if(i==2)g->apply=b;}
        if(g->scripts)for(int i=0;i<2;i++){HWND b=CreateWindowW(L"BUTTON",i?L"x²":L"x₂",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,860+i*50,540,46,34,hwnd,(HMENU)(INT_PTR)(25+i),nullptr,nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)g->font,TRUE);}
        for(int i=0;i<2;i++){HWND b=CreateWindowW(L"BUTTON",g->embedded?(i?L"표 다시 실행":L"표 실행 취소"):(i?L"다시 실행 (Ctrl+Shift+Z)":L"되돌리기 (Ctrl+Z)"),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,16+i*240,580,230,28,hwnd,(HMENU)(INT_PTR)(27+i),nullptr,nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)g->font,TRUE);}
        if(g->structurePreview){HWND b=CreateWindowW(L"BUTTON",L"열(경로) 삭제",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,496,580,180,28,hwnd,(HMENU)(INT_PTR)DeleteColumns,nullptr,nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)g->font,TRUE);}
        refresh(*g);validate(*g);return 0;
    }
    if(!g)return DefWindowProcW(hwnd,msg,wp,lp);
    if(msg==WM_SIZE&&g->embedded){
        RECT r{};GetClientRect(hwnd,&r);int w=r.right,h=r.bottom;
        MoveWindow(g->list,0,0,w,std::max(60,h-100),TRUE);
        if(g->reactionStages){
            int remaining=w-GetSystemMetrics(SM_CXVSCROLL);
            for(int c=0;c<=int(g->columns.size());++c)if(c!=1)remaining-=ListView_GetColumnWidth(g->list,c);
            ListView_SetColumnWidth(g->list,1,std::max(152,remaining));
        }
        MoveWindow(g->error,0,std::max(60,h-96),w,44,TRUE);
        const int stageIds[]={20,24,21,27,28,25,26},dataIds[]={20,24,21,DeleteColumns,27,28},plainIds[]={20,24,21,27,28};
        const int* ids=g->reactionStages?stageIds:g->structurePreview?dataIds:plainIds;
        const int count=g->reactionStages?7:g->structurePreview?6:5;
        const int bw=std::max(70,(w-(count-1)*6)/count);
        for(int i=0;i<count;i++)MoveWindow(GetDlgItem(hwnd,ids[i]),i*(bw+6),h-38,bw,30,TRUE);
        ShowWindow(GetDlgItem(hwnd,22),SW_HIDE);ShowWindow(GetDlgItem(hwnd,23),SW_HIDE);
        if(!g->reactionStages)for(int i=25;i<=26;i++)ShowWindow(GetDlgItem(hwnd,i),SW_HIDE);
        sizeEditor(*g);return 0;
    }
    if(msg==WM_NCDESTROY&&g->embedded){SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);DeleteObject(g->font);DeleteObject(g->italicFont);if(g->interfaceFont)DeleteObject(g->interfaceFont);delete g;return DefWindowProcW(hwnd,msg,wp,lp);}
    if(msg==WM_ERASEBKGND){RECT r;GetClientRect(hwnd,&r);FillRect((HDC)wp,&r,g->colors.panel);return 1;}
    if(msg==WM_CTLCOLORSTATIC)return g->colors.control(msg,(HDC)wp);
    if(msg==WM_DRAWITEM){const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);g->colors.draw(item,item.CtlID==22);return TRUE;}
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
                bool invalid=g->invalidCells.count({r,c})!=0;
                const auto& palette=g->colors.palette;
                COLORREF background=invalid?(selected?RGB(249,183,184):RGB(255,222,222)):colorRef(selected?palette.accent:c<0?palette.panel:palette.raised);
                COLORREF foreground=invalid?RGB(130,19,24):colorRef(selected?palette.onAccent:palette.text);
                if(c>=0&&(g->rows[r][c].find('\n')!=std::string::npos||g->rows[r][c].find("~{")!=std::string::npos)){
                    RECT cell{};ListView_GetSubItemRect(g->list,r,c+1,LVIR_BOUNDS,&cell);
                    auto brush=CreateSolidBrush(background);FillRect(draw->nmcd.hdc,&cell,brush);DeleteObject(brush);
                    cell.left+=4;cell.top+=3;SetBkMode(draw->nmcd.hdc,TRANSPARENT);SetTextColor(draw->nmcd.hdc,foreground);
                    auto old=SelectObject(draw->nmcd.hdc,g->font);auto text=wide(g->rows[r][c]);
                    if(text.find(L"~{")!=std::wstring::npos)drawGridText(*g,draw->nmcd.hdc,text,cell);
                    else DrawTextW(draw->nmcd.hdc,text.c_str(),int(text.size()),&cell,DT_LEFT|DT_TOP|DT_NOPREFIX);SelectObject(draw->nmcd.hdc,old);
                    return CDRF_SKIPDEFAULT;
                }
                draw->clrTextBk=background;draw->clrText=foreground;
                return CDRF_NEWFONT;
            }
        }
    }
    if(msg==WM_CONTEXTMENU&&((HWND)wp==g->list||(HWND)wp==ListView_GetHeader(g->list))){contextMenu(*g,lp);return 0;}
    if(msg==WM_COMMAND){int id=LOWORD(wp);
        if(id==DeleteColumns){deleteColumns(*g);return 0;}
        if(id==ClearCells){clearCells(*g);return 0;}
        if(id==SelectRow){selectRow(*g,g->row);return 0;}
        if(id==SelectColumn){selectColumn(*g,g->col);return 0;}
        if(id==SelectAll){selectAll(*g);return 0;}
        if((id==25||id==26)&&g->scripts){if(g->col!=g->scriptColumn){commit(*g);selectCell(*g,g->row,g->scriptColumn);}if(!g->edit)editCell(*g);if(g->edit)toggleScript(g->edit,id==26);return 0;}
        if(id==27||id==28){undo(*g,id==28);return 0;}
        if(id>=20&&id<=24){
        commit(*g);
        if(g->stages&&id==20){insertStage(*g,false);return 0;}if(g->stages&&id==21){deleteStage(*g);return 0;}
        if(id==20&&g->rows.size()<100){checkpoint(*g);int target=g->rows.empty()?0:g->row+1;g->rows.insert(g->rows.begin()+target,std::vector<std::string>(g->columns.size()));refresh(*g);validate(*g);selectCell(*g,target,0);editCell(*g);}
        if(id==21&&!g->rows.empty()){checkpoint(*g);int first=std::min(g->row,g->anchorRow),last=std::max(g->row,g->anchorRow);g->rows.erase(g->rows.begin()+first,g->rows.begin()+last+1);refresh(*g);validate(*g);selectCell(*g,g->row,0);}
        if(id==24)duplicate(*g);
        if(id==22)apply(*g);if(id==23)requestClose(*g);return 0;
    }}
    if(msg==WM_DESTROY&&!g->embedded&&g->italicFont){DeleteObject(g->italicFont);g->italicFont=nullptr;}
    if(msg==WM_DESTROY&&g->rowHeight){ListView_SetImageList(g->list,nullptr,LVSIL_SMALL);ImageList_Destroy(g->rowHeight);g->rowHeight=nullptr;}
    if(msg==WM_CLOSE){requestClose(*g);return 0;}return DefWindowProcW(hwnd,msg,wp,lp);
}
}
static HWND createEmbeddedTable(HWND parent,const std::vector<std::wstring>& columns,const Table& rows,const TablePreview& preview,const TableCellError& cellError,const TableColumnEdit& columnEdit,bool reactionStages,const TableReadback& readback={},const TableStructurePreview& structurePreview={}){
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW wc{};wc.lpfnWndProc=gridProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawEmbeddedGrid";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    auto* g=new Grid;g->embedded=true;g->columns=columns;g->rows=g->original=rows;g->preview=preview;g->cellError=cellError;g->columnEdit=columnEdit;g->originalColumns=columns;
    g->readback=readback;g->structurePreview=structurePreview;
    g->hint=L"셀 더블클릭 / F2: 입력 · Ctrl+I: 선택 글자 기울임 · Ctrl+V: 붙여넣기 · 입력 즉시 반영";
    g->scripts=true;
    if(reactionStages){
        g->reactionStages=true;
        g->hint=L"범위 0~100% · 끝은 시작보다 최소 1% 크게 · 글자·굵기·화살촉의 0은 자동 · 모든 행을 지우면 숨김";
    }
    if(g->rows.empty())g->rows.push_back(std::vector<std::string>(columns.size()));
    auto h=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"데이터 표",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,700,250,parent,nullptr,wc.hInstance,g);
    if(!h){delete g;return nullptr;}selectCell(*g,0,0);return h;
}
HWND embeddedTable(HWND parent,const std::vector<std::wstring>& columns,const Table& rows,const TablePreview& preview,
    const TableCellError& cellError,const TableColumnEdit& columnEdit,const TableReadback& readback,const TableStructurePreview& structurePreview){
    return createEmbeddedTable(parent,columns,rows,preview,cellError,columnEdit,false,readback,structurePreview);
}
void selectEmbeddedTableCell(HWND grid,int row,int column){
    if(auto* state=reinterpret_cast<Grid*>(GetWindowLongPtrW(grid,GWLP_USERDATA)))selectCell(*state,row,column);
}
HWND embeddedStageTable(HWND parent,const Table& rows,const TablePreview& preview){
    return createEmbeddedTable(parent,{L"반응 이름",L"시작 (%)",L"끝 (%)",L"글자 pt",L"굵기 pt",L"화살촉 pt",L"배치 0/1",L"X 이동",L"Y 이동",L"줄 번호",L"범위 기준"},rows,preview,{},{},true);
}
void distributeEmbeddedStages(HWND grid){
    auto* g=reinterpret_cast<Grid*>(GetWindowLongPtrW(grid,GWLP_USERDATA));
    if(!g||!g->reactionStages)return;
    commit(*g);
    auto rows=g->rows;
    std::vector<size_t> named;
    for(size_t i=0;i<rows.size();++i)
        if(rows[i][0].find_first_not_of(" \t\r\n")!=std::string::npos)named.push_back(i);
    for(size_t i=0;i<named.size();++i){
        auto& row=rows[named[i]];
        row[1]=std::to_string(100.*i/named.size());
        row[2]=std::to_string(100.*(i+1)/named.size());
        row[7]=row[8]="0";
        row[9]="1";
        row[10]="1";
    }
    if(rows==g->rows)return;
    checkpoint(*g);
    g->rows=std::move(rows);
    refresh(*g);
    validate(*g);
    selectCell(*g,g->row,g->col);
}
void setEmbeddedTableFontSize(HWND grid,int points){
    auto* g=reinterpret_cast<Grid*>(GetWindowLongPtrW(grid,GWLP_USERDATA));
    if(!g||!g->embedded)return;
    LOGFONTW style{};
    if(!GetObjectW(g->font,sizeof(style),&style))return;
    const HDC dc=GetDC(grid);
    const int dpi=dc?GetDeviceCaps(dc,LOGPIXELSY):96;
    if(dc)ReleaseDC(grid,dc);
    style.lfHeight=-MulDiv(std::clamp(points,8,24),dpi,72);
    style.lfItalic=FALSE;
    HFONT normal=CreateFontIndirectW(&style);
    style.lfItalic=TRUE;
    HFONT italic=CreateFontIndirectW(&style);
    if(!normal||!italic){if(normal)DeleteObject(normal);if(italic)DeleteObject(italic);return;}
    SendMessageW(g->list,WM_SETFONT,WPARAM(normal),TRUE);
    SendMessageW(ListView_GetHeader(g->list),WM_SETFONT,WPARAM(normal),TRUE);
    if(g->edit)SendMessageW(g->edit,WM_SETFONT,WPARAM(normal),TRUE);
    // Keep the original font alive for the grid help and command buttons.
    if(!g->interfaceFont)g->interfaceFont=g->font;
    else DeleteObject(g->font);
    DeleteObject(g->italicFont);
    g->font=normal;g->italicFont=italic;
    g->linePixels=-style.lfHeight+4;
    resizeRows(*g);sizeEditor(*g);InvalidateRect(g->list,nullptr,TRUE);
}
bool commitEmbeddedTable(HWND grid){auto* g=reinterpret_cast<Grid*>(GetWindowLongPtrW(grid,GWLP_USERDATA));if(!g)return true;commit(*g);return validate(*g);}
std::optional<Table> table(HWND parent,const std::wstring& title,const std::vector<std::wstring>& columns,const Table& rows,const TablePreview& preview,bool scripts,const std::wstring& hint,bool stages,int scriptColumn,const TableCellError& cellError,const TableColumnEdit& columnEdit,const TableStructurePreview& structurePreview){
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&controls);static bool registered=false;
    if(!registered){WNDCLASSW wc{};wc.lpfnWndProc=gridProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawGrid";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&wc);registered=true;}
    Grid g;g.columns=columns;g.rows=rows;g.original=rows;g.preview=preview;g.cellError=cellError;g.scripts=scripts;g.hint=hint;g.stages=stages;g.scriptColumn=scriptColumn<0?(stages?1:0):scriptColumn;if(g.rows.empty())g.rows.push_back(std::vector<std::string>(columns.size()));const RECT r=ownedWindowRect(parent,990,680);
    g.columnEdit=columnEdit;g.originalColumns=columns;g.structurePreview=structurePreview;
    HWND hwnd=CreateWindowExW(0,L"YoungDrawGrid",title.c_str(),WS_CAPTION|WS_SYSMENU|WS_POPUP|WS_CLIPCHILDREN,r.left,r.top,990,680,parent,nullptr,GetModuleHandleW(nullptr),&g);
    if(!hwnd)return {};
    appearance::titlebar(hwnd);EnableWindow(parent,FALSE);ShowWindow(hwnd,SW_SHOW);selectCell(g,0,0);
    MSG msg;while(!g.done&&GetMessageW(&msg,nullptr,0,0)>0){if(msg.message==WM_KEYDOWN&&!g.edit&&(GetKeyState(VK_CONTROL)&0x8000)&&(msg.wParam=='Z'||msg.wParam=='Y')){undo(g,msg.wParam=='Y'||(GetKeyState(VK_SHIFT)&0x8000));continue;}TranslateMessage(&msg);DispatchMessageW(&msg);}
    EnableWindow(parent,TRUE);DestroyWindow(hwnd);DeleteObject(g.font);SetActiveWindow(parent);if(g.ok)return g.rows;return {};
}
}

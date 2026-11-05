#include "win_dialogs.h"
#include "win_ui.h"
#include "win_dialog_style.h"
#include "win_scripts.h"
#include <commctrl.h>
#include <commdlg.h>
namespace ui {
static HWND editSurface(HWND control){COMBOBOXINFO info{sizeof(info)};return GetComboBoxInfo(control,&info)&&info.hwndItem?info.hwndItem:control;}
static LRESULT CALLBACK scriptEditProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR ref){
    const auto& field=*reinterpret_cast<const Field*>(ref);
    if(multilineShortcut(hwnd,msg,wp)||(field.scripts&&scriptShortcut(hwnd,msg,wp)))return 0;
    if(msg==WM_CHAR&&(wp==L'+'||wp==L'-')&&field.stepValue){
        const int length=GetWindowTextLengthW(hwnd);
        std::wstring current(length+1,L'\0');
        current.resize(GetWindowTextW(hwnd,current.data(),length+1));
        const auto next=wide(field.stepValue(utf8(current),wp==L'+'?1:-1));
        SetWindowTextW(hwnd,next.c_str());
        SendMessageW(hwnd,EM_SETSEL,next.size(),next.size());
        return 0;
    }
    if(msg==WM_GETDLGCODE)return DefSubclassProc(hwnd,msg,wp,lp)|DLGC_WANTCHARS|DLGC_WANTARROWS;
    if(msg==WM_CHAR&&wp==1)return 0;
    if(msg==WM_KEYDOWN&&wp=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(hwnd,EM_SETSEL,0,-1);return 0;}
    return DefSubclassProc(hwnd,msg,wp,lp);
}
static int inputHeight(const Field& field){return field.multiline?22*field.rows:29;}
static int labelHeight(const Field& field){
    const int lines=1+int(std::count(field.label.begin(),field.label.end(),L'\n'));
    if(lines>1)return 22*lines;
    return field.multiline?std::min(72,inputHeight(field)):34;
}
static int rowHeight(const Field& field){
    constexpr int gap=12,labelOffset=4;
    return std::max(inputHeight(field)+gap,labelOffset+labelHeight(field)+4);
}
struct Form {
    DialogColors colors;
    std::vector<Field> fields;std::vector<HWND> edits;
    std::vector<std::string> result;Preview preview;
    bool done=false,ok=false;HFONT font=nullptr;HWND error=nullptr,apply=nullptr;
};
static std::vector<std::string> read(Form& f){
    std::vector<std::string> values;
    for(size_t i=0;i<f.edits.size();i++){
        if(!f.fields[i].choices.empty()&&!f.fields[i].editableChoices)values.push_back(std::to_string(SendMessageW(f.edits[i],CB_GETCURSEL,0,0)));
        else if(f.fields[i].editableChoices&&SendMessageW(f.edits[i],CB_GETCURSEL,0,0)!=CB_ERR){
            auto index=SendMessageW(f.edits[i],CB_GETCURSEL,0,0);int n=int(SendMessageW(f.edits[i],CB_GETLBTEXTLEN,index,0));std::wstring w(n+1,0);SendMessageW(f.edits[i],CB_GETLBTEXT,index,(LPARAM)w.data());w.resize(n);values.push_back(utf8(w));
        }
        else{int n=GetWindowTextLengthW(f.edits[i]);std::wstring w(n+1,0);GetWindowTextW(f.edits[i],w.data(),n+1);w.resize(n);values.push_back(utf8(w));}
    }return values;
}
static bool preview(Form& f){
    if(!f.preview)return true;std::wstring error;
    try{error=f.preview(read(f));}catch(const std::exception& e){error=wide(e.what());}
    SetWindowTextW(f.error,error.empty()?L"미리보기가 갱신되었습니다. 취소하면 원래 그림으로 복원합니다.":error.c_str());
    EnableWindow(f.apply,error.empty());return error.empty();
}
static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    auto* f=reinterpret_cast<Form*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_CREATE){
        f=static_cast<Form*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(f));
        f->font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,ui::interfaceFont());int y=16;
        for(size_t i=0;i<f->fields.size();i++){
            auto& field=f->fields[i];HWND label=CreateWindowW(L"STATIC",field.label.c_str(),WS_CHILD|WS_VISIBLE,16,y+4,210,labelHeight(field),hwnd,nullptr,nullptr,nullptr);SendMessageW(label,WM_SETFONT,(WPARAM)f->font,TRUE);
            int height=inputHeight(field);HWND edit;
            if(!field.choices.empty()){
                edit=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|(field.editableChoices?CBS_DROPDOWN:CBS_DROPDOWNLIST)|WS_VSCROLL|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,230,y,field.scripts?330:430,200,hwnd,(HMENU)(INT_PTR)(100+i),nullptr,nullptr);
                for(auto& c:field.choices)SendMessageW(edit,CB_ADDSTRING,0,(LPARAM)c.c_str());
                if(field.editableChoices)SetWindowTextW(edit,wide(field.value).c_str());else SendMessageW(edit,CB_SETCURSEL,std::stoi(field.value),0);
            }else edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",wide(field.value).c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|(field.multiline?ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL:ES_AUTOHSCROLL),230,y,field.scripts?330:field.color?385:430,height,hwnd,(HMENU)(INT_PTR)(100+i),nullptr,nullptr);
            SendMessageW(edit,WM_SETFONT,(WPARAM)f->font,TRUE);SendMessageW(edit,EM_SETLIMITTEXT,100000,0);f->edits.push_back(edit);
            if(field.color){HWND b=CreateWindowW(L"BUTTON",L"...",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,621,y,39,29,hwnd,(HMENU)(INT_PTR)(500+i),nullptr,nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)f->font,TRUE);}
            if(field.choices.empty()||field.editableChoices)SetWindowSubclass(editSurface(edit),scriptEditProc,1,reinterpret_cast<DWORD_PTR>(&field));
            if(field.scripts)for(int k=0;k<2;k++){HWND b=CreateWindowW(L"BUTTON",k?L"x₂":L"x²",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,565+k*48,y,44,29,hwnd,(HMENU)(INT_PTR)((k?800:700)+i),nullptr,nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)f->font,TRUE);}
            y+=rowHeight(field);
        }
        if(f->preview){f->error=CreateWindowW(L"STATIC",L"올바른 입력은 그림에 즉시 반영됩니다.",WS_CHILD|WS_VISIBLE,16,y,644,40,hwnd,nullptr,nullptr,nullptr);SendMessageW(f->error,WM_SETFONT,(WPARAM)f->font,TRUE);y+=43;}
        for(int i=0;i<2;i++){HWND b=CreateWindowW(L"BUTTON",i?L"취소":L"적용",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,450+i*110,y+4,100,32,hwnd,(HMENU)(INT_PTR)(i?IDCANCEL:IDOK),nullptr,nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)f->font,TRUE);if(!i)f->apply=b;}
        return 0;
    }
    if(!f)return DefWindowProcW(hwnd,msg,wp,lp);
    if(msg==DM_GETDEFID)return MAKELRESULT(IDOK,DC_HASDEFID);
    if(msg==WM_ERASEBKGND){RECT r;GetClientRect(hwnd,&r);FillRect((HDC)wp,&r,f->colors.panel);return 1;}
    if(msg==WM_CTLCOLORSTATIC||msg==WM_CTLCOLOREDIT||msg==WM_CTLCOLORLISTBOX)return f->colors.control(msg,(HDC)wp);
    if(msg==WM_MEASUREITEM){reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->itemHeight=25;return TRUE;}
    if(msg==WM_DRAWITEM){const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);f->colors.draw(item,item.CtlID==IDOK);return TRUE;}
    if(msg==WM_COMMAND){int id=LOWORD(wp);
        if((id>=700&&id<int(700+f->fields.size()))||(id>=800&&id<int(800+f->fields.size()))){size_t i=size_t(id-(id>=800?800:700));toggleScript(editSurface(f->edits[i]),id<800);return 0;}
        if(id>=500&&id<int(500+f->fields.size())){
            size_t i=size_t(id-500);static COLORREF custom[16]={};CHOOSECOLORW c{};c.lStructSize=sizeof(c);c.hwndOwner=hwnd;c.lpCustColors=custom;c.Flags=CC_FULLOPEN|CC_RGBINIT;
            try{auto value=read(*f)[i];unsigned rgb=std::stoul(value.substr(1),nullptr,16);c.rgbResult=RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255);}catch(...){}
            if(ChooseColorW(&c)){char value[8];sprintf_s(value,"#%02x%02x%02x",GetRValue(c.rgbResult),GetGValue(c.rgbResult),GetBValue(c.rgbResult));SetWindowTextW(f->edits[i],wide(value).c_str());}return 0;
        }
        if(id==IDOK||id==IDCANCEL){if(id==IDOK){if(!preview(*f))return 0;f->result=read(*f);f->ok=true;}f->done=true;PostMessageW(hwnd,WM_NULL,0,0);return 0;}
        if(f->apply&&f->edits.size()==f->fields.size()&&(HIWORD(wp)==EN_CHANGE||HIWORD(wp)==CBN_SELCHANGE||HIWORD(wp)==CBN_EDITCHANGE))preview(*f);
    }
    if(msg==WM_CLOSE){f->done=true;PostMessageW(hwnd,WM_NULL,0,0);return 0;}return DefWindowProcW(hwnd,msg,wp,lp);
}
std::optional<std::vector<std::string>> form(HWND parent,const std::wstring& title,const std::vector<Field>& fields,const Preview& callback){
    static bool registered=false;if(!registered){WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawForm";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&wc);registered=true;}
    Form f;f.fields=fields;f.preview=callback;int height=108+(callback?43:0);for(auto& v:fields)height+=rowHeight(v);
    const RECT r=ownedWindowRect(parent,700,height,bool(callback));
    HWND hwnd=CreateWindowExW(WS_EX_DLGMODALFRAME,L"YoungDrawForm",title.c_str(),WS_CAPTION|WS_SYSMENU|WS_POPUP|WS_CLIPCHILDREN,r.left,r.top,700,height,parent,nullptr,GetModuleHandleW(nullptr),&f);
    if(!hwnd)return {};
    appearance::titlebar(hwnd);EnableWindow(parent,FALSE);ShowWindow(hwnd,SW_SHOW);
    SetFocus(f.edits.empty()?f.apply:f.edits.front());MSG msg{};while(!f.done&&GetMessageW(&msg,nullptr,0,0)>0){const bool editKey=msg.message==WM_KEYDOWN&&
            (msg.wParam==VK_HOME||msg.wParam==VK_END||(msg.wParam=='A'&&(GetKeyState(VK_CONTROL)&0x8000)))&&
            GetWindowSubclass(msg.hwnd,scriptEditProc,1,nullptr);
        if(editKey||!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    DestroyWindow(hwnd);if(f.font)DeleteObject(f.font);EnableWindow(parent,TRUE);SetActiveWindow(parent);if(f.ok)return f.result;return {};
}
}

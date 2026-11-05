#pragma once
#include "win_dialogs.h"
#include <algorithm>
namespace ui {
// Alt+Enter arrives as a system key; consume its translated character too.
inline bool multilineShortcut(HWND editor,UINT message,WPARAM key){
    if(!(GetWindowLongPtrW(editor,GWL_STYLE)&ES_MULTILINE))return false;
    if((message==WM_SYSKEYDOWN||message==WM_KEYDOWN)&&key==VK_RETURN&&(GetKeyState(VK_MENU)&0x8000)){
        SendMessageW(editor,EM_REPLACESEL,TRUE,(LPARAM)L"\r\n");return true;
    }
    return message==WM_SYSCHAR&&key==VK_RETURN;
}
struct ScriptChange {std::wstring text;size_t start=0,end=0;};
// Word-style formatting for source-backed editable text: selected text toggles,
// an empty selection starts a script run, and the same shortcut exits that run.
inline ScriptChange scriptChange(std::wstring text,size_t start,size_t end,bool superscript){
    start=std::min(start,text.size());end=std::clamp(end,start,text.size());wchar_t kind=superscript?L'^':L'_';
    for(size_t open=0;open+1<text.size();open++)if((text[open]==L'^'||text[open]==L'_')&&text[open+1]==L'{'){
        size_t close=text.find(L'}',open+2);if(close==std::wstring::npos)break;
        if(start>=open+2&&end<=close){
            if(start==end){
                if(text[open]!=kind){text[open]=kind;return {text,start,end};}
                if(close==open+2){text.erase(open,3);return {text,open,open};}
                return {text,close+1,close+1};
            }
            auto wrap=[](const std::wstring& s,wchar_t k){return s.empty()?std::wstring{}:std::wstring(1,k)+L"{"+s+L"}";};
            auto lead=wrap(text.substr(open+2,start-open-2),text[open]);auto selected=text.substr(start,end-start);auto tail=wrap(text.substr(end,close-end),text[open]);
            bool remove=text[open]==kind;auto middle=remove?selected:wrap(selected,kind);size_t selectedStart=open+lead.size()+(remove?0:2);
            text.replace(open,close-open+1,lead+middle+tail);return {text,selectedStart,selectedStart+selected.size()};
        }
        open=close;
    }
    auto selected=text.substr(start,end-start);text.replace(start,end-start,std::wstring(1,kind)+L"{"+selected+L"}");
    return {text,start+2,start+2+selected.size()};
}
inline void toggleScript(HWND editor,bool superscript){
    int length=GetWindowTextLengthW(editor);std::wstring text(length+1,0);GetWindowTextW(editor,text.data(),length+1);text.resize(length);
    DWORD start=0,end=0;SendMessageW(editor,EM_GETSEL,(WPARAM)&start,(LPARAM)&end);auto change=scriptChange(text,start,end,superscript);
    if(change.text!=text){SendMessageW(editor,EM_SETSEL,0,-1);SendMessageW(editor,EM_REPLACESEL,TRUE,(LPARAM)change.text.c_str());}
    SendMessageW(editor,EM_SETSEL,change.start,change.end);SetFocus(editor);
}
inline bool scriptShortcut(HWND editor,UINT message,WPARAM key){
    bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
    if(message==WM_KEYDOWN&&(key==VK_F9||key==VK_F10)){toggleScript(editor,key==VK_F10);return true;}
    if(message==WM_KEYDOWN&&ctrl&&(key==VK_OEM_PLUS||key==VK_ADD)){toggleScript(editor,(GetKeyState(VK_SHIFT)&0x8000)!=0);return true;}
    return message==WM_CHAR&&ctrl&&(key==L'='||key==L'+'||key==0x1d);
}
}

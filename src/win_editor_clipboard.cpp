#include "win_editor_clipboard.h"
#include "win_editor_files.h"
#include "win_editor_images.h"
#include "win_editor_state.h"
#include "cdxml.h"
#include "office.h"
#include "win_dialogs.h"
#include "win_ui.h"
#include <shellapi.h>

namespace yd::editor {

Document selectedDocument(){auto chosen=selection;for(int id:selection)if(auto b=doc.bond(id)){chosen.push_back(b->a);chosen.push_back(b->b);}return doc.subset(chosen);}
UINT clipboardFormat(){return RegisterClipboardFormatW(L"YoungDraw.Project.v2");}
void copy(){if(selection.empty())return;if(FAILED(office::copy(selectedDocument())))throw std::runtime_error("클립보드 복사에 실패했습니다. 잠시 후 다시 시도하세요.");status=L"복사했습니다. Word·Excel에 붙여넣거나 YoungDraw에서 편집할 수 있습니다.";}
std::string clipboardMemoryText(HGLOBAL memory,bool unicode){
    if(!memory)return {};auto size=GlobalSize(memory);auto* bytes=static_cast<const char*>(GlobalLock(memory));if(!bytes)return {};
    std::string value;
    try{
        bool wide=unicode||(size>=2&&((BYTE(bytes[0])==0xff&&BYTE(bytes[1])==0xfe)||bytes[1]==0));
        if(wide){auto* chars=reinterpret_cast<const wchar_t*>(bytes);size_t count=size/sizeof(wchar_t),length=0;while(length<count&&chars[length])length++;if(length&&chars[0]==0xfeff){chars++;length--;}value=ui::utf8(std::wstring(chars,length));}
        else value.assign(bytes,strnlen(bytes,size));
    }catch(...){GlobalUnlock(memory);throw;}
    GlobalUnlock(memory);return value;
}
std::optional<Document> clipboardStructureText(const std::string& text){
    if(text.empty())return {};
    try{auto structure=fromCdxml(text);if(structure.allIds().empty())return {};return structure;}catch(...){return {};}
}
std::string clipboardMemoryBytes(HGLOBAL memory){
    if(!memory)return {};auto size=GlobalSize(memory);if(!size||size>64*1024*1024)return {};
    auto* data=static_cast<const char*>(GlobalLock(memory));if(!data)return {};
    std::string bytes;try{bytes.assign(data,size);}catch(...){GlobalUnlock(memory);throw;}GlobalUnlock(memory);return bytes;
}
std::optional<Document> clipboardDocument(){
    if(!OpenClipboard(win))throw std::runtime_error("클립보드를 읽을 수 없습니다. 잠시 후 다시 붙여넣으세요.");
    struct Close {~Close(){CloseClipboard();}} close;
    for(UINT format:{clipboardFormat(),RegisterClipboardFormatW(L"ChemDraw XML Format"),RegisterClipboardFormatW(L"CDXML"),RegisterClipboardFormatW(L"ChemDraw XML"),UINT(CF_UNICODETEXT),UINT(CF_TEXT)}){
        auto text=clipboardMemoryText(GetClipboardData(format),format==CF_UNICODETEXT);
        if(auto structure=clipboardStructureText(text))return structure;
    }
    std::string binaryError;
    for(const wchar_t* name:{L"ChemDraw Interchange Format",L"ChemDraw CDX",L"CDX"}){
        auto bytes=clipboardMemoryBytes(GetClipboardData(RegisterClipboardFormatW(name)));
        if(bytes.empty())continue;
        try{auto structure=fromCdx(bytes);if(!structure.allIds().empty())return structure;}
        catch(const std::exception& e){binaryError=e.what();}
    }
    // Prefer encoded originals over bitmap clipboard previews, which lose animation.
    for(const wchar_t* name:{L"GIF",L"image/gif",L"PNG",L"image/png"}){
        auto bytes=clipboardMemoryBytes(GetClipboardData(RegisterClipboardFormatW(name)));
        if(!bytes.empty()&&!imageMime(bytes).empty())return imageDocument(bytes);
    }
    if(auto drop=static_cast<HDROP>(GetClipboardData(CF_HDROP))){
        UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0);
        for(UINT i=0;i<count;i++){
            UINT length=DragQueryFileW(drop,i,nullptr,0);std::wstring path(length+1,L'\0');DragQueryFileW(drop,i,path.data(),length+1);path.resize(length);
            auto ext=fs::path(path).extension().wstring();
            if(_wcsicmp(ext.c_str(),L".gif")&&_wcsicmp(ext.c_str(),L".png")&&_wcsicmp(ext.c_str(),L".jpg")&&_wcsicmp(ext.c_str(),L".jpeg"))continue;
            return imageDocument(readFile(path));
        }
    }
    if(!binaryError.empty())throw std::runtime_error("ChemDraw 구조를 읽지 못했습니다: "+binaryError);
    return {};
}
void insert(const Document& source,Vec at){Document fragment=source;upgradeBondGeometry(fragment);fragment.rebaseStructureStyle(doc.style);auto b=makeScene(fragment).bounds;int first=doc.nextId;history.checkpoint(doc);doc.append(fragment,at-Vec{b.x+b.w/2,b.y+b.h/2});selection.clear();for(int id:doc.allIds())if(id>=first)selection.push_back(id);changed();}

} // namespace yd::editor

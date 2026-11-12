#include "win_editor_commands.h"
#include "win_editor_state.h"
#include "win_editor_structure.h"
#include "win_editor_tabs.h"
#include "win_editor_text.h"
#include "cdxml.h"
#include "mechanism_data.h"
#include "win_dialogs.h"
#include "win_scripts.h"
#include "win_ui.h"
#include <commdlg.h>
#include <windowsx.h>

namespace yd::editor {

HWND inlineEdit=nullptr;HFONT inlineFont=nullptr;WNDPROC oldEditProc=nullptr;
InlineTarget inlineTarget;Document inlineBefore;bool inlineClosing=false,inlineValid=true,inlineComposing=false;
std::string inlineTypeface="Arial";int inlineFace=1;COLORREF inlineColor=RGB(0,0,0);
Vec inlineAnchor;int inlineWidth=0,inlineHeight=0;
Object* textObject(int id){for(auto& o:doc.objects)if(o.id==id&&o.kind==ObjectKind::Text)return &o;return nullptr;}
std::string inlineValue(const InlineTarget& t){
    if(t.role==0){if(auto* o=textObject(t.owner))return o->text;if(auto* a=doc.atom(t.owner))return a->label.empty()?symbol(a->element):a->label;}
    if(auto* r=reaction(t.owner)){if(t.role==1)return r->slots.at(size_t(t.index)).label;if(t.role==2)return transitionCaption(r->above.at(size_t(t.index))).description;if(t.role==11)return transitionCaption(r->above.at(size_t(t.index))).name;if(t.role==3)return r->below.at(size_t(t.index));if(t.role==9)return r->slots.at(size_t(t.index)).energy;if(t.role==10)return r->centerLabel;}
    if(auto* p=plot(t.owner)){if(t.role==12)return p->stages.at(size_t(t.index)).label;if(t.role==4)return p->title;if(t.role==5)return p->unit;if(t.role==6)return p->states.at(size_t(t.index)).label;if(t.role==7)return p->series.at(size_t(t.index)).name;if(t.role==8)return number(p->states.at(size_t(t.index)).energy);}
    return {};
}
void setInlineValue(const InlineTarget& t,const std::string& value){
    if(t.role==0){if(auto* o=textObject(t.owner)){o->text=chemicalScripts(value);o->runs.clear();return;}if(auto* a=doc.atom(t.owner)){int e=atomicNumber(value);if(e){setAtomElement(doc,a->id,e);a->label.clear();a->explicitH=-1;}else a->label=chemicalScripts(value);a->labelRuns.clear();return;}}
    if(auto* r=reaction(t.owner)){if(t.role==1)r->slots.at(size_t(t.index)).label=value;if(t.role==2||t.role==11){auto& raw=r->above.at(size_t(t.index));auto caption=transitionCaption(raw);if(t.role==2)caption.description=value;else{caption.name=value;caption.separated=true;}raw=transitionCaptionText(caption);}if(t.role==3)r->below.at(size_t(t.index))=value;if(t.role==9)r->slots.at(size_t(t.index)).energy=value;if(t.role==10)r->centerLabel=value;return;}
    if(auto* p=plot(t.owner)){if(t.role==12){if(value.empty())throw std::runtime_error("단계 이름을 입력하세요.");p->stages.at(size_t(t.index)).label=value;}if(t.role==4)p->title=value;if(t.role==5)p->unit=value;if(t.role==6)p->states.at(size_t(t.index)).label=value;if(t.role==7)p->series.at(size_t(t.index)).name=value;if(t.role==8){double e=numeric(value);p->states.at(size_t(t.index)).energy=e;if(!p->series.empty()){auto& path=p->series[0];path.values.at(size_t(t.index))=e;if(!path.present.empty())path.present.at(size_t(t.index))=true;}}return;}
}
std::optional<InlineTarget> textAt(Vec p){
    auto sc=scene();for(auto i=sc.items.rbegin();i!=sc.items.rend();++i){const auto& t=*i;if(t.type!=Primitive::Text||t.points.empty()||t.textRole==13||t.textRole==mullikenSpinTextRole||doc.isLocked(t.owner))continue;if(!t.textRole&&!textObject(t.owner)&&!doc.atom(t.owner))continue;
        auto a=t.points[0];double w=measure(t.text,t.fontSize,t.font,t.face),h=t.fontSize*1.3*(1+std::count(t.text.begin(),t.text.end(),'\n'));
        if(p.x>=a.x-2&&p.x<=a.x+w+2&&p.y>=a.y-2&&p.y<=a.y+h+2)return InlineTarget{t.owner,t.textRole,t.textIndex,a,t.fontSize};
    }return {};
}
void sizeInline(){
    if(!inlineEdit)return;int n=GetWindowTextLengthW(inlineEdit);std::wstring w(n+1,0);GetWindowTextW(inlineEdit,w.data(),n+1);w.resize(n);auto value=ui::utf8(w);
    double textWidth=0;for(auto& row:lines(value))textWidth=std::max(textWidth,measure(row,inlineTarget.size,inlineTypeface,inlineFace)*zoom);
    int width=inlineWidth&&textWidth+8<=inlineWidth?inlineWidth:std::max(96,int(textWidth)+48),height=std::max(28,int(inlineTarget.size*zoom*1.35*(1+std::count(value.begin(),value.end(),'\n'))+8));
    width=std::min(width,std::max(96,ww-left-right-10));height=std::min(height,std::max(28,wh-top-bottom-10));
    if(width<=inlineWidth&&height<=inlineHeight)return;inlineWidth=std::max(width,inlineWidth);inlineHeight=std::max(height,inlineHeight);
    Vec p=screen(inlineAnchor);int x=int(std::clamp(p.x-inlineWidth/2.,double(left+2),double(std::max(left+2,ww-right-inlineWidth-2))));int y=int(std::clamp(p.y-2,double(top+2),double(std::max(top+2,wh-bottom-inlineHeight-2))));
    SetWindowPos(inlineEdit,nullptr,x,y,inlineWidth,inlineHeight,SWP_NOZORDER|SWP_NOACTIVATE);redraw();
}
void previewInline(){
    if(!inlineEdit||inlineClosing)return;int n=GetWindowTextLengthW(inlineEdit);std::wstring w(n+1,0);GetWindowTextW(inlineEdit,w.data(),n+1);w.resize(n);
    try{if(inlineTarget.role==8)numeric(ui::utf8(w));inlineValid=true;status=inlineTarget.role!=8?L"Ctrl+=: 아래첨자 · Ctrl+Shift+=: 위첨자 · 같은 키: 일반 입력 복귀 · Alt+Enter: 줄바꿈 · Enter: 완료 · Esc: 취소":L"Enter: 완료 · Alt+Enter / Shift+Enter: 줄바꿈 · Tab/Shift+Tab: 다음/이전 텍스트 · Esc: 취소";}
    catch(const std::exception& e){inlineValid=false;status=ui::wide(e.what());}
    sizeInline();RECT statusRect{0,wh-bottom,ww,wh};InvalidateRect(win,&statusRect,FALSE);
}
bool finishInline(bool commit){
    if(!inlineEdit||inlineClosing)return true;if(commit&&!inlineValid){SetFocus(inlineEdit);return false;}
    if(commit){int n=GetWindowTextLengthW(inlineEdit);std::wstring value(n+1,0);GetWindowTextW(inlineEdit,value.data(),n+1);value.resize(n);setInlineValue(inlineTarget,ui::utf8(value));if(auto* o=textObject(inlineTarget.owner)){o->centerText=true;o->points[0]=inlineAnchor;}}
    inlineClosing=true;HWND edit=inlineEdit;inlineEdit=nullptr;DestroyWindow(edit);if(inlineFont){DeleteObject(inlineFont);inlineFont=nullptr;}
    if(commit)if(auto* r=reaction(inlineTarget.owner))optimizeMechanism(doc,{r->id},r->cyclic);
    if(!commit)doc=inlineBefore;else{
        if(auto* o=textObject(inlineTarget.owner);o&&o->text.empty())doc.erase({o->id});
        if(toNative(doc)!=toNative(inlineBefore)){history.checkpoint(inlineBefore);changed();}
    }
    inlineClosing=false;selection={inlineTarget.owner};status=commit?L"텍스트를 수정했습니다. t 도구로 다시 편집할 수 있습니다.":L"텍스트 편집을 취소했습니다.";redraw();return true;
}
LRESULT CALLBACK inlineProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_IME_STARTCOMPOSITION)inlineComposing=true;
    if(msg==WM_IME_ENDCOMPOSITION)inlineComposing=false;
    if(!inlineComposing&&ui::multilineShortcut(hwnd,msg,wp))return 0;
    if(!inlineComposing&&inlineTarget.role!=8&&ui::scriptShortcut(hwnd,msg,wp))return 0;
    if(msg==WM_KEYDOWN&&!inlineComposing){
        if((GetKeyState(VK_CONTROL)&0x8000)&&(wp==VK_TAB||wp=='W')){if(isolatedStructureEditing){if(wp=='W')command(187);return 0;}if(finishInline(true)){if(wp=='W')closeDocumentTab(activeDocument);else switchDocumentTab((activeDocument+documents.size()+((GetKeyState(VK_SHIFT)&0x8000)?-1:1))%documents.size());SetFocus(win);}return 0;}
        if(wp=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(hwnd,EM_SETSEL,0,-1);return 0;}if(wp==VK_ESCAPE){finishInline(false);SetFocus(win);return 0;}
        if(wp==VK_TAB){std::vector<InlineTarget> targets;for(auto& t:scene().items)if(t.type==Primitive::Text&&!t.points.empty()&&t.textRole!=mullikenSpinTextRole&&(t.textRole||textObject(t.owner))&&std::none_of(targets.begin(),targets.end(),[&](const InlineTarget& a){return a.owner==t.owner&&a.role==t.textRole&&a.index==t.textIndex;}))targets.push_back({t.owner,t.textRole,t.textIndex,t.points[0],t.fontSize});
            auto current=std::find_if(targets.begin(),targets.end(),[](const InlineTarget& t){return t.owner==inlineTarget.owner&&t.role==inlineTarget.role&&t.index==inlineTarget.index;});
            if(current!=targets.end()&&targets.size()>1){int i=int(current-targets.begin()),n=int(targets.size());auto next=targets[size_t((i+n+((GetKeyState(VK_SHIFT)&0x8000)?-1:1))%n)];if(finishInline(true))beginInline(next,next.p);}else if(finishInline(true))SetFocus(win);return 0;}
        if(wp==VK_RETURN&&(!(GetKeyState(VK_SHIFT)&0x8000)||(GetKeyState(VK_CONTROL)&0x8000))){if(finishInline(true))SetFocus(win);return 0;}if(wp=='S'&&(GetKeyState(VK_CONTROL)&0x8000)){if(finishInline(true))save();return 0;}}
    if(msg==WM_CHAR&&wp==VK_TAB)return 0;
    return CallWindowProcW(oldEditProc,hwnd,msg,wp,lp);
}
void beginInline(std::optional<InlineTarget> target,Vec at){
    if(tool!=Text)return;
    if(!finishInline(true))return;if(target&&doc.isLocked(target->owner))return;inlineBefore=doc;
    if(target)inlineTarget=*target;else{Object o;o.id=doc.nextId++;o.kind=ObjectKind::Text;o.fontSize=doc.style.fontSize;o.face=1;o.points={at};doc.objects.push_back(o);inlineTarget={o.id,0,-1,at,o.fontSize};}
    selection={inlineTarget.owner};inlineValid=true;auto value=inlineValue(inlineTarget);
    inlineTypeface="Arial";inlineFace=doc.style.labelFace;inlineColor=RGB(0,0,0);
    inlineWidth=inlineHeight=0;inlineAnchor=inlineTarget.p;double lo=1e20,hi=-1e20,ty=1e20;
    for(auto& t:scene().items)if(t.type==Primitive::Text&&!t.points.empty()&&t.owner==inlineTarget.owner&&t.textRole==inlineTarget.role&&t.textIndex==inlineTarget.index){inlineTarget.size=std::max(inlineTarget.size,t.fontSize);lo=std::min(lo,t.points[0].x);hi=std::max(hi,t.points[0].x+measure(t.text,t.fontSize,t.font,t.face));ty=std::min(ty,t.points[0].y);}
    if(hi>=lo)inlineAnchor={(lo+hi)/2,ty};if(auto* o=textObject(inlineTarget.owner);o&&o->centerText)inlineAnchor=o->points[0];
    for(auto& t:scene().items)if(t.type==Primitive::Text&&t.owner==inlineTarget.owner&&t.textRole==inlineTarget.role&&t.textIndex==inlineTarget.index){inlineTypeface=t.font;inlineFace=t.face;auto c=rgb(t.color);inlineColor=RGB(c.GetR(),c.GetG(),c.GetB());break;}
    inlineComposing=false;inlineClosing=true;inlineEdit=CreateWindowExW(0,L"EDIT",ui::wide(value).c_str(),WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP|ES_CENTER|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|ES_NOHIDESEL,0,0,96,28,win,(HMENU)900,GetModuleHandleW(nullptr),nullptr);inlineClosing=false;
    if(!inlineEdit){doc=inlineBefore;throw std::runtime_error("Cannot create text editor");}
    inlineFont=CreateFontW(-int(std::clamp(inlineTarget.size*zoom,1.,160.)),0,0,0,inlineFace&1?FW_BOLD:FW_NORMAL,inlineFace&2?TRUE:FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,ui::wide(inlineTypeface).c_str());SendMessageW(inlineEdit,WM_SETFONT,(WPARAM)inlineFont,TRUE);SendMessageW(inlineEdit,EM_SETLIMITTEXT,100000,0);
    oldEditProc=(WNDPROC)SetWindowLongPtrW(inlineEdit,GWLP_WNDPROC,(LONG_PTR)inlineProc);SendMessageW(inlineEdit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(2,2));sizeInline();SetFocus(inlineEdit);SendMessageW(inlineEdit,EM_SETSEL,0,-1);status=inlineTarget.role!=8?L"Ctrl+=: 아래첨자 · Ctrl+Shift+=: 위첨자 · 같은 키: 일반 입력 복귀 · Alt+Enter: 줄바꿈 · Enter: 완료 · Esc: 취소":L"Enter: 완료 · Alt+Enter / Shift+Enter: 줄바꿈 · Tab/Shift+Tab: 다음/이전 텍스트 · Esc: 취소";redraw();
}
bool applyTextColor(const std::string& value,const std::optional<InlineTarget>& target){
    colorValue(value);Document before=doc;bool found=false;
    if(target&&contains(selection,target->owner)&&!doc.isLocked(target->owner)){
        if(auto* r=reaction(target->owner)){
            auto it=std::find_if(r->captionColors.begin(),r->captionColors.end(),[&](const CaptionColor& c){return c.role==target->role&&c.index==target->index;});
            if(it==r->captionColors.end())r->captionColors.push_back({target->role,target->index,value});else it->color=value;found=true;
        }
    }
    if(!found)for(int id:selection)if(!doc.isLocked(id)){
        if(auto* r=reaction(id)){r->textColor=value;r->captionColors.clear();found=true;}
        if(auto* o=textObject(id)){o->color=value;for(auto& run:o->runs)run.color=value;found=true;}
    }
    if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}
    return found;
}
void textColorPalette(const std::optional<InlineTarget>& target){
    bool eligible=false;for(int id:selection)if(!doc.isLocked(id)&&(reaction(id)||textObject(id)))eligible=true;
    if(!eligible){status=L"색을 바꿀 단계 텍스트를 편집하거나 반응 블록·텍스트를 먼저 선택하세요.";redraw();return;}
    std::string current="#000000";for(auto& item:scene().items)if(item.type==Primitive::Text&&contains(selection,item.owner)){
        if(target&&(item.owner!=target->owner||item.textRole!=target->role||item.textIndex!=target->index))continue;
        current=item.color;break;
    }
    auto initial=rgb(current);static COLORREF custom[16]={};CHOOSECOLORW picker{};
    picker.lStructSize=sizeof(picker);picker.hwndOwner=win;picker.lpCustColors=custom;picker.Flags=CC_FULLOPEN|CC_RGBINIT;picker.rgbResult=RGB(initial.GetR(),initial.GetG(),initial.GetB());
    if(!ChooseColorW(&picker))return;
    char value[8];sprintf_s(value,"#%02x%02x%02x",GetRValue(picker.rgbResult),GetGValue(picker.rgbResult),GetBValue(picker.rgbResult));
    applyTextColor(value,target);status=L"텍스트 색상을 변경했습니다. · Ctrl+Z: 되돌리기";redraw();
}

} // namespace yd::editor

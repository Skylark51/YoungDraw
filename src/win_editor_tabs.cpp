#include "win_editor_chrome.h"
#include "win_editor_files.h"
#include "win_editor_images.h"
#include "win_editor_plot.h"
#include "win_editor_reaction.h"
#include "win_editor_render.h"
#include "win_editor_state.h"
#include "win_editor_tabs.h"
#include "win_editor_text.h"
#include "cdxml.h"
#include "office.h"
#include "win_projects.h"
#include "win_storage.h"
#include "win_theme.h"
#include "win_ui.h"
#include <commctrl.h>
#include <windowsx.h>

namespace yd::editor {

HWND documentTabs=nullptr;HFONT tabFont=nullptr;bool officeDocument=false;
std::vector<DocumentTab> documents;size_t activeDocument=0;
bool save(bool as){if(officeDocument&&office::editing()&&!as){if(FAILED(office::update()))throw std::runtime_error("Office 문서에 변경 내용을 반영하지 못했습니다.");dirty=false;title();status=L"Office 문서에 변경 내용을 반영했습니다.";redraw();return true;}auto path=filePath;
    if(path.empty()||as||_wcsicmp(fs::path(path).extension().c_str(),L".cdxml")!=0){
        auto p=fileDialog(true);if(!p)return false;path=*p;
        if(_wcsicmp(fs::path(path).extension().c_str(),L".cdxml")!=0)
            throw std::runtime_error("CDXML 확장자로 저장하세요.");
    }
    writeFile(path,toCdxml(doc));filePath=path;dirty=false;title();status=L"CDXML을 저장했습니다. ChemDraw와 YoungDraw에서 열 수 있습니다.";redraw();return true;}
bool discard(){if(!dirty)return true;int answer=MessageBoxW(win,L"변경 내용을 저장하시겠습니까?",L"YoungDraw",MB_YESNOCANCEL|MB_ICONQUESTION);return answer==IDNO||(answer==IDYES&&save());}
void storeDocumentTab(){
    if(documents.empty())documents.emplace_back();
    documents[activeDocument]={doc,history,filePath,status,selection,targetContainer,targetSlot,targetAbove,dirty,officeDocument,zoom,origin,tool};
}
void refreshDocumentTabs(){
    if(!documentTabs)return;
    TabCtrl_DeleteAllItems(documentTabs);
    for(size_t i=0;i<documents.size();i++){
        auto path=i==activeDocument?filePath:documents[i].path;
        bool modified=i==activeDocument?dirty:documents[i].dirty;
        auto projectName=projects::displayName(path);
        std::wstring label=(path.empty()?L"제목 없음 "+std::to_wstring(i+1):projectName.empty()?fs::path(path).filename().wstring():projectName)+(modified?L" *":L"")+L"   ×";
        TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=label.data();TabCtrl_InsertItem(documentTabs,int(i),&item);
    }
    TabCtrl_SetCurSel(documentTabs,int(activeDocument));
}
bool switchDocumentTab(size_t index){
    mechanismConnecting=mechanismConnectionDrag=false;
    if(index>=documents.size())return false;
    if(!finishInline(true)){TabCtrl_SetCurSel(documentTabs,int(activeDocument));return false;}
    if(index==activeDocument)return true;
    selectedCycleOwner=0;selectedCycleArrow=-1;snapPlot=0;snapStart.reset();storeDocumentTab();activeDocument=index;const auto& next=documents[index];
    doc=next.document;history=next.history;filePath=next.path;status=next.status;selection=next.selection;
    targetContainer=next.target;targetSlot=next.slot;targetAbove=next.above;dirty=next.dirty;officeDocument=next.office;
    zoom=next.zoom;origin=next.origin;tool=next.tool;hoverAtom=hoverBond=0;curveHandle=curveOwner=-1;
    title();redraw();return true;
}
void newDocumentTab(Document document,std::wstring path,bool fromOffice){
    selectedCycleOwner=0;selectedCycleArrow=-1;
    snapPlot=0;snapStart.reset();
    if(!finishInline(true))return;
    storeDocumentTab();activeDocument=documents.size();documents.emplace_back();
    doc=std::move(document);history.clear();filePath=std::move(path);selection.clear();targetContainer=0;targetSlot=0;targetAbove=false;
    Document beforeUpgrade=doc;normalizeChemicalLabels(doc);bool upgraded=upgradeBondGeometry(doc);upgraded=upgraded||toNative(doc)!=toNative(beforeUpgrade);if(upgraded)history.checkpoint(beforeUpgrade);dirty=upgraded;officeDocument=fromOffice;tool=Select;hoverAtom=hoverBond=0;status=upgraded?L"벤젠 75%·벤질 연결 길이 기준을 적용했습니다. 저장하면 새 기준이 유지됩니다.":L"새 문서 탭 · Ctrl+Tab: 다음 탭 · Ctrl+W: 현재 탭 닫기";
    fit();storeDocumentTab();title();
}
bool closeDocumentTab(size_t index){
    snapPlot=0;snapStart.reset();
    if(!switchDocumentTab(index)||!discard())return false;
    bool closingOffice=officeDocument&&office::editing();if(closingOffice)office::close();
    documents.erase(documents.begin()+index);
    if(documents.empty()){documents.emplace_back();activeDocument=0;doc={};history.clear();filePath.clear();selection.clear();targetContainer=0;targetSlot=0;targetAbove=false;dirty=officeDocument=false;tool=Select;fit();storeDocumentTab();title();}
    else {activeDocument=std::min(index,documents.size()-1);auto next=documents[activeDocument];
        // The active globals still describe the removed tab; restore without saving them.
        doc=next.document;history=next.history;filePath=next.path;status=next.status;selection=next.selection;targetContainer=next.target;targetSlot=next.slot;targetAbove=next.above;dirty=next.dirty;officeDocument=next.office;zoom=next.zoom;origin=next.origin;tool=next.tool;hoverAtom=hoverBond=0;title();redraw();}
    if(closingOffice)ShowWindow(win,SW_SHOW);
    return true;
}
bool closeAllDocuments(){
    size_t initial=activeDocument;
    for(size_t i=0;i<documents.size();i++){if(!switchDocumentTab(i)||!discard())return false;storeDocumentTab();}
    switchDocumentTab(initial);return true;
}
void manageProjects(){
    if(!finishInline(true))return;
    projects::Callbacks callbacks;
    callbacks.blank=toCdxml(Document{});
    callbacks.current=[]()->std::optional<std::pair<std::wstring,std::string>>{
        if(!finishInline(true))return {};
        auto name=projects::displayName(filePath);if(name.empty())name=filePath.empty()?L"새 구조":fs::path(filePath).stem().wstring();
        return std::make_pair(name,toCdxml(doc));
    };
    callbacks.open=[](const std::wstring& source){
        bool isImage=projects::image(source);auto path=isImage?projects::imageDrawing(source).wstring():source;
        for(size_t i=0;i<documents.size();i++){
            auto current=i==activeDocument?filePath:documents[i].path;
            if(!current.empty()&&_wcsicmp(fs::absolute(current).lexically_normal().c_str(),fs::absolute(isImage?source+L".cdxml":path).lexically_normal().c_str())==0){switchDocumentTab(i);return;}
        }
        auto imported=isImage&&!fs::exists(path)?imageDocument(readFile(source)):fromCdxml(readFile(path));
        newDocumentTab(std::move(imported),path);
        if(isImage){filePath=source+L".cdxml";status=L"이미지 원본은 보관됩니다. 수정 후 Ctrl+S를 누르면 프로젝트 안에 편집 문서를 저장합니다.";if(!fs::exists(path))dirty=true;}
        else if(!doc.importWarnings.empty())MessageBoxW(win,L"일부 CDXML 객체는 표시·편집되지 않을 수 있습니다. 지원하지 않는 XML은 CDXML 저장 시 보존됩니다.",L"CDXML 열기",MB_OK|MB_ICONINFORMATION);
        title();redraw();
    };
    callbacks.close=[](const std::vector<std::wstring>& paths){
        for(size_t i=documents.size();i-->0;){auto path=i==activeDocument?filePath:documents[i].path;bool found=false;
            for(auto& source:paths)if(_wcsicmp(path.c_str(),source.c_str())==0||(projects::image(source)&&(_wcsicmp(path.c_str(),(source+L".cdxml").c_str())==0||_wcsicmp(path.c_str(),(source+L".ydraw").c_str())==0))){found=true;break;}
            if(found&&!closeDocumentTab(i))return false;
        }return true;
    };
    callbacks.preview=[](HDC dc,const RECT& area,const std::wstring& source){
        auto drawing=projects::imageDrawing(source);
        auto path=projects::image(source)&&fs::exists(drawing)?drawing.wstring():source;
        auto document=projects::image(path)?imageDocument(readFile(path)):fromCdxml(readFile(path));
        normalizeChemicalLabels(document);auto sc=makeScene(document,measure);auto b=sc.bounds;
        double scale=std::min(double(area.right-area.left-24)/std::max(1.,b.w),double(area.bottom-area.top-24)/std::max(1.,b.h));
        Graphics g(dc);g.SetClip(Gdiplus::Rect(area.left,area.top,area.right-area.left,area.bottom-area.top));g.SetSmoothingMode(SmoothingModeHighQuality);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.TranslateTransform(float(area.left+(area.right-area.left-b.w*scale)/2-b.x*scale),float(area.top+(area.bottom-area.top-b.h*scale)/2-b.y*scale));g.ScaleTransform(float(scale),float(scale));render(g,sc);
    };
    callbacks.saveForExport=[](const fs::path& folder){
        const auto initial=activeDocument;
        try{
            for(size_t i=0;i<documents.size();++i){
                const auto path=i==activeDocument?filePath:documents[i].path;
                if(!path.empty()&&_wcsicmp(fs::absolute(fs::path(path).parent_path()).lexically_normal().c_str(),fs::absolute(folder).lexically_normal().c_str())==0){
                    if(!switchDocumentTab(i)||(dirty&&!save())){switchDocumentTab(initial);return false;}
                }
            }
        }catch(...){switchDocumentTab(initial);throw;}
        switchDocumentTab(initial);return true;
    };
    projects::manage(win,callbacks);title();redraw();
}
Document officeCurrentDocument(){
    if(officeDocument)return doc;
    for(auto& tab:documents)if(tab.office)return tab.document;
    return doc;
}
LRESULT CALLBACK documentTabProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_MOUSEMOVE){TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,hwnd,0};TrackMouseEvent(&tracking);InvalidateRect(hwnd,nullptr,FALSE);}
    if(msg==WM_MOUSELEAVE)InvalidateRect(hwnd,nullptr,FALSE);
    if(msg==WM_PAINT){
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT client{};GetClientRect(hwnd,&client);
        Bitmap buffer(std::max(1L,client.right),std::max(1L,client.bottom),PixelFormat32bppARGB);Graphics g(&buffer);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        const auto& p=appearance::palette();g.Clear(p.chrome);POINT cursor{};GetCursorPos(&cursor);ScreenToClient(hwnd,&cursor);
        for(int i=0;i<TabCtrl_GetItemCount(hwnd);i++){RECT r{};if(!TabCtrl_GetItemRect(hwnd,i,&r))continue;bool selected=i==TabCtrl_GetCurSel(hwnd),hovered=PtInRect(&r,cursor)!=FALSE;
            RectF box(float(r.left+2),2.f,float(r.right-r.left-4),float(client.bottom-4));appearance::rounded(g,box,6,selected?p.raised:hovered?p.hover:p.chrome);
            if(selected)appearance::rounded(g,{box.X+12,box.GetBottom()-3,box.Width-24,2},1,p.accent);
            wchar_t label[512]{};TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=label;item.cchTextMax=512;TabCtrl_GetItem(hwnd,i,&item);std::wstring name(label);auto close=name.rfind(L"   ×");if(close!=std::wstring::npos)name.resize(close);
            auto saved=g.Save();g.SetClip(RectF(box.X+10,box.Y,box.Width-34,box.Height));text(g,name,box.X+10,box.Y+6,12,selected?p.text:p.muted,selected);g.Restore(saved);
            Pen cross(hovered?p.text:p.muted,1.2f);float x=float(r.right-14),y=float(client.bottom)/2;g.DrawLine(&cross,x-3,y-3,x+3,y+3);g.DrawLine(&cross,x-3,y+3,x+3,y-3);
        }
        Graphics out(dc);ui::presentPixels(out,buffer);EndPaint(hwnd,&ps);return 0;
    }
    if(msg==WM_LBUTTONUP){TCHITTESTINFO hit{{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)},0};int index=TabCtrl_HitTest(hwnd,&hit);RECT r{};
        if(index>=0&&TabCtrl_GetItemRect(hwnd,index,&r)&&hit.pt.x>=r.right-24){PostMessageW(win,WM_APP+3,index,0);return 0;}}
    return DefSubclassProc(hwnd,msg,wp,lp);
}
void createDocumentTabs(){
    if(isolatedStructureEditing){storeDocumentTab();return;}
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TAB_CLASSES};InitCommonControlsEx(&controls);
    documentTabs=CreateWindowExW(0,WC_TABCONTROLW,L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|TCS_FOCUSNEVER, left,89,ww-left-right,30,win,(HMENU)950,GetModuleHandleW(nullptr),nullptr);
    tabFont=CreateFontW(-14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,ui::interfaceFont());
    SendMessageW(documentTabs,WM_SETFONT,(WPARAM)tabFont,TRUE);SetWindowSubclass(documentTabs,documentTabProc,1,0);
    TabCtrl_SetPadding(documentTabs,20,5);
    storeDocumentTab();refreshDocumentTabs();
}

} // namespace yd::editor

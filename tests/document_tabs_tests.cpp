#include "win_button_help.h"
#include "win_dialogs.h"
// Exercise the production document switching and saving code in a hidden window.
#include "win_editor_state.h"
#include "win_editor_chrome.h"
#include "win_editor_images.h"
#include "win_editor_render.h"
#include "win_editor_files.h"
#include "win_editor_clipboard.h"
#include "win_editor_templates.h"
#include "win_editor_tabs.h"
#include "win_editor_text.h"
#include "win_editor_reaction.h"
#include "win_editor_plot.h"
#include "win_plot_manager.h"
#include "win_editor_structure.h"
#include "win_editor_commands.h"
#include "win_editor_canvas.h"
#include "win_editor_pointer.h"
#include "win_editor_shortcuts.h"
#include "win_editor_window.h"
#include "win_ui.h"
#include "win_theme.h"
#include "win_projects.h"
#include "win_storage.h"
#include "cdxml.h"
#include "office.h"
#include "plot_data.h"
#include "mechanism_data.h"
#include "structure_templates.h"
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

using namespace yd;
using namespace yd::editor;
#include <iostream>
#include "mechanism_layout_fixture.h"
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void CALLBACK applyBranchDetails(HWND parent,UINT,UINT_PTR timer,DWORD){
    HWND form=nullptr;while((form=FindWindowExW(nullptr,form,L"YoungDrawForm",L"분기 이름·에너지")))if(GetWindow(form,GW_OWNER)==parent)break;
    if(!form)return;KillTimer(parent,timer);
    SetDlgItemTextW(form,101,L"-4.5");SetDlgItemTextW(form,104,L"TS-branch");SetDlgItemTextW(form,105,L"17.2");PostMessageW(form,WM_COMMAND,IDOK,0);
}
static void neutralMechanismClick(Vec point){BYTE keys[256]{};SetKeyboardState(keys);SetTimer(win,990,10,applyBranchDetails);mouseDown(point);KillTimer(win,990);}
static void dropFiles(HWND hwnd,std::initializer_list<std::wstring> paths){
    size_t characters=1;
    for(const auto& path:paths)characters+=path.size()+1;
    auto handle=GlobalAlloc(GHND,sizeof(DROPFILES)+characters*sizeof(wchar_t));
    check(handle!=nullptr,"allocate file drop");
    auto* drop=static_cast<DROPFILES*>(GlobalLock(handle));
    if(!drop)GlobalFree(handle);
    check(drop!=nullptr,"lock file drop");
    drop->pFiles=sizeof(DROPFILES);
    drop->fWide=TRUE;
    auto* name=reinterpret_cast<wchar_t*>(drop+1);
    for(const auto& path:paths)name=std::copy_n(path.c_str(),path.size()+1,name);
    GlobalUnlock(handle);
    SendMessageW(hwnd,WM_DROPFILES,reinterpret_cast<WPARAM>(handle),0);
}
static int formAction=0;static bool formDriven=false,previewObserved=false;static std::string formBefore;
static bool paletteDriven=false;
static int pathNameStep=0,pathNameTicks=0;
static bool pathNameHeaderUpdated=false;
static void CALLBACK drivePathName(HWND parent,UINT,UINT_PTR timer,DWORD){
    const auto grid=FindWindowExW(plotManagerWindow,nullptr,L"YoungDrawEmbeddedGrid",nullptr);
    if(!grid)return;
    const auto header=ListView_GetHeader(GetDlgItem(grid,10));
    if(pathNameStep==0){
        RECT rect{};Header_GetItemRect(header,2,&rect);
        const auto point=MAKELPARAM((rect.left+rect.right)/2,(rect.top+rect.bottom)/2);
        PostMessageW(header,WM_LBUTTONDOWN,MK_LBUTTON,point);
        PostMessageW(header,WM_LBUTTONUP,0,point);
        PostMessageW(header,WM_LBUTTONDBLCLK,MK_LBUTTON,point);
        PostMessageW(header,WM_LBUTTONUP,0,point);
        pathNameStep=1;
        return;
    }
    if(pathNameStep==1){
        const auto dialog=plotManagerWindow?FindWindowExW(plotManagerWindow,nullptr,L"YoungDrawForm",L"경로 이름 변경"):nullptr;
        if(dialog&&GetParent(dialog)==plotManagerWindow){
            SetDlgItemTextW(dialog,100,L"S = 1");
            PostMessageW(dialog,WM_COMMAND,IDOK,0);
            pathNameStep=2;
            return;
        }
        if(++pathNameTicks<100)return;
    }
    wchar_t title[64]{};
    HDITEMW item{};item.mask=HDI_TEXT;item.pszText=title;item.cchTextMax=64;
    Header_GetItem(header,2,&item);
    pathNameHeaderUpdated=pathNameStep==2&&std::wstring(title)==L"S = 1";
    KillTimer(parent,timer);
    PostMessageW(plotManagerWindow,WM_CLOSE,0,0);
}
static int graphStyleMode=0,graphStyleResult=IDCANCEL;
static bool graphStyleCardsSeen=false,graphStylePreviewSeen=false;
static RECT graphManagerTabBounds{};
static bool graphManagerTabsAnchored(HWND manager){
    RECT bounds{};
    return GetWindowRect(GetDlgItem(manager,4000),&bounds)&&EqualRect(&bounds,&graphManagerTabBounds);
}
static void CALLBACK driveGraphStyle(HWND parent,UINT,UINT_PTR timer,DWORD){
    auto picker=FindWindowExW(plotManagerWindow,nullptr,L"YoungDrawGraphPicker",L"그래프 형태");
    if(!picker)picker=FindWindowExW(plotManagerWindow,nullptr,L"YoungDrawForm",L"그래프 형태");
    if(!picker)return;
    KillTimer(parent,timer);
    graphStyleCardsSeen=graphManagerTabsAnchored(plotManagerWindow);
    RECT previous{},panel{};GetWindowRect(picker,&panel);
    for(int i=0;i<5;++i){
        const auto card=GetDlgItem(picker,100+i);
        RECT bounds{};GetWindowRect(card,&bounds);
        graphStyleCardsSeen=graphStyleCardsSeen&&card&&ui::hasButtonHelp(card)&&
            (GetWindowLongPtrW(card,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW&&
            bounds.left>=panel.left&&bounds.right<=panel.right&&bounds.right>bounds.left&&
            (!i||(bounds.left>=previous.right&&bounds.top==previous.top));
        previous=bounds;
    }
    if(graphStyleCardsSeen){
        const auto selected=GetDlgItem(picker,100+doc.plots.front().mode);
        RECT bounds{};GetClientRect(selected,&bounds);
        Bitmap bitmap(bounds.right,bounds.bottom,PixelFormat32bppRGB);
        Graphics graphics(&bitmap);
        const auto dc=graphics.GetHDC();
        DRAWITEMSTRUCT item{ODT_BUTTON,UINT(GetDlgCtrlID(selected)),0,ODA_DRAWENTIRE,ODS_FOCUS,selected,dc,bounds};
        SendMessageW(picker,WM_DRAWITEM,item.CtlID,LPARAM(&item));
        const auto& palette=appearance::palette();
        graphStyleCardsSeen=GetPixel(dc,1,1)==palette.panel.ToCOLORREF()&&
            GetPixel(dc,bounds.right/2,1)==palette.hover.ToCOLORREF();
        graphics.ReleaseHDC(dc);
    }
    if(graphStyleCardsSeen){
        const int id=100+graphStyleMode;
        SendMessageW(picker,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),LPARAM(GetDlgItem(picker,id)));
        graphStylePreviewSeen=doc.plots.front().mode==graphStyleMode;
    }
    PostMessageW(picker,WM_COMMAND,graphStyleCardsSeen?graphStyleResult:IDCANCEL,0);
}
static bool chemistryMenuOpened=false;
enum class CrossingAction { AddLeft,AddRight,CancelEdit,DeleteLeft };
static CrossingAction crossingAction;
static bool crossingPreviewSeen=false;
static void CALLBACK driveCrossing(HWND parent,UINT,UINT_PTR timer,DWORD){
    const auto dialog=GetLastActivePopup(parent);
    if(dialog==parent)return;
    wchar_t title[80]{};
    GetWindowTextW(dialog,title,80);
    const std::wstring name=title;
    if(name==L"스핀 교차점 관리"||name==L"삭제할 스핀 교차점"){
        const auto list=GetDlgItem(dialog,11);
        const auto entry=name==L"스핀 교차점 관리"&&crossingAction==CrossingAction::DeleteLeft
            ?SendMessageW(list,LB_GETCOUNT,0,0)-1:0;
        SendMessageW(list,LB_SETCURSEL,entry,0);
        PostMessageW(dialog,WM_COMMAND,IDOK,0);
        if(name==L"삭제할 스핀 교차점")KillTimer(parent,timer);
        return;
    }
    if(name!=L"스핀 교차점 추가"&&name!=L"스핀 교차점 편집")return;
    KillTimer(parent,timer);
    if(crossingAction==CrossingAction::AddLeft){
        SendDlgItemMessageW(dialog,101,CB_SETCURSEL,1,0);
        SendDlgItemMessageW(dialog,102,CB_SETCURSEL,2,0);
    }
    if(crossingAction==CrossingAction::AddRight)SetDlgItemTextW(dialog,104,L"35");
    SetDlgItemTextW(dialog,103,crossingAction==CrossingAction::AddLeft?L"6.92":
        crossingAction==CrossingAction::AddRight?L"6.57":L"7.1");
    if(crossingAction==CrossingAction::CancelEdit)
        crossingPreviewSeen=doc.plots.front().crossings.front().energy==7.1;
    else{
        wchar_t segment[64]{};
        SendDlgItemMessageW(dialog,100,CB_GETLBTEXT,0,reinterpret_cast<LPARAM>(segment));
        crossingPreviewSeen=std::wstring(segment)==(crossingAction==CrossingAction::AddLeft?L"R → TS":L"TS → P")&&
            scene().crossingCaptions.size()==(crossingAction==CrossingAction::AddLeft?1:2);
    }
    PostMessageW(dialog,WM_COMMAND,IsWindowEnabled(GetDlgItem(dialog,IDOK))&&
        crossingAction!=CrossingAction::CancelEdit?IDOK:IDCANCEL,0);
}
static void CALLBACK driveChemistryMenu(HWND parent,UINT,UINT_PTR timer,DWORD){
    HWND dialog=GetLastActivePopup(parent);
    wchar_t name[64]{};
    GetClassNameW(dialog,name,64);
    if(std::wstring(name)==L"YoungDrawGrid"){
        KillTimer(parent,timer);
        const auto list=GetDlgItem(dialog,10);
        SendMessageW(list,WM_KEYDOWN,VK_RIGHT,0);
        SendMessageW(list,WM_KEYDOWN,VK_RIGHT,0);
        SendMessageW(list,WM_KEYDOWN,VK_F2,0);
        if(auto editor=FindWindowExW(list,nullptr,L"EDIT",nullptr)){
            SetWindowTextW(editor,L"-0.314");
            chemistryMenuOpened=true;
            PostMessageW(dialog,WM_COMMAND,22,0);
        }else PostMessageW(dialog,WM_CLOSE,0,0);
    }else if(std::wstring(name)==L"YoungDrawForm"){ui::attachWindowHelp(dialog);EnumChildWindows(dialog,[](HWND button,LPARAM)->BOOL{wchar_t type[32]{};GetClassNameW(button,type,32);if(!_wcsicmp(type,L"Button"))check(ui::hasButtonHelp(button),"all dialog buttons have help");return TRUE;},0);
        KillTimer(parent,timer);
        chemistryMenuOpened=true;
        PostMessageW(dialog,WM_COMMAND,IDOK,0);
    }
}
static std::vector<std::wstring> molecularInputs;
static bool molecularFormOpened=false;
static bool cancelMolecularForm=false;
static void CALLBACK driveMolecularForm(HWND owner,UINT,UINT_PTR timer,DWORD){
    const auto dialog=GetLastActivePopup(owner);
    wchar_t title[160]{};
    GetWindowTextW(dialog,title,int(std::size(title)));
    if(std::wstring(title).find(L"분자 주석 · ")!=0)return;
    KillTimer(owner,timer);
    molecularFormOpened=true;
    for(size_t i=0;i<molecularInputs.size();++i)
        SetDlgItemTextW(dialog,100+int(i),molecularInputs[i].c_str());
    const bool applicable=IsWindowEnabled(GetDlgItem(dialog,IDOK));
    molecularFormOpened=molecularFormOpened&&applicable;
    PostMessageW(dialog,WM_COMMAND,applicable&&!cancelMolecularForm?IDOK:IDCANCEL,0);
}

static void checkMolecularAnnotations(HWND hwnd){
    doc=Document{};
    history.clear();
    const int a=doc.addAtom({0,0},27),b=doc.addAtom({42,0},8),c=doc.addAtom({68,32},6);
    const int ab=doc.addBond(a,b),bc=doc.addBond(b,c);
    auto edit=[&](std::vector<int> anchors,std::vector<std::wstring> values,bool cancel=false){
        selection=std::move(anchors);
        molecularInputs=std::move(values);
        cancelMolecularForm=cancel;
        molecularFormOpened=false;
        SetTimer(hwnd,978,10,driveMolecularForm);
        SendMessageW(hwnd,WM_COMMAND,molecularAnnotationsCommand,0);
        KillTimer(hwnd,978);
        check(molecularFormOpened,"molecular annotation command opens its contextual form");
    };
    edit({a},{L"-0.21",L"0.314",L"#126aaa",L"#a7336e"});
    check(doc.atom(a)->partialCharge.value==-.21&&doc.atom(a)->mullikenSpin==.314&&
          !doc.atom(b)->partialCharge.value&&!doc.atom(b)->mullikenSpin,"charge and spin are optional per atom");
    edit({ab},{L"0.75",L"1.43"});
    check(doc.bond(ab)->mayerOrder.value==.75&&doc.measurements.size()==1,"bond order and bond length can be entered together");
    edit({ab,bc},{L"2.67",L"121.4"});
    edit({a,b,c},{L"116.5"});
    const auto drawing=toNative(doc);
    edit({c,b,a},{L"30"},true);
    check(toNative(doc)==drawing,"cancelling a preview restores prior measurements");
    edit({c,b,a},{L""});
    check(doc.measurements.size()==3&&history.undo(doc)&&toNative(doc)==drawing,
          "clearing reversed angle targets removes the same annotation and is undoable");
    redraw();
    const auto svg=toSvg(scene());
    check(svg.find("MBO 0.75")!=std::string::npos&&svg.find("#126aaa")!=std::string::npos&&
          svg.find("121.4°")!=std::string::npos&&svg.find("2.67 Å")!=std::string::npos,
          "SVG includes colored numerical annotation labels and units");
    const auto xml=toCdxml(doc);
    check(xml.find("MBO 0.75")!=std::string::npos&&toNative(fromCdxml(xml))==drawing,
          "CDXML contains visible labels and preserves editable annotation data");
    SendMessageW(hwnd,WM_COMMAND,showMolecularAnnotationsCommand,0);
    check(toSvg(scene()).find("MBO 0.75")==std::string::npos&&doc.bond(ab)->mayerOrder.value==.75,
          "visibility toggle keeps annotation data");
    SendMessageW(hwnd,WM_COMMAND,showMolecularAnnotationsCommand,0);
    auto fragment=doc.subset({a,b,c});
    Document copy;
    copy.addAtom({-60,0});
    copy.append(fragment,{150,0});
    check(copy.measurements.size()==4&&copy.measurements.front().anchors.front()!=a,
          "copy remaps all measurement anchors");
    copy.eraseBond(copy.bonds.front().id);
    check(copy.measurements.size()==2,"deleting a bond removes measurements that reference that bond");
    selection.clear();
    history.clear();
}

static int mechanismGridAction=0;
static int mechanismLayoutChoice=0;
static bool mechanismDraftHidden=false;
static void CALLBACK driveNewMechanism(HWND parent,UINT,UINT_PTR timer,DWORD){
    HWND dialog=GetLastActivePopup(parent);
    wchar_t name[64]{};
    GetClassNameW(dialog,name,64);
    if(std::wstring(name)==L"YoungDrawMechanismPicker"){
        for(int id=100;id<104;++id)check(ui::hasButtonHelp(GetDlgItem(dialog,id)),"mechanism previews have path descriptions");
        if(!mechanismGridAction){
            RECT bounds{};GetClientRect(dialog,&bounds);
            Bitmap snapshot(bounds.right,bounds.bottom,PixelFormat32bppARGB);
            Graphics g(&snapshot);auto dc=g.GetHDC();
            const auto painted=PrintWindow(dialog,dc,PW_CLIENTONLY);
            g.ReleaseHDC(dc);
            CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
            check(painted&&snapshot.Save(L"mechanism-path-picker.png",&png,nullptr)==Ok,"capture mechanism path cards");
        }
        SendMessageW(dialog,WM_COMMAND,100+mechanismLayoutChoice,0);
        PostMessageW(dialog,WM_COMMAND,IDOK,0);
        return;
    }
    if(std::wstring(name)==L"YoungDrawForm"){
        wchar_t title[64]{};
        GetWindowTextW(dialog,title,64);
        if(std::wstring(title)==L"원형 분기·합류 단계 수"){
            SetDlgItemTextW(dialog,100,L"4");
            SetDlgItemTextW(dialog,101,L"1");
            SetDlgItemTextW(dialog,102,L"3");
            PostMessageW(dialog,WM_COMMAND,IDOK,0);
        }
        return;
    }
    if(std::wstring(name)!=L"YoungDrawMechanismEditor")return;
    KillTimer(parent,timer);
    mechanismDraftHidden=toNative(doc)==formBefore;
    if(mechanismGridAction)PostMessageW(dialog,WM_COMMAND,IDOK,0);
    else PostMessageW(dialog,WM_CLOSE,0,0);
}
static void checkNewMechanism(HWND hwnd){
    int atom=doc.addAtom({10,20});
    selection={atom};
    dirty=false;
    tool=Text;
    sidebarPage=1;
    history.clear();
    formBefore=toNative(doc);
    mechanismGridAction=0;
    SetTimer(hwnd,901,10,driveNewMechanism);
    command(120);
    check(mechanismDraftHidden&&toNative(doc)==formBefore&&!dirty&&!history.undo(doc)&&
          selection==std::vector<int>{atom}&&tool==Text&&sidebarPage==1,
          "discarding the new mechanism leaves canvas, selection, tool, sidebar and history intact");
    mechanismGridAction=1;
    for(int choice=0;choice<4;choice++){
        mechanismLayoutChoice=choice;
        SetTimer(hwnd,901,10,driveNewMechanism);
        command(120);
        check(doc.reactions.size()==1&&dirty&&sidebarPage==0,"creation command applies each selected mechanism layout");
        const auto r=doc.reactions.front();
        check(r.cyclic==(choice==3)&&r.fixedFrames==(choice==1||choice==2)&&
              r.slots.size()==std::vector<size_t>{3,6,11,4}[choice],"creation menu selects the intended topology and requested counts");
        if(choice==2){
            auto before=toNative(doc);
            for(size_t edge=0;edge<r.links.size();edge++){
                fit();
                auto buttons=mechanismSplitButtons(doc.reactions.front());
                check(buttons.empty(),"transition states never expose branch plus buttons");
                insertMechanismLinkStage(r.id,edge);
                const auto& added=doc.reactions.front();
                check(added.slots.size()==r.slots.size()+1&&added.links[edge].to==int(r.slots.size())&&
                      added.links.back().to==r.links[edge].to,"explicit intermediate insertion splits exactly its connection");
                check(history.undo(doc)&&toNative(doc)==before,"connection insertion is undone in one step");
            }
        }
        check(history.undo(doc)&&toNative(doc)==formBefore&&!history.undo(doc),
              "applying a new mechanism creates exactly one undo step");
    }
    doc={};selection.clear();history.clear();dirty=false;tool=Select;
}
static bool recognizedTableOpened=false;
static void CALLBACK driveSidebarRecognition(HWND parent,UINT,UINT_PTR timer,DWORD){
    auto editor=FindWindowW(L"YoungDrawMechanismEditor",nullptr);
    if(editor&&GetWindow(editor,GW_OWNER)==parent){KillTimer(parent,timer);recognizedTableOpened=true;PostMessageW(editor,WM_COMMAND,IDOK,0);}
}

static void CALLBACK driveTextPalette(HWND parent,UINT,UINT_PTR timer,DWORD){
    HWND dialog=nullptr;
    while((dialog=FindWindowExW(nullptr,dialog,L"YoungDrawColorPicker",nullptr)))if(GetWindow(dialog,GW_OWNER)==parent)break;
    if(!dialog)return;
    KillTimer(parent,timer);paletteDriven=true;
    SetDlgItemTextW(dialog,300,L"#235AE1");
    PostMessageW(dialog,WM_COMMAND,IDOK,0);
}
static void CALLBACK driveMechanismAdvanced(HWND parent,UINT,UINT_PTR timer,DWORD){
    HWND dialog=nullptr;
    while((dialog=FindWindowExW(nullptr,dialog,L"YoungDrawForm",L"여백·글자·자동 길이"))){if(GetWindow(dialog,GW_OWNER)==parent)break;}
    if(!dialog)return;
    KillTimer(parent,timer);SetDlgItemTextW(dialog,100,L"180");PostMessageW(dialog,WM_COMMAND,IDOK,0);
}
static void CALLBACK driveForm(HWND parent,UINT,UINT_PTR timer,DWORD){
    HWND dialog=plotManagerWindow?FindWindowExW(plotManagerWindow,nullptr,L"YoungDrawForm",nullptr):nullptr;
    if(!dialog)dialog=GetLastActivePopup(parent);
    wchar_t name[64]{};GetClassNameW(dialog,name,64);
    if(std::wstring(name)==L"YoungDrawMechanismEditor"&&(formAction==3||formAction==4||formAction==26)){
        KillTimer(parent,timer);formDriven=true;previewObserved=toNative(doc)==formBefore;
        SendDlgItemMessageW(dialog,11,CB_SETCURSEL,formAction==26?5:2,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(11,CBN_SELCHANGE),0);
        if(formAction!=26){SetDlgItemTextW(dialog,19,L"2");SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(19,EN_KILLFOCUS),0);SetTimer(dialog,902,10,driveMechanismAdvanced);SendMessageW(dialog,WM_COMMAND,35,0);KillTimer(dialog,902);}
        PostMessageW(dialog,WM_COMMAND,formAction==3?IDCANCEL:IDOK,0);return;
    }
    if(std::wstring(name)!=L"YoungDrawForm")return;
    KillTimer(parent,timer);formDriven=true;
    if(formAction==41||formAction==42){
        SetDlgItemTextW(dialog,100,L"17");SetDlgItemTextW(dialog,101,L"21");SetDlgItemTextW(dialog,102,L"16");
        previewObserved=toNative(doc)==formBefore;
    }else if(formAction>=20&&formAction<=24){
        if(formAction<=22){SendDlgItemMessageW(dialog,101,CB_SETCURSEL,formAction==22?0:1,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(101,CBN_SELCHANGE),(LPARAM)GetDlgItem(dialog,101));}
        else if(formAction==23){SetDlgItemTextW(dialog,101,L"kJ/mol");SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(101,CBN_EDITCHANGE),(LPARAM)GetDlgItem(dialog,101));}
        previewObserved=toNative(doc)!=formBefore;
    }else if(formAction==18||formAction==19){
        SendDlgItemMessageW(dialog,117,CB_SETCURSEL,1,0);
        SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(117,CBN_SELCHANGE),(LPARAM)GetDlgItem(dialog,117));previewObserved=toNative(doc)!=formBefore;
    }else if(formAction>=15&&formAction<=17){
        SendDlgItemMessageW(dialog,116,CB_SETCURSEL,formAction==17?0:1,0);
        SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(116,CBN_SELCHANGE),(LPARAM)GetDlgItem(dialog,116));previewObserved=toNative(doc)!=formBefore;
    }else if(formAction==35||formAction==36){
        SetDlgItemTextW(dialog,100,formAction==35?L"Times New Roman":L"Arial");SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(100,CBN_EDITCHANGE),(LPARAM)GetDlgItem(dialog,100));SendDlgItemMessageW(dialog,101,CB_SETCURSEL,formAction==35?3:2,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(101,CBN_SELCHANGE),(LPARAM)GetDlgItem(dialog,101));previewObserved=toNative(doc)!=formBefore;
    }else if(formAction==43||formAction==44){
        const std::string symbol=formAction==43?"ΔE":"ΔG";
        SendDlgItemMessageW(dialog,123,CB_SETCURSEL,formAction==43?1:2,0);
        SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(123,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(dialog,123)));
        wchar_t title[32]{};GetDlgItemTextW(dialog,103,title,32);
        previewObserved=ui::utf8(title)==symbol&&doc.plots.front().energySymbol==symbol&&doc.plots.front().yLabel==symbol&&toSvg(makeScene(doc,measure)).find(symbol)!=std::string::npos;
    }else if(formAction>=37&&formAction<=40){
        if(formAction==37)SetDlgItemTextW(dialog,100,L"2.5");
        if(formAction<=38)SendDlgItemMessageW(dialog,109,BM_CLICK,0,0);
        if(formAction==39)SendDlgItemMessageW(dialog,107,CB_SETCURSEL,2,0);
        if(formAction==40){
            SetDlgItemTextW(dialog,107,L"48");
            SendDlgItemMessageW(dialog,109,CB_SETCURSEL,2,0);
        }
        if(formAction>=39){
            SetDlgItemTextW(dialog,101,formAction==39?L"12":L"18");
            SetDlgItemTextW(dialog,102,formAction==39?L"6":L"9");
        }
    }else if(formAction>=31&&formAction<=34){
        const int field=(formAction==32||formAction==34)?101:100;
        SetDlgItemTextW(dialog,field,formAction==31?L"3.2":formAction==32?L"4":formAction==33?L"5":L"12");
        if(formAction==31&&GetParent(dialog)==plotManagerWindow)SetDlgItemTextW(dialog,101,L"4");
        previewObserved=toNative(doc)!=formBefore;
    }else if(formAction==27||formAction==28){
        SetDlgItemTextW(dialog,100,formAction==27?L"60":L"80");previewObserved=toNative(doc)!=formBefore;
    }else if(formAction==25){
        SetDlgItemTextW(dialog,100,L"4");
    }else if(formAction==26){
        SendDlgItemMessageW(dialog,101,CB_SETCURSEL,4,0);
    }else if(formAction==11||formAction==12){
        SetDlgItemTextW(dialog,100,L"4");previewObserved=toNative(doc)!=formBefore;
    }else if(formAction==13||formAction==14){
        SetDlgItemTextW(dialog,101,L"15");SetDlgItemTextW(dialog,102,L"85");previewObserved=toNative(doc)!=formBefore;
    }else if(formAction==9||formAction==10){
        SendDlgItemMessageW(dialog,101,CB_SETCURSEL,1,0);SendDlgItemMessageW(dialog,102,CB_SETCURSEL,2,0);
        SetDlgItemTextW(dialog,104,L"#cc2255");previewObserved=toNative(doc)!=formBefore;
    }else if(formAction==8){SetDlgItemTextW(dialog,100,L"30");
    }else if(formAction==7){
        SendDlgItemMessageW(dialog,100,CB_SETCURSEL,2,0);SendDlgItemMessageW(dialog,101,CB_SETCURSEL,1,0);
        SetDlgItemTextW(dialog,102,L"40");SetDlgItemTextW(dialog,103,L"45");
    }else if(formAction==5||formAction==6){
        HWND edit=GetDlgItem(dialog,100);
        SetWindowTextW(edit,L"45");
        auto before=toNative(doc);
        SendMessageW(edit,WM_CHAR,L'+',0);
        auto after=toNative(doc);
        SendMessageW(edit,WM_CHAR,L'-',0);
        previewObserved=before!=after&&toNative(doc)==before;
        SendMessageW(edit,WM_CHAR,L'+',0);
        previewObserved=previewObserved&&toNative(doc)==after;
    }else if(formAction==3||formAction==4){
        SendDlgItemMessageW(dialog,101,CB_SETCURSEL,2,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(101,CBN_SELCHANGE),(LPARAM)GetDlgItem(dialog,101));
        SetDlgItemTextW(dialog,103,L"2");SetDlgItemTextW(dialog,104,L"180");previewObserved=toNative(doc)!=formBefore;
    }else if(formAction==0){
        HWND edit=GetDlgItem(dialog,100);SetWindowTextW(edit,L"H2");SendMessageW(edit,EM_SETSEL,1,2);
        BYTE before[256]{};GetKeyboardState(before);BYTE keys[256];memcpy(keys,before,sizeof(keys));keys[VK_CONTROL]=0x80;keys[VK_SHIFT]=0;SetKeyboardState(keys);
        SendMessageW(edit,WM_KEYDOWN,VK_OEM_PLUS,0);SetKeyboardState(before);
        int length=GetWindowTextLengthW(edit);SendMessageW(edit,EM_SETSEL,length,length);
        memcpy(keys,before,sizeof(keys));keys[VK_MENU]=0x80;SetKeyboardState(keys);
        SendMessageW(edit,WM_SYSKEYDOWN,VK_RETURN,0);SendMessageW(edit,WM_SYSCHAR,VK_RETURN,0);SetKeyboardState(before);
        SendMessageW(edit,EM_REPLACESEL,TRUE,(LPARAM)L"Water");
    }else{
        SetDlgItemTextW(dialog,101,L"30");previewObserved=toNative(doc)!=formBefore;
    }
    SendMessageW(dialog,WM_COMMAND,(formAction==1||formAction==3||formAction==6||formAction==10||formAction==12||formAction==14||formAction==15||formAction==18||formAction==20||formAction==28||formAction==33||formAction==34||formAction==36||formAction==42)?IDCANCEL:IDOK,0);
}
static void graphCaptionChecks(HWND hwnd){
    doc=coordinateDemo();history.clear();selection.clear();tool=Select;guides=true;fit();
    auto& plot=doc.plots[0];plot.series[0].name="Editable pathway";
    auto picture=scene();auto text=std::find_if(picture.items.begin(),picture.items.end(),[&](const Primitive& p){return p.owner==plot.id&&p.type==Primitive::Text&&p.textRole==7;});
    check(text!=picture.items.end(),"legend exists for caption test");
    auto at=plotAnchor(plot,1,true);plot.legendOffset=at-text->points[0]-Vec{3,3};fit();
    auto target=textAt(at);check(target&&target->role==7&&pickSlot(at),"legend text overlaps an insertion frame");
    cancel();auto point=screen(at);SendMessageW(hwnd,WM_LBUTTONDBLCLK,0,MAKELPARAM(int(point.x),int(point.y)));
    check(inlineEdit&&inlineTarget.role==7,"double-click edits caption before opening overlapping structure frame");
    SetWindowTextW(inlineEdit,L"Verified pathway");check(finishInline(true)&&doc.plots[0].series[0].name=="Verified pathway","caption edit applies to graph data");
    check(history.undo(doc)&&doc.plots[0].series[0].name=="Editable pathway","caption edit remains undoable");
    doc={};history.clear();selection.clear();tool=Select;dirty=false;
}
static void graphUnitChecks(HWND hwnd){
    doc=coordinateDemo();doc.plots[0].unit="kcal/mol";history.clear();selection={doc.plots[0].id};dirty=false;int owner=selection[0];
    auto before=toNative(doc);formBefore=before;formAction=20;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);plotSettings(owner);
    check(formDriven&&previewObserved&&toNative(doc)==before,"unit selection previews immediately and Cancel restores the graph");
    formAction=21;formDriven=false;SetTimer(hwnd,901,10,driveForm);plotSettings(owner);
    check(formDriven&&doc.plots[0].unit=="kcal mol^{-1}","inverse-mole choice stores superscript formatting");
    bool raised=false;for(const auto& text:makeScene(doc,measure).items)if(text.rotation==-90&&text.text=="–1")raised=true;
    check(raised,"energy axis renders en dash one as a separate raised text run");
    for(auto copy:{fromNative(toNative(doc)),fromCdxml(toCdxml(doc))})check(copy.plots[0].unit=="kcal mol^{-1}","unit survives native and CDXML roundtrips");
    check(toSvg(makeScene(doc,measure)).find("kcal mol")!=std::string::npos,"SVG contains the selected unit");
    writePreview(L"graph-inverse-mole-unit.png",doc);
    {
        auto preview=doc;auto& graph=preview.plots[0];graph.energyAtTop=true;
        graph.series.push_back({"S = 1","#b44758",{0,26,10,19,-9.9},1,2});graph.series[0].name="S = 0";
        writePreview(L"graph-top-energy-header.png",preview);
        graph.showYTicks=false;writePreview(L"graph-top-energy-no-ticks.png",preview);
        graph.showXAxis=false;graph.showYAxis=false;writePreview(L"graph-no-axes.png",preview);
    }

    check(history.undo(doc)&&doc.plots[0].unit=="kcal/mol","unit choice is undoable");
    check(history.redo(doc)&&doc.plots[0].unit=="kcal mol^{-1}","unit choice is redoable");
    formAction=24;SetTimer(hwnd,901,10,driveForm);plotSettings(owner);check(doc.plots[0].unit=="kcal mol^{-1}","reopening settings preserves selected notation");
    formAction=22;SetTimer(hwnd,901,10,driveForm);plotSettings(owner);check(doc.plots[0].unit=="kcal/mol","slash unit can be restored from the list");
    formAction=23;SetTimer(hwnd,901,10,driveForm);plotSettings(owner);check(doc.plots[0].unit=="kJ/mol","custom energy units remain editable");
    formAction=24;SetTimer(hwnd,901,10,driveForm);plotSettings(owner);check(doc.plots[0].unit=="kJ/mol","custom unit survives reopening settings");
    for(int notation:{43,44}){
        const auto previous=toNative(doc);history.clear();formAction=notation;formDriven=previewObserved=false;
        SetTimer(hwnd,901,10,driveForm);plotSettings(owner);
        check(formDriven&&previewObserved,"general graph settings synchronize Delta notation, visible axis title and SVG");
        check(history.undo(doc)&&toNative(doc)==previous&&!history.undo(doc),"Delta title change remains one undo step");
    }
    doc={};history.clear();selection.clear();dirty=false;
    std::cout<<"Graph energy unit selection, preview, cancellation, undo and export checks passed\n";
}
static void pngExportChecks(HWND hwnd){
    struct ClipboardRestore{IDataObject* original=nullptr;ClipboardRestore(){check(SUCCEEDED(OleGetClipboard(&original)),"preserve clipboard before PNG regression");}~ClipboardRestore(){if(SUCCEEDED(OleSetClipboard(original))&&original)OleFlushClipboard();if(original)original->Release();}} restore;
    doc={};selection.clear();history.clear();dirty=false;
    Object first;first.id=doc.nextId++;first.kind=ObjectKind::Line;first.points={{-35,-20},{35,20}};first.width=1.7;first.color="#194fa0";doc.objects.push_back(first);
    auto second=first;second.id=doc.nextId++;for(auto& p:second.points)p.x+=400;doc.objects.push_back(second);
    const auto before=toNative(doc);auto bitmap=drawingBitmap(doc,true);Color corner;bitmap->GetPixel(0,0,&corner);check(corner.GetA()==0,"PNG background has zero alpha");
    size_t opaque=0,partial=0;for(UINT y=0;y<bitmap->GetHeight();y++)for(UINT x=0;x<bitmap->GetWidth();x++){Color pixel;bitmap->GetPixel(x,y,&pixel);if(pixel.GetA()==255)opaque++;else if(pixel.GetA()>0)partial++;}
    check(opaque>10&&partial>10,"transparent PNG contains visible strokes and antialiased alpha edges");
    writePreview(L"transparent-png-export.png",doc,true);Bitmap saved(L"transparent-png-export.png");check(saved.GetLastStatus()==Ok&&saved.GetWidth()==bitmap->GetWidth(),"exported PNG is a decodable image");saved.GetPixel(0,0,&corner);check(corner.GetA()==0,"file export preserves transparent background");
    auto clipboardPng=[&](){check(OpenClipboard(hwnd)!=0,"open PNG clipboard for verification");auto bytes=clipboardMemoryBytes(GetClipboardData(RegisterClipboardFormatW(L"PNG")));auto alias=clipboardMemoryBytes(GetClipboardData(RegisterClipboardFormatW(L"image/png")));CloseClipboard();check(bytes==alias&&bytes.size()>8&&bytes.substr(0,8)==std::string("\x89PNG\r\n\x1a\n",8),"clipboard carries real PNG bytes in both native and MIME formats");return bytes;};
    std::cerr<<"PNG file alpha verified; copying entire document\n";SendMessageW(hwnd,WM_COMMAND,copyPngCommand,0);std::cerr<<"PNG document copy returned\n";check(clipboardPng()==encodePng(*bitmap),"PNG copy without selection copies the whole document");
    selection={first.id};auto selected=drawingBitmap(selectedDocument(),true);check(selected->GetWidth()<bitmap->GetWidth(),"selected image bounds omit unselected objects");command(copyPngCommand);check(clipboardPng()==encodePng(*selected),"PNG copy uses only the selection when present");
    isolatedStructureEditing=true;
    Bitmap toolbar(ww,wh,PixelFormat32bppARGB);
    {Graphics graphics(&toolbar);HDC dc=graphics.GetHDC();paint(dc);graphics.ReleaseHDC(dc);}
    const auto copyIcon=std::find_if(hits.begin(),hits.end(),[](const auto& hit){return hit.id==copyPngCommand;});
    const auto applyButton=std::find_if(hits.begin(),hits.end(),[](const auto& hit){return hit.id==186;});
    check(copyIcon!=hits.end()&&applyButton!=hits.end()&&copyIcon->box.Y==applyButton->box.Y&&
        copyIcon->box.Height==applyButton->box.Height&&applyButton->box.X-copyIcon->box.GetRight()==8,
        "independent structure editor displays PNG copy next to Apply in the top header");
    const auto copyPoint=MAKELPARAM(int(copyIcon->box.X+copyIcon->box.Width/2),int(copyIcon->box.Y+copyIcon->box.Height/2));
    selection.clear();
    auto highResolution=drawingBitmap(doc,true,highResolutionDpi);
    check(highResolution->GetWidth()>3*bitmap->GetWidth(),"high-resolution export rasterizes at higher pixel dimensions");
    SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,copyPoint);
    check(clipboardPng()==encodePng(*highResolution),"independent structure editor icon copies the complete structure at high resolution");
    selection={first.id};
    auto highResolutionSelection=drawingBitmap(selectedDocument(),true,highResolutionDpi);
    SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,copyPoint);
    check(clipboardPng()==encodePng(*highResolutionSelection),"independent structure editor icon copies only selected items at high resolution");
    writeTiff(L"structure-600dpi.tif",selectedDocument());
    Bitmap tiff(L"structure-600dpi.tif");
    GUID format{};
    check(tiff.GetRawFormat(&format)==Ok&&IsEqualGUID(format,ImageFormatTIFF)&&tiff.GetWidth()==highResolutionSelection->GetWidth()&&
        tiff.GetHeight()==highResolutionSelection->GetHeight()&&std::abs(tiff.GetHorizontalResolution()-600)<1&&
        std::abs(tiff.GetVerticalResolution()-600)<1,"Structure TIFF has the selected content dimensions and 600 DPI metadata");
    tiff.GetPixel(0,0,&corner);
    check(corner.GetValue()==Color(255,255,255,255).GetValue(),"Structure TIFF has an opaque white background");
    isolatedStructureEditing=false;
    check(toNative(doc)==before&&!dirty,"PNG export and copy never modify the source document");
    auto menu=GetSubMenu(GetMenu(hwnd),0);wchar_t label[128]{};check(GetMenuStringW(menu,exportPngCommand,label,128,MF_BYCOMMAND)>0,"file menu exposes PNG picture export");
    Document wavePreview;auto graph=reactionProfilePlot();graph.id=wavePreview.nextId++;graph.states[1].breakAfter=true;graph.states[1].breakStyle=1;graph.states[1].breakScale=.1;graph.inkScale=1.5;graph.series[0].width=4;graph.series[0].name="Reaction pathway";graph.legendFontSize=20;wavePreview.plots={graph};writePreview(L"wave-legend-preview.png",wavePreview,true);
    doc={};selection.clear();history.clear();std::cout<<"Transparent PNG file and clipboard checks passed\n";
}
static void CALLBACK driveDocumentStyle(HWND parent,UINT,UINT_PTR timer,DWORD){
    HWND dialog=GetLastActivePopup(parent);
    wchar_t title[64]{};GetWindowTextW(dialog,title,64);
    if(std::wstring(title)!=L"문서 서식")return;
    KillTimer(parent,timer);
    SetDlgItemTextW(dialog,100,L"Times New Roman");
    SendDlgItemMessageW(dialog,101,CB_SETCURSEL,0,0);
    SendDlgItemMessageW(dialog,102,WM_KEYDOWN,VK_UP,0);
    RECT width{};GetWindowRect(GetDlgItem(dialog,103),&width);
    SendMessageW(dialog,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(width.left+10,width.top+10));
    const auto apply=IsWindowEnabled(GetDlgItem(dialog,IDOK));
    PostMessageW(dialog,WM_COMMAND,apply?IDOK:IDCANCEL,0);
}
static void documentStyleChecks(HWND hwnd){
    doc={};history.clear();selection.clear();
    const int atom=doc.addAtom({20,25},8);
    selection={atom};origin={270,360};zoom=1.7;
    const auto before=toNative(doc);
    SetTimer(hwnd,903,10,driveDocumentStyle);
    command(150);
    KillTimer(hwnd,903);
    check(doc.style.labelFont=="Times New Roman"&&!(doc.style.labelFace&1)&&doc.style.fontSize==15&&
        std::abs(doc.style.lineWidth-2.1)<1e-8,"sidebar style edits from typed input, a direction key and the mouse wheel");
    check(origin.x==270&&origin.y==360&&zoom==1.7&&selection==std::vector<int>{atom}&&
        doc.atom(atom)->p.x==20&&doc.atom(atom)->p.y==25,"style edits retain selection, coordinates and viewport");
    check(fromNative(toNative(doc)).style.labelFont==doc.style.labelFont&&
        fromCdxml(toCdxml(doc)).style.labelFont==doc.style.labelFont,"document font survives native and CDXML round trips");
    check(history.undo(doc)&&toNative(doc)==before,"document style changes share one undo checkpoint");
}
static bool photoFormSeen=false,removePhoto=false;
static void CALLBACK drivePhotoForm(HWND owner,UINT,UINT_PTR timer,DWORD){
    const auto form=FindWindowExW(plotManagerWindow,nullptr,L"YoungDrawForm",L"광여기 hν");
    if(!form)return;
    KillTimer(owner,timer);
    const auto combo=GetDlgItem(form,100);
    wchar_t text[256]{};
    SendMessageW(combo,CB_GETLBTEXT,0,LPARAM(text));
    const std::wstring choice=text;
    photoFormSeen=choice.find(removePhoto?L"해제":L"추가")!=std::wstring::npos&&
                  choice.find(L"0")!=std::wstring::npos&&choice.find(L"37.2")!=std::wstring::npos;
    PostMessageW(form,WM_COMMAND,IDOK,0);
}
static void annotationPreviewChecks(HWND){
    doc=coordinateDemo();
    auto& graph=doc.plots.front();const int owner=graph.id;
    graph.stages=defaultMechanismStages();graph.yColor="#00a056";graph.yAxisWidth=8;
    for(auto& state:graph.states){state.above.clear();state.below.clear();}
    doc.atoms.clear();doc.bonds.clear();doc.objects.clear();doc.reactions.clear();doc.groups.clear();
    history.clear();selection={owner};tool=Select;
    const Document original=doc;const auto before=toNative(doc);
    const auto originalAxes=makeScene(doc,measure).plotAxes.at(owner);
    managePlot(owner);const auto manager=plotManagerWindow;
    SendMessageW(manager,WM_COMMAND,4002,0);
    auto axisPixels=[&](const wchar_t* filename){
        RECT client{},tab{};GetClientRect(manager,&client);GetWindowRect(GetDlgItem(manager,4000),&tab);
        MapWindowPoints(nullptr,manager,reinterpret_cast<POINT*>(&tab),2);
        Bitmap image(client.right,client.bottom,PixelFormat32bppARGB);
        {Graphics graphics(&image);auto dc=graphics.GetHDC();
         check(PrintWindow(manager,dc,PW_CLIENTONLY)!=FALSE,"capture annotation preview window");graphics.ReleaseHDC(dc);}
        const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
        check(image.Save(filename,&png)==Ok,"save annotation preview evidence");
        // The longest black vertical run inside the white preview is the Y-axis shaft,
        // independent of the preview's fitting implementation or model coordinates.
        RECT best{};int longest=0;
        for(int x=40;x<client.right-40;++x){int start=0,length=0;
            for(int y=100;y<tab.top-30;++y){Color pixel;image.GetPixel(x,y,&pixel);
                if(pixel.GetR()<45&&pixel.GetG()<45&&pixel.GetB()<45){
                    if(!length)start=y;++length;
                    if(length>longest){longest=length;best={x,start,x+1,y+1};}
                }else length=0;
            }
        }
        check(longest>20,"preview renders an identifiable Y-axis shaft");return best;
    };
    for(const auto [column,amount]:std::vector<std::pair<int,int>>{{8,100},{8,-100},{7,80}}){
        SendMessageW(manager,WM_COMMAND,139,0);
        const auto grid=FindWindowExW(manager,nullptr,L"YoungDrawEmbeddedGrid",nullptr);
        check(grid!=nullptr,"reaction annotation table opens");const auto list=GetDlgItem(grid,10);
        const auto evidence=L"reaction-annotation-"+std::to_wstring(column)+L"-"+std::to_wstring(amount);
        const auto initial=axisPixels((evidence+L"-before.png").c_str());
        for(int i=0;i<column;++i)SendMessageW(list,WM_KEYDOWN,VK_RIGHT,0);
        SendMessageW(list,WM_KEYDOWN,VK_F2,0);
        const auto edit=FindWindowExW(list,nullptr,L"EDIT",nullptr);
        check(edit!=nullptr,"annotation offset cell opens for editing");
        SetWindowTextW(edit,std::to_wstring(amount).c_str());SendMessageW(list,WM_KEYDOWN,VK_RETURN,0);
        auto expected=original;
        expected.plots.front().stages[0].offset=column==8?Vec{0,double(amount)}:Vec{double(amount),0};
        check(toNative(doc)==toNative(expected),"annotation movement changes only its offset, preserving axes and energy data");
        const auto movedScene=makeScene(doc,measure);const auto movedAxes=movedScene.plotAxes.at(owner);
        check((movedAxes.origin-originalAxes.origin).length()<1e-9&&
              (movedAxes.yEnd-originalAxes.yEnd).length()<1e-9,"annotation movement preserves document axis geometry");
        if(column==8&&amount>0){
            const auto caption=std::find_if(movedScene.items.begin(),movedScene.items.end(),[](const auto& p){return p.textRole==12&&p.textIndex==0;});
            check(caption!=movedScene.items.end()&&caption->points.front().y>movedAxes.origin.y,
                  "positive annotation offset places the reaction name below the horizontal axis");
        }
        const auto moved=axisPixels((evidence+L"-moved.png").c_str());
        std::cerr<<"Annotation offset column="<<column<<" value="<<amount<<"; Y-axis pixels "
                 <<initial.left<<','<<initial.top<<','<<initial.bottom<<" -> "<<moved.left<<','<<moved.top<<','<<moved.bottom<<'\n';
        check(std::abs(moved.left-initial.left)<=1&&std::abs(moved.top-initial.top)<=1&&
              std::abs(moved.bottom-initial.bottom)<=1,"moving a reaction annotation must keep the preview Y-axis fixed");
        if(column==8&&amount>0){
            RECT window{};GetWindowRect(manager,&window);
            const int width=window.right-window.left,height=window.bottom-window.top;
            // Shrink within the supported range: CI monitors can already be at
            // the maximum tracking size and reject requests to grow the window.
            SetWindowPos(manager,nullptr,0,0,width,height-60,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            const auto resized=axisPixels(L"reaction-annotation-resized.png");
            check(resized.bottom-resized.top<moved.bottom-moved.top-15,
                  "explicit window resize still fits the retained annotation preview frame");
            SetWindowPos(manager,nullptr,0,0,width,height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            const auto restored=axisPixels(L"reaction-annotation-size-restored.png");
            check(std::abs(restored.left-initial.left)<=1&&std::abs(restored.bottom-initial.bottom)<=1,
                  "restoring the window size restores the same fixed axis preview");
        }
        SendMessageW(manager,WM_COMMAND,104,0);
        check(toNative(doc)==before,"annotation offset edit is restored by one Undo");
    }
    closePlotManager();doc={};selection.clear();history.clear();dirty=false;
    std::cout<<"Reaction annotation offsets preserve preview axes, document geometry, data and Undo\n";
}
static void graphPreviewAccessChecks(HWND hwnd){
    doc=coordinateDemo();auto& graph=doc.plots.front();const int owner=graph.id;
    graph.title="Preview access";graph.legendVisibility=1;graph.showValues=true;
    auto alternate=graph.series.front();alternate.name="Alternate";alternate.color="#cc3344";
    for(auto& value:alternate.values)value+=40;
    graph.series.push_back(alternate);history.clear();selection={owner};
    const auto before=toNative(doc);managePlot(owner,false);const auto manager=plotManagerWindow;
    check(manager!=nullptr,"preview access fixture opens its graph manager");
    RECT anchored{};GetWindowRect(GetDlgItem(manager,4000),&anchored);
    auto position=[&](const Scene& scene,Vec point){
        RECT tab{},client{};GetWindowRect(GetDlgItem(manager,4000),&tab);
        POINT origin{tab.left,tab.top};ScreenToClient(manager,&origin);GetClientRect(manager,&client);
        const double width=client.right-40,height=origin.y-110;const auto& bounds=scene.bounds;
        const double scale=std::max(.01,std::min((width-32)/std::max(1.,bounds.w),(height-32)/std::max(1.,bounds.h)));
        return (point-Vec{bounds.x,bounds.y})*scale+Vec{20+(width-bounds.w*scale)/2,88+(height-bounds.h*scale)/2};
    };
    auto doubleClick=[&](const Scene& scene,Vec point){
        const auto at=position(scene,point);
        SendMessageW(manager,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(int(at.x),int(at.y)));
        RECT current{};GetWindowRect(GetDlgItem(manager,4000),&current);
        check(EqualRect(&anchored,&current),"preview access keeps category tabs and preview anchored");
    };
    auto textPosition=[&](const Scene& scene,int role,int index){
        auto item=std::find_if(scene.items.begin(),scene.items.end(),[&](const auto& item){
            return item.owner==owner&&item.type==Primitive::Text&&item.textRole==role&&
                (index<0||item.textIndex==index)&&!item.points.empty()&&!item.text.empty();
        });
        check(item!=scene.items.end(),"preview fixture renders the target caption");
        return item->points.front()+Vec{measure(item->text,item->fontSize,item->font,item->face)/2,item->fontSize*.6};
    };
    auto activeCell=[&](int row,int column){
        auto grid=FindWindowExW(manager,nullptr,L"YoungDrawEmbeddedGrid",nullptr);
        auto list=GetDlgItem(grid,10);auto edit=FindWindowExW(list,nullptr,L"EDIT",nullptr);
        check(grid&&list&&edit,"preview double-click opens an active data cell directly");
        RECT cell{},input{};ListView_GetSubItemRect(list,row,column,LVIR_BOUNDS,&cell);
        GetWindowRect(edit,&input);MapWindowPoints(nullptr,list,reinterpret_cast<POINT*>(&input),2);
        check(input.left>=cell.left&&input.left<cell.right&&input.top>=cell.top&&input.top<cell.bottom,
              "preview access focuses the requested state row and pathway column");
        check(toNative(doc)==before,"opening a preview data cell never mutates the model");
        return std::pair{list,edit};
    };
    // A state name targets the label column, even when accessed from another category.
    SendMessageW(manager,WM_COMMAND,4004,0);auto preview=makeScene(doc.subset({owner}),measure);
    doubleClick(preview,textPosition(preview,6,2));auto [nameList,nameEdit]=activeCell(2,1);
    SetWindowTextW(nameEdit,L"Preview intermediate");SendMessageW(nameList,WM_KEYDOWN,VK_RETURN,0);
    check(doc.plots.front().states[2].label=="Preview intermediate"&&doc.plots.front().states[1].label=="TS1"&&
          doc.plots.front().series[1].values==alternate.values,"preview name editing changes only the chosen state label");
    SendMessageW(manager,WM_COMMAND,104,0);check(toNative(doc)==before,"preview name edit undoes without stale model changes");

    // Only base-path value captions have role 8; test their numeric cell separately from path points.
    preview=makeScene(doc.subset({owner}),measure);doubleClick(preview,textPosition(preview,8,2));
    auto [valueList,valueEdit]=activeCell(2,2);SetWindowTextW(valueEdit,L"-43.25");SendMessageW(valueList,WM_KEYDOWN,VK_RETURN,0);
    check(doc.plots.front().series[0].values[2]==-43.25&&doc.plots.front().series[1].values==alternate.values,
          "base value double-click edits its own energy and preserves the alternate path");
    SendMessageW(manager,WM_COMMAND,104,0);check(toNative(doc)==before,"preview value edit undoes in one operation");

    // The second path has role-0 value labels; its actual state point carries the unambiguous path index.
    preview=makeScene(doc.subset({owner}),measure);const auto& points=preview.plotSnapPoints.at(owner);
    auto point=std::find_if(points.begin(),points.end(),[](const auto& point){return point.path==1&&point.state==2&&point.edge==0;});
    check(point!=points.end(),"alternate path exposes an exact state snap point");doubleClick(preview,point->p);
    auto [pathList,pathEdit]=activeCell(2,3);const auto baseValues=doc.plots.front().series[0].values;
    SetWindowTextW(pathEdit,L"64.5");SendMessageW(pathList,WM_KEYDOWN,VK_RETURN,0);
    check(doc.plots.front().series[1].values[2]==64.5&&doc.plots.front().series[0].values==baseValues&&
          doc.plots.front().series[1].values[1]==alternate.values[1],"alternate state point edits the exact path and state without touching the base path");
    SendMessageW(manager,WM_COMMAND,104,0);check(toNative(doc)==before,"alternate path edit restores the complete original model on undo");

    preview=makeScene(doc.subset({owner}),measure);doubleClick(preview,textPosition(preview,7,-1));
    auto legend=FindWindowExW(manager,nullptr,L"YoungDrawEmbeddedForm",nullptr);
    check(legend&&GetDlgItem(legend,104)&&toNative(doc)==before,"legend double-click opens the inline legend editor without committing data");
    SetDlgItemTextW(legend,104,L"25");SendMessageW(legend,WM_COMMAND,IDCANCEL,0);
    check(toNative(doc)==before,"cancelling the directly opened legend discards its preview");
    formBefore=before;formAction=20;formDriven=false;SetTimer(hwnd,901,10,driveForm);
    preview=makeScene(doc.subset({owner}),measure);doubleClick(preview,textPosition(preview,4,-1));KillTimer(hwnd,901);
    check(formDriven&&toNative(doc)==before,"title double-click opens the existing settings route and cancellation restores the model");
    closePlotManager();history.clear();
}
static int unifiedAxisMode=0,unifiedAxisWidth=0;
static bool unifiedAxisDriven=false;
static std::string unifiedAxisFailure;
static void chooseAxisNotationByMouse(HWND combo,int index){
    SendMessageW(combo,CB_SHOWDROPDOWN,TRUE,0);
    COMBOBOXINFO info{sizeof(info)};
    check(GetComboBoxInfo(combo,&info)!=FALSE&&IsWindowVisible(info.hwndList),"native energy notation dropdown is visible");
    RECT row{},bounds{};GetClientRect(info.hwndList,&bounds);
    check(SendMessageW(info.hwndList,LB_GETITEMRECT,index,reinterpret_cast<LPARAM>(&row))!=LB_ERR&&row.top>=0&&row.bottom<=bounds.bottom,"Delta energy choice is inside the native dropdown click area");
    const auto at=MAKELPARAM((row.left+row.right)/2,(row.top+row.bottom)/2);
    SendMessageW(info.hwndList,WM_LBUTTONDOWN,MK_LBUTTON,at);SendMessageW(info.hwndList,WM_LBUTTONUP,0,at);
    check(SendMessageW(combo,CB_GETCURSEL,0,0)==index,"mouse click selects the requested Delta energy item");
}
static std::string projectAxisFailure;
static int projectAxisNotation=1;
static void saveProjectAxisCanvas(const wchar_t* name){
    Bitmap image(ww,wh,PixelFormat32bppARGB);{Graphics graphics(&image);auto dc=graphics.GetHDC();paint(dc,true);graphics.ReleaseHDC(dc);}
    CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    check(image.Save(name,&png)==Ok,"save actual editor canvas evidence");
}
static void CALLBACK driveProjectAxis(HWND parent,UINT,UINT_PTR timer,DWORD){
    const auto dialog=FindWindowExW(plotManagerWindow,nullptr,L"YoungDrawForm",L"축 관리");
    if(!dialog)return;KillTimer(parent,timer);
    try{
        const auto tabs=FindWindowExW(dialog,nullptr,WC_TABCONTROLW,nullptr);RECT rect{};TabCtrl_GetItemRect(tabs,4,&rect);
        auto at=MAKELPARAM((rect.left+rect.right)/2,(rect.top+rect.bottom)/2);
        SendMessageW(tabs,WM_LBUTTONDOWN,MK_LBUTTON,at);SendMessageW(tabs,WM_LBUTTONUP,0,at);
        chooseAxisNotationByMouse(GetDlgItem(dialog,119),projectAxisNotation);
        const std::string symbol=projectAxisNotation==1?"ΔE":"ΔG";
        if(doc.plots.front().yLabel!=symbol){
            EnumChildWindows(dialog,[](HWND child,LPARAM)->BOOL{wchar_t type[32]{},value[1024]{};GetClassNameW(child,type,32);GetWindowTextW(child,value,1024);if(IsWindowVisible(child)&&!wcscmp(type,L"Static"))std::cerr<<"Axis label/error: "<<ui::utf8(value)<<'\n';return TRUE;},0);
        }
        check(doc.plots.front().yLabel==symbol,"project plot mouse selection replaces Energy with Delta on the canvas");
        saveProjectAxisCanvas(projectAxisNotation==1?L"project-canvas-delta-e.png":L"project-canvas-delta-g.png");
        PostMessageW(GetDlgItem(dialog,IDOK),BM_CLICK,0,0);
    }catch(const std::exception& error){projectAxisFailure=error.what();PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);}
}
static void projectAxisChecks(HWND hwnd,const std::wstring& path){
    doc=fromCdxml(readFile(path));check(!doc.plots.empty(),"project file imports an editable energy graph");
    std::cout<<"Project plots="<<doc.plots.size()<<" label="<<doc.plots.front().yLabel<<" symbol="<<doc.plots.front().energySymbol<<'\n';
    const auto original=doc;selection={doc.plots.front().id};history.clear();changed();fit();dirty=false;
    saveProjectAxisCanvas(L"project-canvas-before.png");
    managePlot(selection.front(),true);SendMessageW(plotManagerWindow,WM_COMMAND,4001,0);
    for(int notation:{1,2}){
        doc=original;history.clear();changed();projectAxisNotation=notation;projectAxisFailure.clear();
        SetTimer(hwnd,909,10,driveProjectAxis);SendMessageW(plotManagerWindow,WM_COMMAND,4406,0);KillTimer(hwnd,909);
        check(projectAxisFailure.empty(),projectAxisFailure.c_str());
        const auto& scene=sceneForPaint(true);const std::string symbol=notation==1?"ΔE":"ΔG";
        check(std::any_of(scene.items.begin(),scene.items.end(),[&](const auto& item){return item.type==Primitive::Text&&item.owner==doc.plots.front().id&&item.rotation==-90&&item.text.find(symbol)!=std::string::npos;}),"project canvas cached vertical title contains the selected Delta");
        check(history.undo(doc)&&toNative(doc)==toNative(original)&&!history.undo(doc),"project Delta edit is exactly one undo step");
    }
    closePlotManager();std::cout<<"PASS: actual project Delta mouse selection and cached canvas\n";
}
static int axisQuickCase=0;
static std::string axisQuickFailure;
static bool axisQuickDriven=false;
static void captureAxesManager(const std::wstring& name);
static void CALLBACK driveAxisQuick(HWND parent,UINT,UINT_PTR timer,DWORD){
    auto dialog=FindWindowExW(plotManagerWindow,nullptr,L"YoungDrawForm",L"축 관리");
    if(!dialog)return;KillTimer(parent,timer);axisQuickDriven=true;
    try{
        auto tabs=FindWindowExW(dialog,nullptr,WC_TABCONTROLW,nullptr);
        check(TabCtrl_GetCurSel(tabs)==(axisQuickCase==3?1:4),"axis title access opens its matching detail directly and remembers the last detail");
        if(axisQuickCase==1){
            for(int choice:{1,2}){
                auto button=GetDlgItem(dialog,20000+19*32+choice-1);check(button&&IsWindowVisible(button),"one-click Delta buttons are visible beside energy notation");
                RECT rect{};GetClientRect(button,&rect);const auto at=MAKELPARAM(rect.right/2,rect.bottom/2);
                SendMessageW(button,WM_LBUTTONDOWN,MK_LBUTTON,at);SendMessageW(button,WM_LBUTTONUP,0,at);
                check(doc.plots.front().yLabel==(choice==1?"ΔE":"ΔG")&&SendDlgItemMessageW(dialog,119,CB_GETCURSEL,0,0)==choice,"one Delta button click updates the axis and selected notation");
            }
            SetDlgItemTextW(dialog,114,L"Relative energy");
            check(doc.plots.front().yLabel=="Relative energy"&&SendDlgItemMessageW(dialog,119,CB_GETCURSEL,0,0)==0,"typing a custom title leaves Delta preset mode and preserves the typed title");
            SendDlgItemMessageW(dialog,20000+19*32+1,BM_CLICK,0,0);
        }
        if(axisQuickCase==2){check(SendDlgItemMessageW(dialog,119,CB_GETCURSEL,0,0)==2,"reopening shows the actual stored Delta notation instead of a keep-current placeholder");captureAxesManager(L"axis-quick-title.png");}
        PostMessageW(GetDlgItem(dialog,axisQuickCase==1?IDOK:IDCANCEL),BM_CLICK,0,0);
    }catch(const std::exception& error){axisQuickFailure=error.what();PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);}
}
static Vec axisTitlePoint(const Scene& scene,bool xAxis){
    for(const auto& item:scene.items)if(item.type==Primitive::Text&&item.textRole==(xAxis?plotXAxisTitleTextRole:plotYAxisTitleTextRole)&&!item.text.empty()){
        const Vec local{measure(item.text,item.fontSize,item.font,item.face)/2,item.fontSize*.5};const double angle=item.rotation*pi/180;
        return item.points.front()+Vec{local.x*std::cos(angle)-local.y*std::sin(angle),local.x*std::sin(angle)+local.y*std::cos(angle)};
    }
    throw std::runtime_error("missing rendered axis title");
}
static void axisQuickChecks(HWND hwnd){
    doc=coordinateDemo();selection.clear();history.clear();tool=Select;changed();fit();dirty=false;
    const auto original=doc;const auto before=toNative(doc);const auto owner=doc.plots.front().id;
    auto drive=[&](int mode,const std::function<void()>& open){
        axisQuickCase=mode;axisQuickFailure.clear();axisQuickDriven=false;SetTimer(hwnd,910,10,driveAxisQuick);open();KillTimer(hwnd,910);
        check(axisQuickDriven,"axis quick access opens the requested native form");
        check(axisQuickFailure.empty(),axisQuickFailure.c_str());
    };
    const auto y=screen(axisTitlePoint(scene(),false));
    drive(0,[&]{SendMessageW(hwnd,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(int(y.x),int(y.y)));});
    check(toNative(doc)==before&&!history.undo(doc),"direct Y title access and cancellation do not modify the document");
    drive(1,[&]{SendMessageW(plotManagerWindow,WM_COMMAND,4406,0);});
    auto expected=original;expected.plots.front().yLabel="ΔG";expected.plots.front().energySymbol="ΔG";
    check(toNative(doc)==toNative(expected),"one-click energy notation preserves every other project property and data value");
    drive(2,[&]{SendMessageW(plotManagerWindow,WM_COMMAND,4406,0);});
    check(history.undo(doc)&&toNative(doc)==before&&!history.undo(doc),"quick notation apply is exactly one reversible edit");changed();
    auto sc=makeScene(doc.subset({owner}),measure);RECT client{};GetClientRect(plotManagerWindow,&client);
    const double panelTop=std::max(180L,client.bottom-460),height=panelTop-110,width=client.right-40;
    const double scale=std::max(.01,std::min((width-32)/sc.bounds.w,(height-32)/sc.bounds.h));
    const Vec offset{20+(width-sc.bounds.w*scale)/2-sc.bounds.x*scale,88+(height-sc.bounds.h*scale)/2-sc.bounds.y*scale};
    const auto x=axisTitlePoint(sc,true)*scale+offset;
    drive(3,[&]{SendMessageW(plotManagerWindow,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(int(x.x),int(x.y)));});
    check(toNative(doc)==before,"preview X title access preserves data on cancel");closePlotManager();
    doc.plots.front().energyAtTop=true;changed();fit();const auto upper=screen(axisTitlePoint(scene(),false));
    drive(4,[&]{SendMessageW(hwnd,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(int(upper.x),int(upper.y)));});closePlotManager();
    doc.graphsLocked=true;check(!plotAxisTitleAt(scene(),world(upper),4/zoom),"locked graph titles retain their edit protection");
    doc=original;selection.clear();history.clear();changed();
}
static void captureAxesManager(const std::wstring& name){
    RECT bounds{};GetClientRect(plotManagerWindow,&bounds);
    Bitmap image(bounds.right,bounds.bottom,PixelFormat32bppARGB);
    {Graphics graphics(&image);auto dc=graphics.GetHDC();
     SendMessageW(plotManagerWindow,WM_PRINTCLIENT,(WPARAM)dc,PRF_CLIENT);
     struct Context{HWND manager;HDC dc;};Context context{plotManagerWindow,dc};
     EnumChildWindows(plotManagerWindow,[](HWND child,LPARAM data)->BOOL{
        auto& context=*reinterpret_cast<Context*>(data);
        for(auto ancestor=child;ancestor&&ancestor!=context.manager;ancestor=GetParent(ancestor))
            if(!(GetWindowLongPtrW(ancestor,GWL_STYLE)&WS_VISIBLE))return TRUE;
        RECT rect{};GetWindowRect(child,&rect);MapWindowPoints(HWND_DESKTOP,context.manager,reinterpret_cast<POINT*>(&rect),2);
        const int saved=SaveDC(context.dc);SetViewportOrgEx(context.dc,rect.left,rect.top,nullptr);
        wchar_t type[32]{};GetClassNameW(child,type,32);
        if(!_wcsicmp(type,L"Button")){
            DRAWITEMSTRUCT item{};item.CtlType=ODT_BUTTON;item.CtlID=GetDlgCtrlID(child);item.hwndItem=child;
            item.hDC=context.dc;item.rcItem={0,0,rect.right-rect.left,rect.bottom-rect.top};
            SendMessageW(GetParent(child),WM_DRAWITEM,item.CtlID,(LPARAM)&item);
        }else if(!_wcsicmp(type,L"Edit"))SendMessageW(child,WM_PRINT,(WPARAM)context.dc,PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND);
        else SendMessageW(child,WM_PRINTCLIENT,(WPARAM)context.dc,PRF_CLIENT);
        RestoreDC(context.dc,saved);return TRUE;
     },(LPARAM)&context);graphics.ReleaseHDC(dc);}
    CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    check(image.Save(name.c_str(),&png)==Ok,"save unified axis manager evidence");
}
static void CALLBACK driveUnifiedAxes(HWND parent,UINT,UINT_PTR timer,DWORD){
    const auto dialog=FindWindowExW(plotManagerWindow,nullptr,L"YoungDrawForm",L"축 관리");
    if(!dialog)return;KillTimer(parent,timer);unifiedAxisDriven=true;
    try{
    const auto tabs=FindWindowExW(dialog,nullptr,WC_TABCONTROLW,nullptr);
    check(tabs&&TabCtrl_GetItemCount(tabs)==7,"one axis editor contains all seven X and Y detail tabs");
    const int icons[]={242,213,240,243,246,131,241};
    const int firstField[]={109,104,107,128,114,122,120};
    auto choose=[&](int index){
        RECT rect{},client{};TabCtrl_GetItemRect(tabs,index,&rect);GetClientRect(tabs,&client);
        check(rect.left>=0&&rect.right<=client.right&&rect.bottom<=client.bottom,"every axis detail icon fits without horizontal scrolling");
        TCITEMW item{};item.mask=TCIF_PARAM;TabCtrl_GetItem(tabs,index,&item);
        check(item.lParam==icons[index],"axis detail tabs retain their assigned function icons");
        const auto at=MAKELPARAM((rect.left+rect.right)/2,(rect.top+rect.bottom)/2);
        SendMessageW(tabs,WM_LBUTTONDOWN,MK_LBUTTON,at);SendMessageW(tabs,WM_LBUTTONUP,0,at);
        check(TabCtrl_GetCurSel(tabs)==index&&IsWindowVisible(GetDlgItem(dialog,firstField[index])),"one icon click directly displays its axis settings");
        EnumChildWindows(dialog,[](HWND child,LPARAM form)->BOOL{
            if(GetParent(child)!=reinterpret_cast<HWND>(form)||!IsWindowVisible(child))return TRUE;
            RECT rect{},bounds{};GetWindowRect(child,&rect);GetClientRect(reinterpret_cast<HWND>(form),&bounds);
            MapWindowPoints(nullptr,reinterpret_cast<HWND>(form),reinterpret_cast<POINT*>(&rect),2);
            check(rect.left>=0&&rect.top>=0&&rect.right<=bounds.right&&rect.bottom<=bounds.bottom,"visible axis form controls fit their parent at the tested size");return TRUE;
        },reinterpret_cast<LPARAM>(dialog));
    };
    choose(0);SetDlgItemTextW(dialog,100,L"3.5");SetDlgItemTextW(dialog,101,L"12");
    choose(3);SetDlgItemTextW(dialog,110,L"4.5");SetDlgItemTextW(dialog,111,L"18");
    check(doc.plots.front().xAxisWidth==3.5&&doc.plots.front().yAxisWidth==4.5,"both axes preview together without applying or closing between them");
    choose(4);SetDlgItemTextW(dialog,117,L"44");
    choose(5);SetDlgItemTextW(dialog,122,L"-50");SetDlgItemTextW(dialog,123,L"70");SetDlgItemTextW(dialog,124,L"20");
    choose(0);wchar_t retained[32]{};GetDlgItemTextW(dialog,100,retained,32);
    check(std::wstring(retained)==L"3.5","switching X and Y icons retains pending input");
    choose(4);
    for(int top:{0,1})for(int notation:{1,2}){
        SendDlgItemMessageW(dialog,118,CB_SETCURSEL,top,0);
        SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(118,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(dialog,118)));
        chooseAxisNotationByMouse(GetDlgItem(dialog,119),notation);
        const std::string symbol=notation==1?"ΔE":"ΔG";
        check(doc.plots.front().energySymbol==symbol&&doc.plots.front().yLabel==symbol,"energy notation choice updates both stored axis titles");
        wchar_t title[32]{};GetDlgItemTextW(dialog,114,title,32);
        check(ui::utf8(title)==symbol,"energy notation choice immediately updates the visible axis title field");
        const auto scene=makeScene(doc,measure);
        check(std::any_of(scene.items.begin(),scene.items.end(),[&](const auto& item){return item.type==Primitive::Text&&item.owner==doc.plots.front().id&&item.text.find(symbol)!=std::string::npos&&item.rotation==(top?0:-90);}),"Delta E and Delta G appear on the actual top or vertical axis");
        check(toSvg(scene).find(symbol)!=std::string::npos,"selected energy notation reaches SVG export");
        const auto& canvas=sceneForPaint(true);
        check(std::any_of(canvas.items.begin(),canvas.items.end(),[&](const auto& item){return item.type==Primitive::Text&&item.owner==doc.plots.front().id&&item.text.find(symbol)!=std::string::npos&&item.rotation==(top?0:-90);}),"actual cached canvas scene reflects mouse-selected Delta energy");
    }
    for(int tab=0;tab<7;++tab){choose(tab);if(unifiedAxisMode==1)captureAxesManager(L"unified-axes-"+std::to_wstring(unifiedAxisWidth)+L"-"+std::to_wstring(tab)+L".png");}
    if(unifiedAxisMode==2){
        choose(4);SetDlgItemTextW(dialog,116,L"2");
        check(!IsWindowEnabled(GetDlgItem(dialog,IDOK)),"invalid Y font prevents committing both axis edits");
        choose(0);check(doc.plots.front().xAxisWidth==3.5,"invalid Y entry does not discard the valid combined preview");
    }
    PostMessageW(GetDlgItem(dialog,unifiedAxisMode==1?IDOK:IDCANCEL),BM_CLICK,0,0);
    }catch(const std::exception& error){unifiedAxisFailure=error.what();PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);}
}
static void unifiedAxisChecks(HWND hwnd){
    doc=coordinateDemo();selection={doc.plots.front().id};history.clear();changed();dirty=false;
    const auto original=toNative(doc);
    managePlot(selection.front(),true);const auto manager=plotManagerWindow;
    for(int width:{900,1280}){
        SetWindowPos(manager,nullptr,0,0,width,width==900?640:900,SWP_NOZORDER|SWP_NOACTIVATE);
        SendMessageW(manager,WM_COMMAND,4001,0);unifiedAxisWidth=width;
        captureAxesManager(L"unified-axes-"+std::to_wstring(width)+L"-entry.png");
        for(int mode:{0,1,2}){
            history.clear();dirty=false;unifiedAxisMode=mode;unifiedAxisDriven=false;unifiedAxisFailure.clear();
            SetTimer(hwnd,908,10,driveUnifiedAxes);SendMessageW(manager,WM_COMMAND,4406,0);KillTimer(hwnd,908);
            check(unifiedAxisDriven,"unified axis card opens the single embedded editor");
            check(unifiedAxisFailure.empty(),unifiedAxisFailure.c_str());
            if(mode==1){
                const auto& graph=doc.plots.front();
                check(graph.xAxisWidth==3.5&&graph.yAxisWidth==4.5&&graph.xAxisHeadLength==12&&graph.yAxisHeadLength==18&&graph.yTitleGap==44&&graph.yMin==-50&&graph.yMax==70&&graph.yTickStep==20,"one apply commits independent X and Y settings, range and ticks");
                check(history.undo(doc)&&toNative(doc)==original&&!history.undo(doc),"combined X and Y edit is exactly one undo step");
                check(history.redo(doc)&&doc.plots.front().yAxisWidth==4.5,"combined axis edit supports redo");history.undo(doc);
            }else check(toNative(doc)==original&&!dirty&&!history.undo(doc),"cancel restores both axes and all data without an undo entry");
        }
    }
    SendMessageW(manager,WM_COMMAND,IDCANCEL,0);check(!plotManagerWindow,"unified axis manager closes normally");
}
static void graphManagerChecks(HWND hwnd){
    graphPreviewAccessChecks(hwnd);
    doc=coordinateDemo();history.clear();selection={doc.plots[0].id};dirty=false;const int owner=selection[0];
    const auto before=toNative(doc);managePlot(owner,false);auto manager=plotManagerWindow;check(manager&&IsWindow(manager)&&toNative(doc)==before,"graph manager opens without modifying the selected graph");
    SendMessageW(manager,WM_COMMAND,4002,0);
    for(bool remove:{false,true}){
        removePhoto=remove;photoFormSeen=false;
        SetTimer(hwnd,901,10,drivePhotoForm);SendMessageW(manager,WM_COMMAND,4405,0);KillTimer(hwnd,901);
        check(photoFormSeen&&doc.plots[0].states[0].photoPath==(remove?-1:0),
              "one photo dialog shows the energy range and adds or removes the arrow");
    }
    check(toNative(doc)==before,"removing photoexcitation restores the original graph");
    history.clear();
    SendMessageW(manager,WM_COMMAND,4000,0);
    auto originalButton=GetDlgItem(manager,131);
    GetWindowRect(GetDlgItem(manager,4000),&graphManagerTabBounds);
    RECT frame{};GetWindowRect(manager,&frame);
    wchar_t doneLabel[32]{};GetDlgItemTextW(manager,4400,doneLabel,32);
    check(std::wstring(doneLabel)==L"확인","data panel confirmation is labeled clearly");
    SendMessageW(manager,WM_COMMAND,131,0);auto inlineGrid=FindWindowExW(manager,nullptr,L"YoungDrawEmbeddedGrid",nullptr);
    check(inlineGrid&&GetParent(inlineGrid)==manager&&toNative(doc)==before,"data opens as a nonmutating child panel");
    check(graphManagerTabsAnchored(manager),"opening the energy grid leaves the tabs and preview anchored");
    auto list=GetDlgItem(inlineGrid,10);RECT cell{};ListView_GetSubItemRect(list,0,2,LVIR_BOUNDS,&cell);
    SendMessageW(list,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(cell.left+6,cell.top+6));auto input=FindWindowExW(list,nullptr,L"EDIT",nullptr);check(input!=nullptr,"inline cells use the shared keyboard editor");
    SetWindowTextW(input,L"invalid");SendMessageW(manager,WM_COMMAND,4001,0);
    check(toNative(doc)==before&&(GetWindowLongPtrW(inlineGrid,GWL_STYLE)&WS_VISIBLE),"invalid data cannot modify the document or disappear on tab switch");
    SendMessageW(list,WM_KEYDOWN,VK_F2,0);input=FindWindowExW(list,nullptr,L"EDIT",nullptr);SetWindowTextW(input,L"-12.5");SendMessageW(list,WM_KEYDOWN,VK_RETURN,0);
    check(doc.plots[0].series[0].values[0]==-12.5,"inline energy edits update the graph without a separate window");
    RECT nameRect{};
    ListView_GetSubItemRect(list,1,1,LVIR_BOUNDS,&nameRect);
    SendMessageW(list,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(nameRect.left+8,nameRect.top+8));
    auto nameCell=FindWindowExW(list,nullptr,L"EDIT",nullptr);
    check(nameCell!=nullptr,"inline formatted data editor remains available");
    SetWindowTextW(nameCell,L"~{R} label");
    SendMessageW(list,WM_KEYDOWN,VK_RETURN,0);
    ListView_EnsureVisible(list,0,FALSE);
    ListView_GetSubItemRect(list,0,2,LVIR_BOUNDS,&cell);
    SendMessageW(list,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(cell.left+8,cell.top+8));
    auto energyCell=FindWindowExW(list,nullptr,L"EDIT",nullptr);
    check(energyCell!=nullptr,"reopening numeric cells preserves editing");
    SetWindowTextW(energyCell,L"-~{12}.5");
    SendMessageW(list,WM_KEYDOWN,VK_RETURN,0);
    check(std::any_of(doc.plots[0].series[0].valueLabels.begin(),doc.plots[0].series[0].valueLabels.end(),
        [](const auto& text){return text=="-~{12}.5";}),"partial numeric styling is stored separately from the energy");
    check(toNative(fromCdxml(toCdxml(doc)))==toNative(doc),"formatted graph data round trips through CDXML");
    SendMessageW(manager,WM_COMMAND,104,0);
    SendMessageW(manager,WM_COMMAND,104,0);
    SendMessageW(manager,WM_COMMAND,104,0);check(toNative(doc)==before,"inline data change undoes in one action");
    SendMessageW(manager,WM_COMMAND,131,0);
    inlineGrid=FindWindowExW(manager,nullptr,L"YoungDrawEmbeddedGrid",nullptr);
    list=GetDlgItem(inlineGrid,10);
    check(inlineGrid&&list,"data table can be reopened after graph undo");
    const auto tableFont=[&](){
        LOGFONTW font{};
        auto handle=reinterpret_cast<HFONT>(SendMessageW(list,WM_GETFONT,0,0));
        return GetObjectW(handle,sizeof(font),&font)?std::abs(font.lfHeight):0L;
    };
    const auto initialTableFont=tableFont();
    formBefore=before;formAction=42;formDriven=previewObserved=false;
    SetTimer(hwnd,901,10,driveForm);SendMessageW(manager,WM_COMMAND,4404,0);KillTimer(hwnd,901);
    check(formDriven&&previewObserved&&toNative(doc)==before&&tableFont()==initialTableFont,
        "cancelling data table font edits keeps document and table unchanged");
    formAction=41;formDriven=previewObserved=false;
    SetTimer(hwnd,901,10,driveForm);SendMessageW(manager,WM_COMMAND,4404,0);KillTimer(hwnd,901);
    check(formDriven&&previewObserved&&tableFont()>initialTableFont&&
          doc.plots.front().stateFontSize==17&&doc.plots.front().valueFontSize==21,
          "data table sets its screen font and the two graph font sizes independently");
    check(history.undo(doc)&&toNative(doc)==before,"graph font settings apply with one undo step");
    for(int page=0;page<5;page++){SendMessageW(manager,WM_COMMAND,4000+page,0);const int expected[]={131,plotFontCommand,139,4300,plotWaveWidthCommand};check(GetDlgItem(manager,expected[page])!=nullptr,"each manager category exposes its icon tools");ui::attachWindowHelp(manager);EnumChildWindows(manager,[](HWND button,LPARAM)->BOOL{wchar_t type[32]{};GetClassNameW(button,type,32);if(!_wcsicmp(type,L"Button"))check(ui::hasButtonHelp(button),"every graph manager button has tooltip help");return TRUE;},0);}
    check(GetDlgItem(manager,131)==originalButton,"tab changes reuse controls instead of recreating them");
    SendMessageW(manager,WM_COMMAND,4000,0);
    SendMessageW(manager,WM_COMMAND,4402,0);
    auto branchForm=FindWindowExW(manager,nullptr,L"YoungDrawEmbeddedForm",nullptr);
    check(branchForm&&toNative(doc)==before&&!GetDlgItem(branchForm,103)&&!GetDlgItem(branchForm,104),
          "branch creation asks for a source and state, without arbitrary direction or energy offsets");
    captureAxesManager(L"graph-branch-creation.png");
    SendDlgItemMessageW(branchForm,101,CB_SETCURSEL,1,0);
    SendMessageW(branchForm,WM_COMMAND,IDOK,0);
    check(doc.plots.front().series.back().branchState==1&&!doc.plots.front().series.back().hasValue(2),
          "the branch starts at the chosen intermediate and leaves future energies blank");
    inlineGrid=FindWindowExW(manager,nullptr,L"YoungDrawEmbeddedGrid",nullptr);list=GetDlgItem(inlineGrid,10);
    check(inlineGrid&&(GetWindowLongPtrW(inlineGrid,GWL_STYLE)&WS_VISIBLE),"applying a branch opens its editable energy table immediately");
    auto enter=[&](int row,int column,const wchar_t* value){
        ui::selectEmbeddedTableCell(inlineGrid,row,column);SendMessageW(list,WM_KEYDOWN,VK_F2,0);
        auto input=FindWindowExW(list,nullptr,L"EDIT",nullptr);check(input!=nullptr,"branch data cell opens its real editor");
        SetWindowTextW(input,value);SendMessageW(list,WM_KEYDOWN,VK_RETURN,0);
    };
    const int branchColumn=int(doc.plots.front().series.size());
    enter(2,branchColumn,L"-19.5");
    enter(1,branchColumn,L"12.25");
    check(doc.plots.front().series.back().values[2]==-19.5&&doc.plots.front().series[0].values[1]==12.25&&
          doc.plots.front().series.back().values[1]==12.25,"branch energies and the shared point are real editable table data");
    wchar_t sharedCell[64]{};ListView_GetItemText(list,1,2,sharedCell,64);
    check(std::wstring(sharedCell)==L"12.25","editing a shared point refreshes the matching source cell in the table");
    enter(1,1,L"11.5");
    check(doc.plots.front().series.back().values[1]==11.5,"editing the source cell also updates the shared branch point");
    enter(2,branchColumn,L"8.75");check(doc.plots.front().series.back().values[2]==8.75,"branch energy can be changed again after initial entry");
    writePreview(L"graph-vertical-branch.png",doc);
    captureAxesManager(L"graph-branch-data.png");
    SendMessageW(manager,WM_COMMAND,4400,0);SendMessageW(manager,WM_COMMAND,131,0);
    inlineGrid=FindWindowExW(manager,nullptr,L"YoungDrawEmbeddedGrid",nullptr);list=GetDlgItem(inlineGrid,10);
    enter(2,branchColumn,L"-4.5");check(doc.plots.front().series.back().values[2]==-4.5,"reopening the data table keeps every branch energy editable");
    for(int i=0;i<6;++i)SendMessageW(manager,WM_COMMAND,104,0);
    check(toNative(doc)==before,"branch creation and later data edits undo independently");
    auto styledGraph=doc;
    graphStyleMode=(doc.plots.front().mode+1)%5;
    styledGraph.plots.front().mode=graphStyleMode;
    for(int result:{IDCANCEL,IDOK}){
        graphStyleResult=result;graphStyleCardsSeen=false;graphStylePreviewSeen=false;
        SetTimer(hwnd,901,10,driveGraphStyle);
        SendMessageW(manager,WM_COMMAND,changePlotStyleCommand,0);KillTimer(hwnd,901);
        check(graphStyleCardsSeen&&graphStylePreviewSeen,"rounded graph cards show help and preview in the anchored manager");
        check(toNative(doc)==(result==IDOK?toNative(styledGraph):before),"graph style applies or cancels without affecting other data");
    }
    check(history.undo(doc)&&toNative(doc)==before,"graph style change undoes in one operation");
    SendMessageW(manager,WM_COMMAND,4001,0);
    formAction=20;formDriven=false;SetTimer(hwnd,901,10,driveForm);
    SendMessageW(manager,WM_COMMAND,137,0);KillTimer(hwnd,901);
    check(formDriven&&toNative(doc)==before,"axis settings open and cancel inside the manager");
    RECT legendCard{},axesCard{},valueCard{};
    GetWindowRect(GetDlgItem(manager,plotLegendCommand),&legendCard);
    GetWindowRect(GetDlgItem(manager,4406),&axesCard);
    GetWindowRect(GetDlgItem(manager,241),&valueCard);
    check(axesCard.top==valueCard.top&&valueCard.left<axesCard.left&&axesCard.left<legendCard.left&&legendCard.top==axesCard.top,"unified axes occupy the former X slot while data values and legend keep their slots");
    SendMessageW(manager,WM_COMMAND,plotLegendCommand,0);
    auto form=FindWindowExW(manager,nullptr,L"YoungDrawEmbeddedForm",nullptr);
    check(form&&GetParent(form)==manager,"legend opens in the graph manager");
    RECT currentFrame{};GetWindowRect(manager,&currentFrame);
    check(EqualRect(&frame,&currentFrame)&&graphManagerTabsAnchored(manager),"switching tabs and opening editors preserves the manager frame and preview position");
    SendDlgItemMessageW(form,100,CB_SETCURSEL,1,0);
    SetDlgItemTextW(form,101,L"Times New Roman");
    SendDlgItemMessageW(form,102,CB_SETCURSEL,3,0);
    SetDlgItemTextW(form,103,L"28");
    SetDlgItemTextW(form,104,L"15");
    SetDlgItemTextW(form,105,L"-12");
    check(toNative(doc)==before,"legend preview leaves the document untouched before Apply");
    SendMessageW(form,WM_COMMAND,IDOK,0);
    const auto& graph=doc.plots.front();
    check(graph.legendVisibility==1&&graph.legendFace==2&&graph.legendFontFamily=="Times New Roman"&&graph.legendFontSize==28&&graph.legendOffset.x==15&&graph.legendOffset.y==-12,
        "legend applies all settings together");
    const auto styled=toNative(doc);
    for(const auto& restored:{fromNative(styled),fromCdxml(toCdxml(doc))})
        check(toNative(restored)==styled,"legend settings survive both file formats");
    auto preview=makeScene(doc,measure);
    check(std::any_of(preview.items.begin(),preview.items.end(),[](const auto& item){
        return item.type==Primitive::Text&&item.textRole==7&&item.face==2&&item.font=="Times New Roman"&&item.fontSize==28;
    }),"legend renders its own italic and size");
    SendMessageW(manager,WM_COMMAND,plotLegendCommand,0);
    form=FindWindowExW(manager,nullptr,L"YoungDrawEmbeddedForm",nullptr);
    preview=makeScene(doc.subset({owner}),measure);
    auto item=std::find_if(preview.items.begin(),preview.items.end(),[](const auto& p){return p.textRole==7&&p.type==Primitive::Text;});
    check(item!=preview.items.end(),"the preview contains a draggable legend");
    RECT topTab{},client{};
    GetWindowRect(GetDlgItem(manager,4001),&topTab);
    POINT tabOrigin{topTab.left,topTab.top};
    ScreenToClient(manager,&tabOrigin);
    GetClientRect(manager,&client);
    const double w=client.right-40,h=tabOrigin.y-110;
    const auto& b=preview.bounds;
    const double scale=std::min((w-32)/std::max(1.,b.w),(h-32)/std::max(1.,b.h));
    const auto position=(item->points.front()-Vec{b.x,b.y}+Vec{4,4})*scale+
        Vec{20+(w-b.w*scale)/2,88+(h-b.h*scale)/2};
    SendMessageW(manager,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(int(position.x),int(position.y)));
    SendMessageW(manager,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(int(position.x)+18,int(position.y)+10));
    SendMessageW(manager,WM_LBUTTONUP,0,MAKELPARAM(int(position.x)+18,int(position.y)+10));
    wchar_t offset[32]{};
    GetDlgItemTextW(form,104,offset,32);
    check(numeric(ui::utf8(offset))>15&&toNative(doc)==styled,"drag adjusts the legend offset in preview only");
    SendMessageW(form,WM_COMMAND,IDCANCEL,0);
    check(toNative(doc)==styled,"canceling a drag retains the applied position");
    SendMessageW(manager,WM_COMMAND,plotLegendCommand,0);
    form=FindWindowExW(manager,nullptr,L"YoungDrawEmbeddedForm",nullptr);
    SendDlgItemMessageW(form,100,CB_SETCURSEL,2,0);
    SendMessageW(form,WM_COMMAND,IDOK,0);
    preview=makeScene(doc,measure);
    check(std::none_of(preview.items.begin(),preview.items.end(),[](const auto& item){return item.textRole==7;}),"hide removes the legend from the rendered graph");
    SendMessageW(manager,WM_COMMAND,104,0);
    SendMessageW(manager,WM_COMMAND,104,0);
    check(toNative(doc)==before,"legend changes each undo in one operation");

    SendMessageW(manager,WM_COMMAND,4002,0);
    SendMessageW(manager,WM_COMMAND,139,0);
    auto stages=FindWindowExW(manager,nullptr,L"YoungDrawEmbeddedGrid",nullptr);
    check(stages&&toNative(doc)==before,"reaction name and arrows open in the manager without changing data");
    const auto labels=GetDlgItem(stages,10);
    SendMessageW(labels,WM_KEYDOWN,VK_F2,0);
    auto label=FindWindowExW(labels,nullptr,L"EDIT",nullptr);
    check(label!=nullptr,"the reaction grid has an inline name editor");
    SetWindowTextW(label,L"Reactant updated");
    SendMessageW(labels,WM_KEYDOWN,VK_RETURN,0);
    check(!doc.plots.front().stages.empty()&&doc.plots.front().stages[0].label=="Reactant updated","reaction labels apply immediately on cell commit");
    SendMessageW(manager,WM_COMMAND,104,0);
    check(toNative(doc)==before,"reaction label edits undo through the manager");

    SendMessageW(manager,WM_COMMAND,4000,0);
    SendMessageW(manager,WM_COMMAND,132,0);
    form=FindWindowExW(manager,nullptr,L"YoungDrawEmbeddedForm",nullptr);
    check(form&&toNative(doc)==before,"adding a pathway opens in the manager without committing the preview");
    SetDlgItemTextW(form,100,L"Alternate pathway");
    SetDlgItemTextW(form,105,L"4");
    SendMessageW(form,WM_COMMAND,1005,0);
    SendMessageW(GetDlgItem(form,105),WM_CHAR,L'-',0);
    SendMessageW(form,WM_COMMAND,1005,0);
    SendMessageW(form,WM_COMMAND,IDOK,0);
    check(doc.plots.front().series.size()==2&&doc.plots.front().series.back().name=="Alternate pathway"&&
        doc.plots.front().series.back().markerSize==4.5,"path creation and marker plus/minus commit together");
    SendMessageW(manager,WM_COMMAND,133,0);
    check(GetWindowLongPtrW(GetDlgItem(manager,4401),GWL_STYLE)&WS_VISIBLE,"path style panel exposes an inline path selector");
    SendDlgItemMessageW(manager,4401,CB_SETCURSEL,1,0);
    SendMessageW(manager,WM_COMMAND,MAKEWPARAM(4401,CBN_SELCHANGE),0);
    form=FindWindowExW(manager,nullptr,L"YoungDrawEmbeddedForm",nullptr);
    SendMessageW(form,WM_COMMAND,905,0);
    SendMessageW(form,WM_COMMAND,IDOK,0);
    check(doc.plots.front().series.back().markerSize==4,"minus changes the selected path marker radius");
    SendMessageW(manager,WM_COMMAND,104,0);
    SendMessageW(manager,WM_COMMAND,104,0);
    check(toNative(doc)==before,"path creation and styling are separately undoable");


    SendMessageW(manager,WM_COMMAND,4001,0);formBefore=before;formAction=36;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);SendMessageW(manager,WM_COMMAND,plotFontCommand,0);
    check(formDriven&&previewObserved&&toNative(doc)==before,"manager font cancellation restores the previewed graph");
    formAction=35;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);SendMessageW(manager,WM_COMMAND,plotFontCommand,0);
    check(formDriven&&previewObserved&&doc.plots[0].fontFamily=="Times New Roman"&&doc.plots[0].fontFace==2,"manager applies independent graph font family and italic style");
    bool lettering=false;for(const auto& item:makeScene(doc,measure).items)if(item.owner==owner&&item.type==Primitive::Text){lettering=true;check(item.font=="Times New Roman"&&(item.face&2),"axis, labels, legend and annotations use the selected graph font");}check(lettering,"graph font renders visible text");
    for(const auto& restored:{fromNative(toNative(doc)),fromCdxml(toCdxml(doc))})check(toNative(restored)==toNative(doc),"graph font persists through native and CDXML saving");
    SendMessageW(manager,WM_COMMAND,104,0);check(toNative(doc)==before,"manager edits undo in one action");SendMessageW(manager,WM_COMMAND,105,0);check(doc.plots[0].fontFamily=="Times New Roman","manager redo restores font edits");
    Document fragment;fragment.addAtom({0,0},7);replaceSlotStructure(doc,fragment,owner,0,true);history.clear();const auto originalSlot=toNative(structureInSlot(doc,owner,0,true));const auto otherSlot=toNative(structureInSlot(doc,owner,1,true));const auto energies=doc.plots[0].series[0].values;
    auto savedEditor=structureEditor;structureEditor=[](const Document& current)->std::optional<Document>{auto changed=current;check(!changed.atoms.empty(),"manager passes only the selected structure to its editor");changed.atoms[0].element=8;return changed;};
    SendMessageW(manager,WM_COMMAND,4003,0);SendDlgItemMessageW(manager,4200,CB_SETCURSEL,0,0);SendDlgItemMessageW(manager,4201,CB_SETCURSEL,0,0);SendMessageW(manager,WM_COMMAND,4300,0);structureEditor=savedEditor;
    check(structureInSlot(doc,owner,0,true).atoms[0].element==8&&toNative(structureInSlot(doc,owner,1,true))==otherSlot&&doc.plots[0].series[0].values==energies,"manager edits a chosen structure independently of other states and graph data");
    SendMessageW(manager,WM_COMMAND,104,0);check(toNative(structureInSlot(doc,owner,0,true))==originalSlot,"independent structure edit undoes through the manager");
    const auto below=structureInSlot(doc,owner,0,false);
    check(below.allIds().empty(),"test uses an empty region opposite an existing structure");
    structureEditor=[](const Document& current)->std::optional<Document>{
        check(current.allIds().empty(),"choosing an empty region starts a blank structure");
        auto result=current;result.addAtom({0,0},8);return result;
    };
    SendDlgItemMessageW(manager,4200,CB_SETCURSEL,0,0);
    SendDlgItemMessageW(manager,4201,CB_SETCURSEL,1,0);
    SendMessageW(manager,WM_COMMAND,4300,0);structureEditor=savedEditor;
    check(structureInSlot(doc,owner,0,false).atoms.size()==1&&toNative(structureInSlot(doc,owner,0,true))==originalSlot,
        "new structure is saved in the chosen empty region");
    SendMessageW(manager,WM_COMMAND,104,0);
    {
        const auto borderBefore=toNative(doc);
        SendDlgItemMessageW(manager,4200,CB_SETCURSEL,0,0);
        SendDlgItemMessageW(manager,4201,CB_SETCURSEL,0,0);
        SendMessageW(manager,WM_COMMAND,4305,0);
        auto borderForm=FindWindowExW(manager,nullptr,L"YoungDrawEmbeddedForm",nullptr);
        check(borderForm&&GetDlgItem(borderForm,501),"structure tab exposes border controls and a color picker");
        check(!(GetWindowLongPtrW(GetDlgItem(manager,4200),GWL_STYLE)&WS_VISIBLE),"border form keeps its selected target fixed while previewing");
        auto previewFrame=[&](){
            RECT client{};GetClientRect(manager,&client);Bitmap image(client.right,client.bottom,PixelFormat32bppARGB);
            Graphics graphics(&image);auto dc=graphics.GetHDC();SendMessageW(manager,WM_PRINTCLIENT,(WPARAM)dc,PRF_CLIENT);graphics.ReleaseHDC(dc);
            bool found=false;for(int y=88;y<client.bottom-460&&!found;y++)for(int x=20;x<client.right-20;x++){
                Color pixel;image.GetPixel(x,y,&pixel);
                if(std::abs(int(pixel.GetR())-18)<6&&std::abs(int(pixel.GetG())-85)<6&&std::abs(int(pixel.GetB())-217)<6){found=true;break;}
            }
            return found;
        };
        auto setBorder=[&](){
            SendDlgItemMessageW(borderForm,100,CB_SETCURSEL,1,0);
            SetDlgItemTextW(borderForm,101,L"#1255d9");SetDlgItemTextW(borderForm,102,L"12");SetDlgItemTextW(borderForm,103,L"2");
            SendMessageW(borderForm,WM_COMMAND,MAKEWPARAM(100,CBN_SELCHANGE),0);
        };
        setBorder();check(toNative(doc)==borderBefore&&previewFrame(),"border color and padding preview without changing the document");
        captureAxesManager(L"graph-structure-border.png");
        SendMessageW(borderForm,WM_COMMAND,IDCANCEL,0);
        check(toNative(doc)==borderBefore&&!previewFrame(),"cancelling the border restores the original preview and document");
        SendMessageW(manager,WM_COMMAND,4305,0);borderForm=FindWindowExW(manager,nullptr,L"YoungDrawEmbeddedForm",nullptr);setBorder();
        SetDlgItemTextW(borderForm,102,L"1");SendMessageW(borderForm,WM_COMMAND,IDOK,0);
        check(toNative(doc)==borderBefore&&IsWindow(borderForm),"insufficient border padding cannot be committed");
        SetDlgItemTextW(borderForm,102,L"12");SendMessageW(borderForm,WM_COMMAND,IDOK,0);
        const auto& border=doc.plots[0].states[0].aboveBorder;
        check(border.enabled&&border.color=="#1255d9"&&border.padding==12&&border.width==2&&
              !doc.plots[0].states[0].belowBorder.enabled&&!doc.plots[0].states[1].aboveBorder.enabled,
              "border form applies only to the chosen upper structure");
        check(toNative(structureInSlot(doc,owner,0,true))==originalSlot&&doc.plots[0].series[0].values==energies,
              "border styling leaves structure and energy data intact");
        SendMessageW(manager,WM_COMMAND,104,0);check(toNative(doc)==borderBefore,"border styling undoes in one step");
        SendMessageW(manager,WM_COMMAND,105,0);check(doc.plots[0].states[0].aboveBorder.enabled,"border styling redoes in one step");
        SendMessageW(manager,WM_COMMAND,4305,0);borderForm=FindWindowExW(manager,nullptr,L"YoungDrawEmbeddedForm",nullptr);
        check(SendDlgItemMessageW(borderForm,100,CB_GETCURSEL,0,0)==1,"border setting survives reopening the panel");
        SendDlgItemMessageW(borderForm,100,CB_SETCURSEL,0,0);SendMessageW(borderForm,WM_COMMAND,IDOK,0);
        check(!doc.plots[0].states[0].aboveBorder.enabled&&doc.plots[0].states[0].aboveBorder.color=="#1255d9","hiding the border retains its chosen style");
        SendMessageW(manager,WM_COMMAND,104,0);SendMessageW(manager,WM_COMMAND,104,0);
        check(toNative(doc)==borderBefore,"border regression restores the original document");
    }
    SendMessageW(manager,WM_COMMAND,4004,0);formBefore=toNative(doc);formAction=31;formDriven=false;SetTimer(hwnd,901,10,driveForm);SendMessageW(manager,WM_COMMAND,plotWaveWidthCommand,0);check(formDriven&&doc.plots[0].waveWidth==3.2&&doc.plots[0].waveGap==4,"the wave manager can edit thickness and clearance together");
    RECT waveCard{};GetWindowRect(GetDlgItem(manager,plotWaveWidthCommand),&waveCard);
    const LPARAM wheelPoint=MAKELPARAM((waveCard.left+waveCard.right)/2,(waveCard.top+waveCard.bottom)/2);
    SendMessageW(manager,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA/2),wheelPoint);
    SendMessageW(manager,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA/2),wheelPoint);
    check(std::abs(doc.plots[0].waveWidth-3.3)<1e-8,"wheel adjusts the hovered card after a full wheel increment");
    SendMessageW(manager,WM_COMMAND,104,0);
    check(std::abs(doc.plots[0].waveWidth-3.2)<1e-8,"graph card wheel increments are undoable");
    auto weightBefore=doc.plots[0].structureInkScale;SendMessageW(manager,WM_KEYDOWN,VK_ADD,0);
    check(std::abs(doc.plots[0].waveWidth-3.3)<1e-8&&doc.plots[0].structureInkScale==weightBefore,"plus adjusts only the selected wave thickness");
    SendMessageW(manager,WM_KEYDOWN,VK_SUBTRACT,0);check(std::abs(doc.plots[0].waveWidth-3.2)<1e-8,"minus reverses a wave thickness step");
    for(int action:{37,38,39}){formAction=action;formDriven=false;SetTimer(hwnd,901,10,driveForm);SendMessageW(manager,WM_COMMAND,242,0);check(formDriven,"unified axis editor opens");check(doc.plots[0].showXAxis==(action!=37)&&doc.plots[0].xAxisWidth==2.5,"the axis visibility toggle preserves explicit thickness");}
    formAction=40;formDriven=false;SetTimer(hwnd,901,10,driveForm);SendMessageW(manager,WM_COMMAND,243,0);KillTimer(hwnd,901);
    const auto& axisGraph=doc.plots.front();
    check(formDriven&&axisGraph.xAxisHeadLength==12&&axisGraph.xAxisHeadWidth==6&&axisGraph.yAxisHeadLength==18&&axisGraph.yAxisHeadWidth==9&&axisGraph.yTitleGap==48&&axisGraph.yLabel=="ΔG",
        "manager edits X and Y arrowheads independently");
    check(std::all_of(axisGraph.states.begin(),axisGraph.states.end(),[](const auto& state){return state.labelPlacement==4;}),
        "X axis management can move state names below their energy data");
    // A graph point keeps the exact stage as the independent editor target.
    SendMessageW(manager,WM_COMMAND,4003,0);tool=Select;fit();auto point=scene().plotSnapPoints.at(owner).front();mouseDown(screen(point.p));mouseUp(screen(point.p));
    check(targetContainer==owner&&targetSlot==size_t(point.state),"clicking a graph point retains its structure stage");
    SendMessageW(manager,WM_COMMAND,4004,0);
    RECT bounds{};GetClientRect(manager,&bounds);Bitmap snapshot(bounds.right,bounds.bottom,PixelFormat32bppARGB);
    auto capture=[&](){Graphics graphics(&snapshot);auto dc=graphics.GetHDC();SendMessageW(manager,WM_PRINTCLIENT,(WPARAM)dc,PRF_CLIENT);struct Context{HWND manager;HDC dc;};Context context{manager,dc};
        EnumChildWindows(manager,[](HWND child,LPARAM data)->BOOL{auto& context=*reinterpret_cast<Context*>(data);for(auto ancestor=child;ancestor&&ancestor!=context.manager;ancestor=GetParent(ancestor))if(!(GetWindowLongPtrW(ancestor,GWL_STYLE)&WS_VISIBLE))return TRUE;RECT rect{};GetWindowRect(child,&rect);MapWindowPoints(HWND_DESKTOP,context.manager,reinterpret_cast<POINT*>(&rect),2);int saved=SaveDC(context.dc);SetViewportOrgEx(context.dc,rect.left,rect.top,nullptr);wchar_t type[32]{};GetClassNameW(child,type,32);
            if(std::wstring(type)==L"Button"){DRAWITEMSTRUCT item{};item.CtlType=ODT_BUTTON;item.CtlID=GetDlgCtrlID(child);item.hwndItem=child;item.hDC=context.dc;item.rcItem={0,0,rect.right-rect.left,rect.bottom-rect.top};SendMessageW(GetParent(child),WM_DRAWITEM,item.CtlID,(LPARAM)&item);}else if(std::wstring(type)==L"Edit"){
                SendMessageW(child,WM_PRINT,(WPARAM)context.dc,PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND);
            }else SendMessageW(child,WM_PRINTCLIENT,(WPARAM)context.dc,PRF_CLIENT);RestoreDC(context.dc,saved);return TRUE;},(LPARAM)&context);graphics.ReleaseHDC(dc);
    };
    CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};capture();check(snapshot.Save(L"graph-manager-preview.png",&png,nullptr)==Ok,"capture the graph manager and live preview");
    SendMessageW(manager,WM_COMMAND,4001,0);capture();check(snapshot.Save(L"graph-manager-font-preview.png",&png,nullptr)==Ok,"capture graph font and axis tools");
    SendMessageW(manager,WM_COMMAND,4000,0);SendMessageW(manager,WM_COMMAND,131,0);capture();check(snapshot.Save(L"graph-manager-data-preview.png",&png,nullptr)==Ok,"capture the inline data panel below the graph");
    SendMessageW(manager,WM_COMMAND,132,0);
    capture();check(snapshot.Save(L"graph-manager-add-path.png",&png,nullptr)==Ok,"capture the inline path form");
    SendMessageW(manager,WM_COMMAND,4001,0);SendMessageW(manager,WM_COMMAND,plotLegendCommand,0);
    auto legendPanel=FindWindowExW(manager,nullptr,L"YoungDrawEmbeddedForm",nullptr);
    SendDlgItemMessageW(legendPanel,100,CB_SETCURSEL,1,0);
    SendMessageW(legendPanel,WM_COMMAND,MAKEWPARAM(100,CBN_SELCHANGE),0);
    capture();check(snapshot.Save(L"graph-manager-legend.png",&png,nullptr)==Ok,"capture legend controls and visible graph");
    SendMessageW(manager,WM_COMMAND,4002,0);SendMessageW(manager,WM_COMMAND,139,0);
    const auto arrowCount=doc.plots.front().stages.empty()?defaultMechanismStages().size():doc.plots.front().stages.size();
    SendMessageW(manager,WM_COMMAND,4403,0);
    const auto& arranged=doc.plots.front().stages;
    check(arranged.size()==arrowCount,"automatic arrangement keeps every arrow");
    for(size_t i=0;i<arranged.size();++i)check(arranged[i].row==1&&arranged[i].levelRange&&
        std::abs(arranged[i].start-100.*i/arrowCount)<1e-5&&std::abs(arranged[i].end-100.*(i+1)/arrowCount)<1e-5,
        "automatic arrangement divides one row into equal parts");
    capture();check(snapshot.Save(L"graph-manager-reaction-arrows.png",&png,nullptr)==Ok,"capture the inline reaction table");
    newDocumentTab();check(!plotManagerWindow,"document changes close the manager before graph IDs can be reused");std::cout<<"Graph manager categories, font, structures, undo and document lifetime checks passed\n";
}
static void carbonRadicalChecks(HWND hwnd){
            doc={};history.clear();selection.clear();hoverBond=0;tool=Select;
            const int a=doc.addAtom({20,20}),b=doc.addAtom({70,20});doc.addBond(a,b);hoverAtom=a;
            const auto carbonVisible=[&](const Document& drawing){
                for(const auto& item:makeScene(drawing).items)
                    if(item.owner==a&&item.type==Primitive::Text&&item.text=="C")return true;
                return false;
            };
            check(!carbonVisible(doc),"ordinary bonded carbon starts with its symbol implicit");
            SendMessageW(hwnd,WM_CHAR,'*',0);
            check(doc.atom(a)->radical==2&&!carbonVisible(doc),"radical shortcut marks the skeletal vertex without forcing C");
            SendMessageW(hwnd,WM_CHAR,'c',0);
            check(doc.atom(a)->radical==2&&carbonVisible(doc),"carbon shortcut explicitly shows C while keeping the radical");
            check(history.undo(doc)&&doc.atom(a)->radical==2&&!carbonVisible(doc),"undoing explicit C restores the vertex radical");
            SendMessageW(hwnd,WM_CHAR,'*',0);
            check(!doc.atom(a)->radical&&!carbonVisible(doc),"radical shortcut toggles off without adding a carbon label");
            history.clear();
            const auto before=toNative(doc);
            SendMessageW(hwnd,WM_CHAR,'c',0);
            check(doc.atom(a)->element==6&&doc.atom(a)->label=="C"&&carbonVisible(doc)&&doc.atoms.size()==2&&doc.bonds.size()==1,
                  "lowercase c explicitly displays C on a bonded carbon without changing connectivity");
            check(carbonVisible(fromNative(toNative(doc))),"explicit carbon symbol survives native save and reload");
            check(carbonVisible(fromCdxml(toCdxml(doc))),"explicit carbon symbol survives CDXML save and reload");
            check(history.undo(doc)&&toNative(doc)==before&&!carbonVisible(doc),"explicit carbon symbol is one undo operation");
            doc.atom(a)->charge=1;doc.atom(a)->radical=2;hotkey('c');
            check(carbonVisible(doc)&&doc.atom(a)->charge==1&&doc.atom(a)->radical==2,"carbon shortcut preserves charge and radical");
            hotkey('0');check(carbonVisible(doc),"clearing annotations preserves the explicit carbon symbol");
            hotkey('n');check(doc.atom(a)->element==7&&doc.atom(a)->label.empty(),"another element shortcut removes the explicit C label");
            hotkey('c');check(doc.atom(a)->element==6&&carbonVisible(doc),"converting another element to carbon explicitly displays C");
            hotkey('C');check(doc.atom(a)->element==17&&doc.atom(a)->label.empty(),"uppercase C retains the chlorine shortcut");
            selection.clear();hoverAtom=hoverBond=0;
}
static bool alignmentCancel=false,alignmentPreview=false;
static int alignmentPreset=4;
static void CALLBACK driveLabelAlignment(HWND parent,UINT,UINT_PTR timer,DWORD){
    auto form=FindWindowW(L"YoungDrawForm",L"원자·라벨 · 결합 위치 미세조정");if(!form)return;KillTimer(parent,timer);
    SendDlgItemMessageW(form,108,CB_SETCURSEL,alignmentPreset,0);
    SetDlgItemTextW(form,105,L"25");SetDlgItemTextW(form,106,L"1.5");SetDlgItemTextW(form,107,L"-2");
    alignmentPreview=doc.atoms.back().labelAlignment==(alignmentPreset==0?std::optional<double>{}:std::optional<double>{alignmentPreset==4?.25:(alignmentPreset-1)*.5})&&(doc.atoms.back().labelAdjustment-Vec{1.5,-2}).length()<1e-8;
    PostMessageW(form,WM_COMMAND,alignmentCancel?IDCANCEL:IDOK,0);
}
static void labelAlignmentChecks(HWND hwnd){
    doc=Document{};int metal=doc.addAtom({0,60},26),oxygen=doc.addAtom({0,0},8);doc.addBond(metal,oxygen,BondKind::Single);
    auto* a=doc.atom(oxygen);a->label="OO";a->labelRuns={{"OO","#123456","Arial",14,1}};a->labelOffset={-7,-7};a->explicitH=0;
    history=History{};const auto before=toNative(doc);
    alignmentCancel=true;SetTimer(hwnd,988,10,driveLabelAlignment);editObject(oxygen);KillTimer(hwnd,988);
    check(alignmentPreview&&toNative(doc)==before,"label alignment previews live and cancel restores exact document");
    alignmentCancel=false;alignmentPreview=false;SetTimer(hwnd,988,10,driveLabelAlignment);editObject(oxygen);KillTimer(hwnd,988);
    check(alignmentPreview&&doc.atom(oxygen)->labelAlignment==.25,"label alignment dialog commits chosen attachment reference");
    check(doc.atom(oxygen)->labelRuns.size()==1&&doc.atom(oxygen)->labelRuns.front().color=="#123456"&&doc.atom(oxygen)->explicitH==0,"fine adjustment preserves imported runs and hydrogen setting");
    check((doc.atom(metal)->p-Vec{0,60}).length()<1e-8&&doc.atom(oxygen)->p.length()<1e-8,"fine adjustment dialog preserves bond geometry");
    const auto after=toNative(doc);check(history.undo(doc)&&toNative(doc)==before,"fine adjustment undoes in one step");
    check(history.redo(doc)&&toNative(doc)==after,"fine adjustment redoes in one step");
    for(int preset=0;preset<=3;++preset){
        alignmentPreset=preset;alignmentPreview=false;SetTimer(hwnd,988,10,driveLabelAlignment);editObject(oxygen);KillTimer(hwnd,988);
        check(alignmentPreview,"left center right and automatic choices preview and commit independently of custom percentage");
        auto value=doc.atom(oxygen)->labelAlignment;
        SetTimer(hwnd,988,10,driveLabelAlignment);editObject(oxygen);KillTimer(hwnd,988);
        check(doc.atom(oxygen)->labelAlignment==value,"preset survives reopening label properties");
    }
    alignmentPreset=4;

}
static void contextualUiChecks(HWND hwnd){
    const auto saved=doc;const auto chosen=selection;auto oldOrigin=origin;auto oldZoom=zoom;
    doc=Document{};selection.clear();targetContainer=0;tool=Select;origin={400,350};zoom=1;
    auto inspect=[&](int owner){std::set<int> commands;auto menu=canvasContextMenu(owner);std::function<void(HMENU)> scan=[&](HMENU m){for(int i=0;i<GetMenuItemCount(m);++i){if(auto sub=GetSubMenu(m,i))scan(sub);else commands.insert(int(GetMenuItemID(m,i)));}};scan(menu);DestroyMenu(menu);return commands;};
    auto commands=inspect(0);check(commands.count(120)&&!commands.count(143)&&!commands.count(169),"empty canvas context excludes mechanism stage actions");
    int atom=doc.addAtom({0,0},26);selection={atom};commands=inspect(atom);check(commands.count(110)&&!commands.count(143),"structure context exposes structure editing only");
    Reaction reaction;reaction.id=doc.nextId++;doc.reactions.push_back(reaction);selection={reaction.id};commands=inspect(reaction.id);check(commands.count(143)&&!commands.count(130),"mechanism context contains stage actions only for a mechanism");
    doc=coordinateDemo();int graph=doc.plots.front().id;sidebarPage=0;showLayers=false;fit();
    auto sc=scene();Vec click{};for(const auto& item:sc.items)if(item.owner==graph&&!item.points.empty()){click=screen(item.points[0]);if(canvas(click))break;}
    mouseDown(click);cancel();check(sidebarPage==1,"clicking graph suggests graph tools");command(182);check(sidebarPage==0,"manual mechanism tab remains available after selecting graph");
    doc=saved;selection=chosen;origin=oldOrigin;zoom=oldZoom;targetContainer=0;
}
int main(int argc,char** argv){
    BYTE initialKeys[256]{};SetKeyboardState(initialKeys);
    OleInitialize(nullptr);GdiplusStartupInput startup;ULONG_PTR token;GdiplusStartup(&token,&startup,nullptr);
    int result=0;
    try{
        // User templates live in an isolated LOCALAPPDATA root so this test never
        // changes a real user's templates, favorites, or active gallery data.
        {
            wchar_t previous[32768]{};DWORD size=GetEnvironmentVariableW(L"LOCALAPPDATA",previous,32768);
            std::wstring oldAppData=size&&size<32768?std::wstring(previous,size):L"";
            auto root=fs::temp_directory_path()/(L"YoungDrawTemplateTests-"+std::to_wstring(GetCurrentProcessId()));
            fs::create_directories(root);
            check(SetEnvironmentVariableW(L"LOCALAPPDATA",root.wstring().c_str())!=0,"isolate template storage");
            try{
                check(validTemplatePart(L"Fe-peroxo")&&!validTemplatePart(L"../escape")&&!validTemplatePart(L"CON"),"template path validation");
                auto path=templateLocation(L"test-molecule",L"catalysis");
                fs::create_directories(path.parent_path());
                Document initial;initial.addAtom({0,0},8);
                writeFile(path.wstring(),toNative(initial));
                size_t skipped=0;
                auto& cards=templateCards(skipped);
                auto match=[&](const std::vector<ui::TemplateCard>& items){
                    return std::find_if(items.begin(),items.end(),[&](const ui::TemplateCard& card){return card.path==path.wstring();});
                };
                auto item=match(cards);
                check(item!=cards.end()&&item->group==L"catalysis"&&item->document.atoms.size()==1,"user template category and load");
                const auto* preview=&item->preview;
                auto& reused=templateCards(skipped);
                check(&match(reused)->preview==preview,"unchanged template preview is cached");
                auto favorites=loadFavorites();favorites.insert(templateKey(*item));saveFavorites(favorites);
                auto& marked=templateCards(skipped);
                check(match(marked)->favorite,"favorite survives gallery reload");
                initial.addAtom({50,0},6);writeFile(path.wstring(),toNative(initial));
                auto& refreshed=templateCards(skipped);
                check(match(refreshed)->document.atoms.size()==2,"edited template invalidates preview cache");
                fs::remove(path);auto& removed=templateCards(skipped);
                check(match(removed)==removed.end(),"deleted user template disappears from gallery");
            }catch(...){
                SetEnvironmentVariableW(L"LOCALAPPDATA",oldAppData.empty()?nullptr:oldAppData.c_str());
                fs::remove_all(root);throw;
            }
            SetEnvironmentVariableW(L"LOCALAPPDATA",oldAppData.empty()?nullptr:oldAppData.c_str());
            fs::remove_all(root);
        }
        // Reproduce differing bitmap/window DPI (100%, 125%, 150%, 200%).
        // The painted UI must fill the client area and retain hit-test positions.
        for(float sourceDpi:{96.f,120.f,144.f,192.f})for(float targetDpi:{96.f,120.f,144.f,192.f}){
            Bitmap buffer(160,100,PixelFormat32bppARGB),output(180,120,PixelFormat32bppARGB);
            buffer.SetResolution(sourceDpi,sourceDpi);output.SetResolution(targetDpi,targetDpi);
            {Graphics source(&buffer);source.Clear(Color(255,255,255,255));
             SolidBrush marker(Color(255,0,123,112));source.FillRectangle(&marker,40,30,20,20);}
            {Graphics target(&output);target.Clear(Color(255,255,0,255));presentBuffer(target,buffer);}
            Color pixel;
            output.GetPixel(158,98,&pixel);check(pixel.GetValue()==Color(255,255,255,255).GetValue(),"DPI-independent paint covers bottom-right client area");
            output.GetPixel(50,40,&pixel);check(pixel.GetValue()==Color(255,0,123,112).GetValue(),"DPI-independent paint preserves button hit-test position");
            output.GetPixel(162,102,&pixel);check(pixel.GetValue()==Color(255,255,0,255).GetValue(),"DPI-independent paint does not enlarge beyond client area");
        }
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawTabRegression";
        check(RegisterClassW(&wc)!=0,"register window");
        auto hwnd=CreateWindowExW(0,wc.lpszClassName,L"Tabs",WS_OVERLAPPEDWINDOW,0,0,1440,940,nullptr,nullptr,wc.hInstance,nullptr);
        check(hwnd&&documentTabs,"create native tab bar");
        if(argc==2&&std::string(argv[1])=="--axis-quick-access-only"){
            axisQuickChecks(hwnd);DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return 0;
        }
        if(argc==3&&std::string(argv[1])=="--project-axis-only"){
            projectAxisChecks(hwnd,ui::wide(argv[2]));DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return 0;
        }
        if(argc>1&&std::string(argv[1])=="--unified-axes-only"){
            unifiedAxisChecks(hwnd);DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return 0;
        }
        if(argc>1&&std::string(argv[1])=="--annotation-preview-only"){
            annotationPreviewChecks(hwnd);DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return 0;
        }
        if(argc>1&&std::string(argv[1])=="--plot-manager-only"){
            documentStyleChecks(hwnd);graphManagerChecks(hwnd);DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return 0;
        }
        if(argc>1&&std::string(argv[1])=="--png-export-only"){
            pngExportChecks(hwnd);DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return 0;
        }
        if(argc>1&&std::string(argv[1])=="--label-alignment-only"){
            labelAlignmentChecks(hwnd);DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return 0;
        }
        labelAlignmentChecks(hwnd);
        if(argc>1&&std::string(argv[1])=="--atom-radicals-only"){
            carbonRadicalChecks(hwnd);DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return 0;
        }
        contextualUiChecks(hwnd);
        graphCaptionChecks(hwnd);
        if(argc>1&&std::string(argv[1])=="--plot-caption-only"){
            DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return 0;
        }
        graphUnitChecks(hwnd);
        if(argc>1&&std::string(argv[1])=="--plot-units-only"){
            DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return 0;
        }
        checkNewMechanism(hwnd);
        {
            doc=mechanismDemo();history.clear();tool=Select;selection.clear();fit();
            auto before=toNative(doc);int owner=doc.reactions.front().id;
            neutralMechanismClick(mechanismAddPosition(doc.reactions.front(),true,0));
            check(doc.reactions.front().slots.size()==4&&doc.reactions.front().links.back().from==0,"canvas plus branches from the first step without removing the main path");
            check(doc.reactions.front().slots[0].energy=="-4.5"&&doc.reactions.front().above.back()=="TS-branch"&&doc.reactions.front().below.back()=="17.2","canvas branch prompts and stores source energy and TS metadata");
            check(history.undo(doc)&&toNative(doc)==before,"early canvas branching undoes in one step");
            auto atoms=doc.atoms.size();selection={owner};targetContainer=owner;targetSlot=1;
            check(deleteSelectedMechanismStage()&&doc.reactions.front().slots.size()==2&&doc.atoms.size()<atoms,"direct stage deletion removes just the selected stage and its structure");
            check(history.undo(doc)&&toNative(doc)==before,"direct stage Delete can be undone exactly");
            doc={};selection.clear();targetContainer=0;history.clear();dirty=false;
        }
        {
            // Hover-only paint reuse must keep geometry, while any document redraw
            // must invalidate the cached scene (including Undo and tab switches).
            auto previousDoc=doc;
            doc={};doc.addAtom({0,0});redraw();
            auto first=sceneForPaint(true).bounds;
            doc.atoms[0].p={200,100};
            auto retained=sceneForPaint(true).bounds;
            check(first.x==retained.x&&first.y==retained.y,"overlay paint reuses the existing scene");
            redraw();
            auto updated=sceneForPaint(true).bounds;
            check(updated.x>first.x+100&&updated.y>first.y+50,"document redraw rebuilds the scene");
            doc=previousDoc;redraw();
        }
        {
            auto originalDoc=doc;auto originalHistory=history;auto originalSelection=selection;auto originalOrigin=origin;double originalZoom=zoom;bool originalDirty=dirty;
            doc={};history.clear();selection.clear();origin={left+200.,top+180.};zoom=2;tool=Select;dirty=false;
            int a=doc.addAtom({0,0}),b=doc.addAtom({25,0});int bond=doc.addBond(a,b);
            Object label;label.id=doc.nextId++;label.kind=ObjectKind::Text;label.text="Catalyst";label.points={{0,50}};label.fontSize=12;doc.objects.push_back(label);
            Object arrow;arrow.id=doc.nextId++;arrow.points={{70,0},{120,0}};doc.objects.push_back(arrow);
            int outsider=doc.addAtom({170,100});
            selection={bond,label.id,arrow.id};
            BYTE previous[256]{};GetKeyboardState(previous);BYTE keys[256];memcpy(keys,previous,sizeof(keys));keys[VK_CONTROL]=0x80;keys[VK_SHIFT]=0;keys[VK_MENU]=0;SetKeyboardState(keys);key('G');SetKeyboardState(previous);
            check(doc.groups.size()==1&&contains(selection,a)&&contains(selection,b),"Ctrl+G groups bonds with endpoints, text and arrows");
            auto grouped=toNative(doc);command(198);check(doc.groups.size()==1&&toNative(doc)==grouped,"repeated grouping creates no duplicate group");
            BYTE dragKeys[256]{};SetKeyboardState(dragKeys);
            selection.clear();Vec at=screen({0,50});mouseDown(at);mouseUp(at);
            check(contains(selection,a)&&contains(selection,label.id)&&contains(selection,arrow.id)&&!contains(selection,outsider),"clicking grouped text selects the entire group");
            mouseDown(at);check(!inlineEdit&&dragging,"grouped text starts a group drag instead of inline editing");mouse=at+Vec{40,20};updateDrag();mouseUp(mouse);SetKeyboardState(previous);
            check((doc.atom(a)->p-Vec{20,10}).length()<.001&&(doc.atom(outsider)->p-Vec{170,100}).length()<.001,"dragging a group moves all members and leaves other objects fixed");
            check((doc.objects[0].points[0]-Vec{20,60}).length()<.001&&(doc.objects[1].points[0]-Vec{90,10}).length()<.001,"group drag preserves relative text and arrow positions");
            check(history.undo(doc)&&toNative(doc)==grouped,"group movement can be undone");
            auto loaded=fromNative(grouped),xml=fromCdxml(toCdxml(doc));check(loaded.groups.size()==1&&xml.groups.size()==1,"native and CDXML round trips retain user groups");
            auto fragment=doc.subset({a});Document pasted;pasted.append(fragment,{10,10});check(pasted.groups.size()==1&&pasted.objects.size()==2,"copying a member preserves and remaps its whole group");
            selection={a};memcpy(keys,previous,sizeof(keys));keys[VK_CONTROL]=keys[VK_SHIFT]=0x80;keys[VK_MENU]=0;SetKeyboardState(keys);key('G');SetKeyboardState(previous);
            check(doc.groups.empty(),"Ctrl+Shift+G ungroups a selected member");check(history.undo(doc)&&doc.groups.size()==1,"ungroup can be undone");
            check(history.undo(doc)&&doc.groups.empty(),"group creation can be undone");
            selection.clear();dirty=false;command(198);check(doc.groups.empty()&&!dirty,"empty grouping does not dirty the document");
            doc=originalDoc;history=originalHistory;selection=originalSelection;origin=originalOrigin;zoom=originalZoom;dirty=originalDirty;tool=Select;title();
        }
        {
            auto originalDoc=doc;auto originalHistory=history;auto originalSelection=selection;bool originalDirty=dirty;auto originalOrigin=origin;double originalZoom=zoom;
            // Hosted Windows desktops may cap the native window below the width
            // required by these full-toolbar layout assertions.
            const int nativeWidth=ww,nativeHeight=wh,nativeRight=right;
            ww=std::max(1440,ww);wh=std::max(1040,wh);right=260;
            Bitmap snapshot(ww,wh,PixelFormat32bppARGB);Graphics graphics(&snapshot);
            auto repaint=[&](){HDC dc=graphics.GetHDC();paint(dc);graphics.ReleaseHDC(dc);};
            auto hit=[&](int id)->RectF{for(auto& h:hits)if(h.id==id)return h.box;return {};};
            auto present=[&](int id){return hit(id).Width>0;};
            sidebarPage=0;showLayers=false;repaint();
            check(present(mechanismManagerCommand)&&present(includeMechanismCommand)&&present(120)&&present(recognizeMechanismCommand),"mechanism sidebar exposes manager, selected structure inclusion, new and recognize");
            check(hit(mechanismManagerCommand).Y==hit(includeMechanismCommand).Y&&hit(includeMechanismCommand).Y==hit(120).Y&&hit(120).Y==hit(recognizeMechanismCommand).Y,"mechanism sidebar prioritizes managing and including structures");
            check(hit(110).Y==hit(112).Y&&hit(112).Y==hit(182).Y&&hit(182).Y==hit(183).Y,"cleanup storage mechanism and graph share one row");
            for(int id:{182,183,mechanismManagerCommand,includeMechanismCommand,120,recognizeMechanismCommand})check(hit(id).Width==hit(id).Height&&hit(id).Width==54,"sidebar uses compact squares for tabs and actions");
            const int fullRight=right;right=180;repaint();check(hit(182).Y>hit(110).Y&&hit(120).Y>hit(mechanismManagerCommand).Y&&hit(120).GetRight()<=ww-12,"narrow sidebar wraps action cards into another row");right=fullRight;repaint();
            check(!present(111)&&hit(mechanismManagerCommand).Width/hit(mechanismManagerCommand).Height<1.2,"sidebar removes duplicate template entry and uses compact square cards");
            auto layoutMenu=GetSubMenu(GetMenu(hwnd),3);check(GetMenuItemCount(layoutMenu)==5,"layout menu groups commands into five submenus");
            check(GetMenuState(GetSubMenu(layoutMenu,4),190,MF_BYCOMMAND)!=UINT(-1)&&GetMenuState(GetSubMenu(layoutMenu,3),190,MF_BYCOMMAND)==UINT(-1),"alignment commands belong only to object alignment submenu");
            for(int id:{121,127,140,143,144,145,146,147,149,169})check(!present(id),"mechanism tools are consolidated into independent editor");
            for(auto& action:hits)check(!helpText(action.id).empty(),"every visible main button has help");
            for(int id:{100,101,102,104,105,107,125,arrowColorCommand,155,156,170,171,172,173,174,160,161,162,163,164,rotateToolCommand}){
                check(present(id)&&hit(id).Y==hit(100).Y&&hit(id).Width==44&&hit(id).GetBottom()<89,"icon toolbar fits one row above document tabs");
                check(!helpText(id).empty(),"each toolbar icon has Korean help");
            }
            check(hit(155).X==hit(arrowColorCommand).X+48,"arrow palette is immediately left of atom palette");
            check(hit(156).X==hit(155).X+48,"text palette is immediately right of atom palette");
            check(present(copyPngCommand)&&hit(copyPngCommand).X==hit(bondAnimationCommand).X+48&&!helpText(copyPngCommand).empty(),"transparent PNG copy icon appears next to GIF in the top toolbar");
            check(hit(162).X==hit(rotateToolCommand).X+48&&hit(163).X==hit(162).X+48&&
                  hit(164).X==hit(163).X+48,"direct, 90-degree and numeric rotation share one toolbar group");
            check(hit(bondAnimationCommand).Width==hit(164).Width&&hit(bondAnimationCommand).Height==hit(164).Height,
                  "GIF and rotation controls use the same icon size");
            const int fullWidth=ww;
            ww=740;repaint();
            check(hit(rotateToolCommand).Width==44&&hit(arrowColorCommand).Width==44,
                  "mouse rotation and arrow color are fully visible at the minimum window width");
            isolatedStructureEditing=true;repaint();
            check(hit(rotateToolCommand).Width==44&&hit(arrowColorCommand).Width==44&&
                  hit(155).X==hit(arrowColorCommand).X+48,
                  "narrow stage editing keeps mouse rotation and the adjacent color palettes visible");
            isolatedStructureEditing=false;
            ww=fullWidth;repaint();
            check(present(218)&&present(219)&&hit(150).GetBottom()<=wh-bottom,"orbital tools and style button fit the client height");
            auto before=toNative(doc);command(183);repaint();
            check(present(plotManagerCommand)&&present(130)&&!helpText(plotManagerCommand).empty(),"graph sidebar exposes the manager and new graph entry");
            check(hit(plotManagerCommand).Y==hit(130).Y,"graph management is the primary sidebar action");
            for(int id:{131,132,133,137,139,138,151,plotAnnotationsCommand,plotWaveWidthCommand,plotWaveGapCommand,plotLegendCommand,plotAxisLengthsCommand})check(!present(id),"individual graph controls are consolidated into the manager window");
            check(before==toNative(doc)&&dirty==originalDirty,"switching sidebar tabs never modifies the document");
            doc=Document{};selection.clear();history.clear();origin={left+160.,top+180.};zoom=2;dirty=false;
            check(present(dottedBondCommand)&&!helpText(dottedBondCommand).empty(),"dotted single bond tool is available in the sidebar with help");
            const auto dotted=hit(dottedBondCommand);
            mouseDown({dotted.X+dotted.Width/2,dotted.Y+dotted.Height/2});
            mouseDown(origin);mouseUp(origin+Vec{60,0});
            check(tool==DottedBond&&doc.atoms.size()==2&&doc.bonds.size()==1&&doc.bonds[0].kind==BondKind::Dot&&doc.objects.empty(),"dotted bond tool draws a real single bond between two atoms");
            const int dottedId=doc.bonds.front().id;
            command(201);mouseDown(screen((doc.atoms[0].p+doc.atoms[1].p)*.5));mouseUp(screen((doc.atoms[0].p+doc.atoms[1].p)*.5));
            check(doc.bonds.size()==1&&doc.bond(dottedId)->kind==BondKind::Single,"single bond tool restores dotted bond to solid without duplicates");
            selection={dottedId};command(dottedBondCommand);
            check(doc.bond(dottedId)->kind==BondKind::Dot,"selected solid bond converts to dotted from the command");
            command(104);check(doc.bond(dottedId)->kind==BondKind::Single,"dotted bond conversion can be undone");
            selection.clear();command(dottedBondCommand);
            mouseDown(screen((doc.atoms[0].p+doc.atoms[1].p)*.5));mouseUp(screen((doc.atoms[0].p+doc.atoms[1].p)*.5));
            check(doc.bonds.size()==1&&doc.bond(dottedId)->kind==BondKind::Dot,"dotted tool converts an existing bond on click");
            check(fromCdxml(toCdxml(doc)).bond(dottedId)->kind==BondKind::Dot,"drawn dotted bond survives a CDXML round trip");
            doc=Document{};selection.clear();history.clear();dirty=false;
            const auto interaction=hit(interactionToolCommand);
            mouseDown({interaction.X+interaction.Width/2,interaction.Y+interaction.Height/2});
            mouseDown(origin);mouseUp(origin+Vec{60,20});
            check(tool==Interaction&&doc.atoms.empty()&&doc.bonds.empty()&&doc.objects.size()==1&&
                  doc.objects.front().kind==ObjectKind::Line&&doc.objects.front().dash==2&&
                  fromCdxml(toCdxml(doc)).objects.front().dash==2,
                  "the sidebar interaction tool draws a dotted graphic that survives a CDXML round trip");
            command(104);check(doc.objects.empty(),"interaction line can be undone");
            const int donor=doc.addAtom({0,0},8),acceptor=doc.addAtom({45,0},7);
            mouseDown(screen(doc.atom(donor)->p));mouseUp(screen(doc.atom(acceptor)->p));
            auto attached=fromCdxml(toCdxml(doc)).subset({donor,acceptor});
            check(attached.objects.size()==1&&attached.objects.front().sourceAtom==donor&&
                  attached.objects.front().targetAtom==acceptor,"both atom anchors survive saving and copying the structure");
            attached.translate({acceptor},{20,15});
            const auto routed=routeArrow(attached,attached.objects.front());
            check((routed.front()-attached.atom(donor)->p).length()<1e-9&&
                  (routed.back()-attached.atom(acceptor)->p).length()<1e-9,"the interaction endpoint follows the moved atom");
            doc=Document{};selection.clear();history.clear();
            command(218);orbitalKind=OrbitalKind::P;orbitalPhase=2;
            Vec at=origin;mouseDown(at);mouse=at+Vec{60,0};mouseUp(mouse);
            check(doc.objects.size()==2&&doc.groups.size()==1&&selection.size()==2,"orbital tool draws two grouped phase lobes on drag");
            check(orbitalMembers(selection.front())==selection,"orbital hit resolves to the full editable orbital");
            check(std::abs((doc.objects[0].points[3]-doc.objects[0].points[0]).length()-30)<1e-9,"orbital drag maps screen size to document units");
            auto orbital=toNative(doc);command(104);check(doc.objects.empty(),"orbital insertion is one undo action");command(105);check(toNative(doc)==orbital,"orbital insertion redo restores shape and phases");
            selection=doc.groups[0].members;formAction=7;formDriven=false;SetTimer(hwnd,920,25,driveForm);editObject(selection.back());KillTimer(hwnd,920);
            check(formDriven&&doc.objects.size()==4&&selection.size()==4,"orbital properties edit the whole object into a d orbital");
            for(auto& o:doc.objects)check(!o.filled&&std::abs((o.points[3]-o.points[0]).length()-40)<1e-7,"orbital properties apply size and outline phase to every lobe");
            command(104);check(toNative(doc)==orbital,"orbital property edit is one undo action");
            for(int k=0;k<4;k++){orbitalKind=OrbitalKind(k);orbitalPhase=k==1?2:0;down={origin.x+150*k,origin.y+120};auto fragment=draftOrbital(down+Vec{0,-45});doc.append(fragment);}
            selection.clear();sidebarPage=0;fit();repaint();
            CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
            check(snapshot.Save(L"orbital-sidebar-preview.png",&png,nullptr)==Ok,"render orbital drawing tools and mechanism sidebar");
            sidebarPage=1;repaint();check(snapshot.Save(L"graph-sidebar-preview.png",&png,nullptr)==Ok,"render organized graph sidebar");
            ww=nativeWidth;wh=nativeHeight;right=nativeRight;repaint();
            doc=originalDoc;history=originalHistory;selection=originalSelection;dirty=originalDirty;origin=originalOrigin;zoom=originalZoom;tool=Select;orbitalKind=OrbitalKind::P;orbitalPhase=0;sidebarPage=0;
        }
        {
            auto originalDoc=doc;auto originalHistory=history;auto originalSelection=selection;bool originalDirty=dirty;auto originalRepeat=repeatAction;
            BYTE originalKeys[256]{};GetKeyboardState(originalKeys);
            cancel();
            auto press=[&](WPARAM k,bool ctrl,bool shift,bool alt){BYTE keys[256]{};keys[VK_CONTROL]=ctrl?0x80:0;keys[VK_SHIFT]=shift?0x80:0;keys[VK_MENU]=alt?0x80:0;SetKeyboardState(keys);SendMessageW(hwnd,alt?WM_SYSKEYDOWN:WM_KEYDOWN,k,0);SetKeyboardState(originalKeys);};
            doc=Document{};history.clear();int a=doc.addAtom({0,0}),b=doc.addAtom({30,0});doc.addBond(a,b);int c=doc.addAtom({100,80}),d=doc.addAtom({130,80});doc.addBond(c,d);selection={a,c};
            auto before=toNative(doc);press('M',true,true,true);
            check(std::abs(doc.atom(a)->p.y-doc.atom(c)->p.y)<1e-8&&std::abs(doc.atom(b)->p.y-doc.atom(d)->p.y)<1e-8,"Ctrl+Shift+Alt+M via WM_SYSKEYDOWN aligns whole molecules to middle");
            check(std::abs((doc.atom(a)->p-doc.atom(b)->p).length()-30)<1e-8,"keyboard alignment preserves bond geometry");
            press('Z',true,false,false);check(toNative(doc)==before,"alignment keyboard command is undoable");
            press('Z',true,true,false);check(std::abs(doc.atom(a)->p.y-doc.atom(c)->p.y)<1e-8,"Ctrl+Shift+Z redoes alignment");
            selection=doc.editableIds();Vec initial=doc.atom(a)->p;press(VK_RIGHT,false,true,false);check((doc.atom(a)->p-initial-Vec{10,0}).length()<1e-8,"Shift+arrow nudges ten document points");
            press('Y',true,false,false);check((doc.atom(a)->p-initial-Vec{20,0}).length()<1e-8,"Ctrl+Y repeats the last edit instead of invoking redo");
            Vec bond=doc.atom(b)->p-doc.atom(a)->p;press(VK_RIGHT,false,false,true);Vec rotated=doc.atom(b)->p-doc.atom(a)->p;
            check(std::abs(std::atan2(rotated.y,rotated.x)-std::atan2(bond.y,bond.x)-pi/180)<1e-8,"Alt+Right rotates by one degree");
            formAction=8;formDriven=false;SetTimer(hwnd,921,20,driveForm);press('R',true,false,false);KillTimer(hwnd,921);check(formDriven,"Ctrl+R opens the rotation dialog");
            press('G',true,false,false);check(!doc.groups.empty(),"Ctrl+G groups selection");press('G',true,true,false);check(doc.groups.empty(),"Ctrl+Shift+G ungroups selection");
            auto count=documents.size();press('N',true,true,false);check(documents.size()==count,"unsupported Ctrl+Shift+N never opens an unrelated new document");
            hoverAtom=hoverBond=0;hotkey('G');check(tool==Orbital,"Shift+G selects orbital tool");
            doc=Document{};int root=doc.addAtom({0,0});hoverAtom=root;hotkey('a');check(doc.atoms.size()==7&&doc.bonds.size()==7,"a on atom sprouts phenyl with an exocyclic connector");
            check(std::abs((doc.atoms[1].p-doc.atoms[0].p).length()-25.5)<1e-8,"phenyl hotkey uses compact connector length");
            selection=doc.editableIds();press('K',false,false,true);check(std::count_if(doc.bonds.begin(),doc.bonds.end(),[](const Bond& edge){return edge.kind==BondKind::Aromatic;})==6,"Alt+K converts phenyl display to aromatic bonds");
            press('K',false,false,true);check(std::count_if(doc.bonds.begin(),doc.bonds.end(),[](const Bond& edge){return edge.kind==BondKind::Double;})==3,"Alt+K restores Kekule display");
            writePreview(L"benzene-connector-preview.png",demo());
            doc=originalDoc;history=originalHistory;selection=originalSelection;dirty=originalDirty;repeatAction=originalRepeat;tool=Select;hoverAtom=hoverBond=0;
        }
        {
            Document fixture;fixture.style.bondLength=18;fixture.ring({50,50},5);int co=fixture.atoms[0].id;fixture.atom(co)->element=27;fixture.atom(co)->label="LCo^{III}";for(int i:{1,2,4})fixture.atoms[i].element=8;
            int carbon=fixture.atoms[3].id;fixture.addBond(carbon,fixture.addAtom(fixture.atom(carbon)->p+Vec{18,0}));fixture.addBond(carbon,fixture.addAtom(fixture.atom(carbon)->p+Vec{0,18},1));writePreview(L"chelate-before.png",fixture);cleanStructure(fixture);writePreview(L"chelate-after.png",fixture);
            doc={};doc.ring({0,0},6,true);int a=doc.atoms[0].id;int c=doc.addAtom(bondEndpoint(doc,a,{100,0}));doc.addBond(a,c);writePreview(L"benzene-80-default.png",doc);doc={};
        }

        {
            doc={};int a=doc.addAtom({0,0}),b=doc.addAtom({30,0}),c=doc.addAtom({60,0}),d=doc.addAtom({90,0});doc.addBond(a,b);doc.addBond(b,c);doc.addBond(c,d);selection={b,c};targetContainer=0;history.clear();
            formBefore=toNative(doc);formAction=6;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);command(165);check(formDriven&&previewObserved&&toNative(doc)==formBefore&&selection==std::vector<int>({b,c}),"canceling partial resize restores the document and original selection");
            formAction=5;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);BYTE old[256]{},keys[256]{};GetKeyboardState(old);memcpy(keys,old,sizeof keys);keys[VK_CONTROL]=0x80;SetKeyboardState(keys);key('K');SetKeyboardState(old);
            check(formDriven&&previewObserved&&std::abs(doc.atom(b)->p.x-37.5)<1e-8&&std::abs(doc.atom(c)->p.x-52.5)<1e-8,"Ctrl+K scales only selected connected atoms");
            check(doc.atom(a)->p.x==0&&doc.atom(d)->p.x==90&&!contains(selection,a)&&!contains(selection,d),"Ctrl+K leaves unselected connected atoms and selection unchanged");
            check(history.undo(doc)&&toNative(doc)==formBefore,"partial Ctrl+K is one undo operation");
            doc={};selection.clear();history.clear();
        }

        {
            // Exercise actual raster output at fractional zoom and rotated axes.
            for(double scale:{.65,1.,1.25,1.75})for(double rotation:{0.,-90.}){
                Bitmap surface(320,320,PixelFormat32bppARGB);
                {
                    Graphics graphics(&surface);graphics.Clear(Color::White);
                    graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
                    graphics.TranslateTransform(rotation?80.25f:20.25f,rotation?285.25f:40.25f);graphics.ScaleTransform(float(scale),float(scale));
                    Scene lettering;Primitive text;text.type=Primitive::Text;text.points={{0,0}};text.text="37.2 -9.9 S = 1";text.fontSize=12.5;text.face=2;text.rotation=rotation;lettering.items.push_back(text);
                    auto transform=Matrix{};graphics.GetTransform(&transform);auto smoothing=graphics.GetSmoothingMode();auto offset=graphics.GetPixelOffsetMode();
                    render(graphics,lettering);
                    Matrix after;graphics.GetTransform(&after);check(after.Equals(&transform)&&graphics.GetSmoothingMode()==smoothing&&graphics.GetPixelOffsetMode()==offset,"text rendering restores transforms and quality settings");
                }
                int ink=0,coverage=0;std::set<int> shades;
                for(int y=0;y<320;y++)for(int x=0;x<320;x++){Color pixel;surface.GetPixel(x,y,&pixel);int level=pixel.GetRed();if(level<245)ink++;if(level>0&&level<255){coverage++;shades.insert(level);}}
                check(ink>20&&coverage>20&&shades.size()>8,"thin italic text retains smooth grayscale edges at fractional zoom and vertical rotation");
            }
        }
        {
            doc=coordinateDemo();history.clear();selection={doc.plots[0].id};dirty=false;auto before=toNative(doc);int owner=selection[0];
            formBefore=before;formAction=15;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);plotSettings(owner);
            check(formDriven&&previewObserved&&toNative(doc)==before,"Cancel restores the graph after previewing thin mode");
            formAction=16;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);plotSettings(owner);
            check(formDriven&&doc.plots[0].thinLines,"graph settings apply the thin preset");
            writePreview(L"graph-thin-preset.png",doc);
            auto thinBefore=toNative(doc);formBefore=thinBefore;formAction=18;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);plotSettings(owner);
            check(formDriven&&previewObserved&&toNative(doc)==thinBefore,"Cancel restores level emphasis preview");
            formAction=19;formDriven=false;SetTimer(hwnd,901,10,driveForm);plotSettings(owner);
            check(formDriven&&doc.plots[0].thinLines&&doc.plots[0].boldLevels,"graph settings emphasize levels while keeping thin mode");
            writePreview(L"graph-thin-bold-levels.png",doc);
            check(history.undo(doc)&&toNative(doc)==thinBefore,"level emphasis is one undo operation");
            check(history.undo(doc)&&toNative(doc)==before,"graph thickness changes undo in one step");check(history.redo(doc)&&doc.plots[0].thinLines,"graph thickness can be redone");
            formAction=17;formDriven=false;SetTimer(hwnd,901,10,driveForm);plotSettings(owner);check(formDriven&&!doc.plots[0].thinLines,"graph settings can restore the existing bold preset");
            writePreview(L"graph-bold-preset.png",doc);
            const auto spacingBefore=toNative(doc);formBefore=spacingBefore;formAction=28;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);command(plotAppearanceLastCommand);
            check(formDriven&&previewObserved&&toNative(doc)==spacingBefore,"canceling sidebar Y title spacing restores the document after preview");
            formAction=27;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);command(plotAppearanceLastCommand);
            check(formDriven&&previewObserved&&doc.plots[0].yTitleGap==60,"sidebar Y title spacing applies the requested clearance");
            writePreview(L"y-title-spacing.png",doc);
            check(history.undo(doc)&&toNative(doc)==spacingBefore,"Y title spacing is one undo action");
            check(history.redo(doc)&&doc.plots[0].yTitleGap==60,"Y title spacing can be redone");
            const auto waveBefore=toNative(doc);formBefore=waveBefore;formAction=33;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);command(plotWaveWidthCommand);
            check(formDriven&&previewObserved&&toNative(doc)==waveBefore,"cancel restores wave stroke preview");
            formAction=31;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);command(plotWaveWidthCommand);
            check(formDriven&&previewObserved&&doc.plots[0].waveWidth==3.2,"wave stroke sidebar applies an independent width");
            const auto gapBefore=toNative(doc);formBefore=gapBefore;formAction=34;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);command(plotWaveGapCommand);
            check(formDriven&&previewObserved&&toNative(doc)==gapBefore,"cancel restores wave clearance preview");
            formAction=32;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);command(plotWaveGapCommand);
            check(formDriven&&doc.plots[0].waveGap==4&&doc.plots[0].waveWidth==3.2,"wave clearance changes independently of wave stroke");
            check(history.undo(doc)&&toNative(doc)==gapBefore&&history.undo(doc)&&toNative(doc)==waveBefore,"wave clearance and stroke each undo in one action");
            doc={};history.clear();selection.clear();dirty=false;
        }
        {
            doc=coordinateDemo();history.clear();selection={doc.plots[0].id};dirty=false;
            doc.plots[0].series[0].values[0]=.123456789012345;
            const auto before=doc;
            pathNameStep=pathNameTicks=0;pathNameHeaderUpdated=false;
            SetTimer(hwnd,901,10,drivePathName);
            command(131);
            MSG message{};const auto deadline=GetTickCount64()+5000;while(plotManagerWindow&&GetTickCount64()<deadline&&GetMessageW(&message,nullptr,0,0)>0){if(!handlePlotManagerMessage(message)){TranslateMessage(&message);DispatchMessageW(&message);}}
            closePlotManager();KillTimer(hwnd,901);
            auto expected=before;expected.plots[0].series[0].name="S = 1";
            check(pathNameHeaderUpdated&&dirty&&toNative(doc)==toNative(expected)&&
                  doc.plots[0].series[0].values==expected.plots[0].series[0].values,
                  "double-clicking the path column renames only that series and preserves energy precision");
            const auto picture=scene();
            check(std::any_of(picture.items.begin(),picture.items.end(),[](const Primitive& item){
                return item.type==Primitive::Text&&item.textRole==7&&item.text=="S = 1";
            }),"renamed pathway appears in the graph legend");
            check(fromNative(toNative(doc)).plots[0].series[0].name=="S = 1"&&
                  history.undo(doc)&&toNative(doc)==toNative(before),"path name persists on reopen and can be undone");
            doc={};history.clear();selection.clear();dirty=false;
        }
        {
            doc={};history.clear();dirty=false;
            EnergyPlot p;p.id=doc.nextId++;p.states={{"R",-19.52},{"TS",43.4},{"P",3.59}};
            p.series={{"S = 0","#000000",{-19.52,43.4,3.59}},{"S = 1","#b44758",{-20.41,7.79,0}},
                      {"S = 2","#167d9a",{0,0,-5.33}}};
            p.series[2].present={false,false,true};
            doc.plots={p};selection={p.id};
            auto run=[&](CrossingAction action){
                crossingAction=action;crossingPreviewSeen=false;
                SetTimer(hwnd,901,10,driveCrossing);command(151);KillTimer(hwnd,901);
            };
            run(CrossingAction::AddLeft);
            check(crossingPreviewSeen&&doc.plots[0].crossings.size()==1,"adding by named segment previews and commits the left crossing");
            const auto left=doc.plots[0].crossings[0];
            check(left.segment==0&&left.firstPath==1&&left.secondPath==2&&left.position==.5&&
                  left.label=="S = 1 / S = 2"&&left.energy==6.92,"named paths and default midpoint populate the left crossing");
            run(CrossingAction::AddRight);
            check(crossingPreviewSeen&&doc.plots[0].crossings.size()==2,"the remaining named segment adds the right crossing");
            const auto right=doc.plots[0].crossings[1];
            check(right.segment==1&&right.firstPath==0&&right.secondPath==1&&right.position==.35&&right.energy==6.57,
                  "the right crossing uses the chosen position and energy");
            const auto before=toNative(doc);
            run(CrossingAction::CancelEdit);
            check(crossingPreviewSeen&&toNative(doc)==before,"canceling an existing crossing edit restores its preview");
            run(CrossingAction::DeleteLeft);
            check(doc.plots[0].crossings.size()==1&&doc.plots[0].crossings[0].segment==1&&
                  history.undo(doc)&&toNative(doc)==before,"deleting one crossing preserves the other and is undoable");
            doc={};history.clear();selection.clear();dirty=false;
        }
        {
            doc=coordinateDemo();history.clear();selection={doc.plots[0].id};targetContainer=selection.front();tool=Text;dirty=false;doc.graphsLocked=true;
            auto before=toNative(doc);auto chosen=selection;auto target=targetContainer;
            for(double scale:{.6,1.,3.}){
                zoom=scale;origin={double(left+90),double(top+100)};Vec initial=origin,at{double(left+160),double(top+180)};
                SendMessageW(hwnd,WM_RBUTTONDOWN,MK_CONTROL|MK_RBUTTON,MAKELPARAM(int(at.x),int(at.y)));
                check(panning&&rightPanning&&GetCapture()==hwnd,"Ctrl+right drag starts viewport pan over drawing in any tool");
                SendMessageW(hwnd,WM_MOUSEMOVE,MK_RBUTTON|MK_CONTROL,MAKELPARAM(int(at.x+70),int(at.y-30)));
                check((origin-initial-Vec{70,-30}).length()<1e-8,"right drag pans by screen pixels at every zoom");
                SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(int(at.x),int(at.y)));check(rightPanning,"other mouse button release does not end right drag");
                SendMessageW(hwnd,WM_RBUTTONUP,0,MAKELPARAM(-20,int(at.y+40)));
                check(!panning&&!rightPanning&&GetCapture()!=hwnd&&(origin-initial-Vec{-20-at.x,40}).length()<1e-8,"right release outside canvas ends pan at final mouse location even after Ctrl released");
                check(toNative(doc)==before&&!dirty&&selection==chosen&&targetContainer==target,"viewport pan preserves document, selection and insertion target");
            }
            check(!history.undo(doc),"viewport movement does not consume document undo history");
            Vec at{double(left+160),double(top+180)},initial=origin;SendMessageW(hwnd,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(int(at.x),int(at.y)));check(!panning,"plain right click does not pan");
            SendMessageW(hwnd,WM_RBUTTONDOWN,MK_RBUTTON|MK_CONTROL,MAKELPARAM(20,20));check(!panning,"Ctrl+right click on toolbar does not pan");
            SendMessageW(hwnd,WM_RBUTTONDOWN,MK_RBUTTON|MK_CONTROL,MAKELPARAM(int(at.x),int(at.y)));SendMessageW(hwnd,WM_KEYDOWN,VK_ESCAPE,0);check(!panning&&!rightPanning&&GetCapture()!=hwnd,"Escape ends viewport pan");
            SendMessageW(hwnd,WM_RBUTTONDOWN,MK_RBUTTON|MK_CONTROL,MAKELPARAM(int(at.x),int(at.y)));ReleaseCapture();check(!panning&&!rightPanning,"capture loss ends viewport pan");
            doc={};selection.clear();targetContainer=0;tool=Select;history.clear();
        }

        {
            doc={};history.clear();EnergyPlot p;p.id=doc.nextId++;p.p={30,60};p.title="Layer test";p.states={{"R",0},{"P",10}};p.series={{"Path 1","#167d9a",{0,10}}};doc.plots.push_back(p);
            Vec caption;for(auto& item:scene().items)if(item.owner==p.id&&item.textRole==4)caption=item.points[0]+Vec{3,3};
            auto image=imageDocument(readFile(L"animated.gif"));doc.append(image);int imageId=doc.objects.back().id;Vec overlap=p.p+Vec{140,110};doc.objects.back().points={overlap-Vec{30,30},overlap+Vec{30,30}};
            selection={p.id};targetContainer=p.id;hoverAtom=hoverBond=0;tool=Select;zoom=.6;origin={double(left+60),double(top+150)};
            command(176);check(showLayers,"layer panel opens");command(179);check(doc.graphsLocked&&!doc.imagesLocked&&selection.empty()&&!targetContainer,"image-only mode clears locked graph selection and insertion target");
            check(!textAt(caption)&&objectAt(overlap)==imageId,"locked graph title clicks pass through to image");
            check(!pickSlot(plotAnchor(doc.plots[0],0,true)),"locked graph insertion slots cannot intercept clicks");
            mouseDown(screen(overlap));check(selection==std::vector<int>{imageId},"clicking over graph selects editable image");mouseUp(screen(overlap));
            command(180);check(doc.imagesLocked&&!doc.graphsLocked&&selection.empty(),"graph-only mode removes locked image from selection");
            check(objectAt(overlap)==p.id,"locked image allows graph beneath to be selected");
            for(auto& item:scene().items)if(item.owner==p.id&&item.textRole==4)caption=item.points[0]+Vec{3,3};
            check(textAt(caption)&&textAt(caption)->owner==p.id,"unlocked graph title is editable");
            command(177);command(178);check(!doc.imagesLocked&&doc.graphsLocked,"each layer lock can be toggled independently");
            command(179);mouseDown(screen({-20,-40}));check(boxing,"start box selection around overlapping graph and image");mouseUp(screen({700,600}));check(contains(selection,imageId)&&!contains(selection,p.id),"box selection skips locked graphs");
            command(180);BYTE old[256]{},keys[256]{};GetKeyboardState(old);memcpy(keys,old,sizeof keys);keys[VK_CONTROL]=0x80;SetKeyboardState(keys);key('A');SetKeyboardState(old);check(!contains(selection,imageId)&&contains(selection,p.id),"Ctrl+A skips locked images");
            command(181);check(!doc.imagesLocked&&!doc.graphsLocked,"unlock all restores both layers");command(104);check(doc.imagesLocked&&!doc.graphsLocked,"layer lock changes can be undone");command(105);check(!doc.imagesLocked&&!doc.graphsLocked,"layer lock changes can be redone");
            command(179);Bitmap panel(ww,wh,PixelFormat32bppARGB);{Graphics graphics(&panel);auto dc=graphics.GetHDC();paint(dc);graphics.ReleaseHDC(dc);}CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};check(panel.Save(L"layer-panel.png",&png,nullptr)==Ok,"render layer panel preview");
            auto restored=fromNative(toNative(doc));check(restored.graphsLocked&&!restored.imagesLocked,"image-only mode persists in project");command(175);selection.clear();targetContainer=0;doc={};history.clear();
        }

        {
            doc={};history.clear();selection.clear();tool=Select;
            EnergyPlot p;
            p.id=doc.nextId++;
            p.states={{"A",0,{},{}},{"TS",42.7,{},{}}};
            p.series={{"S = 0","#000000",{0,42.7}},{"S = 2","#0000ff",{7.4,19.0}}};
            p.crossings={{"MECP",0,0,1,.44,9.46}};
            doc.plots={p};
            zoom=1.25;origin={double(left+25),double(top+20)};
            redraw();
            const auto caption=scene().crossingCaptions.at(0);
            const Vec at=screen({caption.bounds.x+caption.bounds.w/2,caption.bounds.y+5});
            mouseDown(at);
            check(dragging,"crossing caption starts a drag in select mode");
            mouseUp(at+Vec{50,25});
            const auto& movedPlot=doc.plots.front();
            check((movedPlot.p-p.p).length()<1e-8&&movedPlot.crossings[0].energy==9.46,
                  "dragging the crossing caption keeps the graph and crossing fixed");
            check(movedPlot.crossings[0].labelOffset&&
                  (*movedPlot.crossings[0].labelOffset-caption.offset-Vec{40,20}).length()<1e-8,
                  "crossing caption moves by the pointer displacement in plot coordinates");
            check(history.undo(doc)&&!doc.plots.front().crossings.front().labelOffset,
                  "crossing caption drag is one undo operation");
            selection.clear();
        }

        carbonRadicalChecks(hwnd);
        checkMolecularAnnotations(hwnd);

        {
            doc={};history.clear();int a=doc.addAtom({20,20},27),b=doc.addAtom({70,20},8);doc.atom(a)->label="CoIII";selection={a};hoverAtom=hoverBond=0;tool=Select;
            for(const auto state:std::vector<std::pair<int,int>>{{0,1},{0,2},{0,3},{1,0},{-1,0},{2,2},{-2,2}}){
                doc.atom(a)->charge=state.first;doc.atom(a)->radical=state.second;history.clear();
                const auto before=toNative(doc);
                SendMessageW(hwnd,WM_CHAR,'0',0);
                check(!doc.atom(a)->charge&&!doc.atom(a)->radical&&doc.atom(a)->label=="CoIII"&&doc.atoms.size()==2,
                      "zero clears selected radical and ion annotations while preserving atom and label");
                for(const auto& item:scene().items)check(item.owner!=a||item.textRole!=13,"cleared annotations are no longer rendered");
                check(history.undo(doc)&&toNative(doc)==before,"zero clears both annotations in one undo operation");
                check(!history.undo(doc),"zero adds exactly one undo checkpoint");
            }
            doc.atom(a)->charge=-1;doc.atom(a)->radical=2;
            doc.atom(b)->charge=1;doc.atom(b)->radical=2;hoverAtom=b;history.clear();
            hotkey('0');
            check(!doc.atom(b)->charge&&!doc.atom(b)->radical&&doc.atom(a)->charge==-1&&doc.atom(a)->radical==2,
                  "zero uses hovered atom before the selected atom");
            check(history.undo(doc)&&doc.atom(b)->charge==1&&doc.atom(b)->radical==2,"hovered reset is undoable");
            doc.atom(b)->charge=0;doc.atom(b)->radical=0;history.clear();
            const auto neutral=toNative(doc);hotkey('0');
            check(toNative(doc)==neutral&&!history.undo(doc),"zero on neutral hovered atom makes no edit or undo checkpoint");
            hoverAtom=0;selection.clear();const auto untargeted=toNative(doc);hotkey('0');
            check(toNative(doc)==untargeted&&!history.undo(doc),"zero without atom target leaves the drawing unchanged");
            selection={a};doc.atom(a)->charge=0;doc.atom(a)->radical=0;
            hotkey('-');hotkey('*');check(doc.atom(a)->charge==-1&&doc.atom(a)->radical==2&&doc.atom(a)->label=="CoIII","selected atom accepts radical anion without clearing label");
            hotkey('*');check(!doc.atom(a)->radical&&doc.atom(a)->charge==-1,"radical shortcut toggles independently of charge");
            hoverAtom=b;hotkey('+');hotkey('*');check(doc.atom(b)->charge==1&&doc.atom(b)->radical==2&&doc.atom(a)->charge==-1,"hover target gets radical cation before selected atom");
            hoverAtom=0;zoom=3;origin={double(left+200),double(top+200)};
            Vec at;for(auto& item:scene().items)if(item.owner==b&&item.textRole==13)at=item.points[0]+Vec{measure(item.text,item.fontSize,item.font,item.face)/2,item.fontSize/2};
            Vec original=doc.atom(b)->p;mouseDown(screen(at));check(dragging&&annotationDrag==b,"annotation can be grabbed directly in select tool");mouse=down+Vec{45,30};updateDrag();mouseUp(mouse);
            check(doc.atom(b)->annotationManual&&(doc.atom(b)->p-original).length()<1e-8,"annotation drag leaves atom fixed");
            auto saved=fromNative(toNative(doc));check(saved.atom(b)->annotationManual,"dragged annotation persists");
            check(history.undo(doc)&&!doc.atom(b)->annotationManual,"annotation drag is one undo operation");
            selection={a};
            chemistryMenuOpened=false;
            SetTimer(hwnd,973,10,driveChemistryMenu);
            SendMessageW(hwnd,WM_COMMAND,mullikenSpinCommand,0);
            KillTimer(hwnd,973);
            check(chemistryMenuOpened&&doc.atom(a)->mullikenSpin==-.314&&!doc.atom(b)->mullikenSpin,
                  "Mulliken edit menu annotates only the selected atom");
            check(history.undo(doc)&&!doc.atom(a)->mullikenSpin,"selected spin edit is undoable");
            setInlineValue({a,0},"CoIII");check(doc.atom(a)->label=="Co^{III}","typed atom oxidation state is normalized");
            Object text;text.id=doc.nextId++;text.kind=ObjectKind::Text;text.points={{20,70}};doc.objects.push_back(text);setInlineValue({text.id,0},"CoIII");check(doc.objects.back().text=="Co^{III}","typed free text oxidation state is normalized");
            writePreview(L"atom-annotations.png",doc);selection.clear();hoverAtom=hoverBond=0;annotationDrag=0;
        }
        {
            const auto& groups=functionalGroups();
            const auto entry=std::find_if(groups.begin(),groups.end(),[](const auto& item){
                return std::string(item.abbreviation)=="OMe";
            });
            check(entry!=groups.end(),"methoxy template is available");
            doc=entry->drawing;
            const int root=doc.atoms.front().id;
            history.clear();selection={root};
            chemistryMenuOpened=false;
            SetTimer(hwnd,973,10,driveChemistryMenu);
            SendMessageW(hwnd,WM_COMMAND,abbreviateGroupsCommand,0);
            KillTimer(hwnd,973);
            check(chemistryMenuOpened&&doc.atom(root)->label=="OMe"&&doc.atoms.size()==2,
                  "abbreviation edit menu applies a recognized group");
            check(history.undo(doc)&&doc.atoms.size()==3,"menu abbreviation is undoable");
            selection.clear();
        }

        {
            doc={};selection.clear();history.clear();formBefore=toNative(doc);formAction=12;formDriven=previewObserved=false;
            SetTimer(hwnd,901,10,driveForm);SendMessageW(hwnd,WM_COMMAND,insertBranchArrowsCommand,0);KillTimer(hwnd,901);
            check(formDriven&&previewObserved&&toNative(doc)==formBefore,"branch arrow preview cancels without changes");
            formAction=11;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);SendMessageW(hwnd,WM_COMMAND,insertBranchArrowsCommand,0);KillTimer(hwnd,901);
            check(formDriven&&doc.objects.size()==5&&doc.groups.size()==1,"branch insert dialog creates four arrows on one spine");
            writePreview(L"shared-spine-arrows.png",doc);
            check(history.undo(doc)&&toNative(doc)==formBefore,"branch insertion undoes as one action");
            auto insertMenu=GetSubMenu(GetMenu(hwnd),2);
            check(GetMenuState(insertMenu,insertBranchArrowsCommand,MF_BYCOMMAND)==MF_ENABLED
                &&GetMenuState(insertMenu,externalMechanismCommand,MF_BYCOMMAND)==MF_ENABLED,
                "both branching commands are available in the Insert menu");
            formAction=25;formDriven=false;SetTimer(hwnd,901,10,driveForm);
            SendMessageW(hwnd,WM_COMMAND,externalMechanismCommand,0);KillTimer(hwnd,901);
            check(formDriven&&doc.reactions.size()==1&&doc.reactions.front().links.size()==4,
                "external branching menu command creates a four-branch mechanism");
            check(history.undo(doc)&&toNative(doc)==formBefore,"external branching insertion can be undone");
            doc={};selection.clear();history.clear();targetContainer=0;
            for(Vec p:std::vector<Vec>{{0,0},{240,0},{240,160}}){int a=doc.addAtom(p,27),b=doc.addAtom(p+Vec{22,14},8);doc.atom(a)->label="CoIII";doc.addBond(a,b);}
            Object stem;stem.id=doc.nextId++;stem.points={{60,0},{60,160}};doc.objects.push_back(stem);
            for(double y:{0.,160.}){Object arrow;arrow.id=doc.nextId++;arrow.points={{60,y},{200,y}};arrow.head=1;doc.objects.push_back(arrow);}
            formBefore=toNative(doc);formAction=14;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);command(167);
            check(formDriven&&previewObserved&&toNative(doc)==formBefore,"import arrangement preview cancels without changing original chemistry");
            formAction=13;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);command(167);
            check(formDriven&&doc.reactions.size()==1&&doc.reactions[0].links.size()==2&&doc.reactions[0].links[1].from==0,"import dialog retains a source-connected shared fork");
            check(doc.atoms[0].label=="Co^{III}","import dialog normalizes oxidation state");
            writePreview(L"shared-spine-arranged.png",doc);
            check(history.undo(doc)&&toNative(doc)==formBefore,"import geometry, labels and topology undo together");
            selection={doc.atoms.front().id};
            sidebarPage=1;
            recognizedTableOpened=false;
            SetTimer(hwnd,901,10,driveSidebarRecognition);
            SendMessageW(hwnd,WM_COMMAND,recognizeMechanismCommand,0);
            check(recognizedTableOpened&&sidebarPage==0&&doc.reactions.size()==1&&doc.reactions[0].links.size()==2,
                  "current-file recognition works from the graph sidebar and opens the editable draft preview");
            check(history.undo(doc)&&toNative(doc)==formBefore,"sidebar recognition can be undone in one step");
            selection.clear();targetContainer=0;doc={};history.clear();
            doc.ring({0,0},3);doc.ring({70,0},3);doc.ring({500,200},6);for(size_t i=0;i<6;i++)selection.push_back(doc.atoms[i].id);formBefore=toNative(doc);dirty=false;mechanismGridAction=0;SetTimer(hwnd,901,10,driveNewMechanism);command(includeMechanismCommand);check(mechanismDraftHidden&&toNative(doc)==formBefore&&!dirty&&!history.undo(doc),"selected structure inclusion cancel leaves original and history intact");
            mechanismGridAction=1;SetTimer(hwnd,901,10,driveNewMechanism);SendMessageW(hwnd,WM_COMMAND,includeMechanismCommand,0);check(mechanismDraftHidden&&doc.reactions.size()==1&&doc.reactions.front().slots.size()==2&&dirty,"selected structures open isolated draft and apply as two steps");check(history.undo(doc)&&toNative(doc)==formBefore,"structure incorporation applies in one undo step");selection.clear();doc={};history.clear();
        }

        {
            doc={};Object label;label.id=doc.nextId++;label.kind=ObjectKind::Text;label.points={{30,30}};label.text="H2";doc.objects.push_back(label);
            formAction=0;SetTimer(hwnd,901,10,driveForm);editObject(label.id);check(formDriven&&doc.objects[0].text=="H_{2}\r\nWater","subscript and Alt+Enter work in the actual text properties dialog");
            doc={};int a=doc.addAtom({0,0}),b=doc.addAtom({13,3}),c=doc.addAtom({26,0});doc.addBond(a,b);doc.addBond(b,c);selection={a,b,c};history.clear();dirty=false;
            formBefore=toNative(doc);formAction=1;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);clean();
            check(formDriven&&previewObserved&&toNative(doc)==formBefore,"cleanup previews the chosen rotation and Cancel restores the exact original");
            formAction=2;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);clean();
            check(formDriven&&previewObserved&&toNative(doc)!=formBefore&&history.undo(doc)&&toNative(doc)==formBefore,"cleanup Apply commits once and can be completely undone");
            doc=mechanismDemo();formBefore=toNative(doc);history.clear();formAction=3;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);reactionSettings(doc.reactions[0].id);
            check(formDriven&&previewObserved&&toNative(doc)==formBefore,"serpentine settings preview and cancel restore the document exactly");
            formAction=4;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);reactionSettings(doc.reactions[0].id);
            check(formDriven&&previewObserved&&doc.reactions[0].wrapColumns==2&&doc.reactions[0].rowGap==180,"serpentine settings apply from the real dialog");
            check(history.undo(doc)&&toNative(doc)==formBefore,"serpentine settings undo restores original structures and positions");
            doc={};history.clear();
            auto legacy=mechanismTemplate(MechanismTemplate::SplitAndMerge);
            legacy.id=doc.nextId++;
            legacy.circularMerge=false;
            legacy.slotGrid={{-1,0},{0,0},{0,1},{-1.35,1.65},{-1.35,2.55},{1.45,2.1},{0,3.2}};
            legacy.slots[3].label="Saved intermediate";
            doc.reactions.push_back(legacy);
            formBefore=toNative(doc);
            formAction=26;formDriven=false;SetTimer(hwnd,901,10,driveForm);reactionSettings(legacy.id);
            check(formDriven&&doc.reactions.front().circularMerge&&doc.reactions.front().slots[3].label=="Saved intermediate"&&
                  mechanismLinkArc(doc.reactions.front(),2,doc.style).has_value(),"settings convert an existing oval and keep its stage contents");
            check(history.undo(doc)&&toNative(doc)==formBefore,"converting an existing path is undoable");
            doc=mechanismDemo();for(int i=0;i<4;i++){doc.reactions[0].slots.push_back({"State "+std::to_string(i+4),{},"0.0"});doc.reactions[0].above.push_back("TS / Step");doc.reactions[0].below.push_back("12.3");}
            doc.reactions[0].wrapColumns=3;for(size_t i=0;i<7;i++)if(doc.reactions[0].slots[i].members.empty())fillSlot(doc,demo(),doc.reactions[0].id,i);optimizeMechanism(doc);writePreview(L"mechanism-serpentine.png",doc);
            doc={};EnergyPlot staged;staged.id=doc.nextId++;staged.states={{"A",0},{"B",20},{"C",-10}};staged.series={{"Path 1","#167d9a",{0,20,-10}}};staged.stages=defaultMechanismStages();staged.stages[1].fontSize=24;doc.plots.push_back(staged);
            setInlineValue({staged.id,12,1},"Transition State");check(inlineValue({staged.id,12,1})=="Transition State","plot stage supports direct text editing");writePreview(L"plot-mechanism-stages.png",doc);
            doc={};selection.clear();history.clear();dirty=false;
        }
        {
            doc=mechanismDemo();history.clear();tool=Text;selection={doc.reactions[0].id};fit();
            auto original=toNative(doc);int id=selection[0];
            beginInline(InlineTarget{id,1,0,reactionSlot(doc.reactions[0],0),14},reactionSlot(doc.reactions[0],0));
            Bitmap toolbar(ww,wh,PixelFormat32bppARGB);{Graphics graphics(&toolbar);HDC dc=graphics.GetHDC();paint(dc);graphics.ReleaseHDC(dc);}
            Vec palettePoint;for(auto& h:hits)if(h.id==156)palettePoint={h.box.X+22,h.box.Y+22};
            paletteDriven=false;SetTimer(hwnd,930,10,driveTextPalette);mouseDown(palettePoint);KillTimer(hwnd,930);
            check(paletteDriven&&!inlineEdit&&doc.reactions[0].captionColors.size()==1,"toolbar text palette commits editing and opens the native color picker");
            bool blue=false,black=false;for(auto& item:scene().items)if(item.owner==id&&item.type==Primitive::Text){if(item.textRole==1&&item.textIndex==0)blue=item.color=="#235ae1";if(item.textRole==1&&item.textIndex==1)black=item.color=="#000000";}
            check(blue&&black,"palette changes only the edited stage label");
            auto saved=toNative(doc);for(auto restored:{fromNative(saved),fromCdxml(toCdxml(doc))})check(restored.reactions[0].captionColors[0].color=="#235ae1","caption color survives native and CDXML reopen");
            check(toSvg(scene()).find("#235ae1")!=std::string::npos,"SVG includes the chosen caption color");
            check(history.undo(doc)&&toNative(doc)==original,"text palette can be undone exactly");check(history.redo(doc)&&toNative(doc)==saved,"text palette redo retains chosen caption");
            auto grid=mechanismGridData(doc.reactions[0],mechanismRows(doc.reactions[0]));check(grid.captionColors.size()==1,"table editing preserves caption colors");
            applyTextColor("#009650");for(auto& item:scene().items)if(item.owner==id&&item.type==Primitive::Text)check(item.color=="#009650","selected block text color applies to every caption");
            check(history.undo(doc)&&toNative(doc)==saved,"whole block color can be undone");
            writePreview(L"mechanism-text-colors.png",doc);
            doc={};selection.clear();history.clear();dirty=false;
        }
        {
            doc={};history.clear();selection.clear();tool=Text;dirty=false;
            beginInline({},Vec{40,40});SetWindowTextW(inlineEdit,L"First");SendMessageW(inlineEdit,EM_SETSEL,5,5);
            BYTE beforeAlt[256]{};GetKeyboardState(beforeAlt);BYTE altKeys[256]{};altKeys[VK_MENU]=0x80;SetKeyboardState(altKeys);
            SendMessageW(inlineEdit,WM_SYSKEYDOWN,VK_RETURN,0);SendMessageW(inlineEdit,WM_SYSCHAR,VK_RETURN,0);SetKeyboardState(beforeAlt);
            check(inlineEdit&&GetWindowTextLengthW(inlineEdit)==7,"Alt+Enter keeps inline text editing and inserts one newline");
            SendMessageW(inlineEdit,EM_REPLACESEL,TRUE,(LPARAM)L"Second");BYTE plainKeys[256]{};SetKeyboardState(plainKeys);SendMessageW(inlineEdit,WM_KEYDOWN,VK_RETURN,0);SetKeyboardState(beforeAlt);
            check(!inlineEdit&&doc.objects[0].text=="First\r\nSecond","Enter commits multiline inline text");
            auto saved=toNative(doc);check(fromNative(saved).objects[0].text==doc.objects[0].text,"multiline text survives native save");
            check(history.undo(doc)&&doc.objects.empty(),"multiline text edit undoes in one step");
            doc={};history.clear();selection.clear();tool=Select;dirty=false;
        }
        {
            doc=mechanismDemo();history.clear();selection.clear();tool=Select;fit();
            int id=doc.reactions[0].id;auto count=doc.reactions[0].slots.size();auto before=toNative(doc);
            neutralMechanismClick(mechanismAddPosition(doc.reactions[0]));
            check(doc.reactions[0].slots.size()==count+1&&doc.reactions[0].above.size()==count,"circular plus adds a structure and transition");
            check(history.undo(doc)&&toNative(doc)==before,"circular plus can be undone exactly");
            neutralMechanismClick(mechanismAddPosition(doc.reactions[0],true));
            check(doc.reactions[0].slots.size()==count+1&&doc.reactions[0].slots.back().startsRow,"bottom plus starts a manual row");
            auto lower=reactionSlot(doc.reactions[0],count);
            check(mechanismAddPosition(doc.reactions[0]).x<screen(lower).x,"second row displays the horizontal plus on the left");
            neutralMechanismClick(mechanismAddPosition(doc.reactions[0]));
            check(reactionSlot(doc.reactions[0],count+1).x<lower.x,"left plus extends the second row leftward");
            neutralMechanismClick(mechanismAddPosition(doc.reactions[0],true));
            auto bottom=reactionSlot(doc.reactions[0],count+2);
            check(bottom.y>lower.y&&mechanismAddPosition(doc.reactions[0]).x>screen(bottom).x,"third row displays the plus on the right");
            neutralMechanismClick(mechanismAddPosition(doc.reactions[0]));
            check(reactionSlot(doc.reactions[0],count+3).x>bottom.x,"right plus extends the third row rightward");
            writePreview(L"mechanism-manual-zigzag.png",doc);
            for(int i=0;i<4;i++)check(history.undo(doc),"manual plus click is undoable");
            check(toNative(doc)==before,"undoing manual rows restores the original exactly");
            doc.reactions[0].vertical=true;optimizeMechanism(doc,{id});fit();before=toNative(doc);
            neutralMechanismClick(mechanismAddPosition(doc.reactions[0]));
            check(doc.reactions[0].vertical&&doc.reactions[0].slots.size()==count+1&&reactionSlot(doc.reactions[0],count).y>reactionSlot(doc.reactions[0],count-1).y,"vertical plus extends downward");
            check(history.undo(doc)&&toNative(doc)==before,"vertical plus can be undone exactly");
            writePreview(L"mechanism-vertical.png",doc);
            doc.reactions[0].vertical=false;optimizeMechanism(doc,{id});writePreview(L"mechanism-readable.png",doc);
            doc={};Reaction readable;readable.id=doc.nextId++;readable.slots={{"C",{},"9.6 / 8.8 / 8.8"},{"D",{},"-7.6 / -1.4 / -9.7"},{"E",{},"-23.1 / -30.4 / -40.2"},{"F",{},"-41.1 / -47.0 / -49.3"},{"G",{},"?.? / ?.? / -55.3"}};
            readable.above={"Relaxation","Rearrangement","C-H activation","Product\nRelease"};readable.below={"","8.1 / 7.5 / -4.1","-10.1 / -18.5 / -21.3","?.? / ?.? / -36.1"};doc.reactions.push_back(readable);
            for(size_t i=0;i<readable.slots.size();i++)fillSlot(doc,demo(),readable.id,i);
            writePreview(L"mechanism-readable-default.png",doc);
            doc={};selection.clear();history.clear();dirty=false;
        }
        {
            doc=mechanismDemo();history.clear();selection={doc.reactions[0].id};targetContainer=selection[0];targetSlot=1;targetAbove=false;dirty=false;
            const auto original=doc;const auto bytes=toNative(doc);auto originalRunner=structureEditor;
            bool isolated=false;
            structureEditor=[&](const Document& source)->std::optional<Document>{
                isolated=source.reactions.empty()&&source.plots.empty()&&source.atoms.size()<doc.atoms.size();
                check(toNative(doc)==bytes,"opening isolated editor never mutates parent");
                auto result=source;result.atoms.front().element=9;return result;
            };
            editTargetStructure();check(isolated&&dirty,"structure edit receives only the selected stage");
            auto& current=doc.reactions[0];auto edited=structureInSlot(doc,current.id,1);check(edited.atoms.front().element==9,"applying edit replaces selected structure");
            check(current.spacing==original.reactions[0].spacing&&current.slotWidth==original.reactions[0].slotWidth,"isolated apply preserves mechanism layout");
            for(size_t i:{size_t(0),size_t(2)})for(int id:original.reactions[0].slots[i].members)if(auto atom=original.atom(id))check(doc.atom(id)&&(doc.atom(id)->p-atom->p).length()<1e-9,"other stages keep atom identities and positions");
            check(history.undo(doc)&&toNative(doc)==bytes,"one undo restores complete original mechanism");
            history.clear();dirty=false;structureEditor=[](const Document&)->std::optional<Document>{return {};};editTargetStructure();check(toNative(doc)==bytes&&!dirty&&!history.undo(doc),"cancel leaves parent and undo history unchanged");
            structureEditor=[](const Document& source)->std::optional<Document>{return source;};editTargetStructure();check(toNative(doc)==bytes&&!history.undo(doc),"apply without editing is a no-op");
            structureEditor=originalRunner;doc={};history.clear();selection.clear();targetContainer=0;dirty=false;
        }
        {
            doc=mechanismDemo();makeMechanismGraph(doc.reactions[0]);while(!doc.reactions[0].links.empty())removeMechanismConnection(doc.reactions[0],0);
            history.clear();hits.clear();tool=Select;selection={doc.reactions[0].id};targetContainer=0;selectedCycleOwner=0;selectedCycleArrow=-1;
            origin={left+130.,top+130.};zoom=1;int owner=doc.reactions[0].id;command(145);
            auto source=screen(reactionSlot(doc.reactions[0],0)),destination=screen(reactionSlot(doc.reactions[0],1));
            mouseDown(source);check(mechanismConnectionDrag,"connect mode starts a native frame-to-frame drag");mouse=destination;mouseUp(destination);
            check(doc.reactions[0].links.size()==1&&doc.reactions[0].links[0].from==0&&doc.reactions[0].links[0].to==1,"releasing over target creates a real mechanism connection");
            fit();mechanismConnecting=false;selectedCycleOwner=owner;selectedCycleArrow=0;selection={owner};auto path=mechanismConnectionPath(owner,0);auto head=screen(path.back());destination=screen(reactionSlot(doc.reactions[0],2));
            mouseDown(head);check(mechanismConnectionDrag,"selected arrowhead exposes a draggable reconnection handle");mouse=destination;mouseUp(destination);
            check(doc.reactions[0].links[0].to==2,"dragging the arrowhead to another stage changes its endpoint");
            fit();path=mechanismConnectionPath(owner,0);head=screen(path.back());auto extended=head+Vec{-30,0};mouseDown(head);check(mechanismConnectionDrag,"length drag starts on arrowhead handle");check(!mechanismSlotAt(owner,world(extended)),"length drag ends in empty corridor");mouse=extended;mouseUp(extended);
            check(doc.reactions[0].arrowLength>0,"dragging arrowhead into empty space sets manual arrow length");
            fit();command(145);source=screen(reactionSlot(doc.reactions[0],0));destination=screen(reactionSlot(doc.reactions[0],1));mouseDown(source);mouse=destination;mouseUp(destination);key(VK_ESCAPE);
            check(doc.reactions[0].links.size()==2&&!mechanismConnecting,"second branch is added and Escape exits connection mode");selection={owner};command(125);
            check(doc.reactions[0].slotGrid[1].x==doc.reactions[0].slotGrid[2].x&&doc.reactions[0].slotGrid[1].y!=doc.reactions[0].slotGrid[2].y,"alignment reorganizes the manually connected branch targets");
            writePreview(L"mechanism-manual-connections.png",doc);
            auto before=toNative(doc);selectedCycleOwner=owner;selectedCycleArrow=0;selection={owner};check(deleteSelectedCycleArrow()&&doc.reactions[0].links.size()==1,"Delete removes the selected graph connection");
            check(history.undo(doc)&&toNative(doc)==before,"connection deletion can be undone completely");
            doc={};history.clear();selection.clear();targetContainer=0;selectedCycleOwner=0;selectedCycleArrow=-1;mechanismConnecting=mechanismConnectionDrag=false;dirty=false;
        }
        {
            doc=parallelMechanismFixture(true);history.clear();selection={doc.reactions[0].id};targetContainer=0;auto before=toNative(doc);
            writePreview(L"mechanism-parallel-before.png",doc);command(125);
            auto& r=doc.reactions[0];for(size_t i=4;i<10;i++)check(r.slotGrid[i].y==r.slotGrid[4].y,"native automatic alignment keeps the lower path horizontal across unequal pathway lengths");
            writePreview(L"mechanism-parallel-after.png",doc);check(history.undo(doc)&&toNative(doc)==before,"parallel pathway alignment is exactly undoable");
            doc={};history.clear();selection.clear();dirty=false;
        }
        if(argc>1&&std::string(argv[1])=="--live-chemdraw"){
            auto fragment=clipboardDocument();check(fragment&&fragment->atoms.size()==15&&fragment->bonds.size()==16,"actual Windows ChemDraw clipboard imports all atoms and bonds");
            doc={};history.clear();selection.clear();pasteStructure(*fragment);
            check(doc.atoms.size()==15&&doc.bonds.size()==16,"actual ChemDraw clipboard pastes as editable canvas objects");
            doc={};Reaction r;r.id=doc.nextId++;r.slots={{"R",{}},{"P",{}}};doc.reactions.push_back(r);
            selection={r.id};targetContainer=r.id;targetSlot=1;targetAbove=false;history.clear();auto before=toNative(doc);
            fillTarget(false);check(doc.atoms.size()==15&&doc.bonds.size()==16&&doc.reactions[0].slots[1].members.size()==31,"structure paste button accepts the actual ChemDraw clipboard in a mechanism slot");
            check(history.undo(doc)&&toNative(doc)==before,"ChemDraw clipboard insertion is undoable");
            doc={};history.clear();selection.clear();targetContainer=0;dirty=false;
            std::cout<<"Live ChemDraw clipboard paste passed\n";
        }
        {
            doc={};selection.clear();history.clear();
            auto mixed=clipboardStructureText(R"(<CDXML LineWidth="0.67" BondLength="20.34" BondSpacing="12" LabelSize="6.75">
                <page id="1"><fragment id="2" MarginWidth="0.80"><n id="3" p="0 0"/><n id="4" p="10.97 0" Element="8"/>
                <b id="5" B="3" E="4" Order="2" LineWidth="1" BoldWidth="1.02" HashSpacing="1.26"/>
                </fragment></page></CDXML>)");
            check(mixed.has_value(),"mixed ChemDraw drawing styles decode from clipboard text");
            pasteStructure(*mixed);
            auto reopened=fromNative(toNative(doc));
            check(reopened.style.lineWidth==2&&reopened.atoms[1].labelSize==6.75&&reopened.atoms[1].labelMargin==.8,
                  "canvas paste preserves the source label style across saves");
            check(reopened.bonds[0].boldWidth==1.02&&reopened.bonds[0].hashSpacing==1.26,
                  "local bold width and hash spacing survive paste and save independently");
            std::vector<Primitive> strokes;
            for(auto& item:makeScene(reopened).items)if(item.owner==reopened.bonds[0].id)strokes.push_back(item);
            check(strokes.size()==2&&strokes[0].width==1&&strokes[1].width==1&&
                  std::abs(std::abs(strokes[0].points[0].y-strokes[1].points[0].y)-20.34*.12)<1e-6,
                  "canvas paste preserves bond stroke width and double-bond gap");
            auto source=demo();auto native=clipboardStructureText(toNative(source));
            check(native&&native->atoms.size()==source.atoms.size(),"native clipboard structure is editable");
            std::string xml=R"(<CDXML><page id="1"><fragment id="2"><n id="3" p="0 0" Element="6"/><n id="4" p="30 0" Element="8"/><b id="5" B="3" E="4" Order="1"/></fragment></page></CDXML>)";
            auto external=clipboardStructureText(xml);check(external&&external->atoms.size()==2&&external->bonds.size()==1,"external CDXML clipboard data imports as atoms and bonds");
            check(!clipboardStructureText("ordinary text")&&!clipboardStructureText("<CDXML>broken"),"nonstructure or malformed clipboard content is ignored");
            auto wide=ui::wide(xml);HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(wide.size()+1)*sizeof(wchar_t));auto* data=GlobalLock(memory);memcpy(data,wide.c_str(),(wide.size()+1)*sizeof(wchar_t));GlobalUnlock(memory);
            auto decoded=clipboardStructureText(clipboardMemoryText(memory,true));GlobalFree(memory);check(decoded&&decoded->atoms.size()==2,"Unicode XML clipboard payload decodes correctly");
            doc={};Reaction r;r.id=doc.nextId++;r.slots={{"R",{}},{"P",{}}};doc.reactions.push_back(r);selection={r.id};targetContainer=r.id;targetSlot=1;targetAbove=false;history.clear();
            auto before=toNative(doc);pasteStructure(*native);
            check(doc.reactions[0].slots[0].members.empty()&&!doc.reactions[0].slots[1].members.empty()&&doc.atoms.size()==source.atoms.size(),"paste fills only the selected mechanism structure slot");
            auto pasted=doc.subset(doc.reactions[0].slots[1].members);auto box=makeScene(pasted).bounds;auto anchor=reactionSlot(doc.reactions[0],1);
            check(std::abs(box.x+box.w/2-anchor.x)<1e-6&&std::abs(box.y+box.h/2-anchor.y)<1e-6,"clipboard structure uses the same slot centering as file insertion");
            check(history.undo(doc)&&toNative(doc)==before,"slot paste can be completely undone");
            doc={};EnergyPlot p;p.id=doc.nextId++;p.states={{"R",0,{},{}},{"P",-10,{},{}}};doc.plots.push_back(p);selection={p.id};targetContainer=p.id;targetSlot=0;targetAbove=true;history.clear();
            pasteStructure(*external);check(!doc.plots[0].states[0].above.empty()&&doc.plots[0].states[0].below.empty()&&doc.plots[0].states[1].above.empty(),"paste targets the selected graph state above its energy level");
            targetSlot=1;targetAbove=false;pasteStructure(*external);check(!doc.plots[0].states[1].below.empty(),"graph lower insertion slot also accepts clipboard structures");
            {
                auto scaled=doc;auto members=scaled.plots[0].states[0].above;auto originalPoint=scaled.atom(members[0])->p;auto oldAnchor=plotAnchor(scaled.plots[0],0,true);
                auto lowerMembers=scaled.plots[0].states[1].below;int lowerA=lowerMembers[0],lowerB=lowerMembers[1];double lowerDistance=(scaled.atom(lowerA)->p-scaled.atom(lowerB)->p).length();
                resizeStructure(scaled,members,1.5,p.id,0,true);auto newAnchor=plotAnchor(scaled.plots[0],0,true);
                check((scaled.atom(members[0])->p-newAnchor-(originalPoint-oldAnchor)*1.5).length()<1e-6,"resizing a graph slot keeps its structure at the updated insertion anchor");
                check(std::abs((scaled.atom(lowerA)->p-scaled.atom(lowerB)->p).length()-lowerDistance)<1e-6,"slot resize does not scale another state structure");
                auto unchanged=toNative(scaled);resizeStructure(scaled,members,1,p.id,0,true);check(toNative(scaled)==unchanged,"100 percent resize is a true no-op");
            }
            selection.clear();auto membership=doc.plots[0].states[1].below;size_t count=doc.atoms.size();pasteStructure(*external);
            check(doc.atoms.size()==count+2&&doc.plots[0].states[1].below==membership,"ordinary canvas paste does not use an inactive insertion target");
            targetContainer=999999;before=toNative(doc);bool failed=false;try{insertStructureTarget(*external);}catch(...){failed=true;}
            check(failed&&toNative(doc)==before,"invalid insertion target leaves the document unchanged");
            doc={};history.clear();selection.clear();targetContainer=0;targetSlot=0;targetAbove=false;dirty=false;
        }
        const auto firstPath=fs::absolute(L"tab-first.cdxml").wstring(),secondPath=fs::absolute(L"tab-second.cdxml").wstring();
        doc={};history.clear();doc.addAtom({10,20},6);history.checkpoint(doc);doc.addAtom({40,20},8);
        selection={doc.atoms[1].id};filePath=firstPath;dirty=true;zoom=2.75;origin={33,44};tool=Double;targetContainer=17;targetSlot=2;targetAbove=true;
        newDocumentTab();check(documents.size()==2&&doc.atoms.empty()&&!dirty,"new tab preserves an unsaved document");
        doc.addAtom({1,2},7);filePath=secondPath;dirty=true;zoom=1.25;origin={90,100};history.checkpoint(doc);doc.addAtom({3,4},16);
        TabCtrl_SetCurSel(documentTabs,0);NMHDR notification{documentTabs,950,TCN_SELCHANGE};SendMessageW(hwnd,WM_NOTIFY,950,(LPARAM)&notification);
        check(activeDocument==0,"native tab selection switches the active document");
        check(doc.atoms.size()==2&&doc.atoms[1].element==8&&filePath==firstPath&&dirty,"first tab retains document, path, and modified state");
        check(zoom==2.75&&origin.x==33&&selection.size()==1&&tool==Double&&targetContainer==17&&targetSlot==2&&targetAbove,"view, tool, selection, and insertion target remain per document");
        check(history.undo(doc)&&doc.atoms.size()==1&&doc.atoms[0].element==6,"first tab has its own undo history");
        check(save()&&!dirty,"save first tab");
        switchDocumentTab(1);check(dirty&&doc.atoms.size()==2&&doc.atoms[0].element==7&&filePath==secondPath&&zoom==1.25,"saving first tab does not overwrite or clear second tab");
        check(history.undo(doc)&&doc.atoms.size()==1&&doc.atoms[0].element==7,"second tab has independent undo history");check(save(),"save second tab");
        check(fromCdxml(readFile(firstPath)).atoms[0].element==6&&fromCdxml(readFile(secondPath)).atoms[0].element==7,"both files save their own content");
        {
            auto saved=doc;doc=mechanismDemo();auto rows=mechanismRows(doc.reactions[0]);rows[1][1]="TS1 / Nucleophilic attack";rows[1][2]="0 / 1 / 2";rows[1][3]="0.0 / 8.7 / 13.4";rows[0][2]="0 / 2";rows[0][3]="-3.0 / 4.4";
            doc.reactions[0]=mechanismGridData(doc.reactions[0],rows);
            int reactionId=doc.reactions[0].id;
            auto previousTool=tool;tool=Text;
            auto oldName=doc.reactions[0].slots[0].label;beginInline(InlineTarget{reactionId,1,0,reactionSlot(doc.reactions[0],0),14},{});
            SetWindowTextW(inlineEdit,L"H2");SendMessageW(inlineEdit,EM_SETSEL,1,2);
            BYTE previousKeys[256]{};GetKeyboardState(previousKeys);BYTE scriptKeys[256];memcpy(scriptKeys,previousKeys,sizeof(scriptKeys));scriptKeys[VK_CONTROL]=0x80;scriptKeys[VK_SHIFT]=0;SetKeyboardState(scriptKeys);
            SendMessageW(inlineEdit,WM_KEYDOWN,VK_OEM_PLUS,0);SetKeyboardState(previousKeys);check(finishInline(true)&&doc.reactions[0].slots[0].label=="H_{2}","Word subscript shortcut applies to inline mechanism text");doc.reactions[0].slots[0].label=oldName;
            tool=previousTool;
            check(inlineValue({reactionId,11,0})=="TS1"&&inlineValue({reactionId,2,0})=="Nucleophilic attack","inline editing reads the name and description independently");
            setInlineValue({reactionId,11,0},"TS1 edited");check(transitionCaption(doc.reactions[0].above[0]).description=="Nucleophilic attack","editing TS name preserves arrow description");
            setInlineValue({reactionId,2,0},"Attack");check(transitionCaption(doc.reactions[0].above[0]).name=="TS1 edited","editing arrow description preserves TS name");
            doc.reactions[0].above[0]="TS1 / Nucleophilic attack";optimizeMechanism(doc);fit();
            Bitmap snapshot(ww,wh,PixelFormat32bppARGB);Graphics graphics(&snapshot);HDC dc=graphics.GetHDC();paint(dc);
            int state=SaveDC(dc);SetViewportOrgEx(dc,left,89,nullptr);SendMessageW(documentTabs,WM_PRINT,(WPARAM)dc,PRF_CLIENT|PRF_NONCLIENT|PRF_CHILDREN);RestoreDC(dc,state);graphics.ReleaseHDC(dc);
            CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
            check(snapshot.Save(L"document-tabs-preview.png",&png,nullptr)==Ok,"render document tab preview");
            auto previewDocument=doc;Document balanced;Reaction example;example.id=balanced.nextId++;example.slots={{"A",{}},{"C",{}},{"E",{}},{"G",{}},{"I",{}}};
            example.above={"B / Ring Opening","D / C–C bond cleavage","F / C–OH bond formation","H / 1,2-hydrogen atom transfer"};example.below={"?.? / 8.7 / 13.4","?.? / 17.1 / 22.5","?.? / 11.1 / 18.4","?.? / 11.1 / 18.4"};for(auto& slot:example.slots)slot.energy="-36.0 / ?.? / 22.3";balanced.reactions.push_back(example);
            auto source=demo();if(fs::exists(L"../chemdraw-clipboard.cdx"))source=fromCdx(readFile(L"../chemdraw-clipboard.cdx"));
            for(size_t i=0;i<5;i++)fillSlot(balanced,source,example.id,i);doc=balanced;fit();dc=graphics.GetHDC();paint(dc);graphics.ReleaseHDC(dc);
            check(snapshot.Save(L"mechanism-layout-preview.png",&png,nullptr)==Ok,"render five-stage mechanism with ChemDraw structures and readable captions");
            auto picture=scene();auto bounds=picture.bounds;Bitmap output(1800,420,PixelFormat32bppARGB);Graphics outputGraphics(&output);outputGraphics.Clear(Color(255,255,255,255));
            double outputScale=std::min(1740./bounds.w,360./bounds.h);outputGraphics.TranslateTransform(float(30-bounds.x*outputScale),float(210-(bounds.y+bounds.h/2)*outputScale));outputGraphics.ScaleTransform(float(outputScale),float(outputScale));render(outputGraphics,picture);
            check(output.Save(L"mechanism-balanced-export.png",&png,nullptr)==Ok,"render clean balanced-mechanism export");writeFile(L"mechanism-balanced-export.svg",toSvg(picture));doc=previewDocument;

            auto savedCycleDocument=doc;doc=balanced;optimizeMechanism(doc,{},true);fit();
            doc.reactions[0].captionFontSize=24;optimizeMechanism(doc,{},true);fit();
            writePreview(L"cycle-layout-preview.png",doc);writeFile(L"cycle-layout-preview.svg",toSvg(scene()));
            auto cycleBefore=toNative(doc);int cycleOwner=doc.reactions[0].id;Primitive picked;
            for(auto& item:scene().items)if(item.type==Primitive::Bezier&&item.reactionArrow==0){picked=item;break;}
            check(picked.points.size()==4,"cycle connector exposes selectable geometry");
            Vec onArc=(picked.points[0]+picked.points[3]+(picked.points[1]+picked.points[2])*3)*.125;
            auto hit=cycleArrowAt(onArc);check(hit&&hit->first==cycleOwner&&hit->second==0,"cycle arrow is hit tested along its curve");
            tool=Select;mouseDown(screen(onArc));check(selectedCycleArrow==0&&selectedCycleOwner==cycleOwner,"click selects just the cycle connector");
            key(VK_DELETE);check(doc.reactions.size()==1&&doc.reactions[0].hiddenArrows==std::vector<int>{0},"Delete preserves structures and removes only the picked connector");
            auto openedCycle=fromNative(toNative(doc));check(openedCycle.reactions[0].hiddenArrows==std::vector<int>{0},"arrow zero omission survives save and reopen");
            command(104);check(toNative(doc)==cycleBefore,"undo restores deleted cycle arrow");
            command(105);check(doc.reactions[0].hiddenArrows==std::vector<int>{0},"redo removes the same cycle arrow");
            doc=savedCycleDocument;selectedCycleArrow=-1;selectedCycleOwner=0;selection.clear();fit();
            selection={reactionId};auto atomCount=doc.atoms.size();
            for(int i=0;i<7;i++){
                command(122);
                auto bounds=scene().bounds;auto first=screen({bounds.x,bounds.y}),last=screen({bounds.x+bounds.w,bounds.y+bounds.h});
                check(first.x>=left&&last.x<=ww-right&&first.y>=top&&last.y<=wh-bottom,"toolbar stage addition fits the expanded mechanism inside the canvas");
            }
            check(doc.reactions[0].slots.size()==10&&doc.atoms.size()==atomCount,"toolbar adds more than five stages without losing structures");
            check(history.undo(doc)&&doc.reactions[0].slots.size()==9,"stage addition remains undoable");doc=std::move(saved);dirty=false;
        }
        {
            auto saved=doc;auto savedHistory=history;auto savedZoom=zoom;auto savedOrigin=origin;auto savedSelection=selection;auto savedDirty=dirty;
            doc=coordinateDemo();fit();hits.clear();history=History{};int id=doc.plots[0].id;
            auto points=scene().plotSnapPoints.at(id);check(points.size()>=6,"canvas exposes plateau snap points");
            selection={id};formAction=9;formBefore=toNative(doc);previewObserved=false;formDriven=false;SetTimer(win,994,20,driveForm);editPlotColors(id);
            check(formDriven&&previewObserved&&doc.plots[0].series[0].levelColors[1]=="#cc2255"&&doc.plots[0].series[0].segmentColors[1]=="#cc2255","range color form previews and commits selected levels and connectors");
            check(history.undo(doc)&&toNative(doc)==formBefore,"range color edit is undoable");
            formAction=10;formBefore=toNative(doc);previewObserved=false;formDriven=false;SetTimer(win,995,20,driveForm);editPlotColors(id);
            check(formDriven&&previewObserved&&toNative(doc)==formBefore,"cancel restores colors after preview");
            tool=Select;hits.clear();
            auto point=std::find_if(points.begin(),points.end(),[](const auto& s){return s.state==2&&s.edge==0;});
            check(point!=points.end(),"interior level has a drag handle");
            const auto at=screen(point->p);
            const auto graphBefore=toNative(doc);
            const double previousEnergy=doc.plots[0].series[0].values[2];
            mouseDown(at);mouseUp(at+Vec{0,-12});
            check(doc.plots[0].series[0].values[2]>previousEnergy&&!doc.plots[0].yMax,
                  "vertical drag updates the energy and restores automatic axis mode");
            check(history.undo(doc)&&toNative(doc)==graphBefore,"level height drag is undoable");

            BYTE savedKeys[256]{},altKeys[256]{};
            GetKeyboardState(savedKeys);memcpy(altKeys,savedKeys,sizeof(altKeys));altKeys[VK_MENU]=0x80;
            SetKeyboardState(altKeys);mouseDown(at);mouseUp(at+Vec{15,0});SetKeyboardState(savedKeys);
            check(doc.plots[0].states[2].position&&*doc.plots[0].states[2].position>.5,
                  "Alt-drag moves a graph state on the horizontal axis");
            check(history.undo(doc)&&toNative(doc)==graphBefore,"horizontal level drag is undoable");

            doc=mechanismDemo();fit();hits.clear();
            const auto mechanism=toNative(doc);
            const auto beforeSlot=reactionSlot(doc.reactions[0],1);
            const auto beforeOther=reactionSlot(doc.reactions[0],0);
            const int member=doc.reactions[0].slots[1].members.front();
            const auto beforeAtom=doc.atom(member)->p;
            const auto handle=screen(beforeSlot);
            SetKeyboardState(altKeys);mouseDown(handle);mouseUp(handle+Vec{30,24});SetKeyboardState(savedKeys);
            const auto displacement=reactionSlot(doc.reactions[0],1)-beforeSlot;
            check(doc.reactions[0].fixedFrames&&displacement.length()>1&&
                  (doc.atom(member)->p-beforeAtom-displacement).length()<1e-6&&
                  (reactionSlot(doc.reactions[0],0)-beforeOther).length()<1e-6,
                  "Alt-drag moves the stage and its structure without shifting neighboring stages");
            check(history.undo(doc)&&toNative(doc)==mechanism,"manual stage placement is undoable");
            doc=fromNative(graphBefore);fit();hits.clear();
            snapPlot=id;snapStage=-1;snapLabel={"Spin crossing",0,100};snapStart.reset();
            auto before=toNative(doc);auto first=points.front(),last=points.back();
            mouseDown(screen(last.p));check(snapStart.has_value()&&toNative(doc)==before,"first snap click is nonmutating");
            sidebarPage=1;mouse=screen(first.p);
            {Bitmap snapshot(ww,wh,PixelFormat32bppARGB);Graphics graphics(&snapshot);HDC dc=graphics.GetHDC();paint(dc);graphics.ReleaseHDC(dc);
            CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};snapshot.Save(L"graph-snap-preview.png",&png,nullptr);}
            mouseDown(screen(first.p));check(!snapPlot&&!snapStart&&doc.plots[0].stages.back().label=="Spin crossing","reverse-order clicks create mechanism stage");
            check(std::abs(doc.plots[0].stages.back().start-first.percent)<1e-8&&std::abs(doc.plots[0].stages.back().end-last.percent)<1e-8,"stage arrow projects exactly to clicked plateau endpoints");
            check(history.undo(doc)&&toNative(doc)==before,"snap placement is one undoable action");
            snapPlot=id;snapStart=first;key(VK_ESCAPE);check(!snapPlot&&!snapStart&&toNative(doc)==before,"Esc cancels pending snap without changing the document");
            doc=std::move(saved);history=std::move(savedHistory);zoom=savedZoom;origin=savedOrigin;selection=savedSelection;dirty=savedDirty;
        }
        check(closeDocumentTab(1)&&documents.size()==1&&doc.atoms[0].element==6,"closing a tab restores its neighbor");
        check(closeDocumentTab(0)&&documents.size()==1&&doc.atoms.empty()&&!dirty,"closing last tab leaves a clean empty document");
        check((GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_ACCEPTFILES)!=0,"editor accepts shell file drops");
        const auto nativePath=fs::absolute(L"드롭 테스트.ydraw").wstring();
        Document native;native.addAtom({0,0},8);
        writeFile(nativePath,toNative(native));
        doc.addAtom({0,0},16);dirty=true;
        dropFiles(hwnd,{firstPath,nativePath});
        check(documents.size()==3&&filePath==nativePath&&doc.atoms.size()==1&&doc.atoms[0].element==8,
              "one file drop opens CDXML and Unicode-named native documents in separate tabs");
        check(documents[0].dirty&&documents[0].document.atoms[0].element==16,
              "dropping files preserves the unsaved document");
        dropFiles(hwnd,{firstPath});
        check(documents.size()==3&&filePath==firstPath&&doc.atoms.size()==1&&doc.atoms[0].element==6,
              "dropping an open document activates its existing tab");
        fs::remove(nativePath);
        fs::remove(firstPath);fs::remove(secondPath);DestroyWindow(hwnd);
        std::cout<<"Document tab regression tests passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return result;
}

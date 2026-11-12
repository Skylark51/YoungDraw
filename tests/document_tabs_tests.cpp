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
    const auto grid=FindWindowW(L"YoungDrawGrid",L"에너지 그래프 — 데이터 표");
    if(!grid||GetWindow(grid,GW_OWNER)!=parent)return;
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
        const auto dialog=FindWindowW(L"YoungDrawForm",L"경로 이름 변경");
        if(dialog&&GetWindow(dialog,GW_OWNER)==grid){
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
    PostMessageW(grid,WM_COMMAND,22,0);
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
    }else if(std::wstring(name)==L"YoungDrawForm"){
        KillTimer(parent,timer);
        chemistryMenuOpened=true;
        PostMessageW(dialog,WM_COMMAND,IDOK,0);
    }
}
static int mechanismGridAction=0;
static int mechanismLayoutChoice=0;
static bool mechanismDraftHidden=false;
static void CALLBACK driveNewMechanism(HWND parent,UINT,UINT_PTR timer,DWORD){
    HWND dialog=GetLastActivePopup(parent);
    wchar_t name[64]{};
    GetClassNameW(dialog,name,64);
    if(std::wstring(name)==L"YoungDrawForm"){
        wchar_t title[64]{};
        GetWindowTextW(dialog,title,64);
        if(std::wstring(title)==L"원형 분기·합류 단계 수"){
            SetDlgItemTextW(dialog,100,L"4");
            SetDlgItemTextW(dialog,101,L"1");
            SetDlgItemTextW(dialog,102,L"3");
        }else SendDlgItemMessageW(dialog,100,CB_SETCURSEL,mechanismLayoutChoice,0);
        PostMessageW(dialog,WM_COMMAND,IDOK,0);
        return;
    }
    if(std::wstring(name)!=L"YoungDrawGrid")return;
    KillTimer(parent,timer);
    mechanismDraftHidden=toNative(doc)==formBefore;
    if(mechanismGridAction)PostMessageW(dialog,WM_COMMAND,22,0);
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
                check(buttons.size()==r.links.size()&&buttons[edge].first==edge,"each connection has an insertion button");
                mouseDown(buttons[edge].second);
                const auto& added=doc.reactions.front();
                check(added.slots.size()==r.slots.size()+1&&added.links[edge].to==int(r.slots.size())&&
                      added.links.back().to==r.links[edge].to,"clicking a canvas plus splits exactly its connection");
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
    auto table=FindWindowW(L"YoungDrawGrid",L"메커니즘 분기·프레임 편집");
    if(table&&GetWindow(table,GW_OWNER)==parent){
        KillTimer(parent,timer);
        recognizedTableOpened=true;
        PostMessageW(table,WM_CLOSE,0,0);
        return;
    }
    auto form=FindWindowW(L"YoungDrawForm",L"화살표 사이 구조 묶음 정리 · 미리보기");
    if(form&&GetWindow(form,GW_OWNER)==parent)PostMessageW(form,WM_COMMAND,IDOK,0);
}
static void CALLBACK driveTextPalette(HWND parent,UINT,UINT_PTR timer,DWORD){
    HWND dialog=GetLastActivePopup(parent);wchar_t name[64]{};GetClassNameW(dialog,name,64);if(std::wstring(name)!=L"#32770")return;
    KillTimer(parent,timer);paletteDriven=true;
    SendMessageW(dialog,RegisterWindowMessageW(L"commdlg_SetRGBColor"),0,RGB(35,90,225));
    PostMessageW(dialog,WM_COMMAND,IDOK,0);
}
static void CALLBACK driveForm(HWND parent,UINT,UINT_PTR timer,DWORD){
    HWND dialog=GetLastActivePopup(parent);wchar_t name[64]{};GetClassNameW(dialog,name,64);if(std::wstring(name)!=L"YoungDrawForm")return;
    KillTimer(parent,timer);formDriven=true;
    if(formAction>=20&&formAction<=24){
        if(formAction<=22){SendDlgItemMessageW(dialog,101,CB_SETCURSEL,formAction==22?0:1,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(101,CBN_SELCHANGE),(LPARAM)GetDlgItem(dialog,101));}
        else if(formAction==23){SetDlgItemTextW(dialog,101,L"kJ/mol");SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(101,CBN_EDITCHANGE),(LPARAM)GetDlgItem(dialog,101));}
        previewObserved=toNative(doc)!=formBefore;
    }else if(formAction==18||formAction==19){
        SendDlgItemMessageW(dialog,117,CB_SETCURSEL,1,0);
        SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(117,CBN_SELCHANGE),(LPARAM)GetDlgItem(dialog,117));previewObserved=toNative(doc)!=formBefore;
    }else if(formAction>=15&&formAction<=17){
        SendDlgItemMessageW(dialog,116,CB_SETCURSEL,formAction==17?0:1,0);
        SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(116,CBN_SELCHANGE),(LPARAM)GetDlgItem(dialog,116));previewObserved=toNative(doc)!=formBefore;
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
    SendMessageW(dialog,WM_COMMAND,(formAction==1||formAction==3||formAction==6||formAction==10||formAction==12||formAction==14||formAction==15||formAction==18||formAction==20)?IDCANCEL:IDOK,0);
}
static void graphUnitChecks(HWND hwnd){
    doc=coordinateDemo();doc.plots[0].unit="kcal/mol";history.clear();selection={doc.plots[0].id};dirty=false;int owner=selection[0];
    auto before=toNative(doc);formBefore=before;formAction=20;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);plotSettings(owner);
    check(formDriven&&previewObserved&&toNative(doc)==before,"unit selection previews immediately and Cancel restores the graph");
    formAction=21;formDriven=false;SetTimer(hwnd,901,10,driveForm);plotSettings(owner);
    check(formDriven&&doc.plots[0].unit=="kcal mol^{-1}","inverse-mole choice stores superscript formatting");
    bool raised=false;for(const auto& text:makeScene(doc,measure).items)if(text.rotation==-90&&text.text=="-1")raised=true;
    check(raised,"energy axis renders minus one as a separate raised text run");
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
    doc={};history.clear();selection.clear();dirty=false;
    std::cout<<"Graph energy unit selection, preview, cancellation, undo and export checks passed\n";
}
int main(int argc,char** argv){
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
        graphUnitChecks(hwnd);
        if(argc>1&&std::string(argv[1])=="--plot-units-only"){
            DestroyWindow(hwnd);imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return 0;
        }
        checkNewMechanism(hwnd);
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
            BYTE previous[256]{};GetKeyboardState(previous);BYTE keys[256];memcpy(keys,previous,sizeof(keys));keys[VK_CONTROL]=0x80;keys[VK_SHIFT]=0;SetKeyboardState(keys);key('G');SetKeyboardState(previous);
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
            selection={a};memcpy(keys,previous,sizeof(keys));keys[VK_CONTROL]=keys[VK_SHIFT]=0x80;SetKeyboardState(keys);key('G');SetKeyboardState(previous);
            check(doc.groups.empty(),"Ctrl+Shift+G ungroups a selected member");check(history.undo(doc)&&doc.groups.size()==1,"ungroup can be undone");
            check(history.undo(doc)&&doc.groups.empty(),"group creation can be undone");
            selection.clear();dirty=false;command(198);check(doc.groups.empty()&&!dirty,"empty grouping does not dirty the document");
            doc=originalDoc;history=originalHistory;selection=originalSelection;origin=originalOrigin;zoom=originalZoom;dirty=originalDirty;tool=Select;title();
        }
        {
            auto originalDoc=doc;auto originalHistory=history;auto originalSelection=selection;bool originalDirty=dirty;auto originalOrigin=origin;double originalZoom=zoom;
            // Hosted Windows desktops may cap the native window below the width
            // required by these full-toolbar layout assertions.
            const int nativeWidth=ww,nativeHeight=wh;
            ww=std::max(1440,ww);wh=std::max(900,wh);
            Bitmap snapshot(ww,wh,PixelFormat32bppARGB);Graphics graphics(&snapshot);
            auto repaint=[&](){HDC dc=graphics.GetHDC();paint(dc);graphics.ReleaseHDC(dc);};
            auto hit=[&](int id)->RectF{for(auto& h:hits)if(h.id==id)return h.box;return {};};
            auto present=[&](int id){return hit(id).Width>0;};
            sidebarPage=0;showLayers=false;repaint();
            check(present(140)&&present(143)&&present(144)&&present(145)&&present(146)&&present(147)&&present(149)&&!present(122)&&!present(126)&&!present(148)&&!present(141),"mechanism sidebar consolidates stage addition, table editing and layout controls");
            check(hit(recognizeMechanismCommand).Y<hit(120).Y&&hit(120).Y==hit(121).Y&&hit(121).Y<hit(143).Y&&hit(145).Y<hit(127).Y&&hit(127).Y<hit(134).Y,"mechanism sidebar follows creation, editing, layout and structure insertion");
            const auto structureRow=hit(134).Y;
            for(int id:{100,101,102,104,105,107,125,155,156,170,171,172,173,174,160,161,162,163,164}){
                check(present(id)&&hit(id).Y==hit(100).Y&&hit(id).Width==44&&hit(id).GetBottom()<89,"icon toolbar fits one row above document tabs");
                check(!helpText(id).empty(),"each toolbar icon has Korean help");
            }
            check(hit(156).X==hit(155).X+48,"text palette is immediately right of atom palette");
            check(hit(bondAnimationCommand).X==hit(164).X+64&&hit(bondAnimationCommand).Width==hit(164).Width&&
                  hit(bondAnimationCommand).Height==hit(164).Height,"GIF icon matches the rotation controls after its separator");
            check(hit(164).GetRight()<1200,"all toolbar groups fit minimum window width");
            check(present(218)&&present(219)&&hit(150).GetBottom()<=wh-bottom,"orbital tools and style button fit the client height");
            ww=nativeWidth;wh=nativeHeight;repaint();
            auto before=toNative(doc);command(183);repaint();
            check(present(137)&&present(139)&&present(138)&&present(141)&&present(151)&&!present(127)&&!present(126),"graph tab groups graph-only actions, including spin crossings");
            check(hit(151).Y==hit(137).Y&&!helpText(151).empty(),"spin crossing control fits in the graph tools row and has help");
            check(hit(recognizeMechanismCommand).Y<hit(130).Y&&hit(130).Y==hit(131).Y&&hit(131).Y<hit(137).Y&&hit(137).Y<hit(134).Y&&hit(186).Y==hit(138).Y&&hit(186).X<hit(138).X&&hit(134).Y==structureRow,"graph sidebar puts publication formatting last and shares the structure insertion position");
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
            doc={};history.clear();selection.clear();dirty=false;
        }
        {
            doc=coordinateDemo();history.clear();selection={doc.plots[0].id};dirty=false;
            doc.plots[0].series[0].values[0]=.123456789012345;
            const auto before=doc;
            pathNameStep=pathNameTicks=0;pathNameHeaderUpdated=false;
            SetTimer(hwnd,901,10,drivePathName);
            command(131);
            KillTimer(hwnd,901);
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

        {
            doc={};history.clear();selection.clear();hoverBond=0;tool=Select;
            const int a=doc.addAtom({20,20}),b=doc.addAtom({70,20});doc.addBond(a,b);hoverAtom=a;
            const auto carbonVisible=[&](const Document& drawing){
                for(const auto& item:makeScene(drawing).items)
                    if(item.owner==a&&item.type==Primitive::Text&&item.text=="C")return true;
                return false;
            };
            check(!carbonVisible(doc),"ordinary bonded carbon starts with its symbol implicit");
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
                  "current-file recognition works from the graph sidebar and opens the editable mechanism table");
            check(history.undo(doc)&&toNative(doc)==formBefore,"sidebar recognition can be undone in one step");
            selection.clear();targetContainer=0;doc={};history.clear();
        }

        {
            doc={};Object label;label.id=doc.nextId++;label.kind=ObjectKind::Text;label.points={{30,30}};label.text="H2";doc.objects.push_back(label);
            formAction=0;SetTimer(hwnd,901,10,driveForm);editObject(label.id);check(formDriven&&doc.objects[0].text=="H_{2}\r\nWater","subscript and Alt+Enter work in the actual text properties dialog");
            doc={};int a=doc.addAtom({0,0}),b=doc.addAtom({13,3}),c=doc.addAtom({26,0});doc.addBond(a,b);doc.addBond(b,c);selection={a};history.clear();dirty=false;
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
            BYTE beforeAlt[256]{};GetKeyboardState(beforeAlt);BYTE altKeys[256];memcpy(altKeys,beforeAlt,sizeof(altKeys));altKeys[VK_MENU]=0x80;SetKeyboardState(altKeys);
            SendMessageW(inlineEdit,WM_SYSKEYDOWN,VK_RETURN,0);SendMessageW(inlineEdit,WM_SYSCHAR,VK_RETURN,0);SetKeyboardState(beforeAlt);
            check(inlineEdit&&GetWindowTextLengthW(inlineEdit)==7,"Alt+Enter keeps inline text editing and inserts one newline");
            SendMessageW(inlineEdit,EM_REPLACESEL,TRUE,(LPARAM)L"Second");SendMessageW(inlineEdit,WM_KEYDOWN,VK_RETURN,0);
            check(!inlineEdit&&doc.objects[0].text=="First\r\nSecond","Enter commits multiline inline text");
            auto saved=toNative(doc);check(fromNative(saved).objects[0].text==doc.objects[0].text,"multiline text survives native save");
            check(history.undo(doc)&&doc.objects.empty(),"multiline text edit undoes in one step");
            doc={};history.clear();selection.clear();tool=Select;dirty=false;
        }
        {
            doc=mechanismDemo();history.clear();selection.clear();tool=Select;fit();
            int id=doc.reactions[0].id;auto count=doc.reactions[0].slots.size();auto before=toNative(doc);
            mouseDown(mechanismAddPosition(doc.reactions[0]));
            check(doc.reactions[0].slots.size()==count+1&&doc.reactions[0].above.size()==count,"circular plus adds a structure and transition");
            check(history.undo(doc)&&toNative(doc)==before,"circular plus can be undone exactly");
            mouseDown(mechanismAddPosition(doc.reactions[0],true));
            check(doc.reactions[0].slots.size()==count+1&&doc.reactions[0].slots.back().startsRow,"bottom plus starts a manual row");
            auto lower=reactionSlot(doc.reactions[0],count);
            check(mechanismAddPosition(doc.reactions[0]).x<screen(lower).x,"second row displays the horizontal plus on the left");
            mouseDown(mechanismAddPosition(doc.reactions[0]));
            check(reactionSlot(doc.reactions[0],count+1).x<lower.x,"left plus extends the second row leftward");
            mouseDown(mechanismAddPosition(doc.reactions[0],true));
            auto bottom=reactionSlot(doc.reactions[0],count+2);
            check(bottom.y>lower.y&&mechanismAddPosition(doc.reactions[0]).x>screen(bottom).x,"third row displays the plus on the right");
            mouseDown(mechanismAddPosition(doc.reactions[0]));
            check(reactionSlot(doc.reactions[0],count+3).x>bottom.x,"right plus extends the third row rightward");
            writePreview(L"mechanism-manual-zigzag.png",doc);
            for(int i=0;i<4;i++)check(history.undo(doc),"manual plus click is undoable");
            check(toNative(doc)==before,"undoing manual rows restores the original exactly");
            doc.reactions[0].vertical=true;optimizeMechanism(doc,{id});fit();before=toNative(doc);
            mouseDown(mechanismAddPosition(doc.reactions[0]));
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

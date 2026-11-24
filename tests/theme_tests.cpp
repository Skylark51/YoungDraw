// Render the real editor and exercise the appearance dialog without modifying user preferences.
#include "win_button_help.h"
#include "win_gallery.h"
#include "win_editor_animation.h"
#include "win_editor_canvas.h"
#include "win_editor_chrome.h"
#include "win_editor_files.h"
#include "win_editor_images.h"
#include "win_editor_pointer.h"
#include "win_editor_plot.h"
#include "win_editor_state.h"
#include "win_editor_window.h"
#include "cdxml.h"
#include "win_theme.h"
#include "win_dialogs.h"
#include <commctrl.h>
#include <shobjidl_core.h>
#include <wrl/client.h>

using namespace yd;
using namespace yd::editor;
#include <iostream>
namespace {
int requested=0;bool driven=false,valid=true,plotSettingsValid=true;
enum class NativeCase { AlignmentWarning, Open, Save, GifSave };
NativeCase nativeCase;
bool nativeDialogSeen=false;

void CALLBACK inspectNativeDialog(HWND owner,UINT,UINT_PTR timer,DWORD){
    HWND dialog=nullptr;
    EnumThreadWindows(GetCurrentThreadId(),[](HWND hwnd,LPARAM result)->BOOL{
        wchar_t type[16]{};
        GetClassNameW(hwnd,type,int(std::size(type)));
        if(wcscmp(type,L"#32770")!=0||!IsWindowVisible(hwnd))return TRUE;
        wchar_t title[100]{};
        if(!GetWindowTextW(hwnd,title,int(std::size(title))))return TRUE;
        if(nativeCase==NativeCase::AlignmentWarning&&wcscmp(title,L"YoungDraw")!=0)return TRUE;
        if(nativeCase==NativeCase::GifSave&&wcscmp(title,L"GIF 저장 테마 검증")!=0)return TRUE;
        *reinterpret_cast<HWND*>(result)=hwnd;
        return FALSE;
    },LPARAM(&dialog));
    if(!dialog)return;
    KillTimer(owner,timer);
    nativeDialogSeen=true;

    const auto& palette=appearance::palette();
    Bitmap snapshot(8,8,PixelFormat32bppARGB);
    Graphics graphics(&snapshot);
    const auto dc=graphics.GetHDC();
    SendMessageW(dialog,WM_ERASEBKGND,WPARAM(dc),0);
    const auto background=GetPixel(dc,4,4);
    const auto child=GetWindow(dialog,GW_CHILD);
    auto brush=reinterpret_cast<HBRUSH>(SendMessageW(dialog,WM_CTLCOLORSTATIC,WPARAM(dc),LPARAM(child)));
    LOGBRUSH brushInfo{};
    GetObjectW(brush,sizeof(brushInfo),&brushInfo);
    const bool themed=background==palette.panel.ToCOLORREF()&&brushInfo.lbColor==background&&
        GetTextColor(dc)==palette.text.ToCOLORREF();
    graphics.ReleaseHDC(dc);

    if(!themed){
        wchar_t title[100]{};
        GetWindowTextW(dialog,title,int(std::size(title)));
        std::cerr<<"Native dialog "<<int(nativeCase)<<" ("<<ui::utf8(title)<<") missed palette "
                 <<requested<<" panel="<<background<<" control="<<brushInfo.lbColor<<'\n';
        valid=false;
    }
    PostMessageW(dialog,WM_CLOSE,0,0);
}

void checkNativeDialogs(HWND owner){
    auto exercise=[&](NativeCase type,const auto& open){
        nativeCase=type;
        nativeDialogSeen=false;
        SetTimer(owner,994,15,inspectNativeDialog);
        open();
        KillTimer(owner,994);
        if(!nativeDialogSeen){
            std::cerr<<"Native dialog "<<int(type)<<" did not open\n";
            valid=false;
        }
    };
    const auto before=doc;
    doc=Document{};
    exercise(NativeCase::AlignmentWarning,[&]{SendMessageW(owner,WM_COMMAND,125,0);});
    doc=before;

    if(requested!=2&&requested!=5)return;
    exercise(NativeCase::Open,[&]{valid=openDocumentFiles().empty()&&valid;});
    exercise(NativeCase::Save,[&]{valid=!fileDialog(true,1)&&valid;});
    Microsoft::WRL::ComPtr<IFileSaveDialog> dialog;
    if(FAILED(CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))){
        valid=false;
        return;
    }
    dialog->SetTitle(L"GIF 저장 테마 검증");
    exercise(NativeCase::GifSave,[&]{
        valid=(appearance::showFileDialog(owner,*dialog.Get())==HRESULT_FROM_WIN32(ERROR_CANCELLED))&&valid;
    });
}

bool routeDialogSeen=false;

void CALLBACK inspectRoute(HWND owner,UINT,UINT_PTR timer,DWORD){
    if(const auto form=FindWindowW(L"YoungDrawForm",L"결합 애니메이션 GIF");
        form&&GetWindow(form,GW_OWNER)==owner){
        PostMessageW(form,WM_COMMAND,IDOK,0);
        return;
    }
    const auto route=FindWindowW(L"YoungDrawRoutePicker",nullptr);
    if(!route||GetWindow(route,GW_OWNER)!=owner)return;
    KillTimer(owner,timer);
    routeDialogSeen=true;

    const auto& palette=appearance::palette();
    const auto label=FindWindowExW(route,nullptr,L"Static",nullptr);
    const auto dc=GetDC(route);
    auto brush=reinterpret_cast<HBRUSH>(SendMessageW(route,WM_CTLCOLORSTATIC,WPARAM(dc),LPARAM(label)));
    LOGBRUSH brushInfo{};
    GetObjectW(brush,sizeof(brushInfo),&brushInfo);
    valid=label&&GetTextColor(dc)==palette.text.ToCOLORREF()&&
        brushInfo.lbColor==palette.panel.ToCOLORREF()&&valid;
    ReleaseDC(route,dc);
    PostMessageW(route,WM_COMMAND,IDCANCEL,0);
}

void checkRouteDialog(HWND owner){
    const auto before=doc;
    doc=mechanismDemo();
    doc.reactions.front().links={{0,1},{0,2}};
    routeDialogSeen=false;
    SetTimer(owner,993,15,inspectRoute);
    createBondAnimation();
    KillTimer(owner,993);
    if(!routeDialogSeen){
        std::cerr<<"Branched animation route dialog did not open\n";
        valid=false;
    }
    doc=before;
}

void CALLBACK inspectGallery(HWND owner,UINT,UINT_PTR timer,DWORD){HWND gallery=nullptr;while((gallery=FindWindowExW(nullptr,gallery,L"YoungDrawTemplateGallery",nullptr))){DWORD process=0;GetWindowThreadProcessId(gallery,&process);if(process==GetCurrentProcessId()&&GetWindow(gallery,GW_OWNER)==owner)break;}if(!gallery)return;KillTimer(owner,timer);UpdateWindow(gallery);valid=valid&&ui::regionHelpCount(gallery)==25;SendMessageW(gallery,WM_COMMAND,109,0);UpdateWindow(gallery);valid=valid&&ui::regionHelpCount(gallery)==11;PostMessageW(gallery,WM_COMMAND,IDCANCEL,0);}
void CALLBACK inspectSearch(HWND owner,UINT,UINT_PTR timer,DWORD){
 HWND dialog=nullptr;while((dialog=FindWindowExW(nullptr,dialog,L"YoungDrawSearch",L"테마 검색 검증"))){if(GetWindow(dialog,GW_OWNER)==owner)break;}if(!dialog)return;KillTimer(owner,timer);
 HDC dc=GetDC(dialog);auto brush=(HBRUSH)SendMessageW(dialog,WM_CTLCOLOREDIT,(WPARAM)dc,(LPARAM)GetDlgItem(dialog,10));LOGBRUSH info{};GetObjectW(brush,sizeof(info),&info);
 valid=valid&&GetTextColor(dc)==appearance::palette().text.ToCOLORREF()&&info.lbColor==appearance::palette().raised.ToCOLORREF();ReleaseDC(dialog,dc);
 RECT r{};GetClientRect(dialog,&r);Bitmap snapshot(r.right,r.bottom);Graphics g(&snapshot);dc=g.GetHDC();PrintWindow(dialog,dc,PW_CLIENTONLY);g.ReleaseHDC(dc);CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};snapshot.Save((L"search-theme-"+std::to_wstring(requested)+L".png").c_str(),&png,nullptr);
 PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);
}
void CALLBACK inspectPlotSettings(HWND owner,UINT,UINT_PTR timer,DWORD){
 HWND dialog=nullptr;
 while((dialog=FindWindowExW(nullptr,dialog,L"YoungDrawForm",L"그래프·축 설정"))){
  if(GetWindow(dialog,GW_OWNER)==owner)break;
 }
 if(!dialog)return;
 KillTimer(owner,timer);
 const auto tab=FindWindowExW(dialog,nullptr,WC_TABCONTROLW,nullptr);
 const auto offset=GetDlgItem(dialog,121);
 RECT axisTab{};
 if(!tab||TabCtrl_GetItemCount(tab)!=5||!TabCtrl_GetItemRect(tab,2,&axisTab)||!offset){
  plotSettingsValid=false;
  PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);
  return;
 }
 const auto click=MAKELPARAM((axisTab.left+axisTab.right)/2,(axisTab.top+axisTab.bottom)/2);
 SendMessageW(tab,WM_LBUTTONDOWN,MK_LBUTTON,click);
 SendMessageW(tab,WM_LBUTTONUP,0,click);
 plotSettingsValid=plotSettingsValid&&TabCtrl_GetCurSel(tab)==2&&IsWindowVisible(offset)&&!IsWindowVisible(GetDlgItem(dialog,100));
 SendMessageW(tab,WM_KEYDOWN,VK_LEFT,0);
 plotSettingsValid=plotSettingsValid&&TabCtrl_GetCurSel(tab)==1&&!IsWindowVisible(offset);
 SendMessageW(tab,WM_KEYDOWN,VK_RIGHT,0);
 plotSettingsValid=plotSettingsValid&&TabCtrl_GetCurSel(tab)==2&&IsWindowVisible(offset);

 const auto& palette=appearance::palette();
 RECT bounds{},outer{},edit{},tabBounds{};
 GetClientRect(tab,&tabBounds);
 GetClientRect(dialog,&bounds);
 GetWindowRect(dialog,&outer);
 GetWindowRect(offset,&edit);
 POINT origin{outer.left,outer.top},editOrigin{edit.left,edit.top},tabOrigin{};
 ScreenToClient(dialog,&origin);
 ScreenToClient(dialog,&editOrigin);
 MapWindowPoints(tab,dialog,&tabOrigin,1);
 TabCtrl_GetItemRect(tab,2,&axisTab);

 Bitmap snapshot(bounds.right,bounds.bottom);
 Graphics graphics(&snapshot);
 auto dc=graphics.GetHDC();
 SendMessageW(dialog,WM_ERASEBKGND,(WPARAM)dc,0);
 // WM_PRINT positions child windows relative to the dialog frame.
 SetViewportOrgEx(dc,origin.x,origin.y,nullptr);
 SendMessageW(dialog,WM_PRINT,(WPARAM)dc,PRF_CLIENT|PRF_CHILDREN);
 SetViewportOrgEx(dc,0,0,nullptr);
 const auto panel=GetPixel(dc,tabOrigin.x+625,tabOrigin.y+10);
 const int baseline=tabOrigin.y+tabBounds.bottom-1;
 const auto activeSeam=GetPixel(dc,tabOrigin.x+(axisTab.left+axisTab.right)/2,baseline);
 const auto divider=GetPixel(dc,tabOrigin.x+625,baseline);
 const auto edge=GetPixel(dc,editOrigin.x+30,editOrigin.y);
 graphics.ReleaseHDC(dc);
 if(panel!=palette.panel.ToCOLORREF()||activeSeam!=panel||divider==panel||edge!=palette.edge.ToCOLORREF()){
  std::cerr<<"Graph settings theme colors differ for theme "<<requested<<'\n';
  plotSettingsValid=false;
 }
 const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
 const auto file=L"plot-settings-theme-"+std::to_wstring(requested)+L".png";
 plotSettingsValid=(snapshot.Save(file.c_str(),&png,nullptr)==Ok)&&plotSettingsValid;
 PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);
}

void CALLBACK inspectColor(HWND owner,UINT,UINT_PTR timer,DWORD){
 const auto picker=FindWindowW(L"YoungDrawColorPicker",L"색 선택");
 if(!picker||GetWindow(picker,GW_OWNER)!=owner)return;
 KillTimer(owner,timer);
 const auto dc=GetDC(picker);
 const auto brush=reinterpret_cast<HBRUSH>(SendMessageW(picker,WM_CTLCOLOREDIT,WPARAM(dc),LPARAM(GetDlgItem(picker,300))));
 LOGBRUSH info{};
 GetObjectW(brush,sizeof(info),&info);
 valid=valid&&info.lbColor==appearance::palette().raised.ToCOLORREF();
 ReleaseDC(picker,dc);
 SendMessageW(picker,WM_COMMAND,100,0);
 wchar_t hex[8]{};
 GetDlgItemTextW(picker,300,hex,int(std::size(hex)));
 valid=valid&&std::wstring(hex)==L"#F7B8BF";
 PostMessageW(picker,WM_COMMAND,IDOK,0);
}
void CALLBACK chooseTheme(HWND,UINT,UINT_PTR timer,DWORD){
 HWND picker=nullptr;
 // An independently running editor may also have its appearance dialog open.
 // Drive only this test's picker, never the user's preview window.
 while((picker=FindWindowExW(nullptr,picker,L"YoungDrawAppearance",nullptr))!=nullptr){
  DWORD process=0;GetWindowThreadProcessId(picker,&process);
  if(process==GetCurrentProcessId()&&GetWindow(picker,GW_OWNER)==win)break;
 }
 if(!picker)return;
 KillTimer(win,timer);driven=true;UpdateWindow(picker);valid=valid&&ui::regionHelpCount(picker)==21;
 SendMessageW(picker,WM_COMMAND,100+requested,0);
 SendMessageW(picker,WM_COMMAND,200+requested%6,0);
 SendMessageW(picker,WM_COMMAND,302,0);
 SetTimer(picker,998,10,inspectSearch);ui::search(picker,L"테마 검색 검증",{{L"메커니즘",L"단계와 연결"},{L"그래프",L"축과 주석"}},L"열기",true);KillTimer(picker,998);
 if(requested==2||requested==5){
  SetTimer(picker,995,10,inspectColor);
  const auto color=ui::chooseColor(picker,RGB(0,0,0));
  KillTimer(picker,995);
  valid=valid&&color==RGB(247,184,191);
  SetTimer(win,999,10,inspectPlotSettings);
  plotSettings(doc.plots.front().id);
  KillTimer(win,999);
 }
 auto popup=CreatePopupMenu();AppendMenuW(popup,MF_STRING,9001,L"테마 팝업 검증");SendMessageW(win,WM_INITMENUPOPUP,(WPARAM)popup,0);MENUITEMINFOW mi{sizeof(mi)};mi.fMask=MIIM_FTYPE|MIIM_DATA;GetMenuItemInfoW(popup,0,TRUE,&mi);valid=valid&&(mi.fType&MFT_OWNERDRAW)&&mi.dwItemData;DestroyMenu(popup);
 checkNativeDialogs(win);
 if(requested==5)checkRouteDialog(win);
 const auto& palette=appearance::palette();auto settings=appearance::settings();
 valid=valid&&settings.theme==requested&&settings.pattern==requested%6&&settings.intensity==2;
 Color rendered=appearance::documentColor(Color(255,0,0,0));
 valid=valid&&rendered.GetValue()==palette.canvasInk.GetValue();
 if(palette.darkCanvas)valid=valid&&rendered.GetR()>190;
 Bitmap snapshot(ww,wh,PixelFormat32bppARGB);Graphics graphics(&snapshot);HDC dc=graphics.GetHDC();paint(dc);graphics.ReleaseHDC(dc);
 CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
 valid=valid&&snapshot.Save((L"theme-"+std::to_wstring(requested)+L".png").c_str(),&png,nullptr)==Ok;
 if(requested==6){
  if(std::wstring(palette.name)!=L"교수님이 보고있어"||palette.resource!=205)valid=false;
  bool changed=false,portrait=false;ARGB initial=0;int brightest=-1;
  for(int frame=0;frame<32;++frame){
   Bitmap sample(600,500,PixelFormat32bppARGB);
   {Graphics bg(&sample);appearance::background(bg,{0,0,600,500});}
   // The whole portrait is fitted to height, with both eyes using its transform.
   Color eye;sample.GetPixel(255,149,&eye);
   if(frame==0)initial=eye.GetValue();else changed=changed||eye.GetValue()!=initial;
   Color face;sample.GetPixel(300,80,&face);portrait=portrait||face.GetR()<210;
   int light=eye.GetR()+eye.GetG()+eye.GetB();
   if(light>brightest){
    brightest=light;Bitmap preview(ww,wh,PixelFormat32bppARGB);
    {Graphics pg(&preview);HDC pdc=pg.GetHDC();paint(pdc,true);pg.ReleaseHDC(pdc);}
    valid=valid&&preview.Save(L"professor-glow.png",&png,nullptr)==Ok;
   }
   Sleep(80);
  }
  valid=valid&&changed&&portrait;
  // Keep the animation test separate from the static background cache checks.
 }
 // A second draw uses the cached canvas and must produce the same pixels.
 Bitmap first(180,120,PixelFormat32bppARGB),second(180,120,PixelFormat32bppARGB);
 {Graphics a(&first),b(&second);appearance::background(a,{0,0,180,120});appearance::background(b,{0,0,180,120});}
 if(requested!=6)for(int y=0;y<120;y+=11)for(int x=0;x<180;x+=13){Color a,b;first.GetPixel(x,y,&a);second.GetPixel(x,y,&b);valid=valid&&a.GetValue()==b.GetValue();}
 SendMessageW(picker,WM_COMMAND,IDCANCEL,0);
}
}
int main(){
 OleInitialize(nullptr);GdiplusStartupInput start;ULONG_PTR token;GdiplusStartup(&token,&start,nullptr);int result=0;HWND hwnd=nullptr;
 try{
  WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawThemeRegression";RegisterClassW(&wc);
  hwnd=CreateWindowW(wc.lpszClassName,L"Theme regression",WS_OVERLAPPEDWINDOW,0,0,1440,940,nullptr,nullptr,wc.hInstance,nullptr);
  if(!hwnd)throw std::runtime_error("Cannot create editor");
  doc=coordinateDemo();fit();const auto original=appearance::settings();const auto native=toNative(doc),svg=toSvg(scene());
  for(requested=0;requested<7;requested++){
   driven=false;SetTimer(hwnd,996,10,chooseTheme);appearance::picker(hwnd,[](){refreshAppearance();});
   auto after=appearance::settings();if(!driven||!valid||after.theme!=original.theme||after.pattern!=original.pattern||after.intensity!=original.intensity)throw std::runtime_error("Appearance preview/cancel/cache/contrast regression");
   if(toNative(doc)!=native||toSvg(scene())!=svg)throw std::runtime_error("Appearance changed document or export");
  }
  Document sample;sample.addAtom({0,0},26);sample.atoms[0].label="M";ui::TemplateCard card;card.name=L"설명 검증";card.category=L"기본";card.document=sample;card.preview=makeScene(sample);card.path=L"test-template.ydraw";SetTimer(hwnd,997,10,inspectGallery);ui::templateGallery(hwnd,{card},L"전체",[](Graphics&,const Scene&,RectF){});if(!valid)throw std::runtime_error("gallery button tooltip coverage regression");
   if(!plotSettingsValid)throw std::runtime_error("Graph settings theme or tab navigation regression");std::cout<<"7 themes, 6 patterns, preview rollback, dark contrast, render cache, document and SVG isolation passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
 if(IsWindow(hwnd))DestroyWindow(hwnd);
 imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return result;
}

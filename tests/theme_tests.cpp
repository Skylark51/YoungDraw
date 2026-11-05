// Render the real editor and exercise the appearance dialog without modifying user preferences.
#include "win_editor_canvas.h"
#include "win_editor_chrome.h"
#include "win_editor_images.h"
#include "win_editor_pointer.h"
#include "win_editor_state.h"
#include "win_editor_window.h"
#include "cdxml.h"
#include "win_theme.h"
#include <commctrl.h>

using namespace yd;
using namespace yd::editor;
#include <iostream>
namespace {
int requested=0;bool driven=false,valid=true;
void CALLBACK chooseTheme(HWND,UINT,UINT_PTR timer,DWORD){
 HWND picker=nullptr;
 // An independently running editor may also have its appearance dialog open.
 // Drive only this test's picker, never the user's preview window.
 while((picker=FindWindowExW(nullptr,picker,L"YoungDrawAppearance",nullptr))!=nullptr){
  DWORD process=0;GetWindowThreadProcessId(picker,&process);
  if(process==GetCurrentProcessId()&&GetWindow(picker,GW_OWNER)==win)break;
 }
 if(!picker)return;
 KillTimer(win,timer);driven=true;
 SendMessageW(picker,WM_COMMAND,100+requested,0);
 SendMessageW(picker,WM_COMMAND,200+requested,0);
 SendMessageW(picker,WM_COMMAND,302,0);
 const auto& palette=appearance::palette();auto settings=appearance::settings();
 valid=valid&&settings.theme==requested&&settings.pattern==requested&&settings.intensity==2;
 Color rendered=appearance::documentColor(Color(255,0,0,0));
 valid=valid&&rendered.GetValue()==palette.canvasInk.GetValue();
 if(palette.darkCanvas)valid=valid&&rendered.GetR()>190;
 Bitmap snapshot(ww,wh,PixelFormat32bppARGB);Graphics graphics(&snapshot);HDC dc=graphics.GetHDC();paint(dc);graphics.ReleaseHDC(dc);
 CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
 valid=valid&&snapshot.Save((L"theme-"+std::to_wstring(requested)+L".png").c_str(),&png,nullptr)==Ok;
 // A second draw uses the cached canvas and must produce the same pixels.
 Bitmap first(180,120,PixelFormat32bppARGB),second(180,120,PixelFormat32bppARGB);
 {Graphics a(&first),b(&second);appearance::background(a,{0,0,180,120});appearance::background(b,{0,0,180,120});}
 for(int y=0;y<120;y+=11)for(int x=0;x<180;x+=13){Color a,b;first.GetPixel(x,y,&a);second.GetPixel(x,y,&b);valid=valid&&a.GetValue()==b.GetValue();}
 SendMessageW(picker,WM_COMMAND,IDCANCEL,0);
}
}
int main(){
 OleInitialize(nullptr);GdiplusStartupInput start;ULONG_PTR token;GdiplusStartup(&token,&start,nullptr);int result=0;
 try{
  WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawThemeRegression";RegisterClassW(&wc);
  HWND hwnd=CreateWindowW(wc.lpszClassName,L"Theme regression",WS_OVERLAPPEDWINDOW,0,0,1440,940,nullptr,nullptr,wc.hInstance,nullptr);
  if(!hwnd)throw std::runtime_error("Cannot create editor");
  doc=coordinateDemo();fit();const auto original=appearance::settings();const auto native=toNative(doc),svg=toSvg(scene());
  for(requested=0;requested<6;requested++){
   driven=false;SetTimer(hwnd,996,10,chooseTheme);appearance::picker(hwnd,[](){refreshAppearance();});
   auto after=appearance::settings();if(!driven||!valid||after.theme!=original.theme||after.pattern!=original.pattern||after.intensity!=original.intensity)throw std::runtime_error("Appearance preview/cancel/cache/contrast regression");
   if(toNative(doc)!=native||toSvg(scene())!=svg)throw std::runtime_error("Appearance changed document or export");
  }
  DestroyWindow(hwnd);std::cout<<"6 themes, 6 patterns, preview rollback, dark contrast, render cache, document and SVG isolation passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
 imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return result;
}

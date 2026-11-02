#include "win_theme.h"
#include "win_ui.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <cmath>
namespace appearance {
using namespace Gdiplus;
namespace {
Color c(unsigned rgb,BYTE a=255){return Color(a,BYTE(rgb>>16),BYTE(rgb>>8),BYTE(rgb));}
const Palette themes[]={
 {c(0x101a2b),c(0x162237),c(0x1e2d45),c(0x2a3e58),c(0x30415a),c(0xe9eff8),c(0x98abc3),c(0xdcc28b),c(0x172238),c(0xf9fafc),c(0x202c3e),L"아틀리에",L"네이비 · 샴페인 골드 / 정밀한 작업을 위한 스튜디오",false,0},
 {c(0x0d1420),c(0x121d2c),c(0x1a293c),c(0x253c50),c(0x2b4053),c(0xe0edf3),c(0x8ca7b9),c(0x74d9d0),c(0x102029),c(0x142131),c(0xe1edf5),L"미드나이트",L"딥 블루 · 아이스 민트 / 밤의 몰입",true,0},
 {c(0xe9e2d6),c(0xf3ede2),c(0xfffbf4),c(0xe0d4c1),c(0xd0c5b3),c(0x3c3b38),c(0x7c7467),c(0x846640),c(0xfffbf4),c(0xfffcf3),c(0x39352c),L"아카이브",L"아이보리 · 잉크 / 오래 간직할 연구 노트",false,0},
 {c(0x34301d),c(0x433d25),c(0x514b2c),c(0x665d35),c(0x756841),c(0xf4edca),c(0xbdb183),c(0xe7ce70),c(0x302b18),c(0xe9dfb4),c(0x322c18),L"백룸",L"LEVEL 0 / 형광등의 잔향, 끝없이 반복되는 노란 방",false,201},
 {c(0x2a253c),c(0x382f49),c(0x493c58),c(0x5c496a),c(0x705871),c(0xf8e7dd),c(0xc9aeb8),c(0xf2ba9a),c(0x3a2b3b),c(0xf5e7dc),c(0x473343),L"노스텔지아",L"AFTER THE RAIN / CRT, 여름비, 돌아갈 수 없는 저녁",false,202},
 {c(0x110f17),c(0x1c1620),c(0x2b202b),c(0x412a38),c(0x51323e),c(0xeee5e6),c(0xb799a5),c(0xe79a9f),c(0x29151e),c(0x171d24),c(0xe9e7dd),L"공포",L"THE LAST EXPERIMENT / 붉은 경고등 아래, 비어 있는 실험실",true,203}
};
Settings current;
std::array<std::unique_ptr<Bitmap>,3> images;
std::array<IStream*,3> streams{};
std::unique_ptr<Bitmap> cached;
int cacheW=0,cacheH=0,cacheKey=-1;
const wchar_t* patternNames[]={L"없음",L"도트",L"격자",L"줄노트",L"등각 격자",L"육각 결합"};
const wchar_t* intensityNames[]={L"은은하게",L"균형 있게",L"몰입감 있게"};
void resetCache(){cached.reset();cacheKey=-1;}
Bitmap* image(int id){
 if(id<201||id>203)return nullptr;size_t i=size_t(id-201);
 if(images[i])return images[i].get();
 HMODULE module=GetModuleHandleW(nullptr);HRSRC resource=FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);if(!resource)return nullptr;
 DWORD size=SizeofResource(module,resource);const void* data=LockResource(LoadResource(module,resource));if(!size||!data)return nullptr;
 HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,size);if(!memory)return nullptr;void* dest=GlobalLock(memory);if(!dest){GlobalFree(memory);return nullptr;}memcpy(dest,data,size);GlobalUnlock(memory);
 if(FAILED(CreateStreamOnHGlobal(memory,TRUE,&streams[i]))){GlobalFree(memory);return nullptr;}
 auto bitmap=std::unique_ptr<Bitmap>(Bitmap::FromStream(streams[i]));if(!bitmap||bitmap->GetLastStatus()!=Ok){streams[i]->Release();streams[i]=nullptr;return nullptr;}images[i]=std::move(bitmap);return images[i].get();
}
void label(Graphics& g,const std::wstring& s,RectF box,float size,Color color,bool bold=false,bool center=false){ui::drawLabel(g,s,box,size,color,bold,center);}
void cover(Graphics& g,Bitmap* bitmap,RectF box){if(!bitmap)return;float iw=float(bitmap->GetWidth()),ih=float(bitmap->GetHeight());float scale=std::max(box.Width/iw,box.Height/ih),sw=box.Width/scale,sh=box.Height/scale;g.DrawImage(bitmap,box,(iw-sw)/2,(ih-sh)/2,sw,sh,UnitPixel);}
void pattern(Graphics& g,RectF box,int kind,Color color,float spacing=24){
 auto saved=g.Save();g.SetClip(box,CombineModeIntersect);Pen p(color,1);SolidBrush b(color);
 if(kind==1)for(float y=box.Y+spacing/2;y<box.GetBottom();y+=spacing)for(float x=box.X+spacing/2;x<box.GetRight();x+=spacing)g.FillEllipse(&b,x,y,1.6f,1.6f);
 if(kind==2||kind==3){for(float y=box.Y+spacing;y<box.GetBottom();y+=spacing)g.DrawLine(&p,box.X,y,box.GetRight(),y);if(kind==2)for(float x=box.X+spacing;x<box.GetRight();x+=spacing)g.DrawLine(&p,x,box.Y,x,box.GetBottom());}
 if(kind==4){for(float y=box.Y;y<box.GetBottom();y+=spacing)g.DrawLine(&p,box.X,y,box.GetRight(),y);for(float x=box.X-box.Height*2;x<box.GetRight()+box.Height*2;x+=spacing*2){g.DrawLine(&p,x,box.Y,x+box.Height*.57735f,box.GetBottom());g.DrawLine(&p,x,box.Y,x-box.Height*.57735f,box.GetBottom());}}
 if(kind==5){float r=spacing*.65f;for(int row=0;row<int(box.Height/(1.732f*r))+2;row++)for(int col=0;col<int(box.Width/(1.5f*r))+2;col++){float x=box.X+col*1.5f*r,y=box.Y+(row+(col%2)*.5f)*1.732f*r;PointF pts[6];for(int k=0;k<6;k++){float a=k*3.14159265f/3;pts[k]={x+r*std::cos(a),y+r*std::sin(a)};}g.DrawPolygon(&p,pts,6);}}
 g.Restore(saved);
}
void scene(Graphics& g,RectF box,int theme,int strength,bool thumbnail){
 const auto& p=themes[theme];SolidBrush base(p.canvas);g.FillRectangle(&base,box);
 if(p.resource){
  cover(g,image(p.resource),box);
  BYTE alpha=BYTE(thumbnail?35:strength==0?216:strength==1?183:145);SolidBrush veil(Color(alpha,p.canvas.GetR(),p.canvas.GetG(),p.canvas.GetB()));g.FillRectangle(&veil,box);
  // A soft central pool keeps bonds and labels legible without flattening the atmosphere at the edges.
  if(!thumbnail){GraphicsPath oval;oval.AddEllipse(box.X-box.Width*.18f,box.Y-box.Height*.2f,box.Width*1.36f,box.Height*1.4f);PathGradientBrush light(&oval);light.SetCenterColor(Color(p.darkCanvas?45:72,p.canvas.GetR(),p.canvas.GetG(),p.canvas.GetB()));Color edge(0,p.canvas.GetR(),p.canvas.GetG(),p.canvas.GetB());INT count=1;light.SetSurroundColors(&edge,&count);g.FillRectangle(&light,box);}
 }else if(theme==2){pattern(g,box,3,c(0xb5a38c,35),thumbnail?12.f:28.f);}
}
void save(){HKEY key;if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\YoungDraw\\Appearance",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return;DWORD a=current.theme,b=current.pattern,d=current.intensity;RegSetValueExW(key,L"Theme",0,REG_DWORD,reinterpret_cast<BYTE*>(&a),4);RegSetValueExW(key,L"Pattern",0,REG_DWORD,reinterpret_cast<BYTE*>(&b),4);RegSetValueExW(key,L"Intensity",0,REG_DWORD,reinterpret_cast<BYTE*>(&d),4);RegCloseKey(key);}
struct Picker {HWND parent=nullptr,window=nullptr;bool done=false,accepted=false;Settings before;std::function<void()> refresh;HFONT font=nullptr;};
void preview(Picker& p){resetCache();p.refresh();InvalidateRect(p.window,nullptr,FALSE);for(int id=100;id<306;id++){HWND control=GetDlgItem(p.window,id);if(control)InvalidateRect(control,nullptr,FALSE);}}
void drawControl(DRAWITEMSTRUCT* d){Graphics g(d->hDC);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);RectF r(0,0,float(d->rcItem.right),float(d->rcItem.bottom));const auto& p=palette();SolidBrush bg(p.chrome);g.FillRectangle(&bg,r);int id=int(d->CtlID);bool selected=id>=100&&id<106?current.theme==id-100:id>=200&&id<206?current.pattern==id-200:id>=300&&id<303?current.intensity==id-300:id==IDOK;
 if(id>=100&&id<106){int i=id-100;rounded(g,{1,1,r.Width-2,r.Height-2},10,selected?p.accent:p.edge);rounded(g,{3,3,r.Width-6,r.Height-6},8,themes[i].panel);auto clip=g.Save();GraphicsPath path;path.AddRectangle(RectF(5,5,r.Width-10,86));g.SetClip(&path);scene(g,{5,5,r.Width-10,86},i,2,true);if(i<3)pattern(g,{5,5,r.Width-10,86},i==0?1:i==1?5:3,c(i==1?0x91bbd4:0x62768b,80),16);g.Restore(clip);label(g,themes[i].name,{12,95,r.Width-24,28},19,themes[i].text,true,true);label(g,i>=3?L"시네마틱 배경":L"워크스페이스",{12,123,r.Width-24,23},14,themes[i].muted,false,true);if(selected)rounded(g,{r.Width-30,12,16,16},8,p.accent);
 }else {rounded(g,{1,1,r.Width-2,r.Height-2},7,selected?p.accent:p.raised);wchar_t title[80]{};GetWindowTextW(d->hwndItem,title,80);label(g,title,{8,0,r.Width-16,r.Height},16,selected?p.onAccent:p.text,selected,true);}
 if(d->itemState&ODS_FOCUS){Pen focus(p.accent,1);focus.SetDashStyle(DashStyleDot);g.DrawRectangle(&focus,4.f,4.f,r.Width-8,r.Height-8);}
}
LRESULT CALLBACK pickerProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){auto* p=reinterpret_cast<Picker*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
 if(msg==WM_CREATE){p=static_cast<Picker*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);p->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));p->font=CreateFontW(-15,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,ui::interfaceFont());
 auto button=[&](int id,const wchar_t* text,int x,int y,int w,int h){HWND b=CreateWindowW(L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,x,y,w,h,hwnd,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)p->font,0);};
 for(int i=0;i<6;i++)button(100+i,themes[i].name,24+(i%3)*292,96+(i/3)*160,280,150);
 for(int i=0;i<6;i++)button(200+i,patternNames[i],24+i*146,470,134,38);
 for(int i=0;i<3;i++)button(300+i,intensityNames[i],24+i*194,553,182,38);
 button(IDOK,L"이 분위기로 작업하기",650,634,230,42);button(IDCANCEL,L"취소",526,634,110,42);SetFocus(GetDlgItem(hwnd,100+current.theme));return 0;}
 if(!p)return DefWindowProcW(hwnd,msg,wp,lp);
 if(msg==WM_DRAWITEM){drawControl(reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;}
 if(msg==WM_ERASEBKGND)return 1;
 if(msg==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);Bitmap buffer(916,700,PixelFormat32bppARGB);Graphics g(&buffer);const auto& pal=palette();g.Clear(pal.chrome);label(g,L"작업실의 분위기를 고르세요",{24,18,870,34},23,pal.text,true);label(g,L"테마와 문양은 화면에만 적용됩니다. 문서·SVG·Office 출력은 원래 색상을 유지합니다.",{24,56,870,24},14,pal.muted);label(g,L"바탕 문양",{24,432,870,28},15,pal.text,true);label(g,L"배경의 존재감",{24,518,870,28},15,pal.text,true);label(g,pal.description,{24,598,870,28},14,pal.muted);Graphics out(dc);ui::presentPixels(out,buffer);EndPaint(hwnd,&ps);return 0;}
 if(msg==WM_COMMAND){int id=LOWORD(wp);if(id>=100&&id<106)current.theme=id-100;else if(id>=200&&id<206)current.pattern=id-200;else if(id>=300&&id<303)current.intensity=id-300;else if(id==IDOK||id==IDCANCEL){p->accepted=id==IDOK;p->done=true;return 0;}else return 0;titlebar(hwnd);preview(*p);return 0;}
 if(msg==WM_CLOSE){p->done=true;return 0;}return DefWindowProcW(hwnd,msg,wp,lp);
}
}
const Palette& palette(){return themes[current.theme];}const Palette& palette(int index){return themes[std::clamp(index,0,5)];}Settings settings(){return current;}
void load(){HKEY key;if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\YoungDraw\\Appearance",0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return;auto read=[&](const wchar_t* name,int fallback,int max){DWORD value=0,size=4,type=0;if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(&value),&size)==ERROR_SUCCESS&&type==REG_DWORD&&size==4&&value<=DWORD(max))return int(value);return fallback;};current={read(L"Theme",Settings{}.theme,5),read(L"Pattern",1,5),read(L"Intensity",1,2)};RegCloseKey(key);resetCache();}
void shutdown(){cached.reset();for(auto& i:images)i.reset();for(auto& s:streams)if(s){s->Release();s=nullptr;}}
void rounded(Graphics& g,RectF r,float radius,Color color){if(r.Width<=0||r.Height<=0)return;float d=std::min({radius*2,r.Width,r.Height});GraphicsPath p;p.AddArc(r.X,r.Y,d,d,180,90);p.AddArc(r.GetRight()-d,r.Y,d,d,270,90);p.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);p.AddArc(r.X,r.GetBottom()-d,d,d,90,90);p.CloseFigure();SolidBrush b(color);g.FillPath(&b,&p);}
void background(Graphics& g,RectF box){int w=std::max(1,int(box.Width)),h=std::max(1,int(box.Height)),key=current.theme*100+current.pattern*10+current.intensity;if(!cached||cacheW!=w||cacheH!=h||cacheKey!=key){cached=std::make_unique<Bitmap>(w,h,PixelFormat32bppARGB);Graphics bg(cached.get());bg.SetSmoothingMode(SmoothingModeAntiAlias);bg.SetInterpolationMode(InterpolationModeHighQualityBicubic);scene(bg,{0,0,float(w),float(h)},current.theme,current.intensity,false);const auto& p=palette();pattern(bg,{0,0,float(w),float(h)},current.pattern,Color(p.darkCanvas?24:28,p.canvasInk.GetR(),p.canvasInk.GetG(),p.canvasInk.GetB()));cacheW=w;cacheH=h;cacheKey=key;}g.DrawImage(cached.get(),box.X,box.Y);}
Color documentColor(Color color,bool backing){const auto& p=palette();if(backing&&color.GetR()==255&&color.GetG()==255&&color.GetB()==255)return p.canvas;if(color.GetR()==color.GetG()&&color.GetG()==color.GetB()&&color.GetR()<190){BYTE v=color.GetR();if(v<40)return p.canvasInk;if(p.darkCanvas)return c(0xa4b2bf);}if(p.darkCanvas){float luminance=.2126f*color.GetR()+.7152f*color.GetG()+.0722f*color.GetB();if(luminance<155){auto lift=[](BYTE v){return BYTE(v+(255-v)*.35f);};return Color(color.GetA(),lift(color.GetR()),lift(color.GetG()),lift(color.GetB()));}}return color;}
void titlebar(HWND hwnd){BOOL dark=current.theme!=2;DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));COLORREF caption=RGB(palette().chrome.GetR(),palette().chrome.GetG(),palette().chrome.GetB());DwmSetWindowAttribute(hwnd,35,&caption,sizeof(caption));}
void picker(HWND parent,const std::function<void()>& refresh){Picker p;p.parent=parent;p.before=current;p.refresh=refresh;WNDCLASSW wc{};wc.lpfnWndProc=pickerProc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=L"YoungDrawAppearance";RegisterClassW(&wc);RECT r{0,0,916,700};AdjustWindowRectEx(&r,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);RECT parentRect{};GetWindowRect(parent,&parentRect);int width=r.right-r.left,height=r.bottom-r.top;HMONITOR monitor=MonitorFromWindow(parent,MONITOR_DEFAULTTONEAREST);MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);int x=std::clamp(int(parentRect.left+(parentRect.right-parentRect.left-width)/2),int(info.rcWork.left),std::max(int(info.rcWork.left),int(info.rcWork.right)-width));int y=std::clamp(int(parentRect.top+(parentRect.bottom-parentRect.top-height)/2),int(info.rcWork.top),std::max(int(info.rcWork.top),int(info.rcWork.bottom)-height));HWND hwnd=CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,L"YoungDraw · 배경과 테마",WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,x,y,width,height,parent,nullptr,wc.hInstance,&p);if(!hwnd)return;titlebar(hwnd);EnableWindow(parent,FALSE);ShowWindow(hwnd,SW_SHOW);MSG msg{};bool quit=false;while(!p.done){BOOL result=GetMessageW(&msg,nullptr,0,0);if(result<=0){quit=result==0;break;}if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}if(p.accepted)save();else current=p.before;resetCache();EnableWindow(parent,TRUE);DestroyWindow(hwnd);if(p.font)DeleteObject(p.font);SetActiveWindow(parent);refresh();if(quit)PostQuitMessage(int(msg.wParam));}
}

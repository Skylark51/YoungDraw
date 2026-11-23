#include "win_button_help.h"
#include "win_theme.h"
#include "win_ui.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <shobjidl_core.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <cmath>
#include <set>
#include <map>
#include <uxtheme.h>
#include "win_dialog_style.h"
namespace appearance {
using namespace Gdiplus;
namespace {
Color c(unsigned rgb,BYTE a=255){return Color(a,BYTE(rgb>>16),BYTE(rgb>>8),BYTE(rgb));}
const Palette themes[]={
 {c(0x101a2b),c(0x162237),c(0x1e2d45),c(0x2a3e58),c(0x30415a),c(0xe9eff8),c(0x98abc3),c(0xdcc28b),c(0x172238),c(0xf9fafc),c(0x202c3e),L"아틀리에",L"네이비·골드 도구 영역 · 밝은 캔버스",false,0},
 {c(0x0d1420),c(0x121d2c),c(0x1a293c),c(0x253c50),c(0x2b4053),c(0xe0edf3),c(0x8ca7b9),c(0x74d9d0),c(0x102029),c(0x142131),c(0xe1edf5),L"미드나이트",L"블루·민트 도구 영역 · 어두운 캔버스",true,0},
 {c(0xe9e2d6),c(0xf3ede2),c(0xfffbf4),c(0xe0d4c1),c(0xd0c5b3),c(0x3c3b38),c(0x7c7467),c(0x846640),c(0xfffbf4),c(0xfffcf3),c(0x39352c),L"아카이브",L"아이보리 도구 영역 · 밝은 노트 배경",false,0},
 {c(0x34301d),c(0x433d25),c(0x514b2c),c(0x665d35),c(0x756841),c(0xf4edca),c(0xbdb183),c(0xe7ce70),c(0x302b18),c(0xe9dfb4),c(0x322c18),L"백룸",L"노란 방 사진 · 밝은 캔버스",false,201},
 {c(0x2a253c),c(0x382f49),c(0x493c58),c(0x5c496a),c(0x705871),c(0xf8e7dd),c(0xc9aeb8),c(0xf2ba9a),c(0x3a2b3b),c(0xf5e7dc),c(0x473343),L"노스텔지아",L"비 오는 풍경 사진 · 밝은 캔버스",false,202},
 {c(0x110f17),c(0x1c1620),c(0x2b202b),c(0x412a38),c(0x51323e),c(0xeee5e6),c(0xb799a5),c(0xe79a9f),c(0x29151e),c(0x171d24),c(0xe9e7dd),L"공포",L"실험실 사진 · 어두운 캔버스",true,203},
 {c(0xe5ebf1),c(0xedf1f5),c(0xfbfcfe),c(0xced9e3),c(0xc7d1dc),c(0x34404b),c(0x667687),c(0x4c6278),c(0xffffff),c(0xedf1f5),c(0x283a4a),L"교수님이 보고있어",L"반투명 사진 · 2.4초마다 번쩍이는 눈빛",false,205}
};
Settings current;
std::array<std::unique_ptr<Bitmap>,4> images;
std::array<IStream*,4> streams{};
std::unique_ptr<Bitmap> cached;
int cacheW=0,cacheH=0,cacheKey=-1;
const wchar_t* patternNames[]={L"없음",L"도트",L"격자",L"줄노트",L"등각 격자",L"육각 결합"};
const wchar_t* intensityNames[]={L"약하게",L"보통",L"강하게"};
void resetCache(){cached.reset();cacheKey=-1;}
Bitmap* image(int id){
 if((id<201||id>203)&&id!=205)return nullptr;size_t i=id==205?3:size_t(id-201);
 if(images[i])return images[i].get();
 HMODULE module=GetModuleHandleW(nullptr);HRSRC resource=FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);if(!resource)return nullptr;
 DWORD size=SizeofResource(module,resource);const void* data=LockResource(LoadResource(module,resource));if(!size||!data)return nullptr;
 HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,size);if(!memory)return nullptr;void* dest=GlobalLock(memory);if(!dest){GlobalFree(memory);return nullptr;}memcpy(dest,data,size);GlobalUnlock(memory);
 if(FAILED(CreateStreamOnHGlobal(memory,TRUE,&streams[i]))){GlobalFree(memory);return nullptr;}
 auto bitmap=std::unique_ptr<Bitmap>(Bitmap::FromStream(streams[i]));if(!bitmap||bitmap->GetLastStatus()!=Ok){streams[i]->Release();streams[i]=nullptr;return nullptr;}images[i]=std::move(bitmap);return images[i].get();
}
void label(Graphics& g,const std::wstring& s,RectF box,float size,Color color,bool bold=false,bool center=false){ui::drawLabel(g,s,box,size,color,bold,center);}
void cover(Graphics& g,Bitmap* bitmap,RectF box){if(!bitmap)return;float iw=float(bitmap->GetWidth()),ih=float(bitmap->GetHeight());float scale=std::max(box.Width/iw,box.Height/ih),sw=box.Width/scale,sh=box.Height/scale;g.DrawImage(bitmap,box,(iw-sw)/2,(ih-sh)/2,sw,sh,UnitPixel);}
// Keep the complete portrait visible and use the same transform for both eyes.
RectF portraitBox(RectF box){
 auto* photo=image(205);if(!photo)return box;
 float scale=std::min(box.Width/photo->GetWidth(),box.Height/photo->GetHeight());
 float w=photo->GetWidth()*scale,h=photo->GetHeight()*scale;
 return {box.X+(box.Width-w)/2,box.Y+(box.Height-h)/2,w,h};
}
void eyeGlints(Graphics& g,RectF box,float pulse){
 const RectF photo=portraitBox(box);
 auto saved=g.Save();g.SetClip(box,CombineModeIntersect);
 const float radius=std::max(3.f,photo.Width*.037f);
 for(const auto& at:{PointF(.400f,.299f),PointF(.563f,.291f)}){
  PointF eye(photo.X+photo.Width*at.X,photo.Y+photo.Height*at.Y);
  GraphicsPath oval;oval.AddEllipse(eye.X-radius,eye.Y-radius,radius*2,radius*2);
  PathGradientBrush halo(&oval);halo.SetCenterColor(Color(BYTE(210*pulse),125,218,255));
  Color edge(0,125,218,255);INT count=1;halo.SetSurroundColors(&edge,&count);g.FillPath(&halo,&oval);
  Pen horizontal(Color(BYTE(205*pulse),195,240,255),std::max(.7f,radius*.06f));
  g.DrawLine(&horizontal,eye.X-radius*1.6f,eye.Y,eye.X+radius*1.6f,eye.Y);
  Pen vertical(Color(BYTE(160*pulse),215,245,255),std::max(.7f,radius*.05f));
  g.DrawLine(&vertical,eye.X,eye.Y-radius*.9f,eye.X,eye.Y+radius*.9f);
  SolidBrush core(Color(BYTE(255*pulse),245,253,255));float dot=std::max(1.2f,radius*.16f);
  g.FillEllipse(&core,eye.X-dot,eye.Y-dot,dot*2,dot*2);
 }
 g.Restore(saved);
}
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
 if(p.resource==205){
  if(auto* photo=image(205))g.DrawImage(photo,portraitBox(box));
  BYTE alpha=BYTE(thumbnail?80:strength==0?153:strength==1?102:76);
  SolidBrush veil(Color(alpha,p.canvas.GetR(),p.canvas.GetG(),p.canvas.GetB()));g.FillRectangle(&veil,box);
  if(thumbnail)eyeGlints(g,box,1.f);
 }else if(p.resource){
  cover(g,image(p.resource),box);
  BYTE alpha=BYTE(thumbnail?35:strength==0?216:strength==1?183:145);SolidBrush veil(Color(alpha,p.canvas.GetR(),p.canvas.GetG(),p.canvas.GetB()));g.FillRectangle(&veil,box);
  // A soft central pool keeps bonds and labels legible without flattening the atmosphere at the edges.
  if(!thumbnail){GraphicsPath oval;oval.AddEllipse(box.X-box.Width*.18f,box.Y-box.Height*.2f,box.Width*1.36f,box.Height*1.4f);PathGradientBrush light(&oval);light.SetCenterColor(Color(p.darkCanvas?45:72,p.canvas.GetR(),p.canvas.GetG(),p.canvas.GetB()));Color edge(0,p.canvas.GetR(),p.canvas.GetG(),p.canvas.GetB());INT count=1;light.SetSurroundColors(&edge,&count);g.FillRectangle(&light,box);}
 }else if(theme==2){pattern(g,box,3,c(0xb5a38c,35),thumbnail?12.f:28.f);}
}
void save(){HKEY key;if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\YoungDraw\\Appearance",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return;DWORD a=current.theme,b=current.pattern,d=current.intensity;RegSetValueExW(key,L"Theme",0,REG_DWORD,reinterpret_cast<BYTE*>(&a),4);RegSetValueExW(key,L"Pattern",0,REG_DWORD,reinterpret_cast<BYTE*>(&b),4);RegSetValueExW(key,L"Intensity",0,REG_DWORD,reinterpret_cast<BYTE*>(&d),4);RegCloseKey(key);}
// The picker is painted as one surface. Native owner-drawn BUTTON children used to
// repaint independently, leaving seams and uneven outlines between adjacent cards.
struct Picker {
 HWND parent=nullptr,window=nullptr;
 bool done=false,accepted=false,tracking=false,keyboardFocus=false;
 int hover=-1,focus=100;
 Settings before;
 std::function<void()> refresh;
};
struct Target {RectF box;int id;};
constexpr float pickerWidth=916.f,pickerHeight=700.f;
std::array<Target,18> targets(){
 std::array<Target,18> result{};
 for(int i=0;i<7;i++)result[size_t(i)]={{24.f+float(i%4)*220.f,108.f+float(i/4)*160.f,208.f,148.f},100+i};
 for(int i=0;i<6;i++)result[size_t(7+i)]={{24.f+float(i)*146.f,466.f,134.f,40.f},200+i};
 for(int i=0;i<3;i++)result[size_t(13+i)]={{24.f+float(i)*194.f,567.f,182.f,40.f},300+i};
 result[16]={{648.f,644.f,240.f,42.f},IDOK};
 result[17]={{526.f,644.f,110.f,42.f},IDCANCEL};
 return result;
}
void roundPath(GraphicsPath& path,RectF r,float radius){
 float d=std::min({radius*2,r.Width,r.Height});
 path.AddArc(r.X,r.Y,d,d,180,90);
 path.AddArc(r.GetRight()-d,r.Y,d,d,270,90);
 path.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);
 path.AddArc(r.X,r.GetBottom()-d,d,d,90,90);
 path.CloseFigure();
}
void outline(Graphics& g,RectF r,float radius,Color color,float width){
 GraphicsPath path;roundPath(path,r,radius);
 Pen pen(color,width);pen.SetLineJoin(LineJoinRound);g.DrawPath(&pen,&path);
}
void pickerCard(Graphics& g,const Picker& state,int theme,RectF box){
 const auto& pal=palette(),&look=themes[theme];
 bool selected=current.theme==theme,hot=state.hover==100+theme;
 rounded(g,box,12,look.panel);
 // Clip the entire thumbnail to rounded top corners. No rectangular image
 // can protrude through the antialiased card border.
 GraphicsPath boundary;roundPath(boundary,box,12);
 auto saved=g.Save();g.SetClip(&boundary,CombineModeIntersect);
 RectF picture{box.X+1.5f,box.Y+1.5f,box.Width-3.f,87.f};
 scene(g,picture,theme,2,true);
 if(theme<3)pattern(g,picture,theme==0?1:theme==1?5:3,
     c(theme==1?0x91bbd4:0x62768b,80),16);
 // A separate solid text area makes the caption baseline identical for all cards.
 SolidBrush caption(look.panel);g.FillRectangle(&caption,
     box.X+1.f,box.Y+88.f,box.Width-2.f,box.Height-89.f);
 g.Restore(saved);
 label(g,look.name,{box.X+10.f,box.Y+94.f,box.Width-20.f,27.f},18,look.text,true,true);
 label(g,theme>=3?L"사진 배경":L"기본 배경",
     {box.X+10.f,box.Y+123.f,box.Width-20.f,19.f},13,look.muted,false,true);
 // One crisp outline after painting the contents avoids doubled 1px borders.
 outline(g,{box.X+1.f,box.Y+1.f,box.Width-2.f,box.Height-2.f},11,
     selected?pal.accent:hot?pal.hover:pal.edge,selected?2.4f:1.15f);
 if(selected){
     SolidBrush dot(pal.accent);
     g.FillEllipse(&dot,box.GetRight()-25.f,box.Y+11.f,12.f,12.f);
     Pen ring(look.panel,1.5f);
     g.DrawEllipse(&ring,box.GetRight()-25.f,box.Y+11.f,12.f,12.f);
 }
 if(state.keyboardFocus&&state.focus==100+theme){
     outline(g,{box.X+5.f,box.Y+5.f,box.Width-10.f,box.Height-10.f},
         8,pal.text,1.f);
 }
}
void pickerButton(Graphics& g,const Picker& state,int id,RectF box,
                  const wchar_t* caption,bool selected=false){
 const auto& pal=palette();
 bool hot=state.hover==id;
 Color bg=selected?pal.accent:hot?pal.hover:pal.raised;
 Color ink=selected?pal.onAccent:pal.text;
 rounded(g,box,9,bg);
 outline(g,{box.X+.7f,box.Y+.7f,box.Width-1.4f,box.Height-1.4f},
         8.5f,selected?pal.accent:hot?pal.muted:pal.edge,1.f);
 label(g,caption,{box.X+8.f,box.Y,box.Width-16.f,box.Height},15,ink,selected,true);
 if(state.keyboardFocus&&state.focus==id){
     outline(g,{box.X+4.f,box.Y+4.f,box.Width-8.f,box.Height-8.f},
         6,pal.text,1.f);
 }
}
void drawPicker(Graphics& g,const Picker& state){
 const auto& pal=palette();
 g.Clear(pal.chrome);
 g.SetSmoothingMode(SmoothingModeAntiAlias);
 g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
 g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
 g.SetCompositingQuality(CompositingQualityHighQuality);
 label(g,L"테마 선택",{24,20,870,35},23,pal.text,true);
 label(g,L"누르면 미리보기 · 적용으로 저장 · 취소로 복원. 문서·SVG·Office 출력 색상은 유지됩니다.",
       {24,62,870,25},14,pal.muted);
 const auto items=targets();
 for(int i=0;i<7;i++)pickerCard(g,state,i,items[size_t(i)].box);
 label(g,L"배경 무늬",{24,424,870,28},15,pal.text,true);
 for(int i=0;i<6;i++)pickerButton(g,state,200+i,items[size_t(7+i)].box,
     patternNames[i],current.pattern==i);
 label(g,L"사진 배경 강도",{24,526,870,28},15,pal.text,true);
 for(int i=0;i<3;i++)pickerButton(g,state,300+i,items[size_t(13+i)].box,
     intensityNames[i],current.intensity==i);
 label(g,pal.description,{24,613,600,24},13,pal.muted);
 pickerButton(g,state,IDCANCEL,items[17].box,L"취소");
 pickerButton(g,state,IDOK,items[16].box,L"적용",true);
}
int hitTest(float x,float y){
 const auto items=targets();
 for(const auto& item:items)
     if(x>=item.box.X&&y>=item.box.Y&&
        x<item.box.GetRight()&&y<item.box.GetBottom())return item.id;
 return -1;
}
PointF pointInPicker(HWND hwnd,LPARAM lp){
 RECT rc{};GetClientRect(hwnd,&rc);
 float w=float(std::max(1L,rc.right-rc.left)),h=float(std::max(1L,rc.bottom-rc.top));
 return {GET_X_LPARAM(lp)*pickerWidth/w,GET_Y_LPARAM(lp)*pickerHeight/h};
}
void choose(Picker& state,int id){
 if(id>=100&&id<107)current.theme=id-100;
 else if(id>=200&&id<206)current.pattern=id-200;
 else if(id>=300&&id<303)current.intensity=id-300;
 else if(id==IDOK||id==IDCANCEL){
     state.accepted=id==IDOK;state.done=true;return;
 }else return;
 titlebar(state.window);
 resetCache();
 if(state.refresh)state.refresh();
 InvalidateRect(state.window,nullptr,FALSE);
}
LRESULT CALLBACK pickerProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
 auto* state=reinterpret_cast<Picker*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
 if(msg==WM_CREATE){ui::enableButtonHelp();
     state=static_cast<Picker*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
     state->window=hwnd;state->focus=100+current.theme;
     SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
     return 0;
 }
 if(!state)return DefWindowProcW(hwnd,msg,wp,lp);
 switch(msg){
 // Preserve WM_COMMAND dispatch for automation, accessibility adapters and
 // existing theme regression tests even though buttons are painted in one view.
 case WM_COMMAND:{
     int id=LOWORD(wp);
     if((id>=100&&id<107)||(id>=200&&id<206)||
        (id>=300&&id<303)||id==IDOK||id==IDCANCEL){
         state->focus=id;choose(*state,id);
     }
     return 0;
 }
 case WM_ERASEBKGND:return 1; // The full opaque offscreen frame covers the client.
 case WM_PAINT:{
     PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);
     RECT rect{};GetClientRect(hwnd,&rect);
     int w=std::max(1L,rect.right-rect.left),h=std::max(1L,rect.bottom-rect.top);
     Bitmap buffer(w,h,PixelFormat32bppARGB);
     Graphics paint(&buffer);
     paint.ScaleTransform(float(w)/pickerWidth,float(h)/pickerHeight);
     drawPicker(paint,*state);
     paint.Flush(FlushIntentionSync);
     Graphics output(dc);ui::presentPixels(output,buffer);
     EndPaint(hwnd,&ps);
     std::vector<ui::HelpRegion> help;
     for(const auto& target:targets()){
         const int id=target.id;std::wstring text;
         if(id>=100&&id<107){
             const auto& look=themes[id-100];
             text=std::wstring(look.name)+L" · "+look.description+L". 클릭하여 미리 보고 적용으로 저장합니다.";
         }else if(id>=200&&id<206){
             text=std::wstring(patternNames[id-200])+L" 배경 무늬를 미리 봅니다. 문서 데이터와 SVG·Office 출력은 바뀌지 않습니다.";
         }else if(id>=300&&id<303){
             text=std::wstring(intensityNames[id-300])+L": 사진 배경의 선명도를 미리 봅니다. 기본 배경과 무늬의 강도는 바뀌지 않습니다.";
         }else if(id==IDOK)text=L"미리 본 테마·배경 무늬·사진 강도를 저장하고 닫습니다. 문서 내용은 바뀌지 않습니다.";
         else text=L"이 창을 열기 전의 테마·배경 무늬·사진 강도로 돌아갑니다. Esc로도 취소할 수 있습니다.";
         const auto b=target.box;
         help.push_back({UINT_PTR(id),{LONG(b.X*w/pickerWidth),LONG(b.Y*h/pickerHeight),LONG(b.GetRight()*w/pickerWidth),LONG(b.GetBottom()*h/pickerHeight)},text});
     }
     ui::setRegionHelp(hwnd,help);return 0;
 }
 case WM_MOUSEMOVE:{
     if(!state->tracking){
         TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,hwnd,0};
         TrackMouseEvent(&tracking);state->tracking=true;
     }
     auto point=pointInPicker(hwnd,lp);
     int hover=hitTest(point.X,point.Y);
     if(state->hover!=hover){
         state->hover=hover;InvalidateRect(hwnd,nullptr,FALSE);
     }
     return 0;
 }
 case WM_MOUSELEAVE:
     state->tracking=false;state->hover=-1;InvalidateRect(hwnd,nullptr,FALSE);return 0;
 case WM_SETCURSOR:
     if(LOWORD(lp)==HTCLIENT&&state->hover!=-1){
         SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;
     }
     break;
 case WM_LBUTTONDOWN:{
     SetFocus(hwnd);
     auto point=pointInPicker(hwnd,lp);int id=hitTest(point.X,point.Y);
     if(id!=-1){
         state->focus=id;state->keyboardFocus=false;choose(*state,id);
     }
     return 0;
 }
 case WM_KEYDOWN:{
     if(wp==VK_ESCAPE){choose(*state,IDCANCEL);return 0;}
     if(wp==VK_TAB){
         const auto items=targets();
         int index=0;
         for(size_t i=0;i<items.size();i++)
             if(items[i].id==state->focus){index=int(i);break;}
         index=(index+((GetKeyState(VK_SHIFT)&0x8000)?int(items.size())-1:1))
              %int(items.size());
         state->focus=items[size_t(index)].id;state->keyboardFocus=true;
         InvalidateRect(hwnd,nullptr,FALSE);return 0;
     }
     if(wp==VK_LEFT||wp==VK_RIGHT||wp==VK_UP||wp==VK_DOWN){
         int id=state->focus;
         if(id>=100&&id<107){
             int index=id-100,step=0;
             if(wp==VK_LEFT)step=-1;
             if(wp==VK_RIGHT)step=1;
             if(wp==VK_UP)step=-4;
             if(wp==VK_DOWN)step=4;
             state->focus=100+std::clamp(index+step,0,6);
         }else if(id>=200&&id<206){
             state->focus=200+std::clamp(id-200+
                 ((wp==VK_LEFT||wp==VK_UP)?-1:1),0,5);
         }else if(id>=300&&id<303){
             state->focus=300+std::clamp(id-300+
                 ((wp==VK_LEFT||wp==VK_UP)?-1:1),0,2);
         }
         state->keyboardFocus=true;InvalidateRect(hwnd,nullptr,FALSE);return 0;
     }
     if(wp==VK_RETURN||wp==VK_SPACE){
         choose(*state,state->focus);return 0;
     }
     break;
 }
 case WM_CLOSE:state->done=true;return 0;
 case WM_SIZE:InvalidateRect(hwnd,nullptr,FALSE);return 0;
 }
 return DefWindowProcW(hwnd,msg,wp,lp);
}
}
const Palette& palette(){return themes[current.theme];}const Palette& palette(int index){return themes[std::clamp(index,0,6)];}Settings settings(){return current;}
void load(){HKEY key;if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\YoungDraw\\Appearance",0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return;auto read=[&](const wchar_t* name,int fallback,int max){DWORD value=0,size=4,type=0;if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(&value),&size)==ERROR_SUCCESS&&type==REG_DWORD&&size==4&&value<=DWORD(max))return int(value);return fallback;};current={read(L"Theme",Settings{}.theme,6),read(L"Pattern",1,5),read(L"Intensity",1,2)};RegCloseKey(key);resetCache();}
void shutdown(){cached.reset();for(auto& i:images)i.reset();for(auto& s:streams)if(s){s->Release();s=nullptr;}}
void rounded(Graphics& g,RectF r,float radius,Color color){if(r.Width<=0||r.Height<=0)return;float d=std::min({radius*2,r.Width,r.Height});GraphicsPath p;p.AddArc(r.X,r.Y,d,d,180,90);p.AddArc(r.GetRight()-d,r.Y,d,d,270,90);p.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);p.AddArc(r.X,r.GetBottom()-d,d,d,90,90);p.CloseFigure();SolidBrush b(color);g.FillPath(&b,&p);}
void background(Graphics& g,RectF box){int w=std::max(1,int(box.Width)),h=std::max(1,int(box.Height)),key=current.theme*100+current.pattern*10+current.intensity;if(!cached||cacheW!=w||cacheH!=h||cacheKey!=key){cached=std::make_unique<Bitmap>(w,h,PixelFormat32bppARGB);Graphics bg(cached.get());bg.SetSmoothingMode(SmoothingModeAntiAlias);bg.SetInterpolationMode(InterpolationModeHighQualityBicubic);scene(bg,{0,0,float(w),float(h)},current.theme,current.intensity,false);const auto& p=palette();pattern(bg,{0,0,float(w),float(h)},current.pattern,Color(p.darkCanvas?24:28,p.canvasInk.GetR(),p.canvasInk.GetG(),p.canvasInk.GetB()));cacheW=w;cacheH=h;cacheKey=key;}g.DrawImage(cached.get(),box.X,box.Y);
 if(current.theme==6){float phase=float(GetTickCount64()%2400)/2400.f;
  float pulse=.16f+.84f*std::pow(.5f+.5f*std::cos(phase*6.2831853f),12.f);
  eyeGlints(g,box,pulse);
 }
}
Color documentColor(Color color,bool backing){const auto& p=palette();if(backing&&color.GetR()==255&&color.GetG()==255&&color.GetB()==255)return p.canvas;if(color.GetR()==color.GetG()&&color.GetG()==color.GetB()&&color.GetR()<190){BYTE v=color.GetR();if(v<40)return p.canvasInk;if(p.darkCanvas)return c(0xa4b2bf);}if(p.darkCanvas){float luminance=.2126f*color.GetR()+.7152f*color.GetG()+.0722f*color.GetB();if(luminance<155){auto lift=[](BYTE v){return BYTE(v+(255-v)*.35f);};return Color(color.GetA(),lift(color.GetR()),lift(color.GetG()),lift(color.GetB()));}}return color;}
namespace {
struct MenuBrushes {std::map<COLORREF,HBRUSH> values;~MenuBrushes(){for(auto& entry:values)DeleteObject(entry.second);}};
MenuBrushes menuBrushes;
std::set<std::wstring> menuLabels;
std::set<ULONG_PTR> menuData;
void themePopup(HMENU menu){
    auto& brush=menuBrushes.values[palette().panel.ToCOLORREF()];if(!brush)brush=CreateSolidBrush(palette().panel.ToCOLORREF());
    MENUINFO background{sizeof(background)};background.fMask=MIM_BACKGROUND;background.hbrBack=brush;SetMenuInfo(menu,&background);
    for(int i=0;i<GetMenuItemCount(menu);++i){
        wchar_t label[512]{};MENUITEMINFOW item{sizeof(item)};item.fMask=MIIM_FTYPE|MIIM_STRING|MIIM_SUBMENU;item.dwTypeData=label;item.cch=512;
        if(!GetMenuItemInfoW(menu,i,TRUE,&item)||item.fType&MFT_SEPARATOR)continue;
        auto stored=menuLabels.insert(label).first;auto data=reinterpret_cast<ULONG_PTR>(stored->c_str());menuData.insert(data);
        item.fMask=MIIM_FTYPE|MIIM_DATA;item.fType|=MFT_OWNERDRAW;item.dwItemData=data;SetMenuItemInfoW(menu,i,TRUE,&item);
    }
}
struct NativeColors {ui::DialogColors colors;int theme=settings().theme;};
constexpr UINT_PTR windowThemeId=991;

LRESULT CALLBACK themedWindow(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR paintClient){
    if(msg==WM_NCDESTROY){
        RemoveWindowSubclass(h,themedWindow,id);
        delete static_cast<NativeColors*>(RemovePropW(h,L"YoungDraw.NativeColors"));
        return DefSubclassProc(h,msg,wp,lp);
    }
    auto* state=static_cast<NativeColors*>(GetPropW(h,L"YoungDraw.NativeColors"));
    if(!state||state->theme!=settings().theme){
        auto next=std::make_unique<NativeColors>();
        if(!SetPropW(h,L"YoungDraw.NativeColors",next.get()))return DefSubclassProc(h,msg,wp,lp);
        delete state;
        state=next.release();
    }
    auto& colors=state->colors;
    if(msg==WM_INITMENUPOPUP)themePopup(reinterpret_cast<HMENU>(wp));
    if(msg==WM_MEASUREITEM){auto* item=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);if(item->CtlType==ODT_MENU&&menuData.count(item->itemData)){
        HDC dc=GetDC(h);auto old=SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));SIZE size{};auto label=reinterpret_cast<const wchar_t*>(item->itemData);GetTextExtentPoint32W(dc,label,int(wcslen(label)),&size);SelectObject(dc,old);ReleaseDC(h,dc);item->itemWidth=size.cx+56;item->itemHeight=28;return TRUE;
    }}
    if(msg==WM_DRAWITEM){auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if(paintClient&&(item->CtlType==ODT_BUTTON||item->CtlType==ODT_LISTBOX)){
            colors.draw(*item,item->CtlID==IDOK);
            return TRUE;
        }

        if(item->CtlType==ODT_MENU&&menuData.count(item->itemData)){
            const auto& p=palette();bool selected=item->itemState&ODS_SELECTED;SetDCBrushColor(item->hDC,(selected?p.hover:p.panel).ToCOLORREF());FillRect(item->hDC,&item->rcItem,(HBRUSH)GetStockObject(DC_BRUSH));
            SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,((item->itemState&ODS_DISABLED)?p.muted:p.text).ToCOLORREF());
            std::wstring label=reinterpret_cast<const wchar_t*>(item->itemData);auto tab=label.find(L'\t');RECT r=item->rcItem;r.left+=20;r.right-=22;auto font=SelectObject(item->hDC,GetStockObject(DEFAULT_GUI_FONT));
            DrawTextW(item->hDC,label.substr(0,tab).c_str(),-1,&r,DT_SINGLELINE|DT_VCENTER|DT_LEFT);
            if(tab!=std::wstring::npos)DrawTextW(item->hDC,label.substr(tab+1).c_str(),-1,&r,DT_SINGLELINE|DT_VCENTER|DT_RIGHT);
            if(item->itemState&ODS_CHECKED){r=item->rcItem;r.right=r.left+18;DrawTextW(item->hDC,L"✓",-1,&r,DT_SINGLELINE|DT_VCENTER|DT_CENTER);}
            MENUITEMINFOW info{sizeof(info)};info.fMask=MIIM_SUBMENU;GetMenuItemInfoW((HMENU)item->hwndItem,item->itemID,FALSE,&info);if(info.hSubMenu){r=item->rcItem;r.left=r.right-18;DrawTextW(item->hDC,L"›",-1,&r,DT_SINGLELINE|DT_VCENTER);}
            SelectObject(item->hDC,font);return TRUE;
        }
        if(item->CtlType==ODT_TAB&&GetPropW(item->hwndItem,L"YoungDraw.NativeTab")){wchar_t label[128]{};TCITEMW tab{};tab.mask=TCIF_TEXT;tab.pszText=label;tab.cchTextMax=128;TabCtrl_GetItem(item->hwndItem,item->itemID,&tab);SetDCBrushColor(item->hDC,((item->itemState&ODS_SELECTED)?palette().raised:palette().panel).ToCOLORREF());FillRect(item->hDC,&item->rcItem,(HBRUSH)GetStockObject(DC_BRUSH));SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,palette().text.ToCOLORREF());auto r=item->rcItem;DrawTextW(item->hDC,label,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_CENTER);return TRUE;}
    }
    if(paintClient){
        if(msg==WM_ERASEBKGND||msg==WM_PRINTCLIENT){
            RECT r{};GetClientRect(h,&r);
            FillRect(reinterpret_cast<HDC>(wp),&r,colors.panel);
            return TRUE;
        }
        if(msg==WM_CTLCOLORDLG)return reinterpret_cast<LRESULT>(colors.panel);
        if(msg==WM_CTLCOLORSTATIC||msg==WM_CTLCOLOREDIT||msg==WM_CTLCOLORLISTBOX||msg==WM_CTLCOLORBTN)
            return colors.control(msg,reinterpret_cast<HDC>(wp));
    }
    if(msg==WM_NOTIFY){
        auto* notice=reinterpret_cast<NMHDR*>(lp);
        if(notice->code==TTN_SHOW)nativeControl(notice->hwndFrom);
        if(notice->code==NM_CUSTOMDRAW){
            wchar_t kind[32]{};GetClassNameW(notice->hwndFrom,kind,32);
            const auto type=GetWindowLongPtrW(notice->hwndFrom,GWL_STYLE)&BS_TYPEMASK;
            auto* custom=reinterpret_cast<NMCUSTOMDRAW*>(lp);
            if(!_wcsicmp(kind,L"Button")&&(type==BS_PUSHBUTTON||type==BS_DEFPUSHBUTTON)&&custom->dwDrawStage==CDDS_PREPAINT){
                DRAWITEMSTRUCT draw{};
                draw.CtlType=ODT_BUTTON;draw.hwndItem=notice->hwndFrom;
                draw.hDC=custom->hdc;draw.rcItem=custom->rc;draw.itemState=custom->uItemState;
                const auto preferred=SendMessageW(h,DM_GETDEFID,0,0);
                const int primary=HIWORD(preferred)==DC_HASDEFID?LOWORD(preferred):IDOK;
                colors.draw(draw,GetDlgCtrlID(notice->hwndFrom)==primary);
                return CDRF_SKIPDEFAULT;
            }
        }
    }
    return DefSubclassProc(h,msg,wp,lp);
}

bool isDark(Color color){
    return color.GetR()*299+color.GetG()*587+color.GetB()*114<128000;
}

void styleWindow(HWND hwnd,bool paintClient){
    SetWindowSubclass(hwnd,themedWindow,windowThemeId,paintClient);
    const auto& p=palette();
    const BOOL dark=isDark(p.chrome);
    const COLORREF border=p.edge.ToCOLORREF(),caption=p.chrome.ToCOLORREF(),text=p.text.ToCOLORREF();
    DwmSetWindowAttribute(hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    DwmSetWindowAttribute(hwnd,DWMWA_BORDER_COLOR,&border,sizeof(border));
    DwmSetWindowAttribute(hwnd,DWMWA_CAPTION_COLOR,&caption,sizeof(caption));
    DwmSetWindowAttribute(hwnd,DWMWA_TEXT_COLOR,&text,sizeof(text));
    EnumChildWindows(hwnd,[](HWND child,LPARAM)->BOOL{nativeControl(child);return TRUE;},0);
}

bool isSystemDialog(HWND hwnd){
    wchar_t type[16]{};
    GetClassNameW(hwnd,type,int(std::size(type)));
    return wcscmp(type,L"#32770")==0;
}

BOOL CALLBACK styleShellControl(HWND child,LPARAM){
    wchar_t type[64]{};
    GetClassNameW(child,type,int(std::size(type)));
    if(isSystemDialog(child))SetWindowSubclass(child,themedWindow,windowThemeId,TRUE);
    const auto& p=palette();
    if(!_wcsicmp(type,WC_LISTVIEWW)){
        ListView_SetBkColor(child,p.raised.ToCOLORREF());
        ListView_SetTextBkColor(child,p.raised.ToCOLORREF());
        ListView_SetTextColor(child,p.text.ToCOLORREF());
    }else if(!_wcsicmp(type,WC_TREEVIEWW)){
        TreeView_SetBkColor(child,p.raised.ToCOLORREF());
        TreeView_SetTextColor(child,p.text.ToCOLORREF());
    }
    return TRUE;
}

void styleNativeDialog(HWND root){
    if(!isSystemDialog(root))return;
    if(!GetWindowSubclass(root,themedWindow,windowThemeId,nullptr))styleWindow(root,true);
    EnumChildWindows(root,styleShellControl,0);
}

// Keep Windows' file navigation, validation and keyboard behavior. Some shell
// dialogs build their controls after initialization, just before activation.
class NativeDialogTheme {
    static LRESULT CALLBACK observe(int code,WPARAM wp,LPARAM lp){
        if(code>=0){
            const auto& event=*reinterpret_cast<const CWPRETSTRUCT*>(lp);
            if((event.message==WM_INITDIALOG||event.message==WM_SHOWWINDOW)&&isSystemDialog(event.hwnd))
                styleNativeDialog(GetAncestor(event.hwnd,GA_ROOT));
        }
        return CallNextHookEx(nullptr,code,wp,lp);
    }
    static LRESULT CALLBACK activate(int code,WPARAM wp,LPARAM lp){
        if(code==HCBT_ACTIVATE)styleNativeDialog(reinterpret_cast<HWND>(wp));
        return CallNextHookEx(nullptr,code,wp,lp);
    }
    HHOOK initHook=SetWindowsHookExW(WH_CALLWNDPROCRET,observe,nullptr,GetCurrentThreadId());
    HHOOK activationHook=SetWindowsHookExW(WH_CBT,activate,nullptr,GetCurrentThreadId());
public:
    ~NativeDialogTheme(){
        if(activationHook)UnhookWindowsHookEx(activationHook);
        if(initHook)UnhookWindowsHookEx(initHook);
    }
};
}
void nativeControl(HWND h){
    if(!IsWindow(h))return;
    wchar_t cls[80]{};
    GetClassNameW(h,cls,int(std::size(cls)));
    const auto& p=palette();
    if(!_wcsicmp(cls,TOOLTIPS_CLASSW)){
        const auto theme=reinterpret_cast<HANDLE>(INT_PTR(settings().theme+1));
        constexpr auto tooltipTheme=L"YoungDraw.TooltipTheme";
        if(GetPropW(h,tooltipTheme)==theme)return;
        SetPropW(h,tooltipTheme,theme);
        SetWindowTheme(h,L"",L"");
        SendMessageW(h,TTM_SETTIPBKCOLOR,p.raised.ToCOLORREF(),0);
        SendMessageW(h,TTM_SETTIPTEXTCOLOR,p.text.ToCOLORREF(),0);
    }else if(!_wcsicmp(cls,PROGRESS_CLASSW)){
        SetWindowTheme(h,L"",L"");
        SendMessageW(h,PBM_SETBKCOLOR,0,p.raised.ToCOLORREF());
        SendMessageW(h,PBM_SETBARCOLOR,0,p.accent.ToCOLORREF());
    }else if(!_wcsicmp(cls,L"Edit")||!_wcsicmp(cls,L"ListBox")||!_wcsicmp(cls,L"ComboLBox")||
        !_wcsicmp(cls,L"ComboBox")||!_wcsicmp(cls,WC_LISTVIEWW)||!_wcsicmp(cls,WC_TREEVIEWW)){
        SetWindowTheme(h,isDark(p.panel)?L"DarkMode_Explorer":L"Explorer",nullptr);
    }else if(!_wcsicmp(cls,WC_TABCONTROLW)&&!(GetWindowLongPtrW(h,GWL_STYLE)&TCS_OWNERDRAWFIXED)){
        SetPropW(h,L"YoungDraw.NativeTab",(HANDLE)1);
        SetWindowTheme(h,L"",L"");
        SetWindowLongPtrW(h,GWL_STYLE,GetWindowLongPtrW(h,GWL_STYLE)|TCS_OWNERDRAWFIXED);
    }
}
void titlebar(HWND hwnd){
    auto instance=GetModuleHandleW(nullptr);
    static const auto largeIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));
    static const auto smallIcon=LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED);
    if(largeIcon)SendMessageW(hwnd,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(largeIcon));
    if(smallIcon)SendMessageW(hwnd,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(smallIcon));
    styleWindow(hwnd,false);
}
void dialog(HWND hwnd){
    titlebar(hwnd);
    SetWindowSubclass(hwnd,themedWindow,windowThemeId,TRUE);
}
int messageBox(HWND owner,const wchar_t* text,const wchar_t* caption,UINT type){
    NativeDialogTheme theme;
    return MessageBoxW(owner,text,caption,type);
}
BOOL openFileDialog(OPENFILENAMEW& options){
    NativeDialogTheme theme;
    return GetOpenFileNameW(&options);
}
BOOL saveFileDialog(OPENFILENAMEW& options){
    NativeDialogTheme theme;
    return GetSaveFileNameW(&options);
}
HRESULT showFileDialog(HWND owner,IFileDialog& dialog){
    NativeDialogTheme theme;
    return dialog.Show(owner);
}
void picker(HWND parent,const std::function<void()>& refresh){Picker p;p.parent=parent;p.before=current;p.refresh=refresh;WNDCLASSW wc{};wc.lpfnWndProc=pickerProc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=L"YoungDrawAppearance";RegisterClassW(&wc);RECT r{0,0,916,700};AdjustWindowRectEx(&r,WS_CAPTION|WS_SYSMENU,FALSE,0);RECT parentRect{};GetWindowRect(parent,&parentRect);int width=r.right-r.left,height=r.bottom-r.top;HMONITOR monitor=MonitorFromWindow(parent,MONITOR_DEFAULTTONEAREST);MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);int x=std::clamp(int(parentRect.left+(parentRect.right-parentRect.left-width)/2),int(info.rcWork.left),std::max(int(info.rcWork.left),int(info.rcWork.right)-width));int y=std::clamp(int(parentRect.top+(parentRect.bottom-parentRect.top-height)/2),int(info.rcWork.top),std::max(int(info.rcWork.top),int(info.rcWork.bottom)-height));HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"YoungDraw · 배경과 테마",WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,x,y,width,height,parent,nullptr,wc.hInstance,&p);if(!hwnd)return;titlebar(hwnd);EnableWindow(parent,FALSE);ShowWindow(hwnd,SW_SHOW);SetFocus(hwnd);MSG msg{};bool quit=false;while(!p.done){BOOL result=GetMessageW(&msg,nullptr,0,0);if(result<=0){quit=result==0;break;}TranslateMessage(&msg);DispatchMessageW(&msg);}if(p.accepted)save();else current=p.before;resetCache();EnableWindow(parent,TRUE);DestroyWindow(hwnd);SetActiveWindow(parent);refresh();if(quit)PostQuitMessage(int(msg.wParam));}
}

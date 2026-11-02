#include "win_gallery.h"
#include "win_dialogs.h"
#include "win_ui.h"
#include <windowsx.h>
#include <commctrl.h>
#include <cwctype>
namespace ui {
using namespace Gdiplus;
namespace {
const Color ink(255,28,36,49),muted(255,101,114,131),accent(255,0,123,112),border(255,222,229,234),white(255,255,255,255),background(255,246,248,250);
struct Target {RectF box;int command;};
struct Gallery {
    const std::vector<TemplateCard>* cards=nullptr;TemplatePainter painter;
    std::vector<size_t> filtered;std::vector<Target> targets;
    std::vector<std::wstring> categories={L"전체",L"리간드",L"착물 · axial",L"착물 · equatorial",L"원본 CDXML 착물",L"기질",L"산·시약",L"기본",L"사용자"};
    std::wstring category=L"리간드",search;HWND edit=nullptr;HFONT font=nullptr;
    int page=0,selected=-1,metal=0,perPage=9,width=1160,height=740;bool done=false,ok=false;
};
std::wstring normalized(std::wstring text){std::wstring result;for(wchar_t c:text)if(std::iswalnum(c)||(c>=0x1100&&c<=0xd7ff))result.push_back(wchar_t(std::towlower(c)));return result;}
void filter(Gallery& f){
    f.filtered.clear();auto query=normalized(f.search);
    for(size_t i=0;i<f.cards->size();i++){auto& card=f.cards->at(i);bool category=f.category==L"전체"||card.category==f.category;
        if(f.category==L"착물")category=card.category.find(L"착물")!=std::wstring::npos;
        if(category&&(query.empty()||normalized(card.name+card.category).find(query)!=std::wstring::npos))f.filtered.push_back(i);}
    f.page=0;f.selected=f.filtered.empty()?-1:int(f.filtered.front());
}
void label(Graphics& g,const std::wstring& text,RectF box,float size,Color color,bool bold=false,bool center=false){
    ui::drawLabel(g,text,box,size,color,bold,center,text.find(L'\n')==std::wstring::npos);
}
void panel(Graphics& g,RectF box,Color color,Color edge,float width=1){SolidBrush brush(color);g.FillRectangle(&brush,box);Pen pen(edge,width);g.DrawRectangle(&pen,box);}
void button(Graphics& g,Gallery& f,int id,const std::wstring& text,RectF box,bool active=false,bool enabled=true){
    if(enabled)f.targets.push_back({box,id});panel(g,box,active?accent:white,active?accent:border);
    label(g,text,{box.X+5,box.Y,box.Width-10,box.Height},14,enabled?(active?white:ink):muted,active,true);
}
void paint(HWND hwnd,Gallery& f,HDC dc){
    RECT client;GetClientRect(hwnd,&client);f.width=client.right;f.height=client.bottom;int w=f.width,h=f.height;
    Bitmap bitmap(w,h,PixelFormat32bppARGB);Graphics g(&bitmap);g.Clear(background);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);f.targets.clear();
    float tabWidth=float(w-32)/9;
    const wchar_t* captions[]={L"전체",L"리간드",L"Axial 착물",L"Equatorial 착물",L"원본 착물",L"기질",L"산·시약",L"기본",L"사용자"};
    for(int i=0;i<9;i++)button(g,f,100+i,captions[i],{16+i*tabWidth,14,tabWidth-5,34},f.category==f.categories[size_t(i)]);
    int divider=w-332,gridWidth=divider-32;label(g,L"검색",{16,62,48,30},13,muted,true);
    label(g,std::to_wstring(f.filtered.size())+L"개 · 그림을 누르면 크게 볼 수 있습니다.",{16,96,float(gridWidth),24},12,muted);
    int columns=gridWidth>=620?3:2,rows=h>=650?3:2;f.perPage=columns*rows;
    int pages=std::max(1,int((f.filtered.size()+f.perPage-1)/f.perPage));f.page=std::clamp(f.page,0,pages-1);
    float cardW=float(gridWidth-(columns-1)*10)/columns,cardH=float(h-198-(rows-1)*10)/rows;
    for(int k=0;k<f.perPage;k++){
        size_t index=size_t(f.page*f.perPage+k);if(index>=f.filtered.size())break;size_t selected=f.filtered[index];const auto& card=f.cards->at(selected);
        RectF box{16+(k%columns)*(cardW+10),126+(k/columns)*(cardH+10),cardW,cardH};bool active=f.selected==int(selected);
        panel(g,box,white,active?accent:border,active?2.f:1.f);f.targets.push_back({box,1000+int(selected)});
        f.painter(g,card.preview,{box.X+10,box.Y+8,box.Width-20,std::max(20.f,box.Height-52)});
        if(active){SolidBrush tint(Color(255,229,245,241));g.FillRectangle(&tint,box.X+1,box.Y+box.Height-41,box.Width-2,40.f);}
        label(g,card.name,{box.X+8,box.Y+box.Height-40,box.Width-16,35},12,active?accent:ink,true,true);
    }
    if(f.filtered.empty())label(g,L"일치하는 템플릿이 없습니다.",{16,180,float(gridWidth),120},16,muted,false,true);
    button(g,f,201,L"‹ 이전",{16,float(h-54),88,34},false,f.page>0);
    label(g,std::to_wstring(f.page+1)+L" / "+std::to_wstring(pages),{112,float(h-54),92,34},13,muted,false,true);
    button(g,f,202,L"다음 ›",{212,float(h-54),88,34},false,f.page+1<pages);
    RectF preview{float(divider+16),126,300,float(h-360)};
    panel(g,preview,white,border);
    bool metalAvailable=false;
    if(f.selected>=0){const auto& card=f.cards->at(size_t(f.selected));
        label(g,card.name,{float(divider+16),62,300,52},17,ink,true);
        yd::Scene scene=card.preview;
        const wchar_t* metals[]={L"M",L"Fe",L"Mn",L"Co",L"Ni",L"Cu",L"Zn"};
        for(auto& atom:card.document.atoms)if(atom.label=="M"){metalAvailable=true;break;}
        if(metalAvailable&&f.metal){auto copy=card.document;int elements[]={26,26,25,27,28,29,30};for(auto& a:copy.atoms)if(a.label=="M"){a.element=elements[f.metal];a.label.clear();}scene=yd::makeScene(copy);}
        f.painter(g,scene,{preview.X+14,preview.Y+14,preview.Width-28,preview.Height-28});
        label(g,card.category+L" · 원자 "+std::to_wstring(card.document.atoms.size())+L"개",{preview.X,float(h-222),300,26},12,muted);
        label(g,L"금속",{preview.X,float(h-189),300,24},12,muted,true);
        for(int i=0;i<7;i++)button(g,f,300+i,metals[i],{preview.X+i*43,float(h-161),38,28},metalAvailable&&f.metal==i,metalAvailable);
    }
    label(g,L"선택한 구조만 삽입합니다.\n더블클릭 또는 Enter로도 삽입할 수 있습니다.",{preview.X,float(h-119),300,48},12,muted);
    button(g,f,IDOK,L"선택한 템플릿 삽입",{preview.X,float(h-54),184,34},true,f.selected>=0);
    button(g,f,IDCANCEL,L"닫기",{preview.X+194,float(h-54),106,34});
    Graphics output(dc);ui::presentPixels(output,bitmap);
}
void command(HWND hwnd,Gallery& f,int id){
    if(id>=100&&id<109){f.category=f.categories[size_t(id-100)];filter(f);}
    else if(id>=1000){f.selected=id-1000;SetFocus(hwnd);}
    else if(id>=300&&id<307)f.metal=id-300;
    else if(id==201)f.page=std::max(0,f.page-1);
    else if(id==202)f.page++;
    else if(id==IDOK&&f.selected>=0){f.ok=true;f.done=true;}
    else if(id==IDCANCEL)f.done=true;
    InvalidateRect(hwnd,nullptr,FALSE);
}
LRESULT CALLBACK searchProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(msg==WM_KEYDOWN&&(wp==VK_RETURN||wp==VK_ESCAPE)){SendMessageW(GetParent(hwnd),WM_COMMAND,wp==VK_RETURN?IDOK:IDCANCEL,0);return 0;}
    return DefSubclassProc(hwnd,msg,wp,lp);
}
LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    auto* f=reinterpret_cast<Gallery*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_CREATE){f=static_cast<Gallery*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)f);
        f->font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,ui::interfaceFont());
        f->edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,70,62,400,30,hwnd,(HMENU)500,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(f->edit,WM_SETFONT,(WPARAM)f->font,TRUE);SendMessageW(f->edit,EM_SETCUEBANNER,TRUE,(LPARAM)L"이름 검색 · TBDAP, peroxo, HOTf…");SetWindowSubclass(f->edit,searchProc,1,0);return 0;
    }
    if(!f)return DefWindowProcW(hwnd,msg,wp,lp);
    switch(msg){
    case WM_SIZE:MoveWindow(f->edit,70,62,std::max(120,LOWORD(lp)-418),30,TRUE);InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);paint(hwnd,*f,dc);EndPaint(hwnd,&ps);return 0;}
    case WM_LBUTTONDOWN:case WM_LBUTTONDBLCLK:{float x=float(GET_X_LPARAM(lp)),y=float(GET_Y_LPARAM(lp));for(const auto& hit:f->targets)if(hit.box.Contains(x,y)){int id=hit.command;command(hwnd,*f,id);if(msg==WM_LBUTTONDBLCLK&&id>=1000)command(hwnd,*f,IDOK);break;}return 0;}
    case WM_MOUSEWHEEL:command(hwnd,*f,GET_WHEEL_DELTA_WPARAM(wp)>0?201:202);return 0;
    case WM_COMMAND:if(LOWORD(wp)==500&&HIWORD(wp)==EN_CHANGE){int n=GetWindowTextLengthW(f->edit);std::wstring value(n+1,0);GetWindowTextW(f->edit,value.data(),n+1);value.resize(n);f->search=value;filter(*f);InvalidateRect(hwnd,nullptr,FALSE);}else command(hwnd,*f,LOWORD(wp));return 0;
    case WM_KEYDOWN:if(wp==VK_ESCAPE){command(hwnd,*f,IDCANCEL);return 0;}if(wp==VK_RETURN){command(hwnd,*f,IDOK);return 0;}
        if(wp==VK_LEFT||wp==VK_RIGHT||wp==VK_UP||wp==VK_DOWN){auto it=std::find(f->filtered.begin(),f->filtered.end(),size_t(f->selected));if(it!=f->filtered.end()){int delta=wp==VK_LEFT?-1:wp==VK_RIGHT?1:wp==VK_UP?-3:3;int i=std::clamp(int(it-f->filtered.begin())+delta,0,int(f->filtered.size())-1);f->selected=int(f->filtered[size_t(i)]);f->page=i/f->perPage;InvalidateRect(hwnd,nullptr,FALSE);}return 0;}break;
    case WM_CLOSE:f->done=true;return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}
std::optional<TemplateChoice> templateGallery(HWND parent,const std::vector<TemplateCard>& cards,const std::wstring& initialCategory,const TemplatePainter& painter){
    static bool registered=false;if(!registered){WNDCLASSW wc{};wc.style=CS_DBLCLKS;wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawTemplateGallery";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);registered=true;}
    Gallery f;f.cards=&cards;f.category=initialCategory;f.painter=painter;filter(f);
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);int width=std::min(1180,int(work.right-work.left)-24),height=std::min(820,int(work.bottom-work.top)-24);
    HWND hwnd=CreateWindowExW(WS_EX_DLGMODALFRAME,L"YoungDrawTemplateGallery",L"템플릿 · 미리보기와 삽입",WS_CAPTION|WS_SYSMENU|WS_POPUP|WS_CLIPCHILDREN,work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,parent,nullptr,GetModuleHandleW(nullptr),&f);
    if(!hwnd)return {};EnableWindow(parent,FALSE);ShowWindow(hwnd,SW_SHOW);SetFocus(f.edit);MSG msg;
    while(!f.done&&GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    DestroyWindow(hwnd);if(f.font)DeleteObject(f.font);EnableWindow(parent,TRUE);SetActiveWindow(parent);
    if(f.ok)return TemplateChoice{size_t(f.selected),f.metal};return {};
}
}

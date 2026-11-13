#include "win_mechanism_editor.h"
#include "win_editor_state.h"
#include "mechanism_data.h"
#include "cdxml.h"
#include <commctrl.h>
#include <windowsx.h>
#include <iostream>
#include <stdexcept>
using namespace yd;using namespace yd::editor;
namespace {
int phase=0,tick=0,drawCalls=0;std::string failure;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void command(HWND window,int id){SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),0);}
void select(HWND editor,int row){SendDlgItemMessageW(editor,20,LB_SETCURSEL,row,0);SendMessageW(editor,WM_COMMAND,MAKEWPARAM(20,LBN_SELCHANGE),0);}
RECT childRect(HWND editor,int id){RECT r{};GetWindowRect(GetDlgItem(editor,id),&r);MapWindowPoints(nullptr,editor,(POINT*)&r,2);return r;}
// Render our test window directly: no desktop capture or user-app interaction.
struct Picture {
    Gdiplus::Bitmap bitmap;
    Picture(HWND editor):bitmap(width(editor),height(editor),PixelFormat32bppARGB){Gdiplus::Graphics graphics(&bitmap);auto dc=graphics.GetHDC();SendMessageW(editor,WM_PRINTCLIENT,(WPARAM)dc,PRF_CLIENT);graphics.ReleaseHDC(dc);}
    static int width(HWND h){RECT r{};GetClientRect(h,&r);return r.right;}
    static int height(HWND h){RECT r{};GetClientRect(h,&r);return r.bottom;}
    bool teal(int x,int y){Gdiplus::Color c;bitmap.GetPixel(x,y,&c);return c.GetR()<45&&c.GetG()>110&&c.GetG()<165&&c.GetB()>95&&c.GetB()<155;}
    POINT selectedFrame(HWND editor){auto banner=childRect(editor,22);for(int y=banner.bottom+12;y<int(bitmap.GetHeight())-100;++y)for(int x=banner.left+12;x<int(bitmap.GetWidth())-30;++x){if(!teal(x,y)||!teal(x+20,y)||!teal(x,y+20)||!teal(x,y+40))continue;int right=x;while(right<int(bitmap.GetWidth())-1&&teal(right,y))++right;int bottom=y;while(bottom<int(bitmap.GetHeight())-1&&teal(x,bottom))++bottom;if(right-x>50&&bottom-y>40)return {(x+right)/2,(y+bottom)/2};}throw std::runtime_error("selected picture frame is fully visible");}
    std::vector<POINT> pluses(HWND editor){std::vector<POINT> result;auto banner=childRect(editor,22);for(int y=banner.bottom+18;y<int(bitmap.GetHeight())-72;++y)for(int x=banner.left+12;x<int(bitmap.GetWidth())-22;++x){if(!teal(x,y)||!teal(x-3,y)||!teal(x+3,y)||!teal(x,y-3)||!teal(x,y+3)||teal(x+3,y+3)||!teal(x-9,y)||!teal(x+9,y))continue;if(std::none_of(result.begin(),result.end(),[&](POINT p){return std::abs(p.x-x)<10&&std::abs(p.y-y)<10;}))result.push_back({x,y});}return result;}
};
void CALLBACK fillMetadata(HWND editor,UINT,UINT_PTR timer,DWORD){
    HWND form=nullptr;while((form=FindWindowExW(nullptr,form,L"YoungDrawForm",nullptr)))if(GetWindow(form,GW_OWNER)==editor)break;
    if(!form)return;KillTimer(editor,timer);
    const wchar_t* values[]={L"Branch point",L"-7.5",L"New intermediate",L"-12.8",L"TS-branch",L"18.4"};
    for(int i=0;i<6;i++)SetDlgItemTextW(form,100+i,values[i]);PostMessageW(form,WM_COMMAND,IDOK,0);
}
void addBranch(HWND editor){SetTimer(editor,2,20,fillMetadata);command(editor,33);}
void CALLBACK drive(HWND owner,UINT,UINT_PTR timer,DWORD){
    HWND editor=nullptr;while((editor=FindWindowExW(nullptr,editor,L"YoungDrawMechanismEditor",nullptr))){DWORD pid=0;GetWindowThreadProcessId(editor,&pid);if(pid==GetCurrentProcessId()&&GetWindow(editor,GW_OWNER)==owner)break;}
    if(!editor)return;
    // Stop this timer before entering a nested metadata dialog.
    KillTimer(owner,timer);
    try{
        auto list=GetDlgItem(editor,20);
        if(phase==2){
            check(!IsWindowEnabled(GetDlgItem(editor,33)),"cycle branch action starts disabled");
            Picture picture(editor);auto pluses=picture.pluses(editor);
            check(pluses.size()==3,"cycle exposes only its three supported stage plus buttons, no TS plus");
            SetTimer(editor,2,20,fillMetadata);SendMessageW(editor,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(pluses[0].x,pluses[0].y));
            check(SendMessageW(list,LB_GETCOUNT,0,0)==4,"clicking actual cycle plus adds a stage without invalid errors");
            check(!IsWindowEnabled(GetDlgItem(editor,33)),"cycle never enables unsupported branch action");
            PostMessageW(editor,WM_COMMAND,IDOK,0);return;
        }
        if(phase==3){
            select(editor,0);check(IsWindowEnabled(GetDlgItem(editor,60)),"selected picture frame enables drawing");
            command(editor,60);command(editor,61);command(editor,62);
            check(drawCalls==1,"drawing action invokes independent structure editor once");
            Picture selected(editor);auto center=selected.selectedFrame(editor);SendMessageW(editor,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(center.x,center.y));
            check(drawCalls==2,"double-clicking a picture frame opens its structure editor");
            select(editor,1);command(editor,60); // Cancel structure editor: preserve this frame.
            check(drawCalls==3,"existing frame can be reopened for editing");
            RECT frame{};GetWindowRect(editor,&frame);Gdiplus::Bitmap full(frame.right-frame.left,frame.bottom-frame.top,PixelFormat32bppARGB);{Gdiplus::Graphics graphics(&full);auto dc=graphics.GetHDC();SendMessageW(editor,WM_PRINT,(WPARAM)dc,PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);graphics.ReleaseHDC(dc);}
            CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};full.Save(L"mechanism-editor-controls.png",&png);
            PostMessageW(editor,WM_COMMAND,IDOK,0);return;
        }
        if(phase==4){
            SetWindowPos(editor,nullptr,0,0,760,620,SWP_NOMOVE|SWP_NOZORDER);
            RECT client{};GetClientRect(editor,&client);
            for(int id:{11,13,15,17,19,20,22,30,31,32,33,34,35,40,41,42,43,44,45,60,61,62,63,IDOK,IDCANCEL}){auto r=childRect(editor,id);check(r.left>=0&&r.top>=0&&r.right<=client.right&&r.bottom<=client.bottom,"all controls remain inside small monitor window");}
            check(childRect(editor,22).bottom+65<childRect(editor,IDOK).top,"small editor retains usable picture canvas");
            PostMessageW(editor,WM_COMMAND,IDCANCEL,0);return;
        }
        if(tick==0){
            check(SendMessageW(list,LB_GETCOUNT,0,0)==3,"draft initially lists all three stages");
            check(!IsWindowEnabled(GetDlgItem(editor,60))&&!IsWindowEnabled(GetDlgItem(editor,33)),"drawing and branching require a selected frame");
            Picture picture(editor);check(picture.pluses(editor).size()==4,"linear mechanism exposes stage additions but no TS plus");
            select(editor,0);addBranch(editor);
            check(SendMessageW(list,LB_GETCOUNT,0,0)==4,"first intermediate can branch in the editor");
            if(phase==1){PostMessageW(editor,WM_COMMAND,IDCANCEL,0);return;}
            SendDlgItemMessageW(editor,17,TBM_SETPOS,TRUE,220);SendMessageW(editor,WM_HSCROLL,TB_LINEUP,(LPARAM)GetDlgItem(editor,17));SendMessageW(editor,WM_HSCROLL,TB_ENDTRACK,(LPARAM)GetDlgItem(editor,17));
            SetDlgItemTextW(editor,13,L"eV");SendMessageW(editor,WM_COMMAND,MAKEWPARAM(13,CBN_EDITCHANGE),0);
            command(editor,42);command(editor,43);command(editor,40);command(editor,40);
            select(editor,3);PostMessageW(editor,WM_KEYDOWN,VK_DELETE,0);tick=1;SetTimer(owner,1,20,drive);return;
        }
        check(SendMessageW(list,LB_GETCOUNT,0,0)==3,"Delete removes just the selected stage");command(editor,42);
        check(SendMessageW(list,LB_GETCOUNT,0,0)==4,"local undo restores a removed stage");
        PostMessageW(editor,WM_COMMAND,IDOK,0);
    }catch(const std::exception& e){failure=e.what();PostMessageW(editor,WM_CLOSE,0,0);}
}
std::optional<Document> run(HWND owner,const Document& source,const MechanismEditorCallbacks& callbacks={}){tick=0;SetTimer(owner,1,20,drive);auto result=mechanismEditor(owner,source,source.reactions[0].id,&source,callbacks);check(failure.empty(),failure.c_str());return result;}
}
int main(){
    ULONG_PTR token=0;GdiplusStartupInput start;GdiplusStartup(&token,&start,nullptr);OleInitialize(nullptr);
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_BAR_CLASSES};InitCommonControlsEx(&common);
    HWND owner=CreateWindowExW(0,L"STATIC",L"Mechanism editor regression",WS_OVERLAPPEDWINDOW,0,0,900,750,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    int result=0;
    try{
        auto source=mechanismDemo();auto before=toNative(source);auto applied=run(owner,source);
        check(applied.has_value(),"draft can be applied");auto& r=applied->reactions[0];
        check(r.slots.size()==4&&r.links.size()==3&&r.links.back().from==0,"branch targets first stage and keeps original connections");
        check(r.slots[0].energy=="-7.5"&&r.slots.back().label=="New intermediate"&&r.slots.back().energy=="-12.8"&&r.above.back()=="TS-branch"&&r.below.back()=="18.4","branch point, new intermediate and TS names and energies survive apply");
        auto scene=makeScene(*applied);for(const auto& label:{"Branch point","-7.5","New intermediate","-12.8","TS-branch","18.4"})check(std::any_of(scene.items.begin(),scene.items.end(),[&](const auto& item){return item.text==label;}),"branch point and TS names/energies are visibly rendered");
        check(r.arrowLength==220&&r.unit=="eV","slider and unit settings survive apply");check(toNative(source)==before,"editor never mutates its source document");
        phase=1;auto cancelled=run(owner,source);check(!cancelled&&toNative(source)==before,"Cancel discards all draft mutations");
        auto cycle=source;cycle.reactions[0].cyclic=true;phase=2;auto cycled=run(owner,cycle);
        check(cycled&&cycled->reactions[0].cyclic&&!cycled->reactions[0].explicitConnections()&&cycled->reactions[0].slots.size()==4,"cycle plus preserves cyclic representation and closing arrow");
        auto roundtrip=fromCdxml(toCdxml(*cycled));check(roundtrip.reactions[0].cyclic&&roundtrip.reactions[0].slots.size()==4,"cycle addition round-trips through CDXML");
        phase=3;Document fragment;fragment.addAtom({0,0},8);MechanismEditorCallbacks callbacks;
        callbacks.editStructure=[&](const Document& original,HWND editor)->std::optional<Document>{check(GetWindow(editor,GW_OWNER)==owner,"structure editor is owned by draft editor");++drawCalls;if(drawCalls==1)return fragment;check(!original.atoms.empty(),"existing frame contents supplied for structure editing");return {};};
        callbacks.clipboard=[&]()->std::optional<Document>{fragment.atoms[0].element=7;return fragment;};
        callbacks.chooseStructure=[&](HWND)->std::optional<Document>{fragment.atoms[0].element=16;return fragment;};
        auto pictured=run(owner,source,callbacks);check(pictured.has_value(),"draw/paste/file draft applies");
        auto inserted=structureInSlot(*pictured,pictured->reactions[0].id,0);check(inserted.atoms.size()==1&&inserted.atoms[0].element==16,"drawing, clipboard and file imports replace just the selected frame");
        auto untouched=structureInSlot(*pictured,pictured->reactions[0].id,1),expected=structureInSlot(source,source.reactions[0].id,1);
        check(untouched.atoms.size()==expected.atoms.size()&&untouched.bonds.size()==expected.bonds.size(),"cancelling structure editing preserves sibling chemistry");
        check(toNative(source)==before,"all structure operations remain isolated from source");
        phase=4;run(owner,source);
        std::cout<<"PASS: cycle plus clicks, supported controls, branch metadata, isolated drawing/paste/file, small display, undo and cancellation\n";
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';result=1;}
    KillTimer(owner,1);DestroyWindow(owner);GdiplusShutdown(token);OleUninitialize();return result;
}

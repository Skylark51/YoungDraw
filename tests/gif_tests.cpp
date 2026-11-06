#include "win_editor_animation.h"
#include "win_editor_canvas.h"
#include "win_editor_commands.h"
#include "win_editor_chrome.h"
#include "win_editor_clipboard.h"
#include "win_editor_files.h"
#include "win_editor_images.h"
#include "win_editor_render.h"
#include "win_editor_state.h"
#include "win_editor_structure.h"
#include "win_editor_window.h"
#include "win_theme.h"
#include "cdxml.h"
#include "office.h"
#include <shellapi.h>

using namespace yd;
using namespace yd::editor;
#include <shlobj.h>
#include <commctrl.h>
#include <iostream>
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);std::cout<<message<<std::endl;}
static void putClipboard(UINT format,const void* bytes,size_t size){
    check(OpenClipboard(win)!=0,"open clipboard");EmptyClipboard();
    auto memory=GlobalAlloc(GMEM_MOVEABLE,size);check(memory!=nullptr,"allocate clipboard payload");
    void* data=GlobalLock(memory);memcpy(data,bytes,size);GlobalUnlock(memory);
    auto result=SetClipboardData(format,memory);CloseClipboard();if(!result)GlobalFree(memory);check(result!=nullptr,"set clipboard payload");
}
static void checkBondAnimation(){
    Document bound;
    int a=bound.addAtom({-30,0},17), b=bound.addAtom({30,0},35);
    bound.addBond(a,b);
    Document free=bound;
    free.bonds.clear();
    bound=fromCdxml(toCdxml(bound));
    free=fromNative(toNative(free));
    for(bool forming:{false,true}){
        auto gif=imageDocument(bondAnimationGif({{forming?free:bound},{forming?bound:free}}));
        const auto& hex=gif.objects.front().imageHex;
        auto* bitmap=decode(hex);
        const auto& animation=imageCache.at(hex);
        check(animation.infinite&&animation.ends.size()==72,"generated bond GIF decodes with 72 looping frames");
        const UINT x=bitmap->GetWidth()/2;
        auto redAt=[&](UINT frame){
            check(bitmap->SelectActiveFrame(&FrameDimensionTime,frame)==Ok,"select generated GIF frame");
            int darkest=255;
            for(UINT y=0;y<bitmap->GetHeight();++y){
                Color pixel;
                if(bitmap->GetPixel(x,y,&pixel)!=Ok)throw std::runtime_error("could not read bond pixels");
                darkest=std::min(darkest,int(pixel.GetRed()));
            }
            return darkest;
        };
        auto first=redAt(0),middle=redAt(18),last=redAt(36);
        check(forming?first>middle&&middle>last:first<middle&&middle<last,
              "the center bond fades in or out across generated frames");
        check(redAt(54)==middle&&redAt(71)==redAt(1),"the reverse leg and loop seam reuse the forward frames");
    }
}
static void checkAnimationExportOptions(){
    Document bound;
    int a=bound.addAtom({-30,0},17), b=bound.addAtom({30,0},35);
    bound.addBond(a,b);
    Document free=bound;
    free.bonds.clear();
    std::vector<AnimationStage> stages={{bound},{free}};
    auto transparent=bondAnimationGif(stages,false,AnimationBackground::Transparent);
    auto whiteGif=bondAnimationGif(stages,false,AnimationBackground::White);
    auto theme=bondAnimationGif(stages,false,AnimationBackground::Theme);
    auto opened=imageDocument(transparent);
    const auto& hex=opened.objects.front().imageHex;
    const auto& cache=imageCache.at(hex);
    check(!cache.infinite&&cache.ends.size()==37&&cache.frameAt(cache.ends.back()+10)==36,
          "single-play GIF has no reverse leg or loop and holds the last frame");
    auto* bitmap=decode(hex);
    const UINT width=bitmap->GetWidth(),height=bitmap->GetHeight();
    Bitmap raster(width,height,PixelFormat32bppARGB);
    auto drawFrame=[&](UINT frame){
        check(bitmap->SelectActiveFrame(&FrameDimensionTime,frame)==Ok,"select transparent GIF frame");
        Graphics canvas(&raster);
        canvas.Clear(Color(0,0,0,0));
        check(canvas.DrawImage(bitmap,Gdiplus::Rect(0,0,width,height),0,0,width,height,UnitPixel)==Ok,
              "render transparent GIF frame to the canvas");
    };
    drawFrame(0);
    Color pixel;
    check(raster.GetPixel(0,0,&pixel)==Ok&&pixel.GetAlpha()==0,"transparent GIF has a clear background");
    const UINT x=width/2;
    UINT y=0;
    while(y<height&&raster.GetPixel(x,y,&pixel)==Ok&&pixel.GetAlpha()!=255)++y;
    check(y<height&&pixel.GetAlpha()==255,"transparent GIF keeps the original bond visible");
    drawFrame(36);
    check(raster.GetPixel(x,y,&pixel)==Ok&&pixel.GetAlpha()==0,"transparent frames clear the departed bond");
    auto whiteImage=imageDocument(whiteGif);
    check(decode(whiteImage.objects.front().imageHex)->GetPixel(0,0,&pixel)==Ok&&pixel.GetValue()==0xffffffff,
          "white GIF has an opaque white background");
    check(theme!=whiteGif&&theme!=transparent,"the current theme has its own GIF background");
}

static void checkMechanismAnimation(){
    Document source;
    Reaction r;
    r.id=source.nextId++;
    r.slots={{"Reactant",{},"0 / S=0.5:12.5"},{"Intermediate",{}},{"Product",{}}};
    r.above={"TS1 / addition","TS2 / elimination"};
    source.reactions.push_back(r);
    for(size_t i=0;i<r.slots.size();++i){
        Document fragment;
        int a=fragment.addAtom({-28,0},17);
        int b=fragment.addAtom({28,0},35);
        if(i!=1)fragment.addBond(a,b);
        replaceSlotStructure(source,fragment,r.id,i);
    }
    auto stages=animationMechanismStages(source,source.reactions.front());
    check(stages.size()==3&&stages[0].structureName=="Reactant"&&stages[1].stageName=="TS1"&&
          stages[0].energies=="M=1: 0 / M=2: 12.5 kcal/mol"&&stages[1].structure.bonds.empty(),
          "mechanism slot order, structures, transition titles and multiplicities populate the GIF");
    auto branched=source.reactions.front();
    branched.links={{0,2,{}},{0,1,{}}};
    auto branch=animationMechanismStages(source,branched,{0,2});
    check(branch.size()==2&&branch.back().structureName=="Product","a branch uses only the requested connected stages");

    auto gif=imageDocument(bondAnimationGif(std::move(stages)));
    const auto& hex=gif.objects.front().imageHex;
    auto* bitmap=decode(hex);
    check(imageCache.at(hex).ends.size()==144,"three stages play forward and back across both transitions");
    auto inkIn=[&](UINT start,UINT finish){
        for(UINT y=start;y<finish;++y)for(UINT x=0;x<bitmap->GetWidth();++x){
            Color pixel;
            if(bitmap->GetPixel(x,y,&pixel)==Ok&&pixel.GetRed()<128)return true;
        }
        return false;
    };
    const UINT height=bitmap->GetHeight();
    check(inkIn(0,37)&&inkIn(height-52,height-25)&&inkIn(height-25,height),
          "GIF renders the structure name above and the stage name and energy below");
}
static bool autoCaptionsObserved=false, previewPlaying=false, previewThemed=false;
static HWND activePreview=nullptr;
static bool captionGridVisited=false;
static void CALLBACK driveAutoAnimation(HWND parent,UINT,UINT_PTR timer,DWORD){
    auto dialog=FindWindowW(L"YoungDrawForm",L"결합 애니메이션 GIF");
    if(dialog&&GetWindow(dialog,GW_OWNER)==parent){
        PostMessageW(dialog,WM_COMMAND,IDOK,0);
        return;
    }
    if(!activePreview){
        dialog=FindWindowW(L"YoungDrawAnimationPreview",nullptr);
        if(!dialog||GetWindow(dialog,GW_OWNER)!=parent)return;
        wchar_t text[96]{};
        GetDlgItemTextW(dialog,110,text,96);
        std::wstring readoutText=text;
        const std::wstring firstFrame=L"프레임 1";
        if(readoutText.empty()||(readoutText.size()>=firstFrame.size()&&
           readoutText.compare(readoutText.size()-firstFrame.size(),firstFrame.size(),firstFrame)==0))return;
        const auto initialLoop=SendDlgItemMessageW(dialog,111,CB_GETCURSEL,0,0);
        SendDlgItemMessageW(dialog,111,CB_SETCURSEL,0,0);
        SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(111,CBN_SELCHANGE),LPARAM(GetDlgItem(dialog,111)));
        previewPlaying=GetDlgItem(dialog,108)!=nullptr&&GetDlgItem(dialog,104)!=nullptr&&
            GetDlgItem(dialog,112)!=nullptr&&initialLoop==2&&SendDlgItemMessageW(dialog,111,CB_GETCURSEL,0,0)==0;
        auto readout=GetDlgItem(dialog,110);
        auto dc=GetDC(readout);
        SendMessageW(dialog,WM_CTLCOLORSTATIC,reinterpret_cast<WPARAM>(dc),reinterpret_cast<LPARAM>(readout));
        previewThemed=GetTextColor(dc)==appearance::palette().text.ToCOLORREF()&&
            GetBkColor(dc)==appearance::palette().panel.ToCOLORREF();
        ReleaseDC(readout,dc);
        activePreview=dialog;
        PostMessageW(dialog,WM_COMMAND,106,0);
        return;
    }
    if(captionGridVisited&&IsWindowEnabled(activePreview)){
        KillTimer(parent,timer);
        PostMessageW(activePreview,WM_CLOSE,0,0);
        return;
    }
    dialog=FindWindowW(L"YoungDrawGrid",L"결합 애니메이션 · 단계별 자막");
    if(!dialog||GetWindow(dialog,GW_OWNER)!=activePreview)return;
    captionGridVisited=true;
    auto list=GetDlgItem(dialog,10);
    auto cell=[&](int row,int column){
        wchar_t value[256]{};
        LVITEMW item{};
        item.iSubItem=column;
        item.pszText=value;
        item.cchTextMax=256;
        SendMessageW(list,LVM_GETITEMTEXTW,row,reinterpret_cast<LPARAM>(&item));
        return std::wstring(value);
    };
    autoCaptionsObserved=cell(0,1)==L"Reactant"&&cell(1,1)==L"Product"&&cell(1,2)==L"TS1"&&
        cell(0,3)==L"M=1: 0.0 / M=3: 3.5"&&cell(1,4)==L"M=2: -12.5 kcal·mol⁻¹";
    PostMessageW(dialog,WM_COMMAND,23,0);
}

static void checkOpenDrawingAnimation(HWND hwnd){
    doc=fromCdxml(R"xml(<CDXML><page>
        <fragment id="10"><n id="11" p="230 0" Element="17"/><n id="12" p="255 0" Element="35"/>
        <b id="13" B="11" E="12"/></fragment>
        <fragment id="20"><n id="21" p="0 0" Element="17"/><n id="22" p="25 0" Element="35"/>
        <b id="23" B="21" E="22" Order="2"/></fragment>
        <arrow id="30" Tail3D="185 0 0" Head3D="65 0 0" ArrowheadHead="Full"/>
        <t id="31" p="220 20"><s>Reactant</s></t>
        <t id="32" p="0 20"><s>Product</s></t>
        <t id="33" p="107 -25"><s>TS1 / reaction</s></t>
        <t id="34" p="219 41"><s>M=1: 0.0</s></t>
        <t id="35" p="219 56"><s>S=1: 3.5</s></t>
        <t id="36" p="95 30"><s>M=2: −12.5 kcal/mol</s></t>
    </page></CDXML>)xml");
    selection.clear();targetContainer=0;history.clear();dirty=false;
    auto before=toNative(doc);
    SetTimer(hwnd,940,15,driveAutoAnimation);
    command(bondAnimationCommand);
    KillTimer(hwnd,940);
    check(previewPlaying&&previewThemed,"preview opens before saving, advances frames and uses the active palette");
    check(autoCaptionsObserved,"the active CDXML drawing follows the leftward arrow and prefills structure names and both energies");
    check(toNative(doc)==before&&!dirty&&!history.undo(doc),"recognition and cancellation leave the open document intact");
}
int main(){
    OleInitialize(nullptr);GdiplusStartupInput startup;ULONG_PTR token;GdiplusStartup(&token,&startup,nullptr);
    Microsoft::WRL::ComPtr<IDataObject> previous;OleGetClipboard(&previous);
    int result=0;bool clipboardRestored=false;
    try{
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawGifRegression";RegisterClassW(&wc);
        auto hwnd=CreateWindowW(wc.lpszClassName,L"GIF test",WS_OVERLAPPEDWINDOW,0,0,1440,940,nullptr,nullptr,wc.hInstance,nullptr);check(hwnd!=nullptr,"create canvas window");
        check(SUCCEEDED(office::start({hwnd,[](const Document&){},[](){return doc;},officePicture},false,false)),"initialize native clipboard rendering callbacks");
        checkBondAnimation();
        checkAnimationExportOptions();
        checkMechanismAnimation();
        checkOpenDrawingAnimation(hwnd);
        auto bytes=readFile(L"animated.gif");auto fragment=imageDocument(bytes);auto hex=fragment.objects[0].imageHex;
        check(fragment.objects[0].mime=="image/gif"&&fragment.objects[0].points[1].y==80.*2/3,"detect GIF and preserve aspect ratio");
        auto& cache=imageCache.at(hex);check(cache.ends==std::vector<ULONGLONG>{70,200}&&cache.infinite,"read GIF frame delays and infinite loop metadata");
        check(cache.frameAt(0)==0&&cache.frameAt(69)==0&&cache.frameAt(70)==1&&cache.frameAt(199)==1&&cache.frameAt(200)==0,"advance at exact variable frame boundaries and loop");
        cache.infinite=false;cache.cycles=3;check(cache.playing(599)&&!cache.playing(600)&&cache.frameAt(600)==1,"finite animation stops on its final frame");cache.infinite=true;
        cache.started=GetTickCount64()-100;auto* bitmap=decode(hex,true);Color pixel;bitmap->GetPixel(0,0,&pixel);check(pixel.GetBlue()==255&&pixel.GetRed()==0,"animated decoder renders the second frame");
        bitmap=decode(hex);bitmap->GetPixel(0,0,&pixel);check(pixel.GetRed()==255&&pixel.GetBlue()==0,"static preview consistently uses the first frame");
        {
            auto transparent=imageDocument(readFile(L"transparent-disposal.gif"));auto transparentHex=transparent.objects[0].imageHex;auto& frames=imageCache.at(transparentHex);
            frames.started=GetTickCount64()-150;auto* frame=decode(transparentHex,true);Color red,green,clear;frame->GetPixel(0,0,&red);frame->GetPixel(1,0,&green);frame->GetPixel(3,0,&clear);
            check(red.GetRed()==255&&green.GetGreen()==255&&clear.GetAlpha()==0,"partial GIF frames retain earlier pixels and transparent background");
            frames.started=GetTickCount64()-250;frame=decode(transparentHex,true);frame->GetPixel(1,0,&clear);frame->GetPixel(2,0,&pixel);
            check(clear.GetAlpha()==0&&pixel.GetBlue()==255,"GIF disposal clears the previous frame before the next frame");
        }
        doc={};Reaction r;r.id=doc.nextId++;r.slots={{"R",{}},{"P",{}}};doc.reactions.push_back(r);selection={r.id};targetContainer=r.id;targetSlot=1;history.clear();dirty=false;
        auto before=toNative(doc);putClipboard(RegisterClipboardFormatW(L"GIF"),bytes.data(),bytes.size());pasteClipboard();
        check(doc.objects.size()==1&&doc.reactions[0].slots[0].members.empty()&&doc.reactions[0].slots[1].members==std::vector<int>{doc.objects[0].id},"paste encoded GIF into only the chosen mechanism slot");
        check(doc.objects[0].imageHex==hex&&doc.objects[0].mime=="image/gif","slot fitting preserves the original GIF bytes");
        auto native=fromNative(toNative(doc)),xml=fromCdxml(toCdxml(doc));
        check(native.objects[0].imageHex==hex&&xml.objects[0].imageHex==hex&&xml.objects[0].mime=="image/gif","native and CDXML round trips preserve every frame");
        auto svg=toSvg(makeScene(doc));check(svg.find("data:image/gif;base64,")!=std::string::npos,"SVG embeds the animated GIF with its correct MIME type");
        auto copied=doc.subset({doc.objects[0].id});check(SUCCEEDED(office::copy(copied)),"copy GIF as native clipboard object");auto own=clipboardDocument();check(own&&own->objects[0].imageHex==hex,"copy and paste within YoungDraw preserves the animated original");
        check(history.undo(doc)&&toNative(doc)==before,"GIF insertion can be undone completely");
        auto path=fs::absolute(L"animated.gif").wstring();std::vector<BYTE> drop(sizeof(DROPFILES)+(path.size()+2)*sizeof(wchar_t),0);auto* header=reinterpret_cast<DROPFILES*>(drop.data());header->pFiles=sizeof(DROPFILES);header->fWide=TRUE;memcpy(drop.data()+sizeof(DROPFILES),path.c_str(),path.size()*sizeof(wchar_t));
        putClipboard(CF_HDROP,drop.data(),drop.size());fillTarget(false);check(doc.objects.size()==1&&doc.objects[0].imageHex==hex,"structure paste button accepts a GIF copied in File Explorer");
        Bitmap surface(ww,wh,PixelFormat32bppARGB);Graphics graphics(&surface);auto dc=graphics.GetHDC();paint(dc);graphics.ReleaseHDC(dc);check(animationVisible,"canvas schedules animation redraws");
        auto saved=toNative(doc);SendMessageW(hwnd,WM_TIMER,912,0);check(toNative(doc)==saved,"animation timer never changes saved document or undo state");
        doc={};dc=graphics.GetHDC();paint(dc);graphics.ReleaseHDC(dc);check(!animationVisible,"empty canvas stops animation redraws");
        bool failed=false;try{imageDocument("GIF89a broken");}catch(...){failed=true;}check(failed,"reject damaged GIF data without inserting anything");
        // Release clipboard ownership while its native owner window is alive.
        // OLE may dispatch clipboard callbacks during owner destruction.
        OleSetClipboard(previous.Get());if(previous)OleFlushClipboard();previous.Reset();clipboardRestored=true;
        DestroyWindow(hwnd);std::cout<<"Animated GIF regression tests passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    if(!clipboardRestored){OleSetClipboard(previous.Get());if(previous)OleFlushClipboard();previous.Reset();}office::stop();imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return result;
}

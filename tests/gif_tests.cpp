#include "../src/winmain.cpp"
#include <shlobj.h>
#include <iostream>
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);std::cout<<message<<std::endl;}
static void putClipboard(UINT format,const void* bytes,size_t size){
    check(OpenClipboard(win)!=0,"open clipboard");EmptyClipboard();
    auto memory=GlobalAlloc(GMEM_MOVEABLE,size);check(memory!=nullptr,"allocate clipboard payload");
    void* data=GlobalLock(memory);memcpy(data,bytes,size);GlobalUnlock(memory);
    auto result=SetClipboardData(format,memory);CloseClipboard();if(!result)GlobalFree(memory);check(result!=nullptr,"set clipboard payload");
}
int main(){
    OleInitialize(nullptr);GdiplusStartupInput startup;ULONG_PTR token;GdiplusStartup(&token,&startup,nullptr);
    Microsoft::WRL::ComPtr<IDataObject> previous;OleGetClipboard(&previous);
    int result=0;
    try{
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawGifRegression";RegisterClassW(&wc);
        auto hwnd=CreateWindowW(wc.lpszClassName,L"GIF test",WS_OVERLAPPEDWINDOW,0,0,1440,940,nullptr,nullptr,wc.hInstance,nullptr);check(hwnd!=nullptr,"create canvas window");
        check(SUCCEEDED(office::start({hwnd,[](const Document&){},[](){return doc;},officePicture},false,false)),"initialize native clipboard rendering callbacks");
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
        DestroyWindow(hwnd);std::cout<<"Animated GIF regression tests passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    OleSetClipboard(previous.Get());if(previous)OleFlushClipboard();previous.Reset();office::stop();imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return result;
}

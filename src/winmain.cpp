#include "win_editor_files.h"
#include "win_editor_images.h"
#include "win_editor_render.h"
#include "win_editor_state.h"
#include "win_editor_tabs.h"
#include "win_editor_window.h"
#include "cdxml.h"
#include "lab_templates_data.h"
#include "mechanism_data.h"
#include "office.h"
#include "win_dialogs.h"
#include "win_splash.h"
#include "win_storage.h"
#include "win_theme.h"
#include <shellapi.h>

using namespace yd;
using namespace yd::editor;

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){SetProcessDPIAware();int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);OleInitialize(nullptr);bool embedding=argc>1&&(_wcsicmp(argv[1],L"/Embedding")==0||_wcsicmp(argv[1],L"-Embedding")==0);GdiplusStartupInput start;ULONG_PTR token;GdiplusStartup(&token,&start,nullptr);setMechanismTextMeasure(measure);
 if(argc==2&&std::wstring(argv[1])==L"--copy-office-demo"){office::start({nullptr,[](const Document&){},[](){return coordinateDemo();},officePicture},false);auto result=office::copy(coordinateDemo());if(SUCCEEDED(result))result=OleFlushClipboard();LocalFree(argv);imageCache.clear();GdiplusShutdown(token);OleUninitialize();return FAILED(result)?int(result):0;}
 if(argc==2&&std::wstring(argv[1])==L"--test-office"){office::start({nullptr,[](const Document&){},[](){return coordinateDemo();},officePicture},false,false);auto result=office::selfTest();LocalFree(argv);imageCache.clear();GdiplusShutdown(token);OleUninitialize();return FAILED(result)?int(result):0;}
 if(argc==3&&std::wstring(argv[1])==L"--export-lab-templates"){try{fs::create_directories(argv[2]);size_t i=0;for(auto& entry:labTemplates){auto d=fromNative(entry.data);auto path=fs::path(argv[2])/std::to_wstring(i++);writePreview(path.wstring()+L".png",d);writeFile(path.wstring()+L".svg",toSvg(makeScene(d)));writeFile(path.wstring()+L".cdxml",toCdxml(d));}writePreview((fs::path(argv[2])/L"chair.png").wstring(),chairTemplate());writeFile((fs::path(argv[2])/L"chair.svg").wstring(),toSvg(makeScene(chairTemplate())));LocalFree(argv);GdiplusShutdown(token);OleUninitialize();return 0;}catch(...){LocalFree(argv);GdiplusShutdown(token);OleUninitialize();return 1;}}
 if(argc==3&&std::wstring(argv[1])==L"--export-demo"){try{writeFile(std::wstring(argv[2])+L".cdxml",toCdxml(doc));writeFile(std::wstring(argv[2])+L".svg",toSvg(makeScene(doc)));writeFile(std::wstring(argv[2])+L"-mechanism.ydraw",toNative(mechanismDemo()));writeFile(std::wstring(argv[2])+L"-mechanism.cdxml",toCdxml(mechanismDemo()));writeFile(std::wstring(argv[2])+L"-mechanism.svg",toSvg(makeScene(mechanismDemo())));writeFile(std::wstring(argv[2])+L"-coordinate.ydraw",toNative(coordinateDemo()));writeFile(std::wstring(argv[2])+L"-coordinate.cdxml",toCdxml(coordinateDemo()));writeFile(std::wstring(argv[2])+L"-coordinate.svg",toSvg(makeScene(coordinateDemo())));Document rigid=coordinateDemo();rigid.plots[0].mode=3;rigid.plots[0].series[0].dash=1;rigid.plots[0].series[0].color="#0e6d89";rigid.plots[0].states[1].marker=3;rigid.plots[0].states[3].marker=4;rigid.plots[0].series.push_back({"Path 2","#b44758",{0,26,10,19,-9.9},0,2,2,2.5});writeFile(std::wstring(argv[2])+L"-rigid.ydraw",toNative(rigid));writeFile(std::wstring(argv[2])+L"-rigid.cdxml",toCdxml(rigid));writeFile(std::wstring(argv[2])+L"-rigid.svg",toSvg(makeScene(rigid)));writePreview(std::wstring(argv[2])+L"-rigid.png",rigid);writePreview(std::wstring(argv[2])+L"-coordinate.png",coordinateDemo());writePreview(std::wstring(argv[2])+L"-mechanism.png",mechanismDemo());Document partial;int pa=partial.addAtom({30,45}),pb=partial.addAtom({56,30}),pc=partial.addAtom({82,45});partial.addBond(pa,pb);partial.addBond(pc,pb);delocalizeBonds(partial,partial.allIds());writeFile(std::wstring(argv[2])+L"-delocalized.ydraw",toNative(partial));writeFile(std::wstring(argv[2])+L"-delocalized.cdxml",toCdxml(partial));writeFile(std::wstring(argv[2])+L"-delocalized.svg",toSvg(makeScene(partial)));writePreview(std::wstring(argv[2])+L"-delocalized.png",partial);Document cyclic=mechanismDemo();optimizeMechanism(cyclic,{},true);writeFile(std::wstring(argv[2])+L"-cycle.ydraw",toNative(cyclic));writePreview(std::wstring(argv[2])+L"-cycle.png",cyclic);LocalFree(argv);GdiplusShutdown(token);OleUninitialize();return 0;}catch(...){LocalFree(argv);GdiplusShutdown(token);OleUninitialize();return 1;}}
 if(argc==3&&std::wstring(argv[1])==L"--migrate-ydraw"){
     int result=1;
     try{
         fs::path source=argv[2],target=source,backup=source;
         if(_wcsicmp(source.extension().c_str(),L".ydraw")!=0)throw std::runtime_error("Expected .ydraw input");
         target.replace_extension(L".cdxml");backup+=L".bak";
         auto original=fromCdxml(readFile(source.wstring()));
         auto cdxml=toCdxml(original);
         if(toNative(fromCdxml(cdxml))!=toNative(original))throw std::runtime_error("CDXML verification failed");
         if(fs::exists(target)){
             if(readFile(target.wstring())!=cdxml)throw std::runtime_error("CDXML destination already exists");
         }else storage::writeAtomic(target.wstring(),cdxml,false);
         if(!MoveFileExW(source.c_str(),backup.c_str(),MOVEFILE_WRITE_THROUGH))
             throw std::runtime_error("Could not archive the original");
         result=0;
     }catch(...){}
     LocalFree(argv);GdiplusShutdown(token);OleUninitialize();return result;
 }
 if(argc==4&&std::wstring(argv[1])==L"--convert"){try{doc=fromCdxml(readFile(argv[2]));writeFile(std::wstring(argv[3])+L".ydraw",toNative(doc));writeFile(std::wstring(argv[3])+L".cdxml",toCdxml(doc));writeFile(std::wstring(argv[3])+L".svg",toSvg(scene()));LocalFree(argv);GdiplusShutdown(token);OleUninitialize();return 0;}catch(const std::exception& e){try{writeFile(std::wstring(argv[3])+L".error.txt",e.what());}catch(...){} LocalFree(argv);GdiplusShutdown(token);OleUninitialize();return 1;}}
 isolatedStructureEditing=argc==4&&std::wstring(argv[1])==L"--edit-structure";if(isolatedStructureEditing)isolatedStructureResult=argv[3];
 StartupSplash splash(instance,!embedding && !isolatedStructureEditing && show!=SW_HIDE && show!=SW_SHOWMINIMIZED && show!=SW_SHOWMINNOACTIVE && show!=SW_MINIMIZE);
 std::vector<std::pair<Document,std::wstring>> startupDocuments;
 if(isolatedStructureEditing){try{startupDocuments.push_back({fromNative(readFile(argv[2])),argv[2]});}catch(const std::exception& e){MessageBoxW(nullptr,ui::wide(e.what()).c_str(),L"구조 편집",MB_OK|MB_ICONWARNING);LocalFree(argv);return 1;}}
 else if(!embedding)for(int i=1;i<argc;i++){try{startupDocuments.push_back({fromCdxml(readFile(argv[i])),argv[i]});}catch(...){}}
 if(!startupDocuments.empty()){doc=std::move(startupDocuments[0].first);filePath=startupDocuments[0].second;Document beforeUpgrade=doc;if(!isolatedStructureEditing)normalizeChemicalLabels(doc);dirty=!isolatedStructureEditing&&(upgradeBondGeometry(doc)||toNative(doc)!=toNative(beforeUpgrade));if(dirty)history.checkpoint(beforeUpgrade);tool=Select;}

 RECT work{};
 SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
 int x=CW_USEDEFAULT,y=CW_USEDEFAULT;
 int width=std::min(1440,int(work.right-work.left)),height=std::min(940,int(work.bottom-work.top));
 if(width<=0||height<=0){width=1440;height=940;}
 if(isolatedStructureEditing){
     STARTUPINFOW startup{sizeof(startup)};GetStartupInfoW(&startup);
     if(startup.dwFlags&STARTF_USEPOSITION){x=static_cast<LONG>(startup.dwX);y=static_cast<LONG>(startup.dwY);}
     if(startup.dwFlags&STARTF_USESIZE){width=int(startup.dwXSize);height=int(startup.dwYSize);}
 }
 LocalFree(argv);WNDCLASSW wc{};wc.style=CS_DBLCLKS;wc.lpfnWndProc=proc;wc.hInstance=instance;wc.lpszClassName=L"YoungDrawWindow";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));RegisterClassW(&wc);HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"YoungDraw",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,x,y,width,height,nullptr,nullptr,instance,nullptr);if(!hwnd)return 1;SendMessageW(hwnd,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED)));fit();for(size_t i=1;i<startupDocuments.size();i++)newDocumentTab(std::move(startupDocuments[i].first),startupDocuments[i].second);if(!isolatedStructureEditing&&FAILED(office::start({hwnd,[](const Document& d){storeDocumentTab();for(auto& tab:documents)tab.office=false;officeDocument=false;newDocumentTab(d,L"",true);},[](){return officeCurrentDocument();},officePicture},embedding)))MessageBoxW(hwnd,L"Office 편집 개체를 등록하지 못했습니다.",L"YoungDraw",MB_OK|MB_ICONWARNING);if(!embedding)splash.finish(hwnd,show);fit();UpdateWindow(hwnd);ACCEL commandSearch{FVIRTKEY|FCONTROL|FSHIFT,'F',230};
 auto accelerators=CreateAcceleratorTableW(&commandSearch,1);
 MSG msg{};
 while(GetMessageW(&msg,nullptr,0,0)>0){
     if(accelerators&&IsWindowEnabled(hwnd)&&(msg.hwnd==hwnd||IsChild(hwnd,msg.hwnd))&&TranslateAcceleratorW(hwnd,accelerators,&msg))continue;
     TranslateMessage(&msg);DispatchMessageW(&msg);
 }
 if(accelerators)DestroyAcceleratorTable(accelerators);
 office::stop();imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return int(msg.wParam);}

#include "ligand_spatial.h"
#include "cdxml.h"
#include "scene.h"
#include "win_editor_state.h"
#include "win_editor_window.h"
#include "win_editor_commands.h"
#include "win_editor_structure.h"
#include "win_editor_render.h"
#include "win_editor_files.h"
#include "win_editor_tabs.h"
#include "win_dialogs.h"
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace yd;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static const LigandTemplate& entry(const std::string& slug){for(const auto& e:ligandTemplates())if(slug==e.slug)return e;throw std::runtime_error("missing template");}
static double distance(const Atom& a,const Atom& b){const double z=*a.depth*a.drawingScale-*b.depth*b.drawingScale;return std::sqrt((a.p.x-b.p.x)*(a.p.x-b.p.x)+(a.p.y-b.p.y)*(a.p.y-b.p.y)+z*z);}
static bool cancelForm=false,formDriven=false,previewSeen=false;
static void CALLBACK drive(HWND owner,UINT,UINT_PTR timer,DWORD){
    HWND dialog=nullptr;while((dialog=FindWindowExW(nullptr,dialog,L"YoungDrawForm",L"입체 변환·회전 · BQCN / Co–Si")))if(GetWindow(dialog,GW_OWNER)==owner)break;
    if(!dialog)return;KillTimer(owner,timer);formDriven=true;
    SetDlgItemTextW(dialog,103,L"35");
    previewSeen=std::all_of(yd::editor::doc.atoms.begin(),yd::editor::doc.atoms.end(),[](const auto& a){return a.depth.has_value();});
    RECT rect{};GetClientRect(dialog,&rect);Gdiplus::Bitmap capture(rect.right,rect.bottom);Gdiplus::Graphics g(&capture);auto dc=g.GetHDC();PrintWindow(dialog,dc,PW_CLIENTONLY);g.ReleaseHDC(dc);
    yd::editor::writeFile(L"ligand-spatial-dialog.png",yd::editor::encodePng(capture));
    PostMessageW(dialog,WM_COMMAND,cancelForm?IDCANCEL:IDOK,0);
}
int main(){
 try{
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token;Gdiplus::GdiplusStartup(&token,&input,nullptr);OleInitialize(nullptr);
    check(ligandTemplates().size()==14,"both verified ligand families include 14 templates");
    for(const auto& t:ligandTemplates()){
        const auto& d=t.drawing;check(d.validate().empty(),"template graph validates");
        auto round=fromNative(toNative(d));check(toNative(round)==toNative(d),"native retains all depth and ink");
        auto cdxml=fromCdxml(toCdxml(d));check(toNative(cdxml)==toNative(d),"smart CDXML retains editable 3D data");
        auto standard=toCdxml(d);auto annotation=standard.rfind("<annotation");if(annotation!=std::string::npos)standard.erase(annotation,standard.find("/>",annotation)-annotation+2);
        const auto ordinary=fromCdxml(standard);check(ordinary.atoms.size()==d.atoms.size()&&ordinary.bonds.size()==d.bonds.size(),"ordinary CDXML retains editable ligand topology");
        for(const auto& bond:ordinary.bonds)if(bond.kind==BondKind::Dative)check(bond.customDisplay=="Solid","plain coordination strokes survive ordinary CDXML");
        if(t.spatial){
            check(std::all_of(d.atoms.begin(),d.atoms.end(),[](const auto& a){return a.depth.has_value();}),"every projected atom has depth");
            auto paper=d;for(auto& atom:paper.atoms)atom.depth.reset();check(applyLigandSpatial(paper,paper.allIds(),d),"explicit paper stereobonds agree with stored 3D chirality");
            auto rotated=d;rotateSpatial(rotated,d.allIds(),31,47,0);rotateSpatial(rotated,rotated.allIds(),0,-47,0);rotateSpatial(rotated,rotated.allIds(),-31,0,0);
            for(size_t i=0;i<d.atoms.size();i++){
                check((rotated.atoms[i].p-d.atoms[i].p).length()<1e-6&&std::abs(*rotated.atoms[i].depth-*d.atoms[i].depth)<1e-6,"inverse rotations restore coordinates");
                for(size_t j=0;j<i;j++)check(std::abs(distance(rotated.atoms[i],rotated.atoms[j])-distance(d.atoms[i],d.atoms[j]))<1e-6,"rigid rotations preserve all 3D distances");
            }
            rotateSpatial(rotated,rotated.allIds(),0,180,0);for(const auto& b:rotated.bonds)check(b.color==b.depthColor&&!b.depthRearColor.empty(),"visible skeleton remains original ink after rotating, with local rear accent configured");
            rotateSpatial(rotated,rotated.allIds(),0,0,0,0);for(const auto& b:rotated.bonds)check(b.color==b.depthColor&&b.depthRearColor==b.depthColor,"zero gray restores original ink");
            auto clean=d;cleanStructure(clean);check(toNative(clean)==toNative(d),"2D cleanup preserves projected structures");
        }
        auto path=ui::wide(t.slug);yd::editor::writePreview(path+L".png",d);yd::editor::writeFile(path+L".svg",toSvg(makeScene(d)));yd::editor::writeFile(path+L".cdxml",toCdxml(d));
    }
    for(const std::string family:{"bqcn","bqcn-mirror","mesip2"}){
        auto planar=entry(family+"-planar").drawing;
        check(applyLigandSpatial(planar,planar.allIds(),entry(family+"-ligand-3d").drawing),(family+" planar to own 3D reference").c_str());
        check(planar.atoms.size()==entry(family+"-planar").drawing.atoms.size(),"conversion keeps atom identity/count");
    }
    auto enantiomer=entry("bqcn-planar").drawing;const auto stereoBefore=toNative(enantiomer);
    check(!applyLigandSpatial(enantiomer,enantiomer.allIds(),entry("bqcn-mirror-ligand-3d").drawing)&&toNative(enantiomer)==stereoBefore,"specified BQCN enantiomer is not silently inverted");
    auto wrong=entry("bqcn-planar").drawing;auto original=toNative(wrong);check(!applyLigandSpatial(wrong,wrong.allIds(),entry("mesip2-ligand-3d").drawing)&&toNative(wrong)==original,"wrong ligand is rejected without mutation");
    auto pendant=entry("mesip2-co-3d").drawing;int co=0;for(const auto& a:pendant.atoms)if(a.element==27)co=a.id;
    int carbon=pendant.addAtom(pendant.atom(co)->p+Vec{0,-40});int oxygen=pendant.addAtom(pendant.atom(co)->p+Vec{0,-65},8);pendant.addBond(co,carbon);pendant.addBond(carbon,oxygen,BondKind::Triple);auto offset=pendant.atom(oxygen)->p-pendant.atom(carbon)->p;
    for(auto& a:pendant.atoms)a.depth.reset();
    check(applyLigandSpatial(pendant,{co},entry("mesip2-co-3d").drawing),"bound ligand recognizes and retains pendant CO");check(((pendant.atom(oxygen)->p-pendant.atom(carbon)->p)-offset).length()<1e-6,"pendant CO keeps its geometry");rotateSpatial(pendant,{co},20,30,0);check(pendant.atoms.size()==31,"no atoms lost from pendant CO");
    auto renamed=entry("mesip2-co-3d").drawing;Document shifted;shifted.append(renamed,{500,300});scaleStructures(shifted,shifted.allIds(),1.5);rotateSpatial(shifted,shifted.allIds(),20,0,0);check(fromCdxml(toCdxml(shifted)).atoms[0].depth.has_value(),"copy resize rotation and save retain depth");
    {
        using namespace yd::editor;
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawLigandTests";RegisterClassW(&wc);
        auto hwnd=CreateWindowExW(0,wc.lpszClassName,L"Ligand tests",WS_OVERLAPPEDWINDOW,0,0,1440,940,nullptr,nullptr,wc.hInstance,nullptr);win=hwnd;
        doc=entry("mesip2-planar").drawing;selection=doc.allIds();history.clear();targetContainer=0;changed();auto before=toNative(doc);
        cancelForm=true;SetTimer(hwnd,903,10,drive);SendMessageW(hwnd,WM_COMMAND,ligandSpatialCommand,0);KillTimer(hwnd,903);check(formDriven&&previewSeen&&toNative(doc)==before&&!history.undo(doc),"live preview cancel restores complete document without undo step");
        cancelForm=false;formDriven=previewSeen=false;SetTimer(hwnd,903,10,drive);SendMessageW(hwnd,WM_COMMAND,ligandSpatialCommand,0);KillTimer(hwnd,903);check(formDriven&&previewSeen&&doc.atoms[0].depth&&history.undo(doc)&&toNative(doc)==before&&!history.undo(doc),"UI apply commits exactly one reversible edit");
        doc={};selection.clear();history.clear();DestroyWindow(hwnd);win=nullptr;
    }
    std::cout<<"Ligand templates, stereochemistry, projection, gray, storage and isolated UI passed\n";
    OleUninitialize();Gdiplus::GdiplusShutdown(token);return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}

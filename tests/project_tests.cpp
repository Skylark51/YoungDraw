#include "win_projects.h"
#include "win_storage.h"
#include "win_editor_render.h"
#include "cdxml.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs=std::filesystem;
using namespace yd::projects;
static void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
static std::string read(const fs::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
static void checkExport(const fs::path& temp,const yd::Document& drawing){
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token{};
    check(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok,"start GDI+");
    struct Shutdown {ULONG_PTR token;~Shutdown(){Gdiplus::GdiplusShutdown(token);}} shutdown{token};
    auto p=Project::create(temp/L"projects",L"그림 내보내기");
    auto bytes=yd::toCdxml(drawing);
    p.addDrawing(L"반응 / TS",bytes);
    p.addDrawing(L"반응 _ ts",bytes);
    p.addDrawing(L"CON",bytes);
    auto originalImage=temp/L"참고 이미지.png";
    yd::storage::writeAtomic(originalImage.wstring(),"original image bytes");
    p.add(originalImage);
    auto originalSidecar=imageDrawing(p.folder/p.items.back().file);
    yd::storage::writeAtomic(originalSidecar.wstring(),bytes);
    auto exportBase=temp/L"Named export";
    auto exported=p.exportTo(exportBase,[](const fs::path& cdxml,const fs::path& png){
        yd::editor::writePreview(png.wstring(),yd::fromCdxml(read(cdxml)),true);
    });
    check(exported.items[0].file==L"반응 _ TS.cdxml"&&exported.items[1].file==L"반응 _ ts (2).cdxml"&&exported.items[2].file==L"_CON.cdxml","export uses safe unique display names");
    check(Project::load(p.folder).items[0].file==p.items[0].file,"export leaves source project files unchanged");
    for(const auto& item:exported.items){
        auto path=exported.folder/item.file;
        auto cdxml=image(path)?imageDrawing(path):path;
        check(read(cdxml)==bytes,"exported CDXML preserves original bytes");
        auto png=cdxml;png.replace_extension(L".png");
        Gdiplus::Bitmap bitmap(png.c_str());
        Gdiplus::Color pixel;
        check(bitmap.GetPixel(0,0,&pixel)==Gdiplus::Ok&&pixel.GetA()==0,"exported PNG has transparent margins");
        bool ink=false;
        for(UINT y=0;y<bitmap.GetHeight()&&!ink;y++)for(UINT x=0;x<bitmap.GetWidth()&&!ink;x++)
            if(bitmap.GetPixel(x,y,&pixel)==Gdiplus::Ok&&pixel.GetA()==255)ink=true;
        check(ink,"exported PNG contains the drawing");
    }
    auto imported=Project::importFrom(exported.folder,temp/L"Named import");
    check(imported.items[0].file==exported.items[0].file&&read(imageDrawing(imported.folder/imported.items.back().file))==bytes,"named export reimports with linked image edit");
    auto failedBase=temp/L"Failed export";
    bool failed=false;
    try{p.exportTo(failedBase,[](const fs::path&,const fs::path&){throw std::runtime_error("encoder failed");});}
    catch(const std::exception&){failed=true;}
    check(failed&&fs::is_empty(failedBase),"failed PNG export cleans up incomplete project");
}
int main(){
    const auto temp=fs::temp_directory_path()/(L"YoungDrawProjectsTest-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        fs::create_directories(temp);auto p=Project::create(temp/L"projects",L"촉매 / 연구 프로젝트");p.note=L"프로젝트 설명\r\n인용: \"노트\" \\ test";p.save();
        auto native=temp/L"구조.ydraw",xml=temp/L"반응.CDXML",png=temp/L"참고.png",gif=temp/L"동영상.gif";
        const std::string binary("\0\1\2\xff",4);auto drawing=yd::demo();yd::storage::writeAtomic(native.wstring(),yd::toNative(drawing));yd::storage::writeAtomic(xml.wstring(),"<CDXML>preserve original</CDXML>");yd::storage::writeAtomic(png.wstring(),binary);yd::storage::writeAtomic(gif.wstring(),"GIF89a");
        p.add(native);p.add(xml);p.add(png);p.add(gif);p.items[1].name=L"반응 중간체 / TS: 1";p.items[1].note=L"다중 행\r\n한글 αβ \"quoted\" \\path";p.save();
        auto loaded=Project::load(p.folder);check(loaded.name==p.name&&loaded.note==p.note&&loaded.items.size()==4,"project and note reload");
        check(loaded.items[1].name==p.items[1].name&&loaded.items[1].note==p.items[1].note,"unicode names and multiline notes survive");
        check(read(p.folder/p.items[1].file)==read(xml)&&read(p.folder/p.items[2].file)==binary,"original XML and binary image bytes preserved");
        check(fs::path(p.items[0].file).extension()==L".cdxml"&&
              yd::toNative(yd::fromCdxml(read(p.folder/p.items[0].file)))==yd::toNative(drawing),"legacy imports become real CDXML");
        {
            auto migrated=Project::create(temp/L"projects",L"기존 프로젝트");
            migrated.items.push_back({L"legacy.ydraw",L"기존 구조",L"메모"});migrated.save();
            yd::storage::writeAtomic((migrated.folder/L"legacy.cdxml").wstring(),yd::toCdxml(drawing));
            yd::storage::writeAtomic((migrated.folder/L"legacy.ydraw.bak").wstring(),yd::toNative(drawing));
            auto reopened=Project::load(migrated.folder);
            check(reopened.items[0].file==L"legacy.cdxml"&&reopened.items[0].note==L"메모","project resolves migrated asset without losing notes");
        }
        fs::remove(native);check(fs::exists(p.folder/p.items[0].file),"copied project assets survive source removal");
        auto stable=p.items[1].file;loaded.move(1,3);loaded=Project::load(p.folder);check(loaded.items[3].file==stable,"drag order persists across reload");loaded.move(3,0);loaded=Project::load(p.folder);check(loaded.items[0].file==stable,"move to first persists");
        check(displayName((p.folder/stable).wstring())==p.items[1].name,"tabs resolve display name independently of stored filename");
        check(displayName((p.folder/(p.items[2].file+L".ydraw")).wstring())==p.items[2].name,"edited image sidecar resolves same item name");
        auto count=loaded.items.size();bool rejected=false;try{loaded.add(temp/L"unknown.exe");}catch(...){rejected=true;}check(rejected&&loaded.items.size()==count,"unsupported files cannot mutate project");
        loaded.addDrawing(L"새 구조","document bytes");check(read(loaded.folder/loaded.items.back().file)=="document bytes","new and copied drawings persist");
        auto sidecar=p.folder/(p.items[2].file+L".ydraw");yd::storage::writeAtomic(sidecar.wstring(),"edited image document");
        auto exported=loaded.copyTo(temp/L"USB 이동 폴더");
        check(exported.name==loaded.name&&exported.note==loaded.note&&exported.items.size()==loaded.items.size(),"portable copy preserves project metadata");
        check(read(exported.folder/sidecar.filename())=="edited image document","portable copy preserves image edit sidecar");
        auto imported=Project::importFrom(exported.folder/L"project.ydproject",temp/L"home projects");
        check(imported.folder!=exported.folder&&imported.items[0].file==loaded.items[0].file&&imported.items[0].note==loaded.items[0].note,"import preserves order and notes in independent local copy");
        check(read(imported.folder/imported.items[0].file)==read(loaded.folder/loaded.items[0].file),"import preserves original asset bytes");
        auto second=Project::importFrom(exported.folder,temp/L"home projects");check(second.folder!=imported.folder,"reimport never overwrites an existing project");
        fs::remove(exported.folder/exported.items[0].file);bool missing=false;try{Project::importFrom(exported.folder,temp/L"failed imports");}catch(...){missing=true;}
        check(missing&&!fs::exists(temp/L"failed imports"),"incomplete transfer rejected before creating project");
        check(fs::exists(imported.folder/imported.items[0].file),"import is independent of transferred source");
        auto before=read(p.folder/L"project.ydproject");loaded.name=L" ";rejected=false;try{loaded.save();}catch(...){rejected=true;}check(rejected&&read(p.folder/L"project.ydproject")==before,"invalid edit preserves saved manifest");
        auto corrupt=Project::create(temp/L"projects",L"경로 검사");yd::storage::writeAtomic((corrupt.folder/L"project.ydproject").wstring(),"YoungDrawProject 1\n\"test\" \"\"\n\"../escape.cdxml\" \"name\" \"\"\n");rejected=false;try{Project::load(corrupt.folder);}catch(...){rejected=true;}check(rejected,"manifest traversal rejected");
        {
            auto tree=Project::create(temp/L"projects",L"폴더 회귀");auto f=tree.addFolder(L"Normal");auto folder=tree.items[f].file;
            auto nested=tree.addFolder(L"하위 폴더",folder);auto child=tree.items[nested].file;
            auto a=tree.addDrawing(L"A",yd::toCdxml(drawing));auto aFile=tree.items[a].file;
            auto b=tree.addDrawing(L"B",yd::toCdxml(drawing));auto bFile=tree.items[b].file;
            tree.relocate({a,b},folder,tree.items.size());tree=Project::load(tree.folder);
            check(tree.items[2].parent==folder&&tree.items[3].parent==folder,"multi move and Unicode folder reload");
            auto before=read(tree.folder/L"project.ydproject");bool cycle=false;
            try{tree.relocate({0},child,tree.items.size());}catch(...){cycle=true;}
            check(cycle&&read(tree.folder/L"project.ydproject")==before&&tree.items[0].parent.empty(),"folder cycles rejected and memory rolled back");
            tree.relocate({3},L"",2);check(tree.items[2].file==bFile&&tree.items[2].parent.empty()&&tree.items[3].file==aFile,"filtered multi move insertion preserves order");
            auto copy=tree.copyTo(temp/L"folder copy");check(copy.items.size()==4&&copy.items[1].parent==folder&&copy.items[3].parent==folder,"portable copy retains hierarchy");
            auto exportedTree=tree.exportTo(temp/L"folder export",[](const fs::path&,const fs::path& png){yd::storage::writeAtomic(png.wstring(),"png");});
            check(exportedTree.items[1].parent==folder&&exportedTree.items[3].parent==folder,"PNG export retains virtual folders");
            auto subset=tree.copySelectionTo(temp/L"selection copy",{3});check(subset.items.size()==2&&subset.items[0].isFolder&&subset.items[1].file==aFile,"selection export includes ancestors without unrelated siblings");
            auto duplicates=tree.duplicate({0});check(duplicates.size()==3&&tree.items[duplicates[1]].parent==tree.items[duplicates[0]].file&&tree.items[duplicates[2]].parent==tree.items[duplicates[0]].file,"folder duplication remaps descendants");
            check(read(tree.folder/tree.items[duplicates[2]].file)==read(tree.folder/aFile),"duplication preserves original document bytes");
            tree.items[3].parent=L"missing.ydfolder";bool missingFolder=false;try{tree.save();}catch(...){missingFolder=true;}check(missingFolder,"missing folder rejected");
            auto legacy=Project::create(temp/L"projects",L"v1");yd::storage::writeAtomic((legacy.folder/L"project.ydproject").wstring(),"YoungDrawProject 1\n\"legacy\" \"note\"\n\"a.cdxml\" \"drawing\" \"memo\"\n");
            auto old=Project::load(legacy.folder);check(old.items.size()==1&&old.items[0].parent.empty()&&!old.items[0].isFolder,"v1 project reads without hierarchy");
        }
        checkExport(temp,drawing);
        fs::remove_all(temp);std::cout<<"PASS: project assets, CDXML, image bytes, names, notes, order and recovery\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}

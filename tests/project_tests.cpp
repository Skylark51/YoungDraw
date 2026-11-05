#include "win_projects.h"
#include "win_storage.h"
#include "cdxml.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs=std::filesystem;
using namespace yd::projects;
static void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
static std::string read(const fs::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
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
        fs::remove_all(temp);std::cout<<"PASS: project assets, CDXML, image bytes, names, notes, order and recovery\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}

#include "win_projects.h"
#include "win_storage.h"
#include "cdxml.h"
#include "win_dialogs.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <stdexcept>

namespace yd::projects {
namespace fs=std::filesystem;
static std::wstring extension(const fs::path& path){auto s=path.extension().wstring();for(auto& c:s)c=wchar_t(towlower(c));return s;}
bool image(const fs::path& p){auto e=extension(p);return e==L".png"||e==L".jpg"||e==L".jpeg"||e==L".gif";}
fs::path imageDrawing(const fs::path& source){
    fs::path current=source;current+=L".cdxml";
    fs::path legacy=source;legacy+=L".ydraw";
    return !fs::exists(current)&&fs::exists(legacy)?legacy:current;
}
bool supported(const fs::path& p){auto e=extension(p);return image(p)||e==L".ydraw"||e==L".cdxml";}
fs::path root(){wchar_t value[32768]{};DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",value,32768);if(!n||n>=32768)throw std::runtime_error("프로젝트 저장 위치를 찾을 수 없습니다.");return fs::path(value)/L"YoungDraw"/L"Projects";}
static std::wstring unique(){static unsigned serial=0;return std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++serial);}
static bool validName(const std::wstring& s){return !s.empty()&&s.find_first_not_of(L" \r\n\t")!=std::wstring::npos&&s.size()<=200;}
void Project::save() const {
    if(!validName(name))throw std::runtime_error("이름을 1~200자로 입력하세요.");
    std::ostringstream out;out<<"YoungDrawProject 1\n"<<std::quoted(ui::utf8(name))<<' '<<std::quoted(ui::utf8(note))<<'\n';
    for(const auto& item:items){if(!validName(item.name)||fs::path(item.file).filename()!=fs::path(item.file)||!supported(item.file))throw std::runtime_error("프로젝트 항목 정보가 올바르지 않습니다.");out<<std::quoted(ui::utf8(item.file))<<' '<<std::quoted(ui::utf8(item.name))<<' '<<std::quoted(ui::utf8(item.note))<<'\n';}
    storage::writeAtomic((folder/L"project.ydproject").wstring(),out.str());
}
Project Project::load(const fs::path& dir){
    std::ifstream in(dir/L"project.ydproject",std::ios::binary);std::string magic;int version=0;
    if(!(in>>magic>>version)||magic!="YoungDrawProject"||version!=1)throw std::runtime_error("프로젝트 정보를 읽을 수 없습니다.");
    Project p;p.folder=dir;std::string name,note;if(!(in>>std::quoted(name)>>std::quoted(note)))throw std::runtime_error("프로젝트 정보가 손상되었습니다.");p.name=ui::wide(name);p.note=ui::wide(note);
    std::vector<std::wstring> files;
    while(in>>std::ws&&!in.eof()){std::string file;if(!(in>>std::quoted(file)>>std::quoted(name)>>std::quoted(note)))throw std::runtime_error("프로젝트 항목 정보가 손상되었습니다.");Item item{ui::wide(file),ui::wide(name),ui::wide(note)};if(fs::path(item.file).filename()!=fs::path(item.file)||!supported(item.file)||!validName(item.name)||std::find(files.begin(),files.end(),item.file)!=files.end())throw std::runtime_error("프로젝트 파일 경로가 올바르지 않습니다.");files.push_back(item.file);p.items.push_back(item);}
    for(auto& item:p.items)if(extension(item.file)==L".ydraw"&&!fs::exists(dir/item.file)){
        auto converted=fs::path(item.file).replace_extension(L".cdxml").wstring();
        if(fs::is_regular_file(dir/(item.file+L".bak"))&&fs::is_regular_file(dir/converted)&&
           std::none_of(files.begin(),files.end(),[&](const auto& file){return _wcsicmp(file.c_str(),converted.c_str())==0;}))
            item.file=converted;
    }
    if(!validName(p.name))throw std::runtime_error("프로젝트 이름이 올바르지 않습니다.");return p;
}
Project Project::create(const fs::path& base,const std::wstring& name){
    if(!validName(name))throw std::runtime_error("이름을 1~200자로 입력하세요.");fs::create_directories(base);Project p;p.name=name;
    do{p.folder=base/unique();}while(!fs::create_directory(p.folder));p.save();return p;
}
size_t Project::add(const fs::path& source){
    if(!supported(source)||!fs::is_regular_file(source))throw std::runtime_error("YDRaw, CDXML, PNG, JPEG, GIF 파일을 선택하세요.");
    Item item;item.name=source.stem().wstring();if(item.name.size()>200)item.name.resize(200);
    auto ext=extension(source);
    bool legacy=ext==L".ydraw";
    do{item.file=unique()+(legacy?L".cdxml":ext);}while(fs::exists(folder/item.file));
    if(legacy){
        std::ifstream in(source,std::ios::binary|std::ios::ate);
        auto size=in.tellg();
        if(size<0||size>64*1024*1024)throw std::runtime_error("파일은 64 MB까지 지원합니다.");
        std::string bytes(size_t(size),0);
        in.seekg(0);
        if(!in.read(bytes.data(),size))throw std::runtime_error("파일을 읽을 수 없습니다.");
        storage::writeAtomic((folder/item.file).wstring(),toCdxml(fromCdxml(bytes)),false);
    }else fs::copy_file(source,folder/item.file);
    items.push_back(item);try{save();}catch(...){items.pop_back();fs::remove(folder/item.file);throw;}return items.size()-1;
}
Project Project::importFrom(const fs::path& manifest,const fs::path& base){
    return Project::load(fs::is_directory(manifest)?manifest:manifest.parent_path()).copyTo(base);
}
Project Project::copyTo(const fs::path& base) const {
    // Validate the complete source before creating a destination. Keep relative
    // asset names so image edit sidecars and project notes survive relocation.
    auto source=Project::load(folder);
    std::vector<fs::path> files;
    for(const auto& item:source.items){
        auto asset=source.folder/item.file;
        if(!fs::is_regular_file(asset))throw std::runtime_error(ui::utf8(L"프로젝트 파일이 없습니다: "+item.name+L"\nproject.ydproject와 같은 폴더의 구조·이미지 파일도 함께 가져오세요."));
        files.push_back(asset);
        if(image(asset)&&fs::is_regular_file(imageDrawing(asset)))files.push_back(imageDrawing(asset));
    }
    auto result=Project::create(base,source.name);
    try{
        for(const auto& file:files)fs::copy_file(file,result.folder/file.filename());
        result.note=source.note;result.items=source.items;result.save();
    }catch(...){std::error_code ignored;fs::remove_all(result.folder,ignored);throw;}
    return result;
}
size_t Project::addDrawing(const std::wstring& name,const std::string& bytes){
    if(!validName(name))throw std::runtime_error("이름을 1~200자로 입력하세요.");Item item{unique()+L".cdxml",name,L""};storage::writeAtomic((folder/item.file).wstring(),bytes);items.push_back(item);try{save();}catch(...){items.pop_back();fs::remove(folder/item.file);throw;}return items.size()-1;
}
void Project::move(size_t from,size_t to){if(from>=items.size()||to>=items.size())throw std::out_of_range("project order");auto before=items;auto item=items[from];items.erase(items.begin()+from);items.insert(items.begin()+to,item);try{save();}catch(...){items=std::move(before);throw;}}
std::wstring displayName(const std::wstring& path){if(path.empty())return {};try{fs::path p(path);if(!fs::exists(p.parent_path()/L"project.ydproject"))return {};auto project=Project::load(p.parent_path());for(auto& item:project.items)if(item.file==p.filename()||(image(item.file)&&(item.file+L".ydraw"==p.filename()||item.file+L".cdxml"==p.filename())))return item.name;}catch(...){}return {};}
}

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
static std::string readDrawing(const fs::path& path){
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    auto size=in.tellg();
    if(size<0||size>64*1024*1024)throw std::runtime_error("파일은 64 MB까지 지원합니다.");
    std::string bytes(size_t(size),0);
    in.seekg(0);
    if(!in.read(bytes.data(),size))throw std::runtime_error("파일을 읽을 수 없습니다.");
    return bytes;
}
static void validateItems(const std::vector<Item>& items){
    std::vector<std::wstring> files;
    for(const auto& item:items){
        if(!validName(item.name)||item.file.empty()||fs::path(item.file).filename()!=fs::path(item.file)||
           (item.isFolder?fs::path(item.file).extension()!=L".ydfolder":!supported(item.file))||
           std::find(files.begin(),files.end(),item.file)!=files.end())throw std::runtime_error("프로젝트 항목 정보가 올바르지 않습니다.");
        files.push_back(item.file);
    }
    for(const auto& item:items){
        auto parent=item.parent;size_t depth=0;
        while(!parent.empty()){
            auto found=std::find_if(items.begin(),items.end(),[&](const auto& x){return x.file==parent&&x.isFolder;});
            if(found==items.end()||parent==item.file||++depth>items.size())throw std::runtime_error("프로젝트 폴더 경로가 올바르지 않습니다.");
            parent=found->parent;
        }
    }
}
void Project::save() const {
    if(!validName(name))throw std::runtime_error("이름을 1~200자로 입력하세요.");
    validateItems(items);
    std::ostringstream out;out<<"YoungDrawProject 2\n"<<std::quoted(ui::utf8(name))<<' '<<std::quoted(ui::utf8(note))<<'\n';
    for(const auto& item:items)out<<std::quoted(ui::utf8(item.file))<<' '<<std::quoted(ui::utf8(item.name))<<' '<<std::quoted(ui::utf8(item.note))<<' '<<std::quoted(ui::utf8(item.parent))<<' '<<item.isFolder<<'\n';
    storage::writeAtomic((folder/L"project.ydproject").wstring(),out.str());
}
Project Project::load(const fs::path& dir){
    std::ifstream in(dir/L"project.ydproject",std::ios::binary);std::string magic;int version=0;
    if(!(in>>magic>>version)||magic!="YoungDrawProject"||(version!=1&&version!=2))throw std::runtime_error("프로젝트 정보를 읽을 수 없습니다.");
    Project p;p.folder=dir;std::string name,note;if(!(in>>std::quoted(name)>>std::quoted(note)))throw std::runtime_error("프로젝트 정보가 손상되었습니다.");p.name=ui::wide(name);p.note=ui::wide(note);
    std::vector<std::wstring> files;
    while(in>>std::ws&&!in.eof()){
        std::string file,parent;
        if(!(in>>std::quoted(file)>>std::quoted(name)>>std::quoted(note)))throw std::runtime_error("프로젝트 항목 정보가 손상되었습니다.");
        Item item{ui::wide(file),ui::wide(name),ui::wide(note)};
        if(version==2){int kind=-1;if(!(in>>std::quoted(parent)>>kind)||(kind!=0&&kind!=1))throw std::runtime_error("프로젝트 폴더 정보가 손상되었습니다.");item.parent=ui::wide(parent);item.isFolder=kind==1;}
        files.push_back(item.file);p.items.push_back(item);
    }
    validateItems(p.items);
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
size_t Project::add(const fs::path& source,const std::wstring& parent){
    if(!supported(source)||!fs::is_regular_file(source))throw std::runtime_error("YDRaw, CDXML, PNG, JPEG, GIF 파일을 선택하세요.");
    Item item;item.parent=parent;item.name=source.stem().wstring();if(item.name.size()>200)item.name.resize(200);
    auto ext=extension(source);
    bool legacy=ext==L".ydraw";
    do{item.file=unique()+(legacy?L".cdxml":ext);}while(fs::exists(folder/item.file));
    if(legacy){
        storage::writeAtomic((folder/item.file).wstring(),toCdxml(fromCdxml(readDrawing(source))),false);
    }else fs::copy_file(source,folder/item.file);
    items.push_back(item);try{save();}catch(...){items.pop_back();fs::remove(folder/item.file);throw;}return items.size()-1;
}
Project Project::importFrom(const fs::path& manifest,const fs::path& base){
    return Project::load(fs::is_directory(manifest)?manifest:manifest.parent_path()).copyTo(base);
}
static std::wstring exportStem(std::wstring name){
    for(auto& c:name)if(c<32||wcschr(L"<>:\"/\\|?*",c))c=L'_';
    auto end=name.find_last_not_of(L" .");
    if(end==std::wstring::npos)return L"Drawing";
    name.resize(end+1);
    auto device=name.substr(0,name.find(L'.'));
    auto deviceEnd=device.find_last_not_of(L' ');
    if(deviceEnd!=std::wstring::npos)device.resize(deviceEnd+1);
    for(auto& c:device)c=wchar_t(towupper(c));
    if(device==L"CON"||device==L"PRN"||device==L"AUX"||device==L"NUL"||
       (device.size()==4&&(device.compare(0,3,L"COM")==0||device.compare(0,3,L"LPT")==0)&&
        wcschr(L"123456789¹²³",device.back())))name.insert(0,1,L'_');
    return name;
}
static std::vector<bool> selectedItems(const Project& project,const std::vector<size_t>& indices,bool ancestors){
    std::vector<bool> included(project.items.size(),false);
    for(auto i:indices){if(i>=included.size())throw std::out_of_range("project selection");included[i]=true;}
    bool changed=true;
    while(changed){changed=false;for(size_t i=0;i<project.items.size();++i)if(!included[i]){
        bool related=false;
        for(size_t j=0;j<project.items.size();++j)if(included[j]&&project.items[j].isFolder&&project.items[i].parent==project.items[j].file){related=true;break;}
        if(related){included[i]=true;changed=true;}
    }}
    // Add ancestors after the descendant pass so their unrelated children stay excluded.
    if(ancestors){changed=true;while(changed){changed=false;for(size_t i=0;i<project.items.size();++i)if(included[i]&&!project.items[i].parent.empty())for(size_t j=0;j<project.items.size();++j)if(!included[j]&&project.items[j].file==project.items[i].parent){included[j]=true;changed=true;}}}
    return included;
}
static Project transfer(const fs::path& folder,const fs::path& base,const PngWriter& writePng,const std::vector<size_t>* indices=nullptr){
    // Validate the complete source before creating a destination. Keep relative
    // asset names so image edit sidecars and project notes survive relocation.
    auto source=Project::load(folder);
    if(indices){auto included=selectedItems(source,*indices,true);std::vector<Item> subset;for(size_t i=0;i<included.size();++i)if(included[i])subset.push_back(source.items[i]);source.items=std::move(subset);}
    std::vector<fs::path> sidecars;
    for(const auto& item:source.items){
        if(item.isFolder){sidecars.push_back({});continue;}
        auto asset=source.folder/item.file;
        if(!fs::is_regular_file(asset))throw std::runtime_error(ui::utf8(L"프로젝트 파일이 없습니다: "+item.name+L"\nproject.ydproject와 같은 폴더의 구조·이미지 파일도 함께 가져오세요."));
        auto sidecar=image(asset)?imageDrawing(asset):fs::path{};
        sidecars.push_back(!sidecar.empty()&&fs::is_regular_file(sidecar)?sidecar:fs::path{});
    }
    auto result=Project::create(base,source.name);
    try{
        result.note=source.note;result.items=source.items;
        // Reserve the filenames of plain images before choosing names for drawings.
        for(size_t i=0;i<source.items.size();++i){
            if(source.items[i].isFolder)continue;
            auto asset=source.folder/source.items[i].file;
            if(!writePng||(image(asset)&&sidecars[i].empty())){
                fs::copy_file(asset,result.folder/asset.filename());
                if(!sidecars[i].empty())fs::copy_file(sidecars[i],result.folder/sidecars[i].filename());
            }
        }
        if(writePng)for(size_t i=0;i<source.items.size();++i){
            auto& item=result.items[i];
            if(item.isFolder)continue;
            auto asset=source.folder/item.file;
            bool editedImage=!sidecars[i].empty();
            if(image(asset)&&!editedImage)continue;
            auto stem=exportStem(item.name);
            fs::path imagePath,cdxml,png;
            for(size_t suffix=1;;++suffix){
                auto name=suffix==1?stem:stem+L" ("+std::to_wstring(suffix)+L")";
                imagePath=result.folder/(name+extension(asset));
                auto drawingStem=editedImage?imagePath.filename().wstring():name;
                cdxml=result.folder/(drawingStem+L".cdxml");
                png=result.folder/(drawingStem+L".png");
                if(!fs::exists(cdxml)&&!fs::exists(png)&&(!editedImage||!fs::exists(imagePath)))break;
            }
            auto drawing=editedImage?sidecars[i]:asset;
            if(extension(drawing)==L".ydraw")
                storage::writeAtomic(cdxml.wstring(),toCdxml(fromCdxml(readDrawing(drawing))),false);
            else fs::copy_file(drawing,cdxml);
            if(editedImage)fs::copy_file(asset,imagePath);
            try{writePng(cdxml,png);}
            catch(const std::exception& e){throw std::runtime_error(ui::utf8(item.name+L": ")+e.what());}
            item.file=(editedImage?imagePath:cdxml).filename().wstring();
        }
        result.save();
    }catch(...){std::error_code ignored;fs::remove_all(result.folder,ignored);throw;}
    return result;
}
Project Project::copySelectionTo(const fs::path& base,const std::vector<size_t>& indices) const {return transfer(folder,base,{},&indices);}
std::vector<size_t> Project::duplicate(const std::vector<size_t>& indices){
    auto included=selectedItems(*this,indices,false);auto previous=items;
    std::vector<Item> copies;std::vector<size_t> originals,result;std::vector<fs::path> created;
    try{
        for(size_t i=0;i<previous.size();++i)if(included[i]){
            auto copy=previous[i];copy.file=unique()+fs::path(copy.file).extension().wstring();
            if(std::find(indices.begin(),indices.end(),i)!=indices.end())copy.name=copy.name.substr(0,196)+L" 복사";
            if(!copy.isFolder){auto asset=folder/previous[i].file;auto target=folder/copy.file;fs::copy_file(asset,target);created.push_back(target);
                if(image(asset)){auto sidecar=imageDrawing(asset);if(fs::is_regular_file(sidecar)){auto destination=folder/(copy.file+sidecar.extension().wstring());fs::copy_file(sidecar,destination);created.push_back(destination);}}}
            originals.push_back(i);copies.push_back(std::move(copy));
        }
        for(auto& copy:copies)for(size_t j=0;j<originals.size();++j)if(previous[originals[j]].isFolder&&copy.parent==previous[originals[j]].file){copy.parent=copies[j].file;break;}
        for(auto& copy:copies){result.push_back(items.size());items.push_back(std::move(copy));}save();
    }catch(...){items=std::move(previous);for(auto& path:created){std::error_code error;fs::remove(path,error);}throw;}
    return result;
}
Project Project::copyTo(const fs::path& base) const {return transfer(folder,base,{});}
Project Project::exportTo(const fs::path& base,const PngWriter& writePng) const {
    if(!writePng)throw std::invalid_argument("PNG writer is required");
    return transfer(folder,base,writePng);
}
size_t Project::addDrawing(const std::wstring& name,const std::string& bytes,const std::wstring& parent){
    if(!validName(name))throw std::runtime_error("이름을 1~200자로 입력하세요.");Item item{unique()+L".cdxml",name,L"",parent};storage::writeAtomic((folder/item.file).wstring(),bytes);items.push_back(item);try{save();}catch(...){items.pop_back();fs::remove(folder/item.file);throw;}return items.size()-1;
}
void Project::move(size_t from,size_t to){if(from>=items.size()||to>=items.size())throw std::out_of_range("project order");auto before=items;auto item=items[from];items.erase(items.begin()+from);items.insert(items.begin()+to,item);try{save();}catch(...){items=std::move(before);throw;}}
size_t Project::addFolder(const std::wstring& name,const std::wstring& parent){
    Item item{unique()+L".ydfolder",name,L"",parent,true};items.push_back(item);
    try{save();}catch(...){items.pop_back();throw;}return items.size()-1;
}
void Project::relocate(const std::vector<size_t>& indices,const std::wstring& parent,size_t before){
    auto previous=items;std::vector<Item> moving,remaining;
    for(auto index:indices)if(index>=items.size())throw std::out_of_range("project selection");
    size_t insertion=0;
    for(size_t i=0;i<items.size();++i){
        if(std::find(indices.begin(),indices.end(),i)!=indices.end()){auto entry=items[i];auto ancestor=entry.parent;bool nested=false;
            while(!ancestor.empty()){auto found=std::find_if(items.begin(),items.end(),[&](const auto& e){return e.file==ancestor;});if(found==items.end())break;size_t at=size_t(found-items.begin());if(std::find(indices.begin(),indices.end(),at)!=indices.end()){nested=true;break;}ancestor=found->parent;}
            if(!nested)entry.parent=parent;moving.push_back(entry);}
        else{if(i<before)++insertion;remaining.push_back(items[i]);}
    }
    remaining.insert(remaining.begin()+insertion,moving.begin(),moving.end());items=std::move(remaining);
    try{save();}catch(...){items=std::move(previous);throw;}
}
std::wstring displayName(const std::wstring& path){if(path.empty())return {};try{fs::path p(path);if(!fs::exists(p.parent_path()/L"project.ydproject"))return {};auto project=Project::load(p.parent_path());for(auto& item:project.items)if(item.file==p.filename()||(image(item.file)&&(item.file+L".ydraw"==p.filename()||item.file+L".cdxml"==p.filename())))return item.name;}catch(...){}return {};}
}

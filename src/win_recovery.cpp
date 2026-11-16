#include "win_recovery.h"
#include "win_storage.h"
#include "cdxml.h"
#include <fstream>
#include <stdexcept>

namespace yd::recovery {
namespace fs=std::filesystem;
namespace {
constexpr size_t limit=64u*1024u*1024u;
void field(std::string& out,const std::string& value){out+=std::to_string(value.size())+'\n';out+=value;}
std::string field(const std::string& data,size_t& at){
    size_t end=data.find('\n',at);
    if(end==std::string::npos||end==at||end-at>10)throw std::runtime_error("Invalid recovery length");
    size_t size=0;for(size_t i=at;i<end;++i){if(data[i]<'0'||data[i]>'9')throw std::runtime_error("Invalid recovery length");size=size*10+data[i]-'0';if(size>limit)throw std::runtime_error("Recovery limit exceeded");}
    at=end+1;if(size>data.size()-at)throw std::runtime_error("Truncated recovery snapshot");
    auto result=data.substr(at,size);at+=size;return result;
}
}
Session::Session(const fs::path& root){
    fs::create_directories(root);
    GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("Cannot create recovery session");
    wchar_t name[40]{};StringFromGUID2(id,name,40);folder_=root/name;fs::create_directory(folder_);
    lock_=CreateFileW((folder_/L"session.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(lock_==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot lock recovery session");
}
Session::~Session(){
    if(lock_!=INVALID_HANDLE_VALUE)CloseHandle(lock_);
    std::error_code error;
    if(!fs::exists(folder_/L"documents.recovery",error)&&!error){
        fs::remove(folder_/L"session.lock",error);fs::remove(folder_,error);
    }
}
void Session::save(const std::vector<Snapshot>& documents){
    if(documents.empty()){clear();return;}
    std::string data="YoungDraw Recovery 1\n";
    for(const auto& snapshot:documents){field(data,fs::path(snapshot.path).u8string());field(data,toNative(snapshot.document));if(data.size()>limit)throw std::runtime_error("Recovery limit exceeded");}
    storage::writeAtomic((folder_/L"documents.recovery").wstring(),data);
}
void Session::clear(){fs::remove(folder_/L"documents.recovery");}
std::vector<Abandoned> abandoned(const fs::path& root){
    std::vector<Abandoned> result;std::error_code error;
    for(fs::directory_iterator it(root,error),end;!error&&it!=end;it.increment(error)){
        auto folder=it->path();auto file=folder/L"documents.recovery";
        if(!fs::is_regular_file(file,error)){error.clear();continue;}
        HANDLE lock=CreateFileW((folder/L"session.lock").c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(lock==INVALID_HANDLE_VALUE)continue;
        try{
            auto size=fs::file_size(file);if(size>limit)throw std::runtime_error("Recovery limit exceeded");
            std::ifstream input(file,std::ios::binary);std::string data(size_t(size),'\0');
            if(!input.read(data.data(),std::streamsize(size)))throw std::runtime_error("Cannot read recovery");
            const std::string header="YoungDraw Recovery 1\n";
            if(data.compare(0,header.size(),header))throw std::runtime_error("Invalid recovery header");
            Abandoned entry{file,{}};size_t at=header.size();
            while(at<data.size()){
                auto path=fs::u8path(field(data,at)).wstring();auto document=fromNative(field(data,at));
                entry.documents.push_back({std::move(path),std::move(document)});
            }
            if(!entry.documents.empty())result.push_back(std::move(entry));
        }catch(const std::exception&){/* Keep unreadable snapshots for manual recovery. */}
        CloseHandle(lock);
    }return result;
}
}

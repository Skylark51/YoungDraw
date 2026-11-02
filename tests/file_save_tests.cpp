#include "win_storage.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <iostream>

namespace fs=std::filesystem;
static void require(bool success,const char* message){if(!success)throw std::runtime_error(message);}
static std::string contents(const fs::path& path){
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
int main(){
    wchar_t folder[MAX_PATH]={},name[MAX_PATH]={};
    try{
        require(GetTempPathW(MAX_PATH,folder)>0,"Windows temporary directory unavailable");
        require(GetTempFileNameW(folder,L"YDW",0,name)!=0,"Cannot create test path");
        const fs::path target(name);
        const fs::path stale=target.wstring()+L".youngdraw.tmp";
        const fs::path invalid=target.wstring()+L"\\missing\\file.ydraw";
        try{
            {std::ofstream file(stale);file<<"old abandoned temporary data";}
            yd::storage::writeAtomic(target.wstring(),"first");
            require(contents(target)=="first","new file contents");
            yd::storage::writeAtomic(target.wstring(),std::string(1024*1024+19,'x'));
            require(contents(target)==std::string(1024*1024+19,'x'),"multi-chunk replacement");
            require(contents(stale)=="old abandoned temporary data","crash residue is ignored");
            bool rejected=false;
            try{yd::storage::writeAtomic(invalid.wstring(),"bad");}catch(const std::exception&){rejected=true;}
            require(rejected&&contents(target)==std::string(1024*1024+19,'x'),"failed save preserves destination");
        }catch(...){fs::remove(target);fs::remove(stale);throw;}
        fs::remove(target);fs::remove(stale);
        std::cout<<"PASS: atomic save and crash residue regression\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}

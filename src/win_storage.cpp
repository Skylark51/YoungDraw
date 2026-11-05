#include "win_storage.h"
#include <windows.h>
#include <algorithm>
#include <stdexcept>

namespace yd::storage {
void writeAtomic(const std::wstring& path,const std::string& data,bool replace) {
    if(data.size()>64u*1024u*1024u)
        throw std::runtime_error("프로젝트의 64 MB 제한을 초과했습니다. 삽입 이미지 크기를 줄이세요.");

    HANDLE file=INVALID_HANDLE_VALUE;
    std::wstring temp;
    const std::wstring prefix=path+L"."+std::to_wstring(GetCurrentProcessId())
                            +L"."+std::to_wstring(GetTickCount64());
    for(unsigned attempt=0;attempt<64;attempt++){
        temp=prefix+L"."+std::to_wstring(attempt)+L".youngdraw.tmp";
        file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file!=INVALID_HANDLE_VALUE)break;
        const DWORD error=GetLastError();
        if(error!=ERROR_FILE_EXISTS&&error!=ERROR_ALREADY_EXISTS)break;
    }
    if(file==INVALID_HANDLE_VALUE)
        throw std::runtime_error("임시 저장 파일을 만들 수 없습니다.");

    bool ok=true;
    size_t cursor=0;
    while(cursor<data.size()){
        DWORD written=0;
        const DWORD count=static_cast<DWORD>(std::min<size_t>(data.size()-cursor,1024u*1024u));
        if(!WriteFile(file,data.data()+cursor,count,&written,nullptr)||written==0){
            ok=false;
            break;
        }
        cursor+=written;
    }
    if(ok)ok=FlushFileBuffers(file)!=0;
    if(!CloseHandle(file))ok=false;
    if(!ok){
        DeleteFileW(temp.c_str());
        throw std::runtime_error("파일을 저장할 수 없습니다.");
    }
    if(!MoveFileExW(temp.c_str(),path.c_str(),(replace?MOVEFILE_REPLACE_EXISTING:0)|MOVEFILE_WRITE_THROUGH)){
        DeleteFileW(temp.c_str());
        throw std::runtime_error("저장 파일을 교체할 수 없습니다. 기존 문서는 보존되었습니다.");
    }
}
}

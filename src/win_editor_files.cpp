#include "win_editor_files.h"
#include "cdxml.h"
#include "win_dialogs.h"
#include "win_editor_state.h"
#include "win_editor_tabs.h"
#include "win_editor_text.h"
#include "win_storage.h"
#include <commdlg.h>
#include <fstream>

namespace yd::editor {

std::string readFile(const std::wstring& path){std::ifstream f(fs::path(path),std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("파일을 열 수 없습니다.");auto n=f.tellg();if(n<0||n>64*1024*1024)throw std::runtime_error("파일은 64 MB까지 지원합니다.");std::string s(size_t(n),0);f.seekg(0);if(!f.read(s.data(),n))throw std::runtime_error("파일을 읽을 수 없습니다.");return s;}
void writeFile(const std::wstring& path,const std::string& data){storage::writeAtomic(path,data);}
std::optional<std::wstring> fileDialog(bool save,int type,std::wstring initial){wchar_t path[32768]={};if(save)wcscpy_s(path,type==1?L"structure.svg":type==2?L"structure.cdxml":L"drawing.cdxml");OPENFILENAMEW f{};f.lStructSize=sizeof(f);f.hwndOwner=win;f.lpstrFile=path;f.nMaxFile=32768;f.lpstrFilter=type==1?L"SVG vector graphic\0*.svg\0\0":(type==2||save)?L"ChemDraw / YoungDraw CDXML\0*.cdxml\0\0":type==4?L"Structures / Images\0*.ydraw;*.cdxml;*.gif;*.png;*.jpg;*.jpeg\0\0":type==3?L"Images (GIF, PNG, JPEG)\0*.gif;*.png;*.jpg;*.jpeg\0\0":L"YoungDraw / ChemDraw XML\0*.ydraw;*.cdxml\0YoungDraw project\0*.ydraw\0ChemDraw XML\0*.cdxml\0\0";f.lpstrDefExt=type==1?L"svg":L"cdxml";f.lpstrInitialDir=initial.empty()?nullptr:initial.c_str();f.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);if(save?GetSaveFileNameW(&f):GetOpenFileNameW(&f))return std::wstring(path);return {};}
std::vector<std::wstring> openDocumentFiles(){
    std::vector<wchar_t> buffer(65536);OPENFILENAMEW f{};f.lStructSize=sizeof(f);f.hwndOwner=win;
    f.lpstrFile=buffer.data();f.nMaxFile=DWORD(buffer.size());f.lpstrFilter=L"YoungDraw / ChemDraw XML\0*.ydraw;*.cdxml\0\0";
    f.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST|OFN_ALLOWMULTISELECT;
    if(!GetOpenFileNameW(&f))return {};
    std::wstring first=buffer.data();const wchar_t* cursor=buffer.data()+first.size()+1;
    if(!*cursor)return {first};
    std::vector<std::wstring> paths;while(*cursor){std::wstring name=cursor;paths.push_back((fs::path(first)/name).wstring());cursor+=name.size()+1;}return paths;
}

void openDocuments(const std::vector<std::wstring>& paths){
    if(paths.empty()||!finishInline(true))return;

    std::wstring failures;
    for(const auto& path:paths){
        try{
            const auto fullPath=fs::absolute(path).lexically_normal();
            size_t index=0;
            for(;index<documents.size();++index){
                const auto& currentPath=index==activeDocument?filePath:documents[index].path;
                if(!currentPath.empty()&&_wcsicmp(fs::absolute(currentPath).lexically_normal().c_str(),fullPath.c_str())==0)
                    break;
            }
            if(index<documents.size()){
                switchDocumentTab(index);
                continue;
            }

            auto imported=fromCdxml(readFile(path));
            newDocumentTab(std::move(imported),path);
            status=L"문서를 새 탭에서 열었습니다.";
            if(!doc.importWarnings.empty()){
                std::wstring warnings=L"지원하지 않는 XML 객체는 CDXML 저장 시 보존됩니다. 미리보기와 SVG에는 일부 객체가 생략될 수 있습니다.\n\n";
                for(size_t i=0;i<std::min(size_t(12),doc.importWarnings.size());++i)
                    warnings+=ui::wide(doc.importWarnings[i])+L"\n";
                MessageBoxW(win,warnings.c_str(),L"CDXML 호환성 보고서",MB_OK|MB_ICONINFORMATION);
            }
        }catch(const std::exception& e){
            failures+=L"\n"+fs::path(path).filename().wstring()+L" — "+ui::wide(e.what())+L"\n";
        }
    }
    if(!failures.empty()){
        const auto message=L"다음 파일을 열 수 없습니다.\n"+failures;
        MessageBoxW(win,message.c_str(),L"문서 열기",MB_OK|MB_ICONWARNING);
    }
}

} // namespace yd::editor

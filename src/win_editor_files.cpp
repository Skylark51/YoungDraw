#include "win_editor_files.h"
#include "win_editor_state.h"
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

} // namespace yd::editor

#pragma once

#include "win_editor.h"

namespace yd::editor {

std::string readFile(const std::wstring& path);
void writeFile(const std::wstring& path,const std::string& data);
std::optional<std::wstring> fileDialog(bool save,int type=0,std::wstring initial=L"");
std::vector<std::wstring> openDocumentFiles();

} // namespace yd::editor

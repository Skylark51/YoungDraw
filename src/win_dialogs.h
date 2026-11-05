#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <optional>
#include <functional>
namespace ui {
inline std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),w.data(),n);return w;}
inline std::string utf8(const std::wstring& w){if(w.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),s.data(),n,nullptr,nullptr);return s;}
struct Field {std::wstring label;std::string value;bool multiline=false;std::vector<std::wstring> choices;bool color=false;int rows=4;bool scripts=false;bool editableChoices=false;std::function<std::string(const std::string&,int)> stepValue={};};
using Table = std::vector<std::vector<std::string>>;
using TablePreview = std::function<std::wstring(const Table&)>;
using TableCellError = std::function<std::wstring(size_t column,const std::string& value)>;
std::optional<Table> table(HWND parent,const std::wstring& title,const std::vector<std::wstring>& columns,const Table& rows,const TablePreview& preview={},bool scripts=false,const std::wstring& hint=L"",bool stages=false,int scriptColumn=-1,const TableCellError& cellError={});
using Preview = std::function<std::wstring(const std::vector<std::string>&)>;
std::optional<std::vector<std::string>> form(HWND parent,const std::wstring& title,const std::vector<Field>& fields,const Preview& preview={});
}

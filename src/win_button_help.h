#pragma once
#include <windows.h>
#include <string>
#include <vector>
namespace ui {
struct HelpRegion{UINT_PTR id;RECT rect;std::wstring text;};
void enableButtonHelp();
void attachWindowHelp(HWND window);
std::wstring buttonHelpText(HWND button);
bool hasButtonHelp(HWND button);
void setRegionHelp(HWND window,const std::vector<HelpRegion>& regions);
size_t regionHelpCount(HWND window);
std::wstring labelHelp(const std::wstring& label);
}

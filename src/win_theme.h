#pragma once
#include <windows.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <functional>
#include <array>
#include <string>

struct IFileDialog;

namespace appearance {
struct Settings {
    int theme=2,pattern=1,intensity=1;
    std::array<unsigned,11> colors={0xe9e2d6,0xf3ede2,0xfffbf4,0xe0d4c1,0xd0c5b3,0x3c3b38,0x7c7467,0x846640,0xfffbf4,0xfffcf3,0x39352c};
    std::wstring customName=L"내 테마",backgroundImage;
    int patternSpacing=28,patternOpacity=11,imageOpacity=45;
};
struct Palette {
    Gdiplus::Color chrome,panel,raised,hover,edge,text,muted,accent,onAccent,canvas,canvasInk;
    const wchar_t* name;const wchar_t* description;bool darkCanvas;int resource;
};
const Palette& palette();
const Palette& palette(int index);
Settings settings();
void load();
void shutdown();
void rounded(Gdiplus::Graphics& g,Gdiplus::RectF box,float radius,Gdiplus::Color color);
void background(Gdiplus::Graphics& g,Gdiplus::RectF box);
Gdiplus::Color documentColor(Gdiplus::Color color,bool backing=false);
void picker(HWND parent,const std::function<void()>& refresh);
void titlebar(HWND hwnd);
void dialog(HWND hwnd);
void nativeControl(HWND hwnd);
int messageBox(HWND owner,const wchar_t* text,const wchar_t* caption,UINT type);
BOOL openFileDialog(OPENFILENAMEW& options);
BOOL saveFileDialog(OPENFILENAMEW& options);
HRESULT showFileDialog(HWND owner,IFileDialog& dialog);
}

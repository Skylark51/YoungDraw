#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <functional>

namespace appearance {
struct Settings { int theme=2,pattern=1,intensity=1; };
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
}

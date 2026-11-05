#pragma once

#include "win_editor.h"

namespace yd::editor {

struct Hit{int id;RectF box;};

extern HBRUSH menuBackground;
extern Color ink, muted, accent, border, white, black;
extern std::vector<Hit> hits;
extern const std::vector<std::wstring> toolNames;
void text(Graphics& g,const std::wstring& s,float x,float y,float size,Color c,bool bold=false);
void fill(Graphics& g,RectF r,Color c);
void rule(Graphics& g,float x,float y,float z,float w,Color c);
void icon(Graphics& g,int id,RectF box,Color color);
void toolButton(Graphics& g,int id,RectF r,bool active);
void button(Graphics& g,int id,std::wstring label,RectF r,bool active=false);
std::wstring helpText(int id);
void hideTooltip();
void showTooltip();
void updateTooltip();
void createTooltip();
void menuBar(HWND hwnd);
void refreshAppearance();

} // namespace yd::editor

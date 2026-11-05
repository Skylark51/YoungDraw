#pragma once

#include "win_editor.h"

namespace yd::editor {

struct InlineTarget {int owner=0,role=0,index=-1;Vec p;double size=14;};

extern HWND inlineEdit;
extern InlineTarget inlineTarget;
extern bool inlineClosing;
extern COLORREF inlineColor;
Object* textObject(int id);
std::string inlineValue(const InlineTarget& t);
void setInlineValue(const InlineTarget& t,const std::string& value);
std::optional<InlineTarget> textAt(Vec p);
void previewInline();
bool finishInline(bool commit);
void beginInline(std::optional<InlineTarget> target,Vec at);
bool applyTextColor(const std::string& value,const std::optional<InlineTarget>& target={});
void textColorPalette(const std::optional<InlineTarget>& target);

} // namespace yd::editor

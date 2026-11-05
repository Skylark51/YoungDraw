#pragma once

#include "win_editor.h"
#include <memory>

namespace yd::editor {

extern std::unique_ptr<Bitmap> paintBuffer;
Document draftOrbital(Vec pointer);
Object draftArrow(Vec pointer);
void presentBuffer(Graphics& out,Bitmap& buffer);
void paint(HDC dc,bool reuseScene=false);

} // namespace yd::editor

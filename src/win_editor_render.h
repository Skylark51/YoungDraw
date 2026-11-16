#pragma once

#include "win_editor.h"
#include <memory>

namespace yd::editor {

void render(Graphics& g,const Scene& s,bool animate=false,bool screenTheme=false);
HENHMETAFILE officePicture(const Document& d);
std::unique_ptr<Bitmap> drawingBitmap(const Document& d,bool transparent=true);
std::string encodePng(Bitmap& image);
void writePreview(const std::wstring& path,const Document& d,bool transparent=false);

} // namespace yd::editor

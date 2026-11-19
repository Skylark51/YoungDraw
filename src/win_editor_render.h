#pragma once

#include "win_editor.h"
#include <memory>

namespace yd::editor {

void render(Graphics& g,const Scene& s,bool animate=false,bool screenTheme=false);
HENHMETAFILE officePicture(const Document& d);
inline constexpr float highResolutionDpi=600;
std::unique_ptr<Bitmap> drawingBitmap(const Document& d,bool transparent=true,float dpi=0);
std::string encodePng(Bitmap& image);
void writePreview(const std::wstring& path,const Document& d,bool transparent=false);
void writeTiff(const std::wstring& path,const Document& d);

} // namespace yd::editor

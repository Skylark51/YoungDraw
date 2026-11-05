#pragma once

#include "win_editor.h"

namespace yd::editor {

void render(Graphics& g,const Scene& s,bool animate=false,bool screenTheme=false);
HENHMETAFILE officePicture(const Document& d);
void writePreview(const std::wstring& path,const Document& d);

} // namespace yd::editor

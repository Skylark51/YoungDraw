#pragma once
#include "model.h"
#include <windows.h>
#include <optional>

namespace yd::editor {
// All interaction is performed on a draft. The caller owns the final undo commit.
std::optional<Document> mechanismEditor(HWND parent,const Document& draft,int owner,
                                        const Document* original=nullptr);
}

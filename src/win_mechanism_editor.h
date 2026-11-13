#pragma once
#include "model.h"
#include <windows.h>
#include <optional>
#include <functional>

namespace yd::editor {
struct MechanismEditorCallbacks {
    std::function<std::optional<Document>(const Document&,HWND)> editStructure;
    std::function<std::optional<Document>(HWND)> chooseStructure;
    std::function<std::optional<Document>()> clipboard;
};
std::optional<std::vector<std::string>> mechanismAdditionDetails(HWND parent,const Reaction& reaction,size_t from,bool branch);
// All interaction is performed on a draft. The caller owns the final undo commit.
std::optional<Document> mechanismEditor(HWND parent,const Document& draft,int owner,
                                        const Document* original=nullptr,const MechanismEditorCallbacks& callbacks={});
}

#pragma once

#include "win_editor.h"

namespace yd::editor {

Document selectedDocument();
void copy();
std::string clipboardMemoryText(HGLOBAL memory,bool unicode=false);
std::optional<Document> clipboardStructureText(const std::string& text);
std::optional<Document> clipboardDocument();
void insert(const Document& source,Vec at);

} // namespace yd::editor

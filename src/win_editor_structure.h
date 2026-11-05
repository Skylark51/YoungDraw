#pragma once

#include "win_editor.h"

namespace yd::editor {

extern std::function<std::optional<Document>(const Document&)> structureEditor;
void clean();
std::vector<int> groupedSelection(std::vector<int> ids);
bool groupedObject(int id);
void groupSelected(bool ungroup);
std::vector<int> orbitalMembers(int id);
void editObject(int id);
void edit();
void addImage(Vec p);
void transformSelected(int operation);
bool activeStructureTarget();
void editTargetStructure();
void insertStructureTarget(const Document& fragment);
void pasteStructure(const Document& fragment);
std::vector<int> resizeStructure(Document& candidate,const std::vector<int>& ids,double factor,int container=0,size_t slot=0,bool above=false);
void resizeSelected();
void pasteClipboard();
void fillTarget(bool file);
void atomColorPalette();

} // namespace yd::editor

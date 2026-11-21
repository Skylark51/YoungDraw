#pragma once

#include "win_editor.h"

namespace yd::editor {
void applyAtomElement(int id,int element);
void attachPhenyl(int id);

std::optional<Document> runStructureEditor(const Document& fragment,HWND owner);
extern std::function<std::optional<Document>(const Document&)> structureEditor;
void clean();
void editMullikenSpin();
void editMolecularAnnotations();
void abbreviateGroups();
std::vector<int> groupedSelection(std::vector<int> ids);
bool groupedObject(int id);
void groupSelected(bool ungroup);
std::vector<int> orbitalMembers(int id);
void editObject(int id);
void edit();
void addImage(Vec p);
void transformSelected(int operation);
void editLigandSpatial();
bool activeStructureTarget();
void editTargetStructure();
void insertStructureTarget(const Document& fragment);
void pasteStructure(const Document& fragment);
std::vector<int> resizeStructure(Document& candidate,const std::vector<int>& ids,double factor,int container=0,size_t slot=0,bool above=false);
void resizeSelected();
void pasteClipboard();
void fillTarget(bool file);
void atomColorPalette();
void arrowColorPalette();

} // namespace yd::editor

#pragma once

#include "win_editor.h"

namespace yd::editor {

extern bool mechanismConnecting, mechanismConnectionDrag;
extern int connectionOwner, connectionFrom, connectionEdge, connectionHandle;
extern int selectedCycleOwner, selectedCycleArrow;

void newReaction();
void editReaction(int id);
void reactionSettings(int id);
void editArrowCross();
void branchMechanism();
void externalMechanism();
void insertBranchArrows();
void arrangeImported(bool editAfter=false);
void editMechanismControls(int id);
Vec mechanismAddPosition(const Reaction& r, bool below=false, size_t slot=size_t(-1));
std::optional<size_t> mechanismSlotAt(int owner,Vec p);
std::vector<Vec> mechanismConnectionPath(int owner,int edge);
bool beginMechanismConnection(Vec point);
void finishMechanismConnection(Vec point);
std::optional<std::pair<int,int>> cycleArrowAt(Vec p);
void mechanismContextMenu(Vec at);
bool deleteSelectedCycleArrow();

} // namespace yd::editor

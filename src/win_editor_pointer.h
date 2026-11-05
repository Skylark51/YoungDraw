#pragma once

#include "win_editor.h"

namespace yd::editor {

Vec sproutPoint(int id);
Vec endpoint(Vec p);
int objectAt(Vec p);
bool pickSlot(Vec p);
void updateDrag();
void cancel();
void mouseDown(Vec p);
void mouseUp(Vec p);
Scene energyDragPreview(const Scene& scene);

} // namespace yd::editor

#pragma once
#include "win_editor.h"

namespace yd::editor {
extern int reactionArrowHead;
extern bool partialTripleBond;
bool showDrawingToolOptions(int id,Vec at);
bool drawingOptionCommand(int id);
void drawLayerPanel(Graphics& g,float x,int width);
bool layerPanelCommand(int id);
} // namespace yd::editor

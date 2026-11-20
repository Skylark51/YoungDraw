#pragma once
#include "win_editor.h"
namespace yd::editor {
extern HWND plotManagerWindow;
void managePlot(int id,bool visible=true);
void openPlotManagerTool(int id,int action);
void closePlotManager();
void refreshPlotManager();
bool handlePlotManagerMessage(MSG& message);
}

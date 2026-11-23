#pragma once
#include "win_editor.h"
namespace yd::editor {
extern HWND plotManagerWindow;
void managePlot(int id,bool visible=true);
void openPlotManagerTool(int id,int action);
void openPlotAxesManager(int id,int detail=-1);
void closePlotManager();
void refreshPlotManager();
bool handlePlotManagerMessage(MSG& message);
}

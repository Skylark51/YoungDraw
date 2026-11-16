#pragma once

#include "win_editor.h"

namespace yd::editor {

extern int snapPlot, snapStage;
extern MechanismStage snapLabel;
extern std::optional<PlotSnapPoint> snapStart;
std::optional<PlotSnapPoint> nearestPlotSnap(const Scene& sc,Vec cursor);

void editPlot(int id);
void addBilateralBranch(int id);
void publicationLayout(int id);
void plotSettings(int id);
void editPlotStages(int id);
void editPlotCrossings(int id);
void editPlotAnnotations(int id);
struct PendingEnergyGap {int owner,from,to,path;std::string label;};
extern std::optional<PendingEnergyGap> pendingEnergyGap;
void addEnergyGap(int id);
bool placeEnergyGap(Vec at);
void snapPlotStages(int id);
void editPlotColors(int id);
void editSeries(int id,bool add);

} // namespace yd::editor

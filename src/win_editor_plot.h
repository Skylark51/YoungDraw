#pragma once

#include "win_editor.h"

namespace yd::editor {

extern int snapPlot, snapStage;
extern MechanismStage snapLabel;
extern std::optional<PlotSnapPoint> snapStart;
std::optional<PlotSnapPoint> nearestPlotSnap(const Scene& sc,Vec cursor);

void editPlot(int id);
void relocatePlot(const EnergyPlot& before,const EnergyPlot& after);
void addBilateralBranch(int id);
void publicationLayout(int id);
void plotSettings(int id,bool generalOnly=false);
void editPlotFont(int id);
void changePlotStyle(int id);
void editPlotAxisLengths(int id);
void editPlotAxes(int id);
bool stepPlotAppearance(int id,int action,int steps);
void editPlotAppearance(int id,int field);
bool adjustSelectedPlotWeight(bool increase);
void editPlotCrossings(int id);
void editPlotAnnotations(int id);
void editPlotPhotoExcitation(int id);
std::optional<std::pair<int,size_t>> plotStateAt(Vec at); // Canvas coordinates.
void setPlotTransitionState(int id,size_t index,bool enabled);
bool plotContextMenu(Vec at);
void compressPlotEnergyInterval(int id,size_t index);
struct PendingEnergyGap {int owner,from,to,path;std::string label;};
extern std::optional<PendingEnergyGap> pendingEnergyGap;
void addEnergyGap(int id);
bool placeEnergyGap(Vec at);
void snapPlotStages(int id);
void editPlotColors(int id);

} // namespace yd::editor

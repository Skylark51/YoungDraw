#pragma once
#include "model.h"
namespace yd {
struct EnergyRow { std::string label; double energy=0; int marker=-1; };
struct EnergyDisplayBand {double low,high,scale;bool marker=true;};
std::vector<EnergyDisplayBand> energyDisplayBands(const EnergyPlot& plot);
double displayEnergy(const std::vector<EnergyDisplayBand>& bands,double value);
double energyFromDisplay(const std::vector<EnergyDisplayBand>& bands,double value);
struct EnergyAxis { double low=0,high=1,step=1; int intervals=1; double firstTick=0; };
inline constexpr int independentEnergyLevels = 4;
EnergyPlot spinMultiplicityPlot();
EnergyPlot reactionProfilePlot();
EnergyPlot newEnergyPlot(int mode,bool freeEnergy);
bool isTransitionStateLabel(const std::string& label);
void setEnergyStateLabel(EnergyState& state,const std::string& label);
std::string plotEnergySymbol(const EnergyPlot& plot);
void validateEnergyAnnotations(const EnergyPlot& plot);
EnergyPlot publicationPlot(const EnergyPlot& original);
// Mirror a pathway to the left of the first (shared) state; keep existing paths on the right.
EnergyPlot bilateralPlot(const EnergyPlot& original,size_t sourcePath=0,const std::string& leftName="Left branch");
// Add a second path from one shared state, on the same reaction coordinate.
EnergyPlot verticalBranchPlot(const EnergyPlot& original,size_t sourcePath,size_t fork,
                              const std::string& name);
// Update a material level and every path sharing that exact branch point.
void setPlotEnergy(EnergyPlot& plot,size_t path,size_t state,double energy);
std::vector<MechanismStage> defaultMechanismStages();
std::vector<MechanismStage> mechanismStageData(const std::vector<std::vector<std::string>>& cells);
double plotStateFraction(const EnergyPlot& plot,size_t index);
EnergyAxis energyAxis(const EnergyPlot& plot);
void validateEnergyCrossings(const EnergyPlot& plot);
bool hasCrossingPaths(const EnergyPlot& plot,const EnergyCrossing& crossing);
std::string energyTickLabel(double value,double step);
// CSV columns: state, energy, marker (auto/none/circle/square/diamond/triangle).
std::vector<EnergyRow> parseEnergyTable(const std::string& csv);
std::string energyTable(const EnergyPlot& plot);
void setEnergyData(EnergyPlot& plot,const std::vector<EnergyRow>& rows,bool preserveMarkers=false,std::vector<int>* stateMapping=nullptr);
bool validEnergyGridValue(const std::string& text);
std::string energyCellText(const EnergySeries& path,size_t index,const std::string& fallback);
// Build a candidate before changing the document. Blank labels/energies and empty rows are valid; missing energies stay absent. Columns: state, one per path.
EnergyPlot energyGridData(const EnergyPlot& original,const std::vector<std::vector<std::string>>& cells);
}

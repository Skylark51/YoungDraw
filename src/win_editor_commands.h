#pragma once

#include "win_editor.h"

namespace yd::editor {

inline constexpr int insertBranchArrowsCommand = 220;
inline constexpr int externalMechanismCommand = 221;
inline constexpr int bondAnimationCommand = 222;
inline constexpr int recognizeMechanismCommand = 223;
inline constexpr int interactionToolCommand = 225;
inline constexpr int abbreviateGroupsCommand = 226;
inline constexpr int mullikenSpinCommand = 227;
inline constexpr int spinMultiplicityPlotCommand = 228;
inline constexpr int dottedBondCommand = 229;
inline constexpr int thirdPartyNoticesCommand = 234;
inline constexpr int reactionProfilePlotCommand = 235;
inline constexpr int plotAnnotationsCommand = 236;
inline constexpr int addEnergyGapCommand = 237;
inline constexpr int changePlotStyleCommand = 238;
inline constexpr int plotAxisLengthsCommand = 239;
inline constexpr int plotAppearanceFirstCommand = 240; // Independent sidebar appearance controls.
inline constexpr int plotAppearanceLastCommand = 246; // Existing controls retain their command IDs.
inline constexpr int plotLegendCommand = 249;
inline constexpr int plotWaveWidthCommand = 250;
inline constexpr int plotWaveGapCommand = 251;
inline constexpr int plotManagerCommand = 252;
inline constexpr int plotFontCommand = 253;
inline constexpr int mechanismManagerCommand = 254;
inline constexpr int includeMechanismCommand = 255;
inline constexpr int reopenClosedDocumentCommand = 256;
inline constexpr int exportTiffCommand = 257;
inline constexpr int rotateToolCommand = 258;
inline constexpr int arrowColorCommand = 259;
inline constexpr int ligandSpatialCommand = 260;
inline constexpr int molecularAnnotationsCommand = 261;
inline constexpr int showMolecularAnnotationsCommand = 262;
inline constexpr int selectedBoldCommand = 263;
inline constexpr int selectedItalicCommand = 264;
inline constexpr int selectedUnderlineCommand = 265;
inline constexpr int selectedFontSmallerCommand = 266;
inline constexpr int selectedFontLargerCommand = 267;
inline constexpr int selectedStrokeThinnerCommand = 268;
inline constexpr int selectedStrokeThickerCommand = 269;
inline constexpr int energyGapStyleCommand = 270;
inline constexpr int irreversibleArrowCommand = 271;
inline constexpr int equilibriumArrowCommand = 272;
inline constexpr int upperFavoredArrowCommand = 273;
inline constexpr int lowerFavoredArrowCommand = 274;
inline constexpr int delocalizedBondOptionCommand = 275;
inline constexpr int partialTripleBondOptionCommand = 276;
inline constexpr int imageVisibilityCommand = 277;
inline constexpr int graphVisibilityCommand = 278;
inline constexpr int selectImageLayerCommand = 279;
inline constexpr int selectGraphLayerCommand = 280;
inline constexpr int exportPngCommand = 247;
inline constexpr int copyPngCommand = 248;

inline constexpr bool isEditorCommand(int id){return (id>=100&&id<=selectGraphLayerCommand)||(id>=301&&id<=418);}
struct SelectionFormatting {
    bool properties=false,atoms=false,text=false,arrows=false,stroke=false;
    int commonFace=7;
    bool labels() const {return atoms||text;}
};
SelectionFormatting selectionFormatting();
std::vector<int> selectionFormattingCommands(const SelectionFormatting& formatting);
void appendSelectionFormattingMenu(HMENU menu);
void command(int id);

} // namespace yd::editor

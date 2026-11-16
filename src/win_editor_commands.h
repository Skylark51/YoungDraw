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
inline constexpr int plotLegendSizeCommand = 249;
inline constexpr int exportPngCommand = 247;
inline constexpr int copyPngCommand = 248;

void command(int id);

} // namespace yd::editor

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

void command(int id);

} // namespace yd::editor

#pragma once

#include "win_editor.h"

namespace yd::editor {

inline constexpr int insertBranchArrowsCommand = 220;
inline constexpr int externalMechanismCommand = 221;
inline constexpr int bondAnimationCommand = 222;
inline constexpr int recognizeMechanismCommand = 223;
inline constexpr int interactionToolCommand = 225;

void command(int id);

} // namespace yd::editor

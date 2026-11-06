#pragma once

#include "win_editor.h"

namespace yd::editor {

void hotkey(char c);
void alignSelection(int id);
void key(WPARAM k);
void firstUseGuide(int id);
void shortcutSettings();
void showHelp();
void searchCommands();

} // namespace yd::editor

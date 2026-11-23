#pragma once

#include "win_editor.h"

namespace yd::editor {

void hotkey(char c);
bool adjustSelectedStrokeWidths(bool increase);
void alignSelection(int id);
void key(WPARAM k);
void firstUseGuide(int id);
void shortcutSettings();
void showHelp();
struct EditorCommandEntry {int id;std::wstring title,detail;};
std::vector<EditorCommandEntry> availableEditorCommands(HWND window);
void searchCommands();

} // namespace yd::editor

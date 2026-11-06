#pragma once

#include "win_editor.h"
#include "win_editor_state.h"

namespace yd::editor {

struct DocumentTab {Document document;History history;std::wstring path,status;std::vector<int> selection;int target=0;size_t slot=0;bool above=false,dirty=false,office=false;double zoom=4;Vec origin;Tool tool=Select;};

extern HWND documentTabs;
extern HFONT tabFont;
extern bool officeDocument;
extern std::vector<DocumentTab> documents;
extern size_t activeDocument;
bool save(bool as=false);
void storeDocumentTab();
void refreshDocumentTabs();
bool switchDocumentTab(size_t index);
void newDocumentTab(Document document={},std::wstring path=L"",bool fromOffice=false);
bool closeDocumentTab(size_t index);
bool closeAllDocuments();
void manageProjects(const std::wstring& initial={});
void showRecent();
Document officeCurrentDocument();
void createDocumentTabs();

} // namespace yd::editor

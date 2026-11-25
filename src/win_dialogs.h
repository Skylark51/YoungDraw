#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <optional>
#include <functional>
namespace yd { enum class MechanismTemplate; }
namespace ui {
inline std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),w.data(),n);return w;}
inline std::string utf8(const std::wstring& w){if(w.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),s.data(),n,nullptr,nullptr);return s;}
// Routes an existing dialog workflow to a child panel while its caller retains the edit transaction.
struct InlineDialogHost {
    HWND window;
    std::function<void(HWND)> mount;
    InlineDialogHost* previous;
    InlineDialogHost(HWND window,std::function<void(HWND)> mount);
    ~InlineDialogHost();
};
InlineDialogHost* inlineDialogHost();
void runInlineDialog(HWND child,const bool& done,HWND focus);
void themeCombo(HWND combo);
struct GraphChoice {int mode=0;bool freeEnergy=true;};
std::optional<GraphChoice> graphChoice(HWND owner,int initialMode=0,bool editStyle=false,const std::function<void(int)>& preview={});
std::optional<yd::MechanismTemplate> mechanismChoice(HWND owner);
struct SearchItem {std::wstring title,detail;};
std::optional<size_t> search(HWND owner,const std::wstring& title,const std::vector<SearchItem>& entries,
    const wchar_t* action=L"열기",bool showDetails=false);
struct ChoiceUpdate {size_t field;std::vector<std::optional<std::string>> values;};
struct Field {std::wstring label;std::string value;bool multiline=false;std::vector<std::wstring> choices;bool color=false;int rows=4;bool scripts=false;bool editableChoices=false;std::function<std::string(const std::string&,int)> stepValue={};bool stepWithSigns=false;bool stepButtons=false;bool toggle=false;std::optional<ChoiceUpdate> choiceUpdate;std::vector<std::pair<int,std::wstring>> quickChoices;bool buttonsOnly=false;};
std::vector<std::wstring> fontFamilies(HWND window);
Field numericField(std::wstring label,double value,double minimum,double maximum,double increment);
using Table = std::vector<std::vector<std::string>>;
using TablePreview = std::function<std::wstring(const Table&)>;
using TableCellError = std::function<std::wstring(size_t column,const std::string& value)>;
using TableColumnEdit = std::function<std::optional<std::wstring>(HWND,size_t column)>;
using TableReadback = std::function<Table()>;
// Revisions are unique for this table's lifetime. retained contains the current
// revision plus every revision reachable through undo/redo, sorted and unique.
struct TableHistory {size_t revision=0;std::vector<size_t> retained;};
// Opt in to path-column deletion. sourceColumns includes the state-name column
// (0); remaining entries identify columns in the originally opened table.
// Cache full model states by history.revision to restore exact metadata/energies
// on undo/redo. Discard cached revisions not listed in history.retained.
// This callback replaces TablePreview and also receives undoable column titles.
using TableStructurePreview = std::function<std::wstring(const Table& rows,const std::vector<size_t>& sourceColumns,const std::vector<std::wstring>& titles,const TableHistory& history)>;
std::optional<Table> table(HWND parent,const std::wstring& title,const std::vector<std::wstring>& columns,const Table& rows,const TablePreview& preview={},bool scripts=false,const std::wstring& hint=L"",bool stages=false,int scriptColumn=-1,const TableCellError& cellError={},const TableColumnEdit& columnEdit={},const TableStructurePreview& structurePreview={});
HWND embeddedTable(HWND parent,const std::vector<std::wstring>& columns,const Table& rows,const TablePreview& preview,const TableCellError& cellError={},const TableColumnEdit& columnEdit={},const TableReadback& readback={},const TableStructurePreview& structurePreview={});
void selectEmbeddedTableCell(HWND grid,int row,int column);
bool commitEmbeddedTable(HWND grid);
void setEmbeddedTableFontSize(HWND grid,int points);
void distributeEmbeddedStages(HWND grid);
HWND embeddedStageTable(HWND parent,const Table& rows,const TablePreview& preview);
std::optional<COLORREF> chooseColor(HWND parent,COLORREF initial);
using Preview = std::function<std::wstring(const std::vector<std::string>&)>;
struct FormTabStyle {std::vector<int> icons;int initial=0;int* remember=nullptr;bool unified=false;};
HWND embeddedForm(HWND parent,const wchar_t* title,const std::vector<Field>& fields,const Preview& preview,
    const Preview& apply,const std::function<void()>& dismiss,
    const std::function<std::optional<COLORREF>(COLORREF)>& pickColor={},bool keepOpenOnApply=false);
bool validateEmbeddedForm(HWND form);
std::optional<std::vector<std::string>> form(HWND parent,const std::wstring& title,const std::vector<Field>& fields,const Preview& preview={},
    const std::vector<std::pair<std::wstring,std::vector<size_t>>>& tabs={},
    const std::function<void(HDC,const RECT&,const std::vector<std::string>&)>& illustration={},int illustrationHeight=124,
    const FormTabStyle& tabStyle={});
}

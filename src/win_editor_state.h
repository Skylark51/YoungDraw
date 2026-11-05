#pragma once

#include "win_editor.h"

namespace yd::editor {

enum Tool{Select,Single,Double,Triple,Wedge,Hash,Ring5,Ring6,Benzene,Erase,Arrow,Curve,Fishhook,Text,ImageTool,Straight,Resonance,Delocalized,Orbital};

// Active document and selection
extern HWND win;
extern Document doc, dragBefore, dragBase;
extern History history;
extern std::wstring filePath, status, isolatedStructureResult;
extern std::vector<int> selection;
extern int hoverAtom, hoverBond, element, startAtom, startBond, targetContainer;
extern size_t targetSlot;
extern bool targetAbove, dirty;
extern std::function<void()> repeatAction;
extern bool showLayers, isolatedStructureEditing;
extern int sidebarPage, orbitalPhase;
extern OrbitalKind orbitalKind;
// Canvas viewport and active pointer gesture
extern int ww, wh;
inline constexpr int left=180, right=260, top=122, bottom=34;
extern double zoom;
extern Vec origin, mouse, down, panOrigin, annotationStart;
extern Tool tool;
extern int curveHandle, curveOwner, annotationDrag;
extern bool rightPanning, drawing, dragging, boxing, panning, moved, copyDrag, guides;
extern std::optional<Scene> canvasSceneCache;
bool contains(const std::vector<int>& v,int id);
void redraw();
void repaintOnly(const RECT* region=nullptr);
void title();
void pruneLockedSelection();
void changed();
Vec world(Vec p);
Vec screen(Vec p);
Vec center();
bool canvas(Vec p);
RectF canvasBox();
Color rgb(const std::string& s);
std::string number(double v);
double numeric(const std::string& s,double min=-1e6,double max=1e6);
std::vector<std::string> lines(std::string s);
std::vector<double> values(std::string s);
std::string colorValue(const std::string& s);
double measure(const std::string& s,double size,const std::string& font="Arial",int face=1);
Scene scene();
const Scene& sceneForPaint(bool reuse);
void fit();
Reaction* reaction(int id);
EnergyPlot* plot(int id);
int primary();

} // namespace yd::editor

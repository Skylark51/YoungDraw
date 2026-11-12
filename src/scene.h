#pragma once
#include "model.h"
#include <functional>
#include <map>
namespace yd {
struct Rect { double x=0,y=0,w=0,h=0; };
inline constexpr int mullikenSpinTextRole=14;
struct Primitive {
    enum Type { Line, Polygon, Text, Bezier, Ellipse, Box, Image } type=Line;
    std::vector<Vec> points;
    double width=.6, fontSize=10,rotation=0;
    std::string text;
    std::string color="#000000",font="Arial",imageHex,mime="image/png";
    int dash=0,face=0,owner=0;
    // Canvas-editable text: 1..3 reaction captions; 4..8 plot fields;
    // 9 structure energy; 10 cycle center caption; 11 transition-state name; 12 plot mechanism stage; 13 atom charge/radical annotation.
    int textRole=0,textIndex=-1;
    unsigned char opacity=255;
    int reactionArrow=-1; // Individually selectable cycle connector.
    bool closed=false,filled=false,flatEnd=false,flatStart=false;
};
using Measure = std::function<double(const std::string&,double,const std::string&,int)>;
struct ScriptRun {std::string text;int script=0;};
std::vector<ScriptRun> scriptRuns(const std::string& input);
struct PlotSnapPoint { Vec p; double percent=0; int state=0,path=0,edge=0; }; // edge: -1 left, 0 center, 1 right
struct PlotDragScale { double unitsPerPoint, low, high; };
struct CrossingCaption { int owner; size_t index; Rect bounds; Vec offset; };
struct Scene { std::vector<CrossingCaption> crossingCaptions; std::map<int,double> plotBaselines; std::map<int,PlotDragScale> plotDragScales; std::map<int,std::vector<PlotSnapPoint>> plotSnapPoints; std::vector<Primitive> items; Rect bounds; std::map<int,Rect> objectBounds; std::map<int,std::vector<Vec>> arrowPaths; };
std::vector<Vec> routeArrow(const Document& d,const Object& arrow,const std::map<int,Rect>& atomBoxes={});
std::vector<Primitive> arrowPrimitives(const Object& arrow);
void appendArrowCross(std::vector<Primitive>& items,const std::vector<Primitive>& shaft,const ArrowCross& mark,int owner);
Scene makeScene(const Document& d, const Measure& measure={});
struct PublicationReport {
    double widthPt=0,heightPt=0,scale=1;
    std::vector<std::string> warnings;
};
// Check dimensions, effective font/line size, and raster presence at print size.
// Warnings are advisory: scientific figures may need intentional exceptions.
PublicationReport publicationCheck(const Scene& scene,double widthPt);
std::string toSvg(const Scene& scene,double widthPt=0);
std::string hexToBase64(const std::string& hex);
}

#include "win_editor_state.h"
#include "win_editor_tabs.h"
#include "office.h"
#include "win_dialogs.h"
#include "win_ui.h"
#include <iomanip>
#include <regex>
#include <sstream>

namespace yd::editor {

HWND win;Document doc,dragBefore,dragBase;History history;std::wstring filePath,status=L"선택: Space · 결합: x · 화살표: e · 화면 이동: Ctrl+우클릭 드래그";
std::vector<int> selection;int hoverAtom=0,hoverBond=0,element=6,startAtom=0,startBond=0,targetContainer=0;size_t targetSlot=0;bool targetAbove=false,dirty=false;
std::function<void()> repeatAction;
bool showLayers=false;int sidebarPage=0;OrbitalKind orbitalKind=OrbitalKind::P;int orbitalPhase=0;
bool isolatedStructureEditing=false;
std::wstring isolatedStructureResult;
int ww=1360,wh=880;double zoom=4;Vec origin{300,200},mouse,down,panOrigin;
Tool tool=Select;
int curveHandle=-1,curveOwner=0,annotationDrag=0;
bool rightPanning=false;
bool drawing=false,dragging=false,boxing=false,panning=false,moved=false,copyDrag=false,guides=true;
// Invalidate the cached scene for document edits, and retain it for overlays.
std::optional<Scene> canvasSceneCache;
Vec annotationStart;
bool contains(const std::vector<int>& v,int id){return std::find(v.begin(),v.end(),id)!=v.end();}
void redraw(){canvasSceneCache.reset();InvalidateRect(win,nullptr,FALSE);}
void repaintOnly(const RECT* region){InvalidateRect(win,region,FALSE);}
void title(){std::wstring s=(isolatedStructureEditing?L"단계 구조 편집 | ":L"YoungDraw 0.4 | ")+(filePath.empty()?L"제목 없음":fs::path(filePath).filename().wstring())+(officeDocument&&office::editing()?L" [Office 개체 편집]":L"")+(dirty?L" *":L"");SetWindowTextW(win,s.c_str());refreshDocumentTabs();}
void pruneLockedSelection(){selection.erase(std::remove_if(selection.begin(),selection.end(),[](int id){return doc.isLocked(id);}),selection.end());if(doc.isLocked(targetContainer))targetContainer=0;}
void changed(){pruneLockedSelection();dirty=true;title();redraw();}
Vec world(Vec p){return (p-origin)*(1/zoom);}
Vec screen(Vec p){return origin+p*zoom;}
Vec center(){return world({left+(ww-left-right)/2.,top+(wh-top-bottom)/2.});}
bool canvas(Vec p){return p.x>left&&p.x<ww-right&&p.y>top&&p.y<wh-bottom;}
RectF canvasBox(){return {float(left),float(top),float(std::max(1,ww-left-right)),float(std::max(1,wh-top-bottom))};}
Color rgb(const std::string& s){unsigned n=0;if(s.size()==7)n=std::stoul(s.substr(1),nullptr,16);return Color(255,BYTE(n>>16),BYTE(n>>8),BYTE(n));}
std::string number(double v){std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(8)<<v;return s.str();}
double numeric(const std::string& s,double min,double max){size_t count;double v=std::stod(s,&count);if(count!=s.size()||!std::isfinite(v)||v<min||v>max)throw std::runtime_error("숫자가 허용 범위를 벗어났습니다.");return v;}
std::vector<std::string> lines(std::string s){std::vector<std::string> v;std::istringstream in(s);std::string row;while(std::getline(in,row)){if(!row.empty()&&row.back()=='\r')row.pop_back();v.push_back(row);}return v;}
std::string joined(const std::vector<std::string>& v){std::string s;for(auto& x:v)s+=(s.empty()?"":"\r\n")+x;return s;}
std::vector<double> values(std::string s){for(char& c:s)if(c==','||c==';')c=' ';std::istringstream in(s);in.imbue(std::locale::classic());std::vector<double> v;double n;while(in>>n){if(!std::isfinite(n)||std::abs(n)>1e6)throw std::runtime_error("올바른 에너지 값을 입력하세요.");v.push_back(n);}if(!in.eof())throw std::runtime_error("에너지를 공백으로 구분한 숫자로 입력하세요.");return v;}
std::string valueList(const std::vector<double>& v){std::string s;for(double n:v)s+=(s.empty()?"":" ")+number(n);return s;}
std::string colorValue(const std::string& s){if(!std::regex_match(s,std::regex("#[0-9a-fA-F]{6}")))throw std::runtime_error("색상 형식: #RRGGBB");return s;}
double measure(const std::string& s,double size,const std::string& font,int face){Bitmap bm(1,1,PixelFormat32bppARGB);Graphics g(&bm);FontFamily ff(ui::wide(font).c_str());Font f(&ff,float(size),(face&1?FontStyleBold:0)|(face&2?FontStyleItalic:0),UnitPixel);StringFormat fmt(StringFormat::GenericTypographic());RectF b;auto w=ui::wide(s);g.MeasureString(w.c_str(),-1,&f,PointF(0,0),&fmt,&b);return b.Width;}
Scene scene(){return makeScene(doc,measure);}
const Scene& sceneForPaint(bool reuse){
    // Direct paint calls used by tests request a fresh scene.
    if(!reuse||!canvasSceneCache)canvasSceneCache=makeScene(doc,measure);
    return *canvasSceneCache;
}
void fit(){auto b=scene().bounds;zoom=std::clamp(std::min((ww-left-right-65)/b.w,(wh-top-bottom-110)/b.h),.12,3.);origin={left+(ww-left-right)/2.-(b.x+b.w/2)*zoom,top+(wh-top-bottom)/2.-(b.y+b.h/2)*zoom};redraw();}
Reaction* reaction(int id){for(auto& r:doc.reactions)if(r.id==id)return &r;return nullptr;}
EnergyPlot* plot(int id){for(auto& p:doc.plots)if(p.id==id)return &p;return nullptr;}
int primary(){return selection.empty()?0:selection.back();}

} // namespace yd::editor

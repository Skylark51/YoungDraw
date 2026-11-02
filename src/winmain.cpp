#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include "model.h"
#include "scene.h"
#include "plot_data.h"
#include "mechanism_data.h"
#include "cdxml.h"
#include "win_dialogs.h"
#include "win_scripts.h"
#include "win_gallery.h"
#include "office.h"
#include "lab_templates_data.h"
#include "win_theme.h"
#include "win_ui.h"
#include "win_splash.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <optional>
#include <map>
#include <memory>
#include <regex>
#include <cwctype>
using namespace Gdiplus;using namespace yd;namespace fs=std::filesystem;
namespace {
HWND win;Document doc,dragBefore,dragBase;History history;std::wstring filePath,status=L"선택: Space · 결합: x · 화살표: e · 화면 이동: Ctrl+우클릭 드래그";
std::vector<int> selection;int hoverAtom=0,hoverBond=0,element=6,startAtom=0,startBond=0,targetContainer=0;size_t targetSlot=0;bool targetAbove=false,dirty=false;
std::function<void()> repeatAction;void alignSelection(int id);
bool showLayers=false;int sidebarPage=0;OrbitalKind orbitalKind=OrbitalKind::P;int orbitalPhase=0;
int ww=1360,wh=880;constexpr int left=180,right=260,top=122,bottom=34;double zoom=4;Vec origin{300,200},mouse,down,panOrigin;
enum Tool{Select,Single,Double,Triple,Wedge,Hash,Ring5,Ring6,Benzene,Erase,Arrow,Curve,Fishhook,Text,ImageTool,Straight,Resonance,Delocalized,Orbital};Tool tool=Select;
HWND documentTabs=nullptr;HFONT tabFont=nullptr;bool officeDocument=false;
HBRUSH menuBackground=nullptr;
void refreshAppearance();
struct DocumentTab {Document document;History history;std::wstring path,status;std::vector<int> selection;int target=0;size_t slot=0;bool above=false,dirty=false,office=false;double zoom=4;Vec origin;Tool tool=Select;};
std::vector<DocumentTab> documents;size_t activeDocument=0;
void refreshDocumentTabs();
int selectedCycleOwner=0,selectedCycleArrow=-1;
int curveHandle=-1,curveOwner=0,annotationDrag=0;
int snapPlot=0,snapStage=-1;MechanismStage snapLabel;std::optional<PlotSnapPoint> snapStart;
std::optional<PlotSnapPoint> nearestPlotSnap(const Scene& sc,Vec cursor){
    auto found=sc.plotSnapPoints.find(snapPlot);if(found==sc.plotSnapPoints.end())return {};
    std::optional<PlotSnapPoint> best;double distance=14;
    for(const auto& point:found->second){double d=(point.p*zoom+origin-cursor).length();if(d<distance){distance=d;best=point;}}
    return best;
}
Vec annotationStart;
bool rightPanning=false;
bool drawing=false,dragging=false,boxing=false,panning=false,moved=false,copyDrag=false,guides=true;
struct InlineTarget {int owner=0,role=0,index=-1;Vec p;double size=14;};
HWND inlineEdit=nullptr;HFONT inlineFont=nullptr;WNDPROC oldEditProc=nullptr;
InlineTarget inlineTarget;Document inlineBefore;bool inlineClosing=false,inlineValid=true,inlineComposing=false;
std::string inlineTypeface="Arial";int inlineFace=1;COLORREF inlineColor=RGB(0,0,0);
Vec inlineAnchor;int inlineWidth=0,inlineHeight=0;
void beginInline(std::optional<InlineTarget> target,Vec at);
bool finishInline(bool commit);
Color ink(255,28,36,49),muted(255,101,114,131),accent(255,0,123,112),border(255,222,229,234),white(255,255,255,255),black(255,0,0,0);
struct Hit{int id;RectF box;};std::vector<Hit> hits;
// The canvas scene is independent of hover state, zoom and pan. Invalidate it
// for document edits; retain it across overlay-only repaints.
std::optional<Scene> canvasSceneCache;
std::unique_ptr<Bitmap> paintBuffer;
bool contains(const std::vector<int>& v,int id){return std::find(v.begin(),v.end(),id)!=v.end();}
void redraw(){canvasSceneCache.reset();InvalidateRect(win,nullptr,FALSE);}
void repaintOnly(const RECT* region=nullptr){InvalidateRect(win,region,FALSE);}void title(){std::wstring s=L"YoungDraw 0.4 | "+(filePath.empty()?L"제목 없음":fs::path(filePath).filename().wstring())+(officeDocument&&office::editing()?L" [Office 개체 편집]":L"")+(dirty?L" *":L"");SetWindowTextW(win,s.c_str());refreshDocumentTabs();}
void pruneLockedSelection(){selection.erase(std::remove_if(selection.begin(),selection.end(),[](int id){return doc.isLocked(id);}),selection.end());if(doc.isLocked(targetContainer))targetContainer=0;}
void changed(){pruneLockedSelection();dirty=true;title();redraw();}Vec world(Vec p){return (p-origin)*(1/zoom);}Vec screen(Vec p){return origin+p*zoom;}Vec center(){return world({left+(ww-left-right)/2.,top+(wh-top-bottom)/2.});}
bool canvas(Vec p){return p.x>left&&p.x<ww-right&&p.y>top&&p.y<wh-bottom;}RectF canvasBox(){return {float(left),float(top),float(std::max(1,ww-left-right)),float(std::max(1,wh-top-bottom))};}
Color rgb(const std::string& s){unsigned n=0;if(s.size()==7)n=std::stoul(s.substr(1),nullptr,16);return Color(255,BYTE(n>>16),BYTE(n>>8),BYTE(n));}
std::string number(double v){std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(8)<<v;return s.str();}
double numeric(const std::string& s,double min=-1e6,double max=1e6){size_t count;double v=std::stod(s,&count);if(count!=s.size()||!std::isfinite(v)||v<min||v>max)throw std::runtime_error("숫자가 허용 범위를 벗어났습니다.");return v;}
std::vector<std::string> lines(std::string s){std::vector<std::string> v;std::istringstream in(s);std::string row;while(std::getline(in,row)){if(!row.empty()&&row.back()=='\r')row.pop_back();v.push_back(row);}return v;}
std::string joined(const std::vector<std::string>& v){std::string s;for(auto& x:v)s+=(s.empty()?"":"\r\n")+x;return s;}
std::vector<double> values(std::string s){for(char& c:s)if(c==','||c==';')c=' ';std::istringstream in(s);in.imbue(std::locale::classic());std::vector<double> v;double n;while(in>>n){if(!std::isfinite(n)||std::abs(n)>1e6)throw std::runtime_error("올바른 에너지 값을 입력하세요.");v.push_back(n);}if(!in.eof())throw std::runtime_error("에너지를 공백으로 구분한 숫자로 입력하세요.");return v;}
std::string valueList(const std::vector<double>& v){std::string s;for(double n:v)s+=(s.empty()?"":" ")+number(n);return s;}
std::string colorValue(const std::string& s){if(!std::regex_match(s,std::regex("#[0-9a-fA-F]{6}")))throw std::runtime_error("색상 형식: #RRGGBB");return s;}
void text(Graphics& g,const std::wstring& s,float x,float y,float size,Color c,bool bold=false){FontFamily ff(ui::interfaceFont());Font f(&ff,size,bold?FontStyleBold:FontStyleRegular,UnitPixel);SolidBrush b(c);StringFormat fmt(StringFormat::GenericTypographic());g.DrawString(s.c_str(),-1,&f,PointF(x,y),&fmt,&b);}
void fill(Graphics& g,RectF r,Color c){SolidBrush b(c);g.FillRectangle(&b,r);}void rule(Graphics& g,float x,float y,float z,float w,Color c){Pen p(c,1);g.DrawLine(&p,x,y,z,w);}
void icon(Graphics& g,int id,RectF box,Color color){
    auto st=g.Save();float unit=std::min(box.Width,box.Height)/48;g.TranslateTransform(box.X+box.Width/2,box.Y+box.Height/2);g.ScaleTransform(unit,unit);Pen pen(color,1.9f);pen.SetStartCap(LineCapRound);pen.SetEndCap(LineCapRound);SolidBrush brush(color);
    auto line=[&](float x,float y,float a,float b){g.DrawLine(&pen,x,y,a,b);};
    auto ring=[&](int n,bool benzene){std::vector<PointF> p;for(int i=0;i<n;i++){double a=-pi/2+pi/n+2*pi*i/n;p.push_back({float(17*std::cos(a)),float(17*std::sin(a))});}g.DrawPolygon(&pen,p.data(),n);if(benzene)for(int i=0;i<6;i+=2)line(p[i].X*.72f,p[i].Y*.72f,p[(i+1)%6].X*.72f,p[(i+1)%6].Y*.72f);};
    auto arrow=[&](){line(-20,0,19,0);line(19,0,12,-5);line(19,0,12,5);};
    switch(id){
    case 100:g.DrawRectangle(&pen,-15.f,-20.f,30.f,40.f);line(3,-20,15,-8);line(3,-20,3,-8);line(3,-8,15,-8);break;
    case 101:{PointF folder[]={{-22,-10},{-5,-10},{0,-17},{18,-17},{18,15},{-22,15}};g.DrawPolygon(&pen,folder,6);line(-18,17,-11,-2);line(-11,-2,23,-2);line(23,-2,18,17);break;}
    case 102:g.DrawRectangle(&pen,-19.f,-20.f,38.f,40.f);g.DrawRectangle(&pen,-11.f,-20.f,22.f,14.f);g.DrawRectangle(&pen,-12.f,3.f,24.f,17.f);line(5,-17,5,-9);break;
    case 104:case 105:{auto mirror=g.Save();if(id==105)g.ScaleTransform(-1.f,1.f);g.DrawBezier(&pen,PointF(-18,-6),PointF(18,-23),PointF(26,4),PointF(9,17));line(-18,-6,-16,-19);line(-18,-6,-4,-4);g.Restore(mirror);break;}
    case 107:g.DrawEllipse(&pen,-20.f,-20.f,29.f,29.f);line(5,5,22,22);line(-14,-6,2,-6);line(-6,-14,-6,2);break;
    case 125:for(int i=0;i<3;i++){float y=-15.f+i*15;g.DrawRectangle(&pen,-13.f,y-4,27.f,8.f);}line(-22,-22,-22,22);line(-26,-22,-18,-22);line(-26,22,-18,22);break;
    case 160:case 161:{auto rotation=g.Save();if(id==161)g.RotateTransform(90);pen.SetDashStyle(DashStyleDash);line(0,-23,0,23);pen.SetDashStyle(DashStyleSolid);PointF a[]={{-19,-14},{-6,0},{-19,14}},b[]={{19,-14},{6,0},{19,14}};g.DrawPolygon(&pen,a,3);g.DrawPolygon(&pen,b,3);g.Restore(rotation);break;}
    case 162:case 163:case 164:{
        // Screen y grows downwards: positive sweep is clockwise.
        const double start=-55*pi/180,sweep=(id==162?-280.:280.)*pi/180;
        g.DrawArc(&pen,-17.f,-17.f,34.f,34.f,float(start*180/pi),float(sweep*180/pi));
        double end=start+sweep,direction=sweep>0?1.:-1.;
        Vec tip{17*std::cos(end),17*std::sin(end)},tangent{-std::sin(end)*direction,std::cos(end)*direction},normal{-tangent.y,tangent.x};
        PointF head[]={{float(tip.x),float(tip.y)},{float(tip.x-tangent.x*9+normal.x*4.5),float(tip.y-tangent.y*9+normal.y*4.5)},{float(tip.x-tangent.x*9-normal.x*4.5),float(tip.y-tangent.y*9-normal.y*4.5)}};
        g.FillPolygon(&brush,head,3);
        if(id==164){line(-5,6,9,6);line(-5,6,3,-7);g.DrawArc(&pen,-1.f,-2.f,10.f,10.f,-60.f,60.f);}
        else {line(-7,5,-7,-5);line(-7,-5,5,-5);}break;}
    case 170:for(int i=0;i<3;i++)for(int j=0;j<3;j++)g.DrawRectangle(&pen,-20.f+i*15,-20.f+j*15,10.f,10.f);break;
    case 171:ring(5,false);line(0,-17,0,-25);line(-16,5,-24,8);break;
    case 172:ring(6,false);g.FillEllipse(&brush,-4.f,-4.f,8.f,8.f);for(int i=0;i<6;i++){double a=i*pi/3;line(0,0,float(17*std::cos(a)),float(17*std::sin(a)));}break;
    case 173:line(-23,8,-11,-7);line(-11,-7,2,8);line(2,8,15,-7);g.DrawEllipse(&pen,14.f,-13.f,11.f,11.f);break;
    case 174:line(-7,-22,-7,-5);line(7,-22,7,-5);line(-11,-22,11,-22);line(-7,-5,-20,19);line(7,-5,20,19);line(-20,19,20,19);line(-14,8,14,8);break;
    case 200:{PointF p[]={{-10,-19},{-10,17},{-2,8},{6,21},{12,17},{4,4},{16,3}};g.FillPolygon(&brush,p,7);break;}
    case 201:line(-20,11,20,-11);break;
    case 202:line(-20,6,18,-15);line(-17,14,21,-7);break;
    case 203:for(int i=-1;i<=1;i++)line(-20,9+i*6.f,20,-13+i*6.f);break;
    case 204:{PointF p[]={{-19,10},{15,-18},{23,-4}};g.FillPolygon(&brush,p,3);break;}
    case 205:for(int i=0;i<=7;i++){float t=i/7.f,x=-19+38*t,y=10-20*t;line(x-4*t,y-7*t,x+4*t,y+7*t);}break;
    case 206:ring(5,false);break;case 207:ring(6,false);break;case 208:ring(6,true);break;
    case 209:{PointF p[]={{-18,7},{4,-17},{20,-3},{0,18}};g.DrawPolygon(&pen,p,4);line(-9,-3,10,11);line(-18,20,22,20);break;}
    case 210:arrow();break;
    case 211:case 212:g.DrawBezier(&pen,PointF(-21,13),PointF(-16,-23),PointF(16,-23),PointF(21,9));line(21,9,11,4);if(id==211)line(21,9,23,-2);break;
    case 213:{FontFamily ff(L"Arial");Font f(&ff,35,FontStyleBold,UnitPixel);g.DrawString(L"A",-1,&f,PointF(-16,-24),&brush);line(16,-17,16,19);break;}
    case 214:g.DrawRectangle(&pen,-21.f,-17.f,42.f,34.f);g.DrawEllipse(&pen,7.f,-11.f,7.f,7.f);line(-18,12,-5,-5);line(-5,-5,5,8);line(5,8,12,2);line(12,2,20,12);break;
    case 215:line(-22,0,22,0);break;
    case 216:arrow();line(-20,0,-13,-5);line(-20,0,-13,5);break;
    case 217:line(-20,5,18,-16);pen.SetDashStyle(DashStyleDash);line(-16,14,22,-7);break;
    case 218:{GraphicsPath positive,negative;
        positive.AddBezier(0.f,0.f,-16.f,-23.f,-27.f,-12.f,-20.f,0.f);positive.AddBezier(-20.f,0.f,-27.f,12.f,-16.f,23.f,0.f,0.f);positive.CloseFigure();g.FillPath(&brush,&positive);
        negative.AddBezier(0.f,0.f,16.f,-23.f,27.f,-12.f,20.f,0.f);negative.AddBezier(20.f,0.f,27.f,12.f,16.f,23.f,0.f,0.f);negative.CloseFigure();g.DrawPath(&pen,&negative);break;}
    case 127:case 137:case 219:g.DrawEllipse(&pen,-12.f,-12.f,24.f,24.f);g.DrawEllipse(&pen,-4.f,-4.f,8.f,8.f);for(int i=0;i<8;i++){double a=i*pi/4;line(float(13*std::cos(a)),float(13*std::sin(a)),float(20*std::cos(a)),float(20*std::sin(a)));}break;
    case 130:case 131:case 141:line(-20,18,22,18);line(-20,18,-20,-19);g.DrawBezier(&pen,PointF(-17,11),PointF(-2,-27),PointF(7,20),PointF(20,-10));g.FillEllipse(&brush,-7.f,-5.f,5.f,5.f);break;
    case 132:g.DrawBezier(&pen,PointF(-21,12),PointF(-9,-25),PointF(10,-18),PointF(21,11));line(-21,19,-4,-3);line(-4,-3,21,19);break;
    case 133:line(-22,-10,22,-10);pen.SetDashStyle(DashStyleDash);line(-22,9,22,9);g.FillEllipse(&brush,-4.f,-14.f,8.f,8.f);break;
    case 110:line(-16,14,14,-16);for(int i=0;i<6;i++){double a=i*pi/3;line(float(11+7*std::cos(a)),float(-11+7*std::sin(a)),float(11+12*std::cos(a)),float(-11+12*std::sin(a)));}break;
    case 111:case 112:case 113:g.DrawRectangle(&pen,-20.f,-16.f,35.f,34.f);ring(6,false);if(id==112){line(13,4,13,22);line(4,13,22,13);}break;
    case 120:case 140:arrow();g.DrawEllipse(&pen,-23.f,-18.f,10.f,10.f);g.DrawPolygon(&pen,std::vector<PointF>{{12,-15},{18,-22},{24,-15},{18,-8}}.data(),4);break;
    case 121:line(-15,17,16,-15);line(-15,17,-18,22);line(-18,22,-10,20);line(12,-18,19,-11);break;
    case 122:arrow();line(0,-22,0,-8);line(-7,-15,7,-15);break;
    case 123:case 124:g.DrawRectangle(&pen,-17.f,-16.f,30.f,35.f);g.DrawRectangle(&pen,-8.f,-21.f,14.f,8.f);line(-9,2,19,2);line(19,2,12,-4);line(19,2,12,8);break;
    case 134:pen.SetDashStyle(DashStyleDash);g.DrawRectangle(&pen,-20.f,-17.f,40.f,34.f);line(-6,0,6,0);line(0,-6,0,6);break;
    case 150:{FontFamily ff(L"Arial");Font f(&ff,29,FontStyleBold,UnitPixel);g.DrawString(L"B",-1,&f,PointF(-19,-22),&brush);pen.SetWidth(4);line(-17,18,20,18);break;}
    case 156:{line(-12,7,0,-20);line(0,-20,12,7);line(-8,-2,8,-2);
        const Color colors[]={Color(255,230,40,40),Color(255,35,90,225),Color(255,0,150,80)};
        for(int i=0;i<3;i++){SolidBrush swatch(colors[i]);g.FillRectangle(&swatch,-20.f+i*14,14.f,12.f,7.f);}break;}
    case 155:{const COLORREF colors[]={RGB(0,0,0),RGB(230,40,40),RGB(35,90,225),RGB(0,150,80),RGB(245,150,0),RGB(145,60,190),RGB(110,110,110),RGB(0,170,190),RGB(220,65,140)};
        for(int i=0;i<9;i++){SolidBrush swatch(Color(255,GetRValue(colors[i]),GetGValue(colors[i]),GetBValue(colors[i])));g.FillRectangle(&swatch,-20.f+(i%3)*14,-20.f+(i/3)*14,11.f,11.f);}break;}
    default:arrow();break;
    }g.Restore(st);
}
const std::vector<std::wstring> toolNames={L"선택·이동 [Space]",L"단일 결합 [x]",L"이중 결합",L"삼중 결합",L"쐐기 결합",L"해시 결합",L"5각 고리",L"6각 고리",L"벤젠 [j]",L"지우개",L"반응 화살표 [e]",L"전자쌍 이동 화살표",L"단전자 이동 화살표",L"텍스트 [t] · 클릭하여 입력",L"이미지 삽입",L"직선 [l]",L"공명 화살표 ↔ [r]",L"공명 결합 (실선+파선) [a]",L"오비탈 · 드래그: 방향·크기"};
void toolButton(Graphics& g,int id,RectF r,bool active){
    hits.push_back({id,r});bool hovered=r.Contains(float(mouse.x),float(mouse.y));appearance::rounded(g,r,7,active?accent:hovered?appearance::palette().hover:appearance::palette().raised);
    std::wstring key=id==200?L"Space":id==201?L"x":id==208?L"j":id==210?L"e":id==213?L"t":id==215?L"l":id==216?L"r":id==217?L"a":id==218?L"오비탈":id==219?L"설정":L"";
    icon(g,id,{r.X+10,r.Y+1,r.Width-20,23},active?appearance::palette().onAccent:ink);
    if(!key.empty()){
        RectF strip{r.X+2,r.Y+r.Height-18,r.Width-4,18};
        ui::drawLabel(g,key,strip,id==218||id==219?12.f:13.f,active?appearance::palette().onAccent:appearance::palette().muted,true,true);
    }
}
void button(Graphics& g,int id,std::wstring label,RectF r,bool active=false){
    hits.push_back({id,r});
    appearance::rounded(g,r,6,active?accent:r.Contains(float(mouse.x),float(mouse.y))?appearance::palette().hover:appearance::palette().raised);
    const auto color=active?appearance::palette().onAccent:ink;
    // Narrow paired buttons need their full width for readable Korean labels.
    const bool pictogram=id>=110&&id<=150&&r.Width>=170;
    if(pictogram)icon(g,id,{r.X+8,r.Y+(r.Height-22)/2,22,22},color);
    RectF caption{r.X+(pictogram?36.f:6.f),r.Y,r.Width-(pictogram?42.f:12.f),r.Height};
    ui::drawLabel(g,label,caption,14.f,color,active,true);
}

double measure(const std::string& s,double size,const std::string& font="Arial",int face=1){Bitmap bm(1,1,PixelFormat32bppARGB);Graphics g(&bm);FontFamily ff(ui::wide(font).c_str());Font f(&ff,float(size),(face&1?FontStyleBold:0)|(face&2?FontStyleItalic:0),UnitPixel);StringFormat fmt(StringFormat::GenericTypographic());RectF b;auto w=ui::wide(s);g.MeasureString(w.c_str(),-1,&f,PointF(0,0),&fmt,&b);return b.Width;}
Scene scene(){return makeScene(doc,measure);}
const Scene& sceneForPaint(bool reuse){
    // Direct paint calls used by tests request a fresh scene.
    if(!reuse||!canvasSceneCache)canvasSceneCache=makeScene(doc,measure);
    return *canvasSceneCache;
}
void fit(){auto b=scene().bounds;zoom=std::clamp(std::min((ww-left-right-65)/b.w,(wh-top-bottom-110)/b.h),.12,3.);origin={left+(ww-left-right)/2.-(b.x+b.w/2)*zoom,top+(wh-top-bottom)/2.-(b.y+b.h/2)*zoom};redraw();}
HWND tooltip=nullptr;bool tipActive=false;RECT tipRect{};int tipCommand=0;std::wstring tipText;
std::wstring helpText(int id){
    if(id==184)return L"배경 · 테마\n6가지 작업실 분위기와 도트·격자·줄노트·등각·육각 문양을 선택합니다. 문서와 내보내기에는 영향을 주지 않습니다.";
    if(id==156)return L"텍스트 색상표\n단계 이름·설명을 편집하는 중에 누르면 해당 텍스트의 색을 바꿉니다. 반응 블록을 선택하면 블록 전체 텍스트에 적용합니다. 일반 텍스트도 지원합니다. Ctrl+Z: 되돌리기.";
    if(id==155)return L"원자 색상표\n원자를 선택한 뒤 색상표에서 색을 고르면 선택한 원자의 색을 바꿉니다. 여러 원자를 한 번에 변경할 수 있습니다. Ctrl+Z: 되돌리기.";
    if(id==218)return L"오비탈 그리기\n클릭: 기본 크기 / 드래그: 중심에서 방향·크기 지정\n원자 위에서 시작하면 원자 중심에 놓습니다. Shift: 30° 단위\n옆의 설정 버튼에서 s·p·d·혼성 로브와 위상 표시를 선택합니다.\nSpace로 선택 후 이동·회전·크기 조정, Ctrl+Z로 되돌리기를 지원합니다.";if(id==219)return L"오비탈 설정\ns·p·d·혼성 로브 / 흑백 위상·윤곽선·청색/적색 위상을 선택합니다.";if(id==182||id==183)return L"작업별 도구\n메커니즘과 그래프의 추가·편집·배치·예제를 각각 한곳에서 찾습니다.";if(id>=175&&id<=181)return L"이미지·그래프 레이어\n레이어를 잠그면 그림은 유지하면서 클릭·텍스트 편집·드래그·삭제를 막습니다. 이미지만 편집 / 그래프만 편집으로 빠르게 전환하세요. 잠금 상태는 문서에 저장되며 Ctrl+Z로 되돌릴 수 있습니다.";
    if(id==162)return L"반시계 방향 90° 회전\n선택한 구조를 왼쪽으로 90° 회전합니다. Ctrl+Z: 되돌리기.";
    if(id==163)return L"시계 방향 90° 회전\n선택한 구조를 오른쪽으로 90° 회전합니다. Ctrl+Z: 되돌리기.";
    if(id>=160&&id<=164)return L"선택 구조 회전·반전\n원자나 결합을 선택한 뒤 실행하세요. 구조의 길이와 입체 표시를 보존하며, 각도 회전은 숫자로 입력할 수 있습니다. Ctrl+Z: 되돌리기.";
    if(id>=170&&id<=174){static const wchar_t* names[]={L"템플릿 전체",L"리간드",L"배위 착물",L"기질",L"산·시약"};return std::wstring(names[id-170])+L"\n구조 그림을 살펴보고 큰 미리보기를 확인한 뒤 삽입할 수 있습니다.";}
    if(id==217)return L"공명 결합 (실선+파선) [a]\n선택한 결합들을 변환하거나 클릭·드래그하여 그립니다. 같은 결합 재클릭: 파선 위치 전환.";if(id==216)return L"공명 화살표 ↔ [r]\n드래그하여 공명 구조 사이에 양방향 화살표를 그립니다. Shift: 수평·수직.";if(id==215)return L"직선 [l]\n드래그하여 화살촉 없는 직선을 그립니다. Shift: 수평·수직.";
    if(id>=200&&id<=214){static const std::vector<std::wstring> detail={L"클릭하거나 드래그하여 객체를 선택·이동합니다. Shift: 선택 추가, Ctrl+객체 드래그: 복제.",L"원자를 클릭하거나 드래그하여 단일 결합을 그립니다.",L"드래그하여 이중 결합을 그립니다. 기존 결합을 클릭하면 차수를 바꿉니다.",L"드래그하여 삼중 결합을 그립니다.",L"앞으로 나오는 입체 결합을 그립니다. 같은 결합을 다시 클릭하면 방향을 바꿉니다.",L"뒤로 들어가는 입체 결합을 그립니다. 같은 결합을 다시 클릭하면 방향을 바꿉니다.",L"빈 공간에 5각 고리를 삽입하거나 원자·결합에 고리를 연결합니다.",L"빈 공간에 6각 고리를 삽입하거나 원자·결합에 고리를 연결합니다.",L"방향족 벤젠 고리를 그립니다. 원자·결합 위에서는 고리를 연결합니다.",L"클릭한 원자·결합·그리기 객체를 삭제합니다. Ctrl+Z로 복원할 수 있습니다.",L"드래그하여 반응 화살표를 그립니다. 원자에서 원자로 연결하면 전자 이동 곡선으로 자동 전환됩니다. Shift: 수평·수직.",L"전자쌍의 이동을 곡선과 전체 화살촉으로 표시합니다. 원자·결합선과 여백을 두고 작게 휘도록 배치합니다. 선택 후 점을 드래그하면 곡선을 수정합니다.",L"전자 하나의 이동을 곡선과 반쪽 화살촉으로 표시합니다. 원자·결합선과 약간 떨어져 배치합니다. 선택 후 점을 드래그하면 곡선을 수정합니다.",L"캔버스나 기존 텍스트를 클릭하여 입력합니다. Ctrl+Enter: 완료, Esc: 취소.",L"GIF/PNG/JPEG 원본 이미지를 삽입합니다. GIF 애니메이션과 원본 데이터는 문서에 보관합니다."};return toolNames[id-200]+L"\n"+detail[id-200];}
    if(id>=301&&id<=418)return ui::wide(symbol(id-300))+L" 원소 선택\n다음 원자를 이 원소로 그립니다. 원자를 선택한 상태에서는 해당 원소로 바꿉니다.";
    static const std::map<int,std::wstring> help={
      {100,L"새 문서 (Ctrl+N)\n새 문서를 만듭니다."},{101,L"열기 (Ctrl+O)\nYoungDraw 또는 CDXML 문서를 엽니다."},{102,L"저장 (Ctrl+S)\n구조·그래프·이미지와 편집 정보를 저장합니다."},{103,L"SVG 내보내기\n논문용 벡터 그림으로 저장합니다."},{108,L"CDXML 내보내기\nChemDraw에서 편집할 수 있는 문서로 저장합니다."},{104,L"실행 취소 (Ctrl+Z)\n직전 편집을 되돌립니다."},{105,L"다시 실행 (Ctrl+Shift+Z)\n취소한 편집을 다시 적용합니다."},{107,L"화면 맞춤\n전체 그림이 보이도록 확대·축소합니다."},
      {110,L"분자 구조 정리 (Ctrl+Shift+K)\n결합 길이와 고리 모양을 정돈합니다. 입체 배치는 보존합니다."},{111,L"템플릿\n기본 구조 또는 저장한 개인 템플릿을 삽입합니다."},{112,L"템플릿 보관\n선택한 객체를 재사용할 템플릿 파일로 저장합니다."},{113,L"구조 파일 삽입\nCDXML/.ydraw 파일을 현재 문서에 삽입합니다."},
      {120,L"반응 메커니즘 추가\n구조·단계 이름·화살표 설명을 담는 반응 블록을 만듭니다."},{121,L"선택 항목 편집\n메커니즘은 표로, 일반 객체는 속성창으로 편집합니다."},{122,L"반응 단계 추가\n선택한 메커니즘의 마지막에 새 단계를 추가합니다."},{123,L"구조 붙여넣기\n선택한 구조 삽입 영역에 복사한 구조·이미지를 크기와 위치를 맞춰 넣습니다."},{124,L"구조 파일 삽입\n선택한 구조 삽입 영역을 CDXML/.ydraw 또는 GIF/PNG/JPEG 파일로 채웁니다."},
      {125,L"자동 크기·간격 정렬\n선택한 구조나 반응 블록의 크기·단계 간격·이름 위치를 정돈합니다."},{126,L"촉매 순환 배치\n반응 단계를 원형으로 배치하고 마지막에서 첫 단계로 연결합니다."},{127,L"메커니즘 설정\n단위, 가로·순환 배치, 정방향·평형 화살표를 바꿉니다."},
      {130,L"에너지 그래프 추가\n여러 경로를 비교하는 에너지 프로필을 만듭니다."},{131,L"에너지 표 편집\n상태 이름과 경로별 에너지를 셀 단위로 편집합니다. Excel 표를 붙여넣을 수 있습니다."},{132,L"경로 추가\n선택한 그래프에 비교할 에너지 경로를 추가합니다."},{135,L"양쪽 분기 추가\n첫 상태를 중앙에 두고 기존 경로는 오른쪽, 새 경로는 왼쪽으로 이어집니다."},{133,L"경로 스타일\n이름·색·선 종류·굵기·표식을 바꿉니다."},{134,L"구조 삽입 영역 표시\n메커니즘과 그래프의 구조 삽입 영역을 표시하거나 숨깁니다. 내보내기에는 포함되지 않습니다."},{138,L"출판용 비율·여백 정돈\n선택 그래프의 가로·세로 비율과 글자 크기를 조정합니다. 데이터·색·선·축 범위는 유지합니다."},{139,L"반응 이름·화살표\n그래프 x축 위에 표시할 반응 이름과 화살표의 시작·끝 범위(0~100%)를 편집합니다."},{136,L"화살표 자석 배치\n준위의 왼쪽 끝·중앙·오른쪽 끝에서 두 점을 클릭하여 화살표 범위를 정합니다."},{142,L"구간별 색상\n같은 경로 안에서 준위·표식과 연결선 색상을 구간별로 설정합니다."},{137,L"그래프·축 설정\n제목·단위·크기·그래프 형태·y축 범위·눈금 간격·글자 크기·축 글자색·첨자를 설정합니다."},
      {140,L"메커니즘 예제\n영어 문구가 들어간 반응 메커니즘 예제를 엽니다. Ctrl+Z로 이전 문서를 복원합니다."},{141,L"에너지 그래프 예제\n구조가 연결된 에너지 프로필 예제를 엽니다. Ctrl+Z로 이전 문서를 복원합니다."},{150,L"Arial 굵게 / 2 pt\n14 pt 라벨과 2 pt 결합선을 적용하고 구조 크기를 맞춥니다."}};auto found=help.find(id);return found==help.end()?L"":found->second;
}
void hideTooltip(){KillTimer(win,911);if(!tooltip||!tipActive)return;TOOLINFOW info{};info.cbSize=TTTOOLINFOW_V1_SIZE;info.hwnd=win;info.uId=1;SendMessageW(tooltip,TTM_TRACKACTIVATE,FALSE,(LPARAM)&info);SendMessageW(tooltip,TTM_DELTOOLW,0,(LPARAM)&info);tipActive=false;tipCommand=0;}
void showTooltip(){KillTimer(win,911);if(!tipActive||drawing||dragging||boxing||panning||GetForegroundWindow()!=win)return;POINT point{};GetCursorPos(&point);POINT client=point;ScreenToClient(win,&client);if(!PtInRect(&tipRect,client))return;TOOLINFOW info{};info.cbSize=TTTOOLINFOW_V1_SIZE;info.hwnd=win;info.uId=1;SendMessageW(tooltip,TTM_TRACKPOSITION,0,MAKELPARAM(point.x+16,point.y+22));SendMessageW(tooltip,TTM_TRACKACTIVATE,TRUE,(LPARAM)&info);}
void updateTooltip(){if(!tooltip)return;if(drawing||dragging||boxing||panning){hideTooltip();return;}const Hit* hit=nullptr;for(auto& h:hits)if(h.box.Contains(float(mouse.x),float(mouse.y))){hit=&h;break;}if(!hit){hideTooltip();return;}RECT rect{LONG(hit->box.X),LONG(hit->box.Y),LONG(hit->box.X+hit->box.Width),LONG(hit->box.Y+hit->box.Height)};if(tipActive&&tipCommand==hit->id&&EqualRect(&rect,&tipRect))return;hideTooltip();tipText=helpText(hit->id);if(tipText.empty())return;tipRect=rect;tipCommand=hit->id;TOOLINFOW info{};info.cbSize=TTTOOLINFOW_V1_SIZE;info.hwnd=win;info.uId=1;info.uFlags=TTF_TRACK|TTF_ABSOLUTE;info.rect=rect;info.lpszText=tipText.data();tipActive=SendMessageW(tooltip,TTM_ADDTOOLW,0,(LPARAM)&info)!=0;if(tipActive)SetTimer(win,911,450,nullptr);}
void createTooltip(){INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,win,nullptr,GetModuleHandleW(nullptr),nullptr);SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,360);SendMessageW(tooltip,TTM_SETDELAYTIME,TTDT_INITIAL,450);SendMessageW(tooltip,TTM_SETDELAYTIME,TTDT_AUTOPOP,15000);SendMessageW(tooltip,TTM_SETDELAYTIME,TTDT_RESHOW,80);}
struct ImageCache {
    // The stream must outlive the GDI+ decoder, including all frame changes.
    Microsoft::WRL::ComPtr<IStream> stream;std::unique_ptr<Bitmap> bitmap;
    std::vector<ULONGLONG> ends;ULONGLONG started=0;UINT frame=0,cycles=1;bool infinite=false;
    UINT frameAt(ULONGLONG elapsed) const {
        if(ends.empty())return 0;
        if(!infinite&&elapsed/ends.back()>=cycles)return UINT(ends.size()-1);
        return UINT(std::upper_bound(ends.begin(),ends.end(),elapsed%ends.back())-ends.begin());
    }
    bool playing(ULONGLONG elapsed) const {return ends.size()>1&&(infinite||elapsed/ends.back()<cycles);}
};
std::map<std::string,ImageCache> imageCache;bool animationVisible=false;UINT animationDelay=1000;
Bitmap* decode(const std::string& hex,bool animate=false){
    auto it=imageCache.find(hex);
    if(it==imageCache.end()){
        std::string bytes;for(size_t i=0;i+1<hex.size();){if(isspace((unsigned char)hex[i])){i++;continue;}bytes+=char(std::stoi(hex.substr(i,2),nullptr,16));i+=2;}
        ImageCache c;c.stream.Attach(SHCreateMemStream((const BYTE*)bytes.data(),UINT(bytes.size())));
        if(!c.stream)return nullptr;c.bitmap.reset(Bitmap::FromStream(c.stream.Get()));
        if(!c.bitmap||c.bitmap->GetLastStatus()!=Ok)return nullptr;
        UINT count=c.bitmap->GetFrameDimensionsCount();std::vector<GUID> dimensions(count);
        if(count&&c.bitmap->GetFrameDimensionsList(dimensions.data(),count)==Ok&&std::any_of(dimensions.begin(),dimensions.end(),[](const GUID& id){return IsEqualGUID(id,FrameDimensionTime);})){
            UINT frames=c.bitmap->GetFrameCount(&FrameDimensionTime);
            auto property=[&](PROPID id){UINT size=c.bitmap->GetPropertyItemSize(id);std::vector<BYTE> data(size);if(size&&c.bitmap->GetPropertyItem(id,size,reinterpret_cast<PropertyItem*>(data.data()))!=Ok)data.clear();return data;};
            if(frames>1){
                auto delays=property(PropertyTagFrameDelay);auto* delay=delays.empty()?nullptr:reinterpret_cast<PropertyItem*>(delays.data());ULONGLONG total=0;
                for(UINT i=0;i<frames;i++){ULONG ticks=10;if(delay&&delay->type==PropertyTagTypeLong&&delay->length/sizeof(ULONG)>i)memcpy(&ticks,static_cast<BYTE*>(delay->value)+i*sizeof(ULONG),sizeof(ticks));total+=std::max(1UL,ticks)*10ULL;c.ends.push_back(total);}
                auto loops=property(PropertyTagLoopCount);auto* loop=loops.empty()?nullptr:reinterpret_cast<PropertyItem*>(loops.data());
                if(loop&&loop->type==PropertyTagTypeShort&&loop->length>=sizeof(USHORT)){USHORT repetitions;memcpy(&repetitions,loop->value,sizeof(repetitions));c.infinite=repetitions==0;c.cycles=UINT(repetitions)+1;}
            }
        }
        if(imageCache.size()>100)imageCache.clear();it=imageCache.emplace(hex,std::move(c)).first;
    }
    auto& c=it->second;if(animate&&!c.started)c.started=GetTickCount64();ULONGLONG elapsed=c.started?GetTickCount64()-c.started:0;UINT frame=animate?c.frameAt(elapsed):0;
    if(frame!=c.frame&&c.bitmap->SelectActiveFrame(&FrameDimensionTime,frame)==Ok)c.frame=frame;
    if(animate&&c.playing(elapsed)){animationVisible=true;animationDelay=UINT(std::min<ULONGLONG>(animationDelay,c.ends[frame]-elapsed%c.ends.back()));}
    return c.bitmap.get();
}
void render(Graphics& g,const Scene& s,bool animate=false,bool screenTheme=false){StringFormat fmt(StringFormat::GenericTypographic());for(auto& p:s.items){if(p.points.empty())continue;if(inlineEdit&&p.type==Primitive::Text&&p.owner==inlineTarget.owner&&p.textRole==inlineTarget.role&&p.textIndex==inlineTarget.index)continue;Color color=rgb(p.color);if(screenTheme)color=appearance::documentColor(color,p.type==Primitive::Box&&p.filled&&p.width==0);Pen pen(color,float(p.width));pen.SetStartCap(p.flatStart?LineCapFlat:LineCapRound);pen.SetEndCap(p.flatEnd?LineCapFlat:LineCapRound);if(p.dash){REAL w=std::max(.1f,float(p.width));REAL pattern[]={p.dash==1?4.f/w:1.f/w,p.dash==1?3.f/w:2.f/w};pen.SetDashPattern(pattern,2);}SolidBrush brush(color);std::vector<PointF> ps;for(auto v:p.points)ps.push_back({float(v.x),float(v.y)});
 if(p.type==Primitive::Line&&ps.size()>=2)g.DrawLines(&pen,ps.data(),INT(ps.size()));else if(p.type==Primitive::Bezier&&ps.size()>=4){GraphicsPath path;path.AddBeziers(ps.data(),INT(ps.size()));if(p.closed)path.CloseFigure();if(p.filled)g.FillPath(&brush,&path);if(p.width>0)g.DrawPath(&pen,&path);}else if(p.type==Primitive::Polygon&&ps.size()>=3)g.FillPolygon(&brush,ps.data(),INT(ps.size()));else if(p.type==Primitive::Text){FontFamily ff(ui::wide(p.font).c_str());Font f(&ff,float(p.fontSize),(p.face&1?FontStyleBold:0)|(p.face&2?FontStyleItalic:0)|(p.face&4?FontStyleUnderline:0),UnitPixel);auto w=ui::wide(p.text);auto saved=g.Save();g.TranslateTransform(ps[0].X,ps[0].Y);g.RotateTransform(float(p.rotation));g.DrawString(w.c_str(),-1,&f,PointF(0,0),&fmt,&brush);g.Restore(saved);}else if(ps.size()>=2){RectF box(std::min(ps[0].X,ps[1].X),std::min(ps[0].Y,ps[1].Y),std::abs(ps[1].X-ps[0].X),std::abs(ps[1].Y-ps[0].Y));if(p.type==Primitive::Ellipse){if(p.filled)g.FillEllipse(&brush,box);else g.DrawEllipse(&pen,box);}else if(p.type==Primitive::Box){if(p.filled)g.FillRectangle(&brush,box);else g.DrawRectangle(&pen,box);}else if(p.type==Primitive::Image){if(auto img=decode(p.imageHex,animate))g.DrawImage(img,box);}}}}
HENHMETAFILE officePicture(const Document& d){
    auto sc=makeScene(d,measure);auto b=sc.bounds;HDC dc=GetDC(nullptr);
    RectF frame(0,0,float(b.w+12),float(b.h+12));
    Metafile metafile(dc,frame,MetafileFrameUnitPoint,EmfTypeEmfPlusDual,L"YoungDraw\0Drawing\0");
    {Graphics g(&metafile);g.SetPageUnit(UnitPixel);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    // EMF physical frames use device millimetres, which can differ from logical DPI.
    float dx=GetDeviceCaps(dc,HORZSIZE)>0?float(GetDeviceCaps(dc,HORZRES)*25.4/GetDeviceCaps(dc,HORZSIZE)):g.GetDpiX();
    float dy=GetDeviceCaps(dc,VERTSIZE)>0?float(GetDeviceCaps(dc,VERTRES)*25.4/GetDeviceCaps(dc,VERTSIZE)):g.GetDpiY();
    g.ScaleTransform(dx/72.f,dy/72.f);g.TranslateTransform(float(6-b.x),float(6-b.y));render(g,sc);}
    ReleaseDC(nullptr,dc);return metafile.GetHENHMETAFILE();
}
void writePreview(const std::wstring& path,const Document& d){
    auto sc=makeScene(d,measure);auto b=sc.bounds;double scale=std::min(2.5,2000./std::max(b.w,b.h));
    Bitmap image(int(std::ceil(b.w*scale)),int(std::ceil(b.h*scale)),PixelFormat32bppARGB);Graphics g(&image);g.Clear(white);g.SetSmoothingMode(SmoothingModeHighQuality);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);g.TranslateTransform(float(-b.x*scale),float(-b.y*scale));g.ScaleTransform(float(scale),float(scale));render(g,sc);
    UINT n=0,bytes=0;GetImageEncodersSize(&n,&bytes);std::vector<BYTE> memory(bytes);auto* encoders=(ImageCodecInfo*)memory.data();GetImageEncoders(n,bytes,encoders);for(UINT i=0;i<n;i++)if(wcscmp(encoders[i].MimeType,L"image/png")==0){if(image.Save(path.c_str(),&encoders[i].Clsid,nullptr)!=Ok)throw std::runtime_error("Could not save preview");return;}throw std::runtime_error("PNG encoder unavailable");
}
std::string readFile(const std::wstring& path){std::ifstream f(fs::path(path),std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("파일을 열 수 없습니다.");auto n=f.tellg();if(n<0||n>64*1024*1024)throw std::runtime_error("파일은 64 MB까지 지원합니다.");std::string s(size_t(n),0);f.seekg(0);if(!f.read(s.data(),n))throw std::runtime_error("파일을 읽을 수 없습니다.");return s;}
void writeFile(const std::wstring& path,const std::string& data){if(data.size()>64*1024*1024)throw std::runtime_error("프로젝트의 64 MB 제한을 초과했습니다. 삽입 이미지 크기를 줄이세요.");std::wstring tmp=path+L".youngdraw.tmp";HANDLE f=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)throw std::runtime_error("임시 저장 파일을 만들 수 없습니다.");DWORD written=0;bool ok=WriteFile(f,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size();if(ok)ok=FlushFileBuffers(f)!=0;CloseHandle(f);if(!ok){DeleteFileW(tmp.c_str());throw std::runtime_error("파일을 저장할 수 없습니다.");}if(!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(tmp.c_str());throw std::runtime_error("저장 파일을 교체할 수 없습니다.");}}
std::optional<std::wstring> fileDialog(bool save,int type=0,std::wstring initial=L""){wchar_t path[32768]={};if(save)wcscpy_s(path,type==1?L"structure.svg":type==2?L"structure.cdxml":L"drawing.ydraw");OPENFILENAMEW f{};f.lStructSize=sizeof(f);f.hwndOwner=win;f.lpstrFile=path;f.nMaxFile=32768;f.lpstrFilter=type==1?L"SVG vector graphic\0*.svg\0\0":type==2?L"ChemDraw XML\0*.cdxml\0\0":type==4?L"Structures / Images\0*.ydraw;*.cdxml;*.gif;*.png;*.jpg;*.jpeg\0\0":type==3?L"Images (GIF, PNG, JPEG)\0*.gif;*.png;*.jpg;*.jpeg\0\0":L"YoungDraw / ChemDraw XML\0*.ydraw;*.cdxml\0YoungDraw project\0*.ydraw\0ChemDraw XML\0*.cdxml\0\0";f.lpstrDefExt=type==1?L"svg":type==2?L"cdxml":L"ydraw";f.lpstrInitialDir=initial.empty()?nullptr:initial.c_str();f.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);if(save?GetSaveFileNameW(&f):GetOpenFileNameW(&f))return std::wstring(path);return {};}
std::vector<std::wstring> openDocumentFiles(){
    std::vector<wchar_t> buffer(65536);OPENFILENAMEW f{};f.lStructSize=sizeof(f);f.hwndOwner=win;
    f.lpstrFile=buffer.data();f.nMaxFile=DWORD(buffer.size());f.lpstrFilter=L"YoungDraw / ChemDraw XML\0*.ydraw;*.cdxml\0\0";
    f.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST|OFN_ALLOWMULTISELECT;
    if(!GetOpenFileNameW(&f))return {};
    std::wstring first=buffer.data();const wchar_t* cursor=buffer.data()+first.size()+1;
    if(!*cursor)return {first};
    std::vector<std::wstring> paths;while(*cursor){std::wstring name=cursor;paths.push_back((fs::path(first)/name).wstring());cursor+=name.size()+1;}return paths;
}
Document selectedDocument(){auto chosen=selection;for(int id:selection)if(auto b=doc.bond(id)){chosen.push_back(b->a);chosen.push_back(b->b);}return doc.subset(chosen);}
UINT clipboardFormat(){return RegisterClipboardFormatW(L"YoungDraw.Project.v2");}
void copy(){if(selection.empty())return;if(FAILED(office::copy(selectedDocument())))throw std::runtime_error("클립보드 복사에 실패했습니다. 잠시 후 다시 시도하세요.");status=L"복사했습니다. Word·Excel에 붙여넣거나 YoungDraw에서 편집할 수 있습니다.";}
std::string clipboardMemoryText(HGLOBAL memory,bool unicode=false){
    if(!memory)return {};auto size=GlobalSize(memory);auto* bytes=static_cast<const char*>(GlobalLock(memory));if(!bytes)return {};
    std::string value;
    try{
        bool wide=unicode||(size>=2&&((BYTE(bytes[0])==0xff&&BYTE(bytes[1])==0xfe)||bytes[1]==0));
        if(wide){auto* chars=reinterpret_cast<const wchar_t*>(bytes);size_t count=size/sizeof(wchar_t),length=0;while(length<count&&chars[length])length++;if(length&&chars[0]==0xfeff){chars++;length--;}value=ui::utf8(std::wstring(chars,length));}
        else value.assign(bytes,strnlen(bytes,size));
    }catch(...){GlobalUnlock(memory);throw;}
    GlobalUnlock(memory);return value;
}
std::optional<Document> clipboardStructureText(const std::string& text){
    if(text.empty())return {};
    try{auto structure=fromCdxml(text);if(structure.allIds().empty())return {};return structure;}catch(...){return {};}
}
std::string clipboardMemoryBytes(HGLOBAL memory){
    if(!memory)return {};auto size=GlobalSize(memory);if(!size||size>64*1024*1024)return {};
    auto* data=static_cast<const char*>(GlobalLock(memory));if(!data)return {};
    std::string bytes;try{bytes.assign(data,size);}catch(...){GlobalUnlock(memory);throw;}GlobalUnlock(memory);return bytes;
}
std::string imageMime(const std::string& bytes){
    if(bytes.compare(0,6,"GIF87a")==0||bytes.compare(0,6,"GIF89a")==0)return "image/gif";
    if(bytes.compare(0,8,std::string("\x89PNG\r\n\x1a\n",8))==0)return "image/png";
    if(bytes.size()>=3&&BYTE(bytes[0])==0xff&&BYTE(bytes[1])==0xd8&&BYTE(bytes[2])==0xff)return "image/jpeg";
    return {};
}
Document imageDocument(const std::string& bytes){
    if(bytes.size()>10*1024*1024)throw std::runtime_error("이미지는 10 MB까지 지원합니다.");
    Object o;o.id=1;o.kind=ObjectKind::Image;o.mime=imageMime(bytes);
    if(o.mime.empty())throw std::runtime_error("GIF, PNG, JPEG 이미지를 선택하세요.");
    static const char hex[]="0123456789ABCDEF";o.imageHex.reserve(bytes.size()*2);
    for(unsigned char c:bytes){o.imageHex+=hex[c>>4];o.imageHex+=hex[c&15];}
    auto* img=decode(o.imageHex);if(!img||!img->GetWidth()||!img->GetHeight())throw std::runtime_error("이미지를 읽을 수 없습니다.");
    double width=80,height=width*img->GetHeight()/img->GetWidth();o.points={{0,0},{width,height}};
    Document result;result.nextId=2;result.objects.push_back(std::move(o));return result;
}
std::optional<Document> clipboardDocument(){
    if(!OpenClipboard(win))throw std::runtime_error("클립보드를 읽을 수 없습니다. 잠시 후 다시 붙여넣으세요.");
    struct Close {~Close(){CloseClipboard();}} close;
    for(UINT format:{clipboardFormat(),RegisterClipboardFormatW(L"ChemDraw XML Format"),RegisterClipboardFormatW(L"CDXML"),RegisterClipboardFormatW(L"ChemDraw XML"),UINT(CF_UNICODETEXT),UINT(CF_TEXT)}){
        auto text=clipboardMemoryText(GetClipboardData(format),format==CF_UNICODETEXT);
        if(auto structure=clipboardStructureText(text))return structure;
    }
    std::string binaryError;
    for(const wchar_t* name:{L"ChemDraw Interchange Format",L"ChemDraw CDX",L"CDX"}){
        auto bytes=clipboardMemoryBytes(GetClipboardData(RegisterClipboardFormatW(name)));
        if(bytes.empty())continue;
        try{auto structure=fromCdx(bytes);if(!structure.allIds().empty())return structure;}
        catch(const std::exception& e){binaryError=e.what();}
    }
    // Prefer encoded originals over bitmap clipboard previews, which lose animation.
    for(const wchar_t* name:{L"GIF",L"image/gif",L"PNG",L"image/png"}){
        auto bytes=clipboardMemoryBytes(GetClipboardData(RegisterClipboardFormatW(name)));
        if(!bytes.empty()&&!imageMime(bytes).empty())return imageDocument(bytes);
    }
    if(auto drop=static_cast<HDROP>(GetClipboardData(CF_HDROP))){
        UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0);
        for(UINT i=0;i<count;i++){
            UINT length=DragQueryFileW(drop,i,nullptr,0);std::wstring path(length+1,L'\0');DragQueryFileW(drop,i,path.data(),length+1);path.resize(length);
            auto ext=fs::path(path).extension().wstring();
            if(_wcsicmp(ext.c_str(),L".gif")&&_wcsicmp(ext.c_str(),L".png")&&_wcsicmp(ext.c_str(),L".jpg")&&_wcsicmp(ext.c_str(),L".jpeg"))continue;
            return imageDocument(readFile(path));
        }
    }
    if(!binaryError.empty())throw std::runtime_error("ChemDraw 구조를 읽지 못했습니다: "+binaryError);
    return {};
}
void insert(const Document& source,Vec at){Document fragment=source;upgradeBondGeometry(fragment);auto b=makeScene(fragment).bounds;int first=doc.nextId;history.checkpoint(doc);doc.append(fragment,at-Vec{b.x+b.w/2,b.y+b.h/2});selection.clear();for(int id:doc.allIds())if(id>=first)selection.push_back(id);changed();}
fs::path templateDir(){wchar_t path[32768];DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);if(!n||n>=32768)throw std::runtime_error("LOCALAPPDATA unavailable");fs::path p=fs::path(path)/L"YoungDraw"/L"Templates";fs::create_directories(p);return p;}

fs::path favoritesPath(){return templateDir().parent_path()/L"template-favorites.txt";}
std::string templateKey(const ui::TemplateCard& card){
    return card.path.empty()?std::string("builtin:")+ui::utf8(card.category)+":"+ui::utf8(card.name)
                            :std::string("user:")+ui::utf8(card.path);
}
std::set<std::string> loadFavorites(){
    std::set<std::string> keys;std::ifstream in(favoritesPath(),std::ios::binary);std::string line;
    while(std::getline(in,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(!line.empty())keys.insert(line);}
    return keys;
}
void saveFavorites(const std::set<std::string>& keys){
    std::string data;for(const auto& key:keys)data+=key+"\n";
    writeFile(favoritesPath().wstring(),data);
}
bool validTemplatePart(const std::wstring& part){
    if(part.empty()||part==L"."||part==L".."||part.size()>100||part.back()==L'.'||part.back()==L' ')return false;
    if(part.find_first_of(L"<>:\"/\\|?*")!=std::wstring::npos)return false;
    for(wchar_t c:part)if(c<32)return false;
    std::wstring upper=part;std::transform(upper.begin(),upper.end(),upper.begin(),[](wchar_t c){return wchar_t(std::towupper(c));});
    auto stem=upper.substr(0,upper.find(L'.'));
    if(stem==L"CON"||stem==L"PRN"||stem==L"AUX"||stem==L"NUL")return false;
    if(stem.size()==4&&(stem.substr(0,3)==L"COM"||stem.substr(0,3)==L"LPT")&&stem[3]>=L'1'&&stem[3]<=L'9')return false;
    return true;
}
fs::path templateLocation(const std::wstring& name,const std::wstring& group,const std::wstring& ext=L".ydraw"){
    if(!validTemplatePart(name)||!validTemplatePart(group))throw std::runtime_error("템플릿 이름·분류에 사용할 수 없는 문자가 있습니다.");
    return templateDir()/group/(name+ext);
}
std::wstring groupOf(const fs::path& path){
    auto relative=path.parent_path().lexically_relative(templateDir());
    return relative.empty()||relative==fs::path(L".")?L"일반":relative.wstring();
}
void retargetTemplateTabs(const fs::path& from,const std::optional<fs::path>& to){
    if(filePath==from.wstring())filePath=to?to->wstring():L"";
    for(auto& tab:documents)if(tab.path==from.wstring())tab.path=to?to->wstring():L"";
    title();
}
std::vector<ui::TemplateCard>& templateCards(size_t& unreadable){
    struct Cache {std::string signature;std::vector<ui::TemplateCard> cards;size_t unreadable=0;};
    static Cache cache;
    std::vector<fs::path> paths;
    for(auto& entry:fs::recursive_directory_iterator(templateDir(),fs::directory_options::skip_permission_denied)){
        if(!entry.is_regular_file())continue;
        auto ext=entry.path().extension().wstring();
        if(_wcsicmp(ext.c_str(),L".ydraw")==0||_wcsicmp(ext.c_str(),L".cdxml")==0)paths.push_back(entry.path());
    }
    std::sort(paths.begin(),paths.end());
    std::string fingerprint;
    for(const auto& path:paths){
        try{
            fingerprint+=ui::utf8(path.wstring())+"|"+std::to_string(fs::file_size(path))+"|"+
                         std::to_string(fs::last_write_time(path).time_since_epoch().count())+"\n";
        }catch(const fs::filesystem_error&){fingerprint+=ui::utf8(path.wstring())+"|unreadable\n";}
    }
    if(cache.signature!=fingerprint||cache.cards.empty()){
        Cache next;next.signature=fingerprint;
        auto add=[&](std::wstring category,std::wstring name,Document fragment,std::wstring path=L"",std::wstring group=L""){
            auto preview=makeScene(fragment,measure);
            next.cards.push_back({std::move(name),std::move(category),std::move(fragment),std::move(preview),std::move(path),std::move(group),false});
        };
        Document basic;basic.ring({0,0},6,true);add(L"기본",L"벤젠",basic);
        basic={};basic.ring({0,0},5);add(L"기본",L"사이클로펜테인",basic);
        add(L"기본",L"의자형 · 형태 보호",chairTemplate());
        add(L"기본",L"반응 메커니즘",mechanismDemo());
        add(L"기본",L"촉매 순환 · 수소화 예시",catalyticCycleDemo());
        add(L"기본",L"에너지 그래프",coordinateDemo());
        for(auto& entry:labTemplates)add(ui::wide(entry.category),ui::wide(entry.name),fromNative(entry.data));
        for(const auto& path:paths)try{
            add(L"사용자",path.stem().wstring(),fromCdxml(readFile(path.wstring())),path.wstring(),groupOf(path));
        }catch(...){next.unreadable++;}
        cache=std::move(next);
    }
    auto favorites=loadFavorites();
    for(auto& card:cache.cards)card.favorite=favorites.count(templateKey(card))!=0;
    unreadable=cache.unreadable;
    return cache.cards;
}
void saveCurrentTemplate(){
    Document fragment=selection.empty()?doc:selectedDocument();
    if(fragment.allIds().empty())throw std::runtime_error("템플릿으로 저장할 객체가 없습니다.");
    auto result=ui::form(win,L"템플릿 저장",{{L"템플릿 이름","새 템플릿"},{L"분류(폴더)","일반"}});
    if(!result)return;
    auto path=templateLocation(ui::wide((*result)[0]),ui::wide((*result)[1]));
    if(fs::exists(path)&&MessageBoxW(win,L"같은 이름의 템플릿을 덮어쓰시겠습니까?",L"템플릿 저장",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
    fs::create_directories(path.parent_path());
    writeFile(path.wstring(),toNative(fragment));
    status=L"사용자 템플릿을 저장했습니다. · 삽입 > 템플릿에서 관리할 수 있습니다.";
    redraw();
}

bool save(bool as=false){if(officeDocument&&office::editing()&&!as){if(FAILED(office::update()))throw std::runtime_error("Office 문서에 변경 내용을 반영하지 못했습니다.");dirty=false;title();status=L"Office 문서에 변경 내용을 반영했습니다.";redraw();return true;}auto path=filePath;if(path.empty()||as){auto p=fileDialog(true);if(!p)return false;path=*p;}auto ext=fs::path(path).extension().wstring();writeFile(path,_wcsicmp(ext.c_str(),L".cdxml")==0?toCdxml(doc):toNative(doc));filePath=path;dirty=false;title();status=L"문서를 저장했습니다.";redraw();return true;}
bool discard(){if(!dirty)return true;int answer=MessageBoxW(win,L"변경 내용을 저장하시겠습니까?",L"YoungDraw",MB_YESNOCANCEL|MB_ICONQUESTION);return answer==IDNO||(answer==IDYES&&save());}
void storeDocumentTab(){
    if(documents.empty())documents.emplace_back();
    documents[activeDocument]={doc,history,filePath,status,selection,targetContainer,targetSlot,targetAbove,dirty,officeDocument,zoom,origin,tool};
}
void refreshDocumentTabs(){
    if(!documentTabs)return;
    TabCtrl_DeleteAllItems(documentTabs);
    for(size_t i=0;i<documents.size();i++){
        auto path=i==activeDocument?filePath:documents[i].path;
        bool modified=i==activeDocument?dirty:documents[i].dirty;
        std::wstring label=(path.empty()?L"제목 없음 "+std::to_wstring(i+1):fs::path(path).filename().wstring())+(modified?L" *":L"")+L"   ×";
        TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=label.data();TabCtrl_InsertItem(documentTabs,int(i),&item);
    }
    TabCtrl_SetCurSel(documentTabs,int(activeDocument));
}
bool switchDocumentTab(size_t index){
    if(index>=documents.size())return false;
    if(!finishInline(true)){TabCtrl_SetCurSel(documentTabs,int(activeDocument));return false;}
    if(index==activeDocument)return true;
    selectedCycleOwner=0;selectedCycleArrow=-1;snapPlot=0;snapStart.reset();storeDocumentTab();activeDocument=index;const auto& next=documents[index];
    doc=next.document;history=next.history;filePath=next.path;status=next.status;selection=next.selection;
    targetContainer=next.target;targetSlot=next.slot;targetAbove=next.above;dirty=next.dirty;officeDocument=next.office;
    zoom=next.zoom;origin=next.origin;tool=next.tool;hoverAtom=hoverBond=0;curveHandle=curveOwner=-1;
    title();redraw();return true;
}
void newDocumentTab(Document document={},std::wstring path=L"",bool fromOffice=false){
    selectedCycleOwner=0;selectedCycleArrow=-1;
    snapPlot=0;snapStart.reset();
    if(!finishInline(true))return;
    storeDocumentTab();activeDocument=documents.size();documents.emplace_back();
    doc=std::move(document);history.clear();filePath=std::move(path);selection.clear();targetContainer=0;targetSlot=0;targetAbove=false;
    Document beforeUpgrade=doc;bool upgraded=upgradeBondGeometry(doc);if(upgraded)history.checkpoint(beforeUpgrade);dirty=upgraded;officeDocument=fromOffice;tool=Select;hoverAtom=hoverBond=0;status=upgraded?L"벤젠 75%·벤질 연결 길이 기준을 적용했습니다. 저장하면 새 기준이 유지됩니다.":L"새 문서 탭 · Ctrl+Tab: 다음 탭 · Ctrl+W: 현재 탭 닫기";
    fit();storeDocumentTab();title();
}
bool closeDocumentTab(size_t index){
    snapPlot=0;snapStart.reset();
    if(!switchDocumentTab(index)||!discard())return false;
    bool closingOffice=officeDocument&&office::editing();if(closingOffice)office::close();
    documents.erase(documents.begin()+index);
    if(documents.empty()){documents.emplace_back();activeDocument=0;doc={};history.clear();filePath.clear();selection.clear();targetContainer=0;targetSlot=0;targetAbove=false;dirty=officeDocument=false;tool=Select;fit();storeDocumentTab();title();}
    else {activeDocument=std::min(index,documents.size()-1);auto next=documents[activeDocument];
        // The active globals still describe the removed tab; restore without saving them.
        doc=next.document;history=next.history;filePath=next.path;status=next.status;selection=next.selection;targetContainer=next.target;targetSlot=next.slot;targetAbove=next.above;dirty=next.dirty;officeDocument=next.office;zoom=next.zoom;origin=next.origin;tool=next.tool;hoverAtom=hoverBond=0;title();redraw();}
    if(closingOffice)ShowWindow(win,SW_SHOW);
    return true;
}
bool closeAllDocuments(){
    size_t initial=activeDocument;
    for(size_t i=0;i<documents.size();i++){if(!switchDocumentTab(i)||!discard())return false;storeDocumentTab();}
    switchDocumentTab(initial);return true;
}
Document officeCurrentDocument(){
    if(officeDocument)return doc;
    for(auto& tab:documents)if(tab.office)return tab.document;
    return doc;
}
LRESULT CALLBACK documentTabProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_MOUSEMOVE){TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,hwnd,0};TrackMouseEvent(&tracking);InvalidateRect(hwnd,nullptr,FALSE);}
    if(msg==WM_MOUSELEAVE)InvalidateRect(hwnd,nullptr,FALSE);
    if(msg==WM_PAINT){
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT client{};GetClientRect(hwnd,&client);
        Bitmap buffer(std::max(1L,client.right),std::max(1L,client.bottom),PixelFormat32bppARGB);Graphics g(&buffer);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        const auto& p=appearance::palette();g.Clear(p.chrome);POINT cursor{};GetCursorPos(&cursor);ScreenToClient(hwnd,&cursor);
        for(int i=0;i<TabCtrl_GetItemCount(hwnd);i++){RECT r{};if(!TabCtrl_GetItemRect(hwnd,i,&r))continue;bool selected=i==TabCtrl_GetCurSel(hwnd),hovered=PtInRect(&r,cursor)!=FALSE;
            RectF box(float(r.left+2),2.f,float(r.right-r.left-4),float(client.bottom-4));appearance::rounded(g,box,6,selected?p.raised:hovered?p.hover:p.chrome);
            if(selected)appearance::rounded(g,{box.X+12,box.GetBottom()-3,box.Width-24,2},1,p.accent);
            wchar_t label[512]{};TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=label;item.cchTextMax=512;TabCtrl_GetItem(hwnd,i,&item);std::wstring name(label);auto close=name.rfind(L"   ×");if(close!=std::wstring::npos)name.resize(close);
            auto saved=g.Save();g.SetClip(RectF(box.X+10,box.Y,box.Width-34,box.Height));text(g,name,box.X+10,box.Y+6,12,selected?p.text:p.muted,selected);g.Restore(saved);
            Pen cross(hovered?p.text:p.muted,1.2f);float x=float(r.right-14),y=float(client.bottom)/2;g.DrawLine(&cross,x-3,y-3,x+3,y+3);g.DrawLine(&cross,x-3,y+3,x+3,y-3);
        }
        Graphics out(dc);ui::presentPixels(out,buffer);EndPaint(hwnd,&ps);return 0;
    }
    if(msg==WM_LBUTTONUP){TCHITTESTINFO hit{{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)},0};int index=TabCtrl_HitTest(hwnd,&hit);RECT r{};
        if(index>=0&&TabCtrl_GetItemRect(hwnd,index,&r)&&hit.pt.x>=r.right-24){PostMessageW(win,WM_APP+3,index,0);return 0;}}
    return DefSubclassProc(hwnd,msg,wp,lp);
}
void createDocumentTabs(){
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TAB_CLASSES};InitCommonControlsEx(&controls);
    documentTabs=CreateWindowExW(0,WC_TABCONTROLW,L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|TCS_FOCUSNEVER, left,89,ww-left-right,30,win,(HMENU)950,GetModuleHandleW(nullptr),nullptr);
    tabFont=CreateFontW(-14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,ui::interfaceFont());
    SendMessageW(documentTabs,WM_SETFONT,(WPARAM)tabFont,TRUE);SetWindowSubclass(documentTabs,documentTabProc,1,0);
    TabCtrl_SetPadding(documentTabs,20,5);
    storeDocumentTab();refreshDocumentTabs();
}
Reaction* reaction(int id){for(auto& r:doc.reactions)if(r.id==id)return &r;return nullptr;}EnergyPlot* plot(int id){for(auto& p:doc.plots)if(p.id==id)return &p;return nullptr;}
int primary(){return selection.empty()?0:selection.back();}
void clean(){
    Document before=doc;auto originalSelection=selection;auto chosen=selection;
    if(selection.size()==1&&selection.front()==targetContainer){
        if(auto* r=reaction(targetContainer);r&&targetSlot<r->slots.size())chosen=r->slots[targetSlot].members;
        if(auto* p=plot(targetContainer);p&&targetSlot<p->states.size())chosen=targetAbove?p->states[targetSlot].above:p->states[targetSlot].below;
        if(chosen.empty())throw std::runtime_error("선택한 구조 삽입 영역이 비어 있습니다.");
    }
    auto subset=chosen.empty()?before:before.subset(chosen);std::set<int> requested;
    for(auto& a:subset.atoms)requested.insert(a.id);
    for(auto& b:before.bonds)if(contains(chosen,b.id)){requested.insert(b.a);requested.insert(b.b);}
    std::vector<std::vector<int>> components;std::set<int> visited;
    for(auto& a:before.atoms)if(requested.count(a.id)&&!visited.count(a.id)){auto ids=before.component(a.id);visited.insert(ids.begin(),ids.end());components.push_back(std::move(ids));}
    if(components.empty())throw std::runtime_error("정리할 구조의 원자·결합 또는 구조 삽입 영역을 선택하세요.");
    CleanupReport report;
    auto update=[&](const std::vector<std::string>& values)->std::wstring{
        Document candidate=before;report=cleanStructure(candidate,chosen,values[0]=="0");double degrees=numeric(values[1],-36000,36000);
        if(degrees)for(auto& ids:components)transformStructures(candidate,ids,degrees);
        std::vector<std::pair<int,bool>> affected;
        for(auto& r:candidate.reactions){bool changedSlot=false;for(auto& slot:r.slots)for(int id:slot.members)if(auto* a=candidate.atom(id);a&&before.atom(id)&&(a->p-before.atom(id)->p).length()>.001)changedSlot=true;if(changedSlot)affected.push_back({r.id,r.cyclic});}
        for(auto& r:affected)optimizeMechanism(candidate,{r.first},r.second);
        doc=std::move(candidate);redraw();UpdateWindow(win);return {};
    };
    auto result=ui::form(win,L"구조 정리 · 미리보기",{{L"정리 방식","0",false,{L"원본 형태·방향 유지 / 기준 크기 맞춤",L"2D 결합·고리 재정리 / 겹침 발생 시 원본 유지"}},{L"추가 회전 (도, +: 시계방향)","0"}},update);
    if(!result){doc=before;selection=originalSelection;redraw();return;}
    update(*result);if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();fit();}
    status=L"구조 "+std::to_wstring(report.components)+L"개 정리 · 원본 배치 "+std::to_wstring(report.protectedComponents)+L"개 보호 · Ctrl+Z: 되돌리기";redraw();
}
Object* textObject(int id){for(auto& o:doc.objects)if(o.id==id&&o.kind==ObjectKind::Text)return &o;return nullptr;}
std::string inlineValue(const InlineTarget& t){
    if(t.role==0){if(auto* o=textObject(t.owner))return o->text;if(auto* a=doc.atom(t.owner))return a->label.empty()?symbol(a->element):a->label;}
    if(auto* r=reaction(t.owner)){if(t.role==1)return r->slots.at(size_t(t.index)).label;if(t.role==2)return transitionCaption(r->above.at(size_t(t.index))).description;if(t.role==11)return transitionCaption(r->above.at(size_t(t.index))).name;if(t.role==3)return r->below.at(size_t(t.index));if(t.role==9)return r->slots.at(size_t(t.index)).energy;if(t.role==10)return r->centerLabel;}
    if(auto* p=plot(t.owner)){if(t.role==12)return p->stages.at(size_t(t.index)).label;if(t.role==4)return p->title;if(t.role==5)return p->unit;if(t.role==6)return p->states.at(size_t(t.index)).label;if(t.role==7)return p->series.at(size_t(t.index)).name;if(t.role==8)return number(p->states.at(size_t(t.index)).energy);}
    return {};
}
void setInlineValue(const InlineTarget& t,const std::string& value){
    if(t.role==0){if(auto* o=textObject(t.owner)){o->text=chemicalScripts(value);o->runs.clear();return;}if(auto* a=doc.atom(t.owner)){int e=atomicNumber(value);if(e){setAtomElement(doc,a->id,e);a->label.clear();a->explicitH=-1;}else a->label=chemicalScripts(value);a->labelRuns.clear();return;}}
    if(auto* r=reaction(t.owner)){if(t.role==1)r->slots.at(size_t(t.index)).label=value;if(t.role==2||t.role==11){auto& raw=r->above.at(size_t(t.index));auto caption=transitionCaption(raw);if(t.role==2)caption.description=value;else{caption.name=value;caption.separated=true;}raw=transitionCaptionText(caption);}if(t.role==3)r->below.at(size_t(t.index))=value;if(t.role==9)r->slots.at(size_t(t.index)).energy=value;if(t.role==10)r->centerLabel=value;return;}
    if(auto* p=plot(t.owner)){if(t.role==12){if(value.empty())throw std::runtime_error("단계 이름을 입력하세요.");p->stages.at(size_t(t.index)).label=value;}if(t.role==4)p->title=value;if(t.role==5)p->unit=value;if(t.role==6)p->states.at(size_t(t.index)).label=value;if(t.role==7)p->series.at(size_t(t.index)).name=value;if(t.role==8){double e=numeric(value);p->states.at(size_t(t.index)).energy=e;if(!p->series.empty()){auto& path=p->series[0];path.values.at(size_t(t.index))=e;if(!path.present.empty())path.present.at(size_t(t.index))=true;}}return;}
}
std::optional<InlineTarget> textAt(Vec p){
    auto sc=scene();for(auto i=sc.items.rbegin();i!=sc.items.rend();++i){const auto& t=*i;if(t.type!=Primitive::Text||t.points.empty()||t.textRole==13||doc.isLocked(t.owner))continue;if(!t.textRole&&!textObject(t.owner)&&!doc.atom(t.owner))continue;
        auto a=t.points[0];double w=measure(t.text,t.fontSize,t.font,t.face),h=t.fontSize*1.3*(1+std::count(t.text.begin(),t.text.end(),'\n'));
        if(p.x>=a.x-2&&p.x<=a.x+w+2&&p.y>=a.y-2&&p.y<=a.y+h+2)return InlineTarget{t.owner,t.textRole,t.textIndex,a,t.fontSize};
    }return {};
}
void sizeInline(){
    if(!inlineEdit)return;int n=GetWindowTextLengthW(inlineEdit);std::wstring w(n+1,0);GetWindowTextW(inlineEdit,w.data(),n+1);w.resize(n);auto value=ui::utf8(w);
    double textWidth=0;for(auto& row:lines(value))textWidth=std::max(textWidth,measure(row,inlineTarget.size,inlineTypeface,inlineFace)*zoom);
    int width=inlineWidth&&textWidth+8<=inlineWidth?inlineWidth:std::max(96,int(textWidth)+48),height=std::max(28,int(inlineTarget.size*zoom*1.35*(1+std::count(value.begin(),value.end(),'\n'))+8));
    width=std::min(width,std::max(96,ww-left-right-10));height=std::min(height,std::max(28,wh-top-bottom-10));
    if(width<=inlineWidth&&height<=inlineHeight)return;inlineWidth=std::max(width,inlineWidth);inlineHeight=std::max(height,inlineHeight);
    Vec p=screen(inlineAnchor);int x=int(std::clamp(p.x-inlineWidth/2.,double(left+2),double(std::max(left+2,ww-right-inlineWidth-2))));int y=int(std::clamp(p.y-2,double(top+2),double(std::max(top+2,wh-bottom-inlineHeight-2))));
    SetWindowPos(inlineEdit,nullptr,x,y,inlineWidth,inlineHeight,SWP_NOZORDER|SWP_NOACTIVATE);redraw();
}
void previewInline(){
    if(!inlineEdit||inlineClosing)return;int n=GetWindowTextLengthW(inlineEdit);std::wstring w(n+1,0);GetWindowTextW(inlineEdit,w.data(),n+1);w.resize(n);
    try{if(inlineTarget.role==8)numeric(ui::utf8(w));inlineValid=true;status=inlineTarget.role!=8?L"Ctrl+=: 아래첨자 · Ctrl+Shift+=: 위첨자 · 같은 키: 일반 입력 복귀 · Enter: 완료 · Esc: 취소":L"Enter: 완료 · Shift+Enter: 줄바꿈 · Tab/Shift+Tab: 다음/이전 텍스트 · Esc: 취소";}
    catch(const std::exception& e){inlineValid=false;status=ui::wide(e.what());}
    sizeInline();RECT statusRect{0,wh-bottom,ww,wh};InvalidateRect(win,&statusRect,FALSE);
}
bool finishInline(bool commit){
    if(!inlineEdit||inlineClosing)return true;if(commit&&!inlineValid){SetFocus(inlineEdit);return false;}
    if(commit){int n=GetWindowTextLengthW(inlineEdit);std::wstring value(n+1,0);GetWindowTextW(inlineEdit,value.data(),n+1);value.resize(n);setInlineValue(inlineTarget,ui::utf8(value));if(auto* o=textObject(inlineTarget.owner)){o->centerText=true;o->points[0]=inlineAnchor;}}
    inlineClosing=true;HWND edit=inlineEdit;inlineEdit=nullptr;DestroyWindow(edit);if(inlineFont){DeleteObject(inlineFont);inlineFont=nullptr;}
    if(commit)if(auto* r=reaction(inlineTarget.owner))optimizeMechanism(doc,{r->id},r->cyclic);
    if(!commit)doc=inlineBefore;else{
        if(auto* o=textObject(inlineTarget.owner);o&&o->text.empty())doc.erase({o->id});
        if(toNative(doc)!=toNative(inlineBefore)){history.checkpoint(inlineBefore);changed();}
    }
    inlineClosing=false;selection={inlineTarget.owner};status=commit?L"텍스트를 수정했습니다. t 도구로 다시 편집할 수 있습니다.":L"텍스트 편집을 취소했습니다.";redraw();return true;
}
LRESULT CALLBACK inlineProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_IME_STARTCOMPOSITION)inlineComposing=true;
    if(msg==WM_IME_ENDCOMPOSITION)inlineComposing=false;
    if(!inlineComposing&&inlineTarget.role!=8&&ui::scriptShortcut(hwnd,msg,wp))return 0;
    if(msg==WM_KEYDOWN&&!inlineComposing){
        if((GetKeyState(VK_CONTROL)&0x8000)&&(wp==VK_TAB||wp=='W')){if(finishInline(true)){if(wp=='W')closeDocumentTab(activeDocument);else switchDocumentTab((activeDocument+documents.size()+((GetKeyState(VK_SHIFT)&0x8000)?-1:1))%documents.size());SetFocus(win);}return 0;}
        if(wp=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(hwnd,EM_SETSEL,0,-1);return 0;}if(wp==VK_ESCAPE){finishInline(false);SetFocus(win);return 0;}
        if(wp==VK_TAB){std::vector<InlineTarget> targets;for(auto& t:scene().items)if(t.type==Primitive::Text&&!t.points.empty()&&(t.textRole||textObject(t.owner))&&std::none_of(targets.begin(),targets.end(),[&](const InlineTarget& a){return a.owner==t.owner&&a.role==t.textRole&&a.index==t.textIndex;}))targets.push_back({t.owner,t.textRole,t.textIndex,t.points[0],t.fontSize});
            auto current=std::find_if(targets.begin(),targets.end(),[](const InlineTarget& t){return t.owner==inlineTarget.owner&&t.role==inlineTarget.role&&t.index==inlineTarget.index;});
            if(current!=targets.end()&&targets.size()>1){int i=int(current-targets.begin()),n=int(targets.size());auto next=targets[size_t((i+n+((GetKeyState(VK_SHIFT)&0x8000)?-1:1))%n)];if(finishInline(true))beginInline(next,next.p);}else if(finishInline(true))SetFocus(win);return 0;}
        if(wp==VK_RETURN&&(!(GetKeyState(VK_SHIFT)&0x8000)||(GetKeyState(VK_CONTROL)&0x8000))){if(finishInline(true))SetFocus(win);return 0;}if(wp=='S'&&(GetKeyState(VK_CONTROL)&0x8000)){if(finishInline(true))save();return 0;}}
    if(msg==WM_CHAR&&wp==VK_TAB)return 0;
    return CallWindowProcW(oldEditProc,hwnd,msg,wp,lp);
}
void beginInline(std::optional<InlineTarget> target,Vec at){
    if(!finishInline(true))return;if(target&&doc.isLocked(target->owner))return;inlineBefore=doc;
    if(target)inlineTarget=*target;else{Object o;o.id=doc.nextId++;o.kind=ObjectKind::Text;o.fontSize=doc.style.fontSize;o.face=1;o.points={at};doc.objects.push_back(o);inlineTarget={o.id,0,-1,at,o.fontSize};}
    selection={inlineTarget.owner};inlineValid=true;auto value=inlineValue(inlineTarget);
    inlineTypeface="Arial";inlineFace=doc.style.labelFace;inlineColor=RGB(0,0,0);
    inlineWidth=inlineHeight=0;inlineAnchor=inlineTarget.p;double lo=1e20,hi=-1e20,ty=1e20;
    for(auto& t:scene().items)if(t.type==Primitive::Text&&!t.points.empty()&&t.owner==inlineTarget.owner&&t.textRole==inlineTarget.role&&t.textIndex==inlineTarget.index){inlineTarget.size=std::max(inlineTarget.size,t.fontSize);lo=std::min(lo,t.points[0].x);hi=std::max(hi,t.points[0].x+measure(t.text,t.fontSize,t.font,t.face));ty=std::min(ty,t.points[0].y);}
    if(hi>=lo)inlineAnchor={(lo+hi)/2,ty};if(auto* o=textObject(inlineTarget.owner);o&&o->centerText)inlineAnchor=o->points[0];
    for(auto& t:scene().items)if(t.type==Primitive::Text&&t.owner==inlineTarget.owner&&t.textRole==inlineTarget.role&&t.textIndex==inlineTarget.index){inlineTypeface=t.font;inlineFace=t.face;auto c=rgb(t.color);inlineColor=RGB(c.GetR(),c.GetG(),c.GetB());break;}
    inlineComposing=false;inlineClosing=true;inlineEdit=CreateWindowExW(0,L"EDIT",ui::wide(value).c_str(),WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP|ES_CENTER|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|ES_NOHIDESEL,0,0,96,28,win,(HMENU)900,GetModuleHandleW(nullptr),nullptr);inlineClosing=false;
    if(!inlineEdit){doc=inlineBefore;throw std::runtime_error("Cannot create text editor");}
    inlineFont=CreateFontW(-int(std::clamp(inlineTarget.size*zoom,1.,160.)),0,0,0,inlineFace&1?FW_BOLD:FW_NORMAL,inlineFace&2?TRUE:FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,ui::wide(inlineTypeface).c_str());SendMessageW(inlineEdit,WM_SETFONT,(WPARAM)inlineFont,TRUE);SendMessageW(inlineEdit,EM_SETLIMITTEXT,100000,0);
    oldEditProc=(WNDPROC)SetWindowLongPtrW(inlineEdit,GWLP_WNDPROC,(LONG_PTR)inlineProc);SendMessageW(inlineEdit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(2,2));sizeInline();SetFocus(inlineEdit);SendMessageW(inlineEdit,EM_SETSEL,0,-1);status=inlineTarget.role!=8?L"Ctrl+=: 아래첨자 · Ctrl+Shift+=: 위첨자 · 같은 키: 일반 입력 복귀 · Enter: 완료 · Esc: 취소":L"Enter: 완료 · Shift+Enter: 줄바꿈 · Tab/Shift+Tab: 다음/이전 텍스트 · Esc: 취소";redraw();
}
void editReaction(int id){auto* ptr=reaction(id);if(!ptr)return;Document before=doc;Reaction original=*ptr;
    auto update=[&](const ui::Table& rows)->std::wstring{
        Document candidate=before;auto edited=mechanismGridData(original,rows);
        for(auto& item:candidate.reactions)if(item.id==id)item=std::move(edited);
        optimizeMechanism(candidate,{id},original.cyclic);doc=std::move(candidate);redraw();UpdateWindow(win);return {};
    };
    auto result=ui::table(win,L"메커니즘 단계 편집 · 중간체 → TS → 중간체",
        {L"구분 (중간체 / TS)",L"이름 / TS 설명 (예: TS1 / 산화)",L"S (예: 0 / 2)",L"에너지 (숫자 또는 ?.?)"},mechanismRows(original),update,true,L"TS1 / 설명: 이름은 에너지 위, 설명은 화살표 위 · S와 에너지는 같은 순서로 / 구분 · Ctrl+=: 아래첨자 · Ctrl+Shift+=: 위첨자 · 미정 에너지: ? 또는 ?.? · S=0 검정, S=1 빨강, S=2 파랑",true);
    if(result){if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();fit();}}else{doc=before;redraw();}
}
void reactionSettings(int id){
    auto* ptr=reaction(id);if(!ptr)throw std::runtime_error("반응 블록을 선택하세요.");Document before=doc;Reaction old=*ptr;
    auto update=[&](const std::vector<std::string>& v)->std::wstring{try{
        int mode=std::stoi(v[1]);double columns=numeric(v[3],1,40),gap=numeric(v[4],0,4000);
        if(columns!=std::floor(columns))return L"한 줄의 구조 수는 1~40의 정수로 입력하세요.";
        Document candidate=before;for(auto& r:candidate.reactions)if(r.id==id){r.unit=v[0];bool keepManual=mode==2&&!old.cyclic&&!old.vertical&&old.wrapColumns==0&&reactionRow(old,old.slots.empty()?0:old.slots.size()-1)>0&&columns==3;if(!keepManual){for(auto& slot:r.slots)slot.startsRow=false;r.wrapColumns=mode==2?int(columns):0;}r.arrowType=std::stoi(v[2]);r.vertical=mode==3;r.rowGap=gap;r.captionFontSize=numeric(v[5],0,100);if(v[6]=="1")r.hiddenArrows.clear();}
        optimizeMechanism(candidate,{id},mode==1);doc=std::move(candidate);redraw();UpdateWindow(win);return {};
    }catch(const std::exception& e){return ui::wide(e.what());}};
    auto result=ui::form(win,L"메커니즘 설정",{{L"단위 (오른쪽 위)",old.unit,false,{},false,4,true},
        {L"배치",old.cyclic?"1":old.vertical?"3":(old.wrapColumns>0||reactionRow(old,old.slots.empty()?0:old.slots.size()-1)>0)?"2":"0",false,{L"가로 한 줄",L"촉매 순환",L"ㄹ자 (아래로 이어 그리기)",L"세로 한 줄 (위에서 아래)"}},
        {L"화살표",std::to_string(old.arrowType),false,{L"정방향",L"평형"}},
        {L"ㄹ자: 한 줄의 구조 수 (TS 제외)",std::to_string(old.wrapColumns>0?old.wrapColumns:3)},
        {L"ㄹ자: 줄 사이 최소 여백 (pt)",number(old.rowGap)},
        {L"전체 글자 크기 (pt, 0: 자동)",number(old.captionFontSize)},
        {L"삭제한 순환 화살표", "0",false,{L"현재 상태 유지",L"모두 복원"}}},update);
    if(result){update(*result);if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();fit();}}
    else{doc=before;redraw();}
}
void relocatePlot(const EnergyPlot& before,const EnergyPlot& after){
    for(size_t i=0;i<after.states.size();i++)for(size_t j=0;j<before.states.size();j++){
        if(!after.states[i].above.empty()&&after.states[i].above==before.states[j].above)doc.translate(after.states[i].above,plotAnchor(after,i,true)-plotAnchor(before,j,true));
        if(!after.states[i].below.empty()&&after.states[i].below==before.states[j].below)doc.translate(after.states[i].below,plotAnchor(after,i,false)-plotAnchor(before,j,false));
    }
}
void editPlot(int id){auto* ptr=plot(id);if(!ptr)return;Document before=doc;EnergyPlot original=*ptr;if(original.series.empty()){EnergySeries path;path.name="Path 1";for(auto& state:original.states)path.values.push_back(state.energy);original.series.push_back(path);}
    std::vector<std::wstring> columns={L"상태 이름"};for(auto& path:original.series)columns.push_back(ui::wide(path.name));ui::Table rows;
    for(size_t i=0;i<original.states.size();i++){std::vector<std::string> row={original.states[i].label};for(auto& path:original.series)row.push_back(path.hasValue(i)?number(path.values[i]):"");rows.push_back(row);}
    auto update=[&](const ui::Table& data)->std::wstring{
        try{auto p=energyGridData(original,data);doc=before;relocatePlot(original,p);*plot(id)=std::move(p);redraw();UpdateWindow(win);return {};}
        catch(const std::exception& e){return ui::wide(e.what());}
    };
    auto result=ui::table(win,L"에너지 그래프 — 셀 편집",columns,rows,update,true);
    if(result){
        // Apply the returned table explicitly; committing must not depend on a prior preview.
        update(*result);
        if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}
    }else{doc=before;redraw();}

}
void addBilateralBranch(int id){
    auto* p=plot(id);if(!p)throw std::runtime_error("에너지 그래프를 선택하세요.");
    Document before=doc;EnergyPlot original=*p;std::vector<std::wstring> names;
    for(auto& path:original.series)names.push_back(ui::wide(path.name));if(names.empty())names.push_back(L"Path 1");
    auto update=[&](const std::vector<std::string>& v)->std::wstring{try{
        auto candidate=bilateralPlot(original,size_t(std::stoul(v[0])),v[1]);doc=before;
        relocatePlot(original,candidate);*plot(id)=std::move(candidate);redraw();UpdateWindow(win);return {};
    }catch(const std::exception& e){return ui::wide(e.what());}};
    auto result=ui::form(win,L"양쪽 분기 추가 — 미리보기",{
        {L"왼쪽에 복제할 원본 경로","0",false,names},{L"새 왼쪽 경로 이름","Left branch"},
        {L"배치 안내","첫 상태를 중앙에 두고 양쪽으로 분기합니다."},
        {L"에너지 편집","적용 후 데이터 표에서 좌우 값을 따로 수정하세요."}},update);
    if(result){update(*result);if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();fit();status=L"양쪽 분기를 추가했습니다. 데이터 표에서 좌우 경로의 에너지를 수정하세요. Ctrl+Z로 되돌립니다.";}}
    else{doc=before;redraw();}
}
void publicationLayout(int id){
    auto* p=plot(id);if(!p)throw std::runtime_error("에너지 그래프를 선택하세요.");
    Document before=doc;auto original=*p;auto fitted=publicationPlot(original);
    // Keep neighboring inserted structures clear when reducing the graph width.
    double spacing=0;
    for(bool above:{false,true}){
        double previous=0;for(size_t i=0;i<original.states.size();i++){
            auto& members=above?original.states[i].above:original.states[i].below;
            double width=members.empty()?0:makeScene(doc.subset(members),measure).bounds.w;
            if(i&&(previous>0||width>0))spacing=std::max(spacing,(previous+width)/2+18);previous=width;
        }
    }
    fitted.width=std::max(fitted.width,154.+spacing*(std::max(size_t(1),original.states.size())-1));
    fitted.height=std::max(fitted.height,std::min(600.,fitted.width*.50));
    relocatePlot(original,fitted);*plot(id)=std::move(fitted);history.checkpoint(before);
    status=L"출판용 비율과 자동 글자 크기를 적용했습니다. Ctrl+Z로 되돌릴 수 있습니다.";changed();fit();
}
void plotSettings(int id){auto* p=plot(id);if(!p)throw std::runtime_error("에너지 그래프를 선택하세요.");EnergyPlot original=*p;Document before=doc;
    auto update=[&](const std::vector<std::string>& v)->std::wstring{try{EnergyPlot p=original;p.title=v[0];p.unit=v[1];p.xLabel=v[2];p.yLabel=v[3];p.xColor=colorValue(v[4]);p.yColor=colorValue(v[5]);auto size=values(v[6]);if(size.size()!=2||size[0]<180||size[1]<140||size[0]>4000||size[1]>4000)return L"크기는 180×140 ~ 4000×4000 pt로 입력하세요.";p.width=size[0];p.height=size[1];p.mode=std::stoi(v[7]);p.showValues=v[8]=="1";p.yTickStep=v[9].empty()?0:numeric(v[9],0);p.yMin=v[10].empty()?std::optional<double>{}:numeric(v[10]);p.yMax=v[11].empty()?std::optional<double>{}:numeric(v[11]);
 p.axisFontSize=numeric(v[12],0,72);p.tickFontSize=numeric(v[13],0,72);p.valueFontSize=numeric(v[14],0,72);p.energyLabelFontSize=numeric(v[15],0,72);
 for(double font:{p.axisFontSize,p.tickFontSize,p.valueFontSize,p.energyLabelFontSize})if(font>0&&font<4)return L"글자 크기는 0(자동) 또는 4~72 pt로 입력하세요.";
 energyAxis(p);if(p.yTickStep>0&&(p.yTickStep<1e-9||energyAxis(p).step>p.yTickStep*1.001))return L"y축 눈금이 너무 촘촘합니다. 더 큰 간격을 입력하세요.";doc=before;relocatePlot(original,p);*plot(id)=p;redraw();UpdateWindow(win);return {};}catch(const std::exception& e){return ui::wide(e.what());}};
    auto r=ui::form(win,L"그래프·축 설정",{{L"제목",p->title,false,{},false,4,true},{L"에너지 단위",p->unit,false,{},false,4,true},{L"x축 (위·아래첨자 버튼)",p->xLabel,false,{},false,4,true},{L"y축 (위·아래첨자 버튼)",p->yLabel,false,{},false,4,true},{L"x축 글자색",p->xColor,false,{},true},{L"y축 글자색",p->yColor,false,{},true},{L"너비 / 높이 (pt)",number(p->width)+" "+number(p->height)},{L"그래프 형태",std::to_string(p->mode),false,{L"부드러운 곡선",L"직선",L"에너지 준위 (점선 연결)",L"평평한 준위와 직선"}},{L"에너지 값 표시",p->showValues?"1":"0",false,{L"아니요",L"예"}},{L"y축 눈금 간격 (0: 자동)",number(p->yTickStep)},
 {L"y축 최솟값 (빈칸: 자동)",p->yMin?number(*p->yMin):""},{L"y축 최댓값 (빈칸: 자동)",p->yMax?number(*p->yMax):""},
 {L"x축·그래프 제목 (pt, 0: 자동)",number(p->axisFontSize)},{L"눈금·상태 이름 (pt, 0: 자동)",number(p->tickFontSize)},{L"데이터 값 크기 (pt, 0: 자동)",number(p->valueFontSize)},{L"에너지 축 제목 (pt, 0: 자동)",number(p->energyLabelFontSize)}},update);if(r){if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}else{doc=before;redraw();}
}
void editPlotStages(int id){
    auto* p=plot(id);if(!p)throw std::runtime_error("에너지 그래프를 선택하세요.");Document before=doc;
    auto stages=p->stages.empty()?defaultMechanismStages():p->stages;ui::Table rows;
    for(auto& stage:stages)rows.push_back({stage.label,number(stage.start),number(stage.end),number(stage.fontSize)});
    auto update=[&](const ui::Table& cells)->std::wstring{try{
        auto candidate=mechanismStageData(cells);doc=before;plot(id)->stages=std::move(candidate);redraw();UpdateWindow(win);return {};
    }catch(const std::exception& e){return ui::wide(e.what());}};
    auto result=ui::table(win,L"반응 이름·화살표 범위",{L"반응 이름",L"시작 (%)",L"끝 (%)",L"글자 크기 (pt, 0: 자동)"},rows,update,true,
        L"x축 위에 ← 반응 이름 → 표시 · 범위: x축 전체 폭의 0~100% (최소 1%) · 겹치는 범위는 다른 줄에 표시 · 글자: 0(상태 이름과 같은 크기) 또는 4~72 pt · 긴 설명은 줄바꿈 · 모두 삭제하면 숨김");
    if(result){update(*result);if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}
    else{doc=before;redraw();}
}
void snapPlotStages(int id){
    auto* p=plot(id);if(!p||doc.isLocked(id))throw std::runtime_error("편집할 에너지 그래프를 선택하세요.");
    auto sc=scene();if(sc.plotSnapPoints[id].empty())throw std::runtime_error("축 범위 안에 에너지 준위가 없습니다.");
    std::vector<std::wstring> names={L"새 반응 이름 추가"};for(auto& stage:p->stages)names.push_back(ui::wide(stage.label));
    auto choice=ui::form(win,L"화살표 자석 배치",{{L"배치할 반응 이름","0",false,names}});if(!choice)return;
    int index=std::stoi((*choice)[0])-1;
    if(index<0&&p->stages.size()>=100)throw std::runtime_error("반응 이름은 최대 100개입니다.");
    MechanismStage label=index<0?MechanismStage{"Mechanism",0,100}:p->stages[size_t(index)];
    auto text=ui::form(win,L"반응 이름",{{L"반응 이름",label.label,false,{},false,4,true},{L"글자 크기 (pt, 0: 자동)",number(label.fontSize)}});if(!text)return;
    auto checked=mechanismStageData({{(*text)[0],"0","100",(*text)[1]}});if(checked.empty())return;
    snapLabel=checked.front();snapPlot=id;snapStage=index;snapStart.reset();selection={id};tool=Select;
    status=L"자석 배치: 준위의 왼쪽 끝·중앙·오른쪽 끝 중 첫 점을 클릭하세요. Esc: 취소";redraw();
}
void editPlotColors(int id){
    auto* p=plot(id);if(!p||doc.isLocked(id))throw std::runtime_error("편집할 에너지 그래프를 선택하세요.");
    EnergyPlot original=*p;if(original.series.empty()){EnergySeries path;for(auto& state:original.states)path.values.push_back(state.energy);original.series.push_back(path);}
    if(original.states.empty())throw std::runtime_error("먼저 에너지 데이터를 입력하세요.");
    std::vector<std::wstring> paths,states;for(auto& path:original.series)paths.push_back(ui::wide(path.name));
    for(size_t i=0;i<original.states.size();i++)states.push_back(std::to_wstring(i+1)+L" · "+ui::wide(original.states[i].label));
    Document before=doc;
    auto update=[&](const std::vector<std::string>& v)->std::wstring{try{
        size_t k=std::stoul(v[0]),a=std::stoul(v[1]),b=std::stoul(v[2]);if(a>b)std::swap(a,b);
        std::string color=v[5]=="1"?"":colorValue(v[4]);int scope=std::stoi(v[3]);
        auto candidate=original;auto& path=candidate.series.at(k);path.levelColors.resize(candidate.states.size());path.segmentColors.resize(candidate.states.size()-1);
        if(scope!=2)for(size_t i=a;i<=b;i++)path.levelColors[i]=color;
        if(scope!=1)for(size_t i=a;i<b;i++)path.segmentColors[i]=color;
        doc=before;*plot(id)=std::move(candidate);redraw();UpdateWindow(win);return {};
    }catch(const std::exception& e){return ui::wide(e.what());}};
    auto result=ui::form(win,L"구간별 색상 — 미리보기",{
        {L"경로","0",false,paths},{L"시작 준위","0",false,states},{L"끝 준위",std::to_string(original.states.size()-1),false,states},
        {L"적용 대상","0",false,{L"준위·표식과 사이 연결선",L"준위·표식만",L"사이 연결선만"}},
        {L"구간 색상",original.series[0].color,false,{},true},{L"색상 설정","0",false,{L"선택한 색상 적용",L"경로 기본 색상으로 복원"}}},update);
    if(result){auto error=update(*result);if(!error.empty()){doc=before;throw std::runtime_error(ui::utf8(error));}if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}
    else{doc=before;redraw();}
}
void editSeries(int id,bool add){
    auto* p=plot(id);if(!p)return;int index=0;
    if(!add&&p->series.empty())throw std::runtime_error("그래프에 경로가 없습니다.");
    if(!add&&p->series.size()>1){std::vector<std::wstring> names;for(auto& s:p->series)names.push_back(ui::wide(s.name));auto choice=ui::form(win,L"경로 선택",{{L"경로","0",false,names}});if(!choice)return;index=std::stoi((*choice)[0]);}
    Document before=doc;EnergySeries original=add?EnergySeries{}:p->series.at(index);
    if(add){original.name="Path "+std::to_string(p->series.size()+1);original.color="#b44758";for(auto& state:p->states)original.values.push_back(state.energy);if(!p->series.empty())original.present=p->series[0].present;}
    auto update=[&](const std::vector<std::string>& v)->std::wstring{
        try{
            EnergySeries s=original;s.name=v[0];s.values=original.values;
            s.color=colorValue(v[2]);s.dash=std::stoi(v[3]);s.width=numeric(v[4],.1,10);s.marker=std::stoi(v[5]);s.markerSize=numeric(v[6],.5,12);
            doc=before;auto* current=plot(id);if(add)current->series.push_back(s);else current->series[size_t(index)]=s;
            if(!add&&index==0)for(size_t i=0;i<current->states.size();i++)current->states[i].energy=s.values[i];redraw();UpdateWindow(win);return {};
        }catch(const std::exception& e){return ui::wide(e.what());}
    };
    auto result=ui::form(win,L"경로 스타일 — 미리보기",{
        {L"경로 이름",original.name,false,{},false,4,true},{L"에너지 편집 안내","에너지 값은 [표 편집]에서 수정하세요."},
        {L"선 색상",original.color,false,{},true},
        {L"선 종류",std::to_string(original.dash),false,{L"실선",L"파선",L"점선"}},
        {L"선 굵기 (pt)",number(original.width)},
        {L"기본 표식",std::to_string(original.marker),false,{L"없음",L"원",L"사각",L"마름모",L"삼각"}},
        {L"표식 반지름 (pt)",number(original.markerSize)}
    },update);
    if(result){if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}
    else{doc=before;redraw();}
}
// Keep selection feedback consistent with the model's group-aware move/copy/delete.
std::vector<int> groupedSelection(std::vector<int> ids){
    bool more=true;while(more){size_t before=ids.size();
        for(const auto& group:doc.groups)if(contains(ids,group.id)||std::any_of(group.members.begin(),group.members.end(),[&](int id){return contains(ids,id);}))
            for(int id:group.members)if(!doc.isLocked(id)&&!contains(ids,id))ids.push_back(id);
        more=ids.size()!=before;
    }
    auto valid=doc.editableIds();ids.erase(std::remove_if(ids.begin(),ids.end(),[&](int id){return !contains(valid,id);}),ids.end());
    return ids;
}
bool groupedObject(int id){for(const auto& group:doc.groups)if(contains(group.members,id))return true;return false;}
void groupSelected(bool ungroup){
    pruneLockedSelection();selection=groupedSelection(selection);
    if(ungroup){
        auto candidate=doc;candidate.groups.erase(std::remove_if(candidate.groups.begin(),candidate.groups.end(),[&](const Group& group){
            return std::any_of(group.members.begin(),group.members.end(),[&](int id){return contains(selection,id);});}),candidate.groups.end());
        if(candidate.groups.size()==doc.groups.size()){status=L"해제할 그룹을 선택하세요.";redraw();return;}
        history.checkpoint(doc);doc=std::move(candidate);status=L"그룹을 해제했습니다. Ctrl+Z: 실행 취소";changed();return;
    }
    // A selected bond needs its endpoints to move and survive copy/paste as a unit.
    for(const auto& bond:doc.bonds)if(contains(selection,bond.id)){for(int id:{bond.a,bond.b})if(!contains(selection,id))selection.push_back(id);}
    selection=doc.subset(selection).editableIds();
    if(selection.size()<2){status=L"그룹으로 묶을 객체를 두 개 이상 선택하세요. Shift+클릭 또는 드래그로 선택합니다.";redraw();return;}
    for(const auto& group:doc.groups){auto members=group.members,chosen=selection;std::sort(members.begin(),members.end());std::sort(chosen.begin(),chosen.end());
        if(members==chosen){status=L"이미 같은 그룹으로 묶여 있습니다.";redraw();return;}}
    history.checkpoint(doc);doc.groups.push_back({doc.nextId++,selection});tool=Select;targetContainer=0;
    status=L"그룹을 만들었습니다. 클릭하여 함께 이동 · Ctrl+Shift+G: 그룹 해제";changed();
}
std::vector<int> orbitalMembers(int id){
    auto tagged=[&](int member){for(auto& o:doc.objects)if(o.id==member)return o.text.rfind("YoungDraw.Orbital:",0)==0;return false;};
    if(!tagged(id))return {};
    for(auto& group:doc.groups)if(contains(group.members,id)&&std::all_of(group.members.begin(),group.members.end(),tagged))return group.members;
    return {};
}
bool editOrbital(int id){
    auto members=orbitalMembers(id);if(members.empty())return false;
    auto first=std::find_if(doc.objects.begin(),doc.objects.end(),[&](const Object& o){return o.id==members.front();});
    if(first==doc.objects.end()||first->points.size()<2)return false;
    int shape=0,phase=0;char separator=0;std::istringstream metadata(first->text.substr(18));metadata>>shape>>separator>>phase;
    Vec at=first->points[0],v;
    if(first->kind==ObjectKind::Ellipse){at=(first->points[0]+first->points[1])*.5;v={(first->points[1].x-first->points[0].x)/2,0};}
    else if(first->points.size()>=4)v=first->points[3]-at;else return false;
    auto values=ui::form(win,L"오비탈 편집",{{L"모양",std::to_string(shape),false,{L"s (원형)",L"p (두 로브)",L"d (네 로브)",L"혼성 (큰 로브 / 작은 로브)"}},
        {L"위상 표시",std::to_string(phase),false,{L"흑백 (채움 / 윤곽)",L"윤곽선",L"색상 (청색 / 적색)"}},
        {L"중심에서 로브 끝까지 (pt)",number(v.length())},{L"방향 (도, +는 시계방향)",number(std::atan2(v.y,v.x)*180/pi)}});
    if(!values)return true;
    double length=numeric((*values)[2],1,2000),angle=numeric((*values)[3],-36000,36000)*pi/180;
    Document candidate=doc;candidate.erase(members);auto ids=addOrbital(candidate,at,at+Vec{std::cos(angle),std::sin(angle)}*length,OrbitalKind(std::stoi((*values)[0])),std::stoi((*values)[1]));
    history.checkpoint(doc);doc=std::move(candidate);selection=std::move(ids);changed();return true;
}

void editObject(int id){if(editOrbital(id))return;if(doc.isLocked(id)){status=L"레이어의 잠금을 해제한 뒤 편집하세요.";redraw();return;}if(auto* a=doc.atom(id)){auto r=ui::form(win,L"원자·라벨",{{L"원소 기호",symbol(a->element)},{L"라벨 (빈칸: 자동)",a->label,false,{},false,4,true},{L"형식 전하",std::to_string(a->charge)},{L"동위원소 (0: 자연 조성)",std::to_string(a->isotope)},{L"현재 구조 형태 보호",a->protect?"1":"0",false,{L"아니요",L"예"}}});if(!r)return;int e=atomicNumber((*r)[0]);if(!e)throw std::runtime_error("알 수 없는 원소 기호입니다.");int q=int(numeric((*r)[2],-8,8)),isotope=int(numeric((*r)[3],0,300));history.checkpoint(doc);a=doc.atom(id);setAtomElement(doc,id,e);a->label=chemicalScripts((*r)[1]);a->labelRuns.clear();a->charge=q;a->isotope=isotope;a->explicitH=-1;a->protect=(*r)[4]=="1";changed();return;}
 for(auto& o:doc.objects)if(o.id==id){std::string transformValue=o.kind==ObjectKind::Image?"1":"12";if(o.kind==ObjectKind::Curve&&o.points.size()==4){if(o.autoRoute)transformValue="자동";else{Vec delta=o.points.back()-o.points.front();double len=delta.length();if(len>.001){Vec n{-delta.y/len,delta.x/len};Vec bend=o.points[1]-o.points.front()-delta*(1./3);transformValue=number(bend.x*n.x+bend.y*n.y);}}}auto r=ui::form(win,L"그리기 속성",{{L"텍스트 (Ctrl+= / Ctrl+Shift+=)",o.text,true,{},false,4,true},{L"색상 (#RRGGBB)",o.color},{L"선 굵기 (pt)",number(o.width)},{L"선 종류",std::to_string(o.dash),false,{L"실선",L"파선",L"점선"}},{L"글자 크기 (pt)",number(o.fontSize)},{L"화살촉",std::to_string(o.head),false,{L"없음",L"전체 화살촉",L"반쪽 화살촉",L"공명 (양쪽 화살촉)"}},{L"곡률 (자동 또는 숫자) / 이미지 배율",transformValue}});if(!r)return;Object update=o;update.text=chemicalScripts((*r)[0]);update.color=colorValue((*r)[1]);update.width=numeric((*r)[2],.1,10);update.dash=std::stoi((*r)[3]);update.fontSize=numeric((*r)[4],4,100);update.head=std::stoi((*r)[5]);update.runs.clear();if(update.kind==ObjectKind::Curve&&(*r)[6]=="자동")update.autoRoute=true;else if(update.kind==ObjectKind::Curve&&update.points.size()==4&&(*r)[6]!=transformValue){update.points=routeArrow(doc,update);update.sourceAtom=update.targetAtom=update.sourceBond=update.targetBond=0;update.autoRoute=false;double bend=numeric((*r)[6],-200,200);Vec a=update.points.front(),b=update.points.back(),v=b-a;double len=v.length();if(len>.001){Vec n{-v.y/len,v.x/len};update.points[1]=a+v*(1./3)+n*bend;update.points[2]=a+v*(2./3)+n*bend;}}if(update.kind==ObjectKind::Image){double scale=numeric((*r)[6],.05,20);update.points[1]=update.points[0]+(update.points[1]-update.points[0])*scale;}history.checkpoint(doc);o=update;changed();return;}}
void edit(){int id=primary();if(auto* o=textObject(id)){beginInline(InlineTarget{id,0,-1,o->points[0],o->fontSize},o->points[0]);return;}if(reaction(id))editReaction(id);else if(plot(id))editPlot(id);else editObject(id);}
void addImage(Vec p){auto path=fileDialog(false,3);if(!path)return;insert(imageDocument(readFile(*path)),p);tool=Select;}


void library(const std::wstring& initialCategory=L"리간드"){
    std::wstring category=initialCategory;
    for(;;){
        size_t unreadable=0;
        auto& cards=templateCards(unreadable);
        auto result=ui::templateGallery(win,cards,category,[](Graphics& g,const Scene& scene,RectF box){
            auto b=scene.bounds;double scale=std::min(double(box.Width)/std::max(1.,b.w),double(box.Height)/std::max(1.,b.h));auto saved=g.Save();g.SetClip(box,CombineModeIntersect);
            g.TranslateTransform(float(box.X+box.Width/2-(b.x+b.w/2)*scale),float(box.Y+box.Height/2-(b.y+b.h/2)*scale));g.ScaleTransform(float(scale),float(scale));render(g,scene,false,true);g.Restore(saved);
        });
        if(!result){if(unreadable)status=L"읽을 수 없는 사용자 템플릿 "+std::to_wstring(unreadable)+L"개를 제외했습니다.";return;}
        auto card=cards.at(result->index);
        if(result->action==ui::TemplateAction::Favorite){
            auto favorites=loadFavorites();auto key=templateKey(card);
            if(!favorites.erase(key))favorites.insert(key);
            saveFavorites(favorites);category=L"전체";continue;
        }
        if(result->action==ui::TemplateAction::Edit){
            auto fragment=card.document;
            if(result->metal){const int elements[]={26,26,25,27,28,29,30};for(auto& a:fragment.atoms)if(a.label=="M"){a.element=elements[result->metal];a.label.clear();}}
            newDocumentTab(std::move(fragment),card.path);
            status=card.path.empty()?L"기본 템플릿의 복사본입니다. 파일 > 템플릿으로 저장으로 보관하세요.":L"템플릿 편집 탭입니다. Ctrl+S로 원본 템플릿을 덮어씁니다.";
            redraw();return;
        }
        if(result->action==ui::TemplateAction::Rename||result->action==ui::TemplateAction::Move){
            if(card.path.empty())continue;
            auto isMove=result->action==ui::TemplateAction::Move;
            auto answer=ui::form(win,isMove?L"템플릿 분류 이동":L"템플릿 이름 변경",
                {{L"템플릿 이름",ui::utf8(card.name)},{L"분류(폴더)",ui::utf8(card.group)}});
            if(!answer)continue;
            auto oldPath=fs::path(card.path),nextPath=templateLocation(ui::wide((*answer)[0]),ui::wide((*answer)[1]),oldPath.extension().wstring());
            if(nextPath==oldPath)continue;
            if(fs::exists(nextPath))throw std::runtime_error("같은 이름의 템플릿이 이미 있습니다.");
            fs::create_directories(nextPath.parent_path());
            fs::rename(oldPath,nextPath);
            auto favorites=loadFavorites();
            if(favorites.erase(templateKey(card))){card.path=nextPath.wstring();favorites.insert(templateKey(card));saveFavorites(favorites);}
            retargetTemplateTabs(oldPath,nextPath);
            category=L"사용자";continue;
        }
        if(result->action==ui::TemplateAction::Remove){
            if(card.path.empty())continue;
            if(MessageBoxW(win,(L"다음 사용자 템플릿을 삭제하시겠습니까?\n"+card.name).c_str(),L"템플릿 삭제",MB_YESNO|MB_ICONWARNING)!=IDYES)continue;
            auto path=fs::path(card.path);fs::remove(path);
            auto favorites=loadFavorites();if(favorites.erase(templateKey(card)))saveFavorites(favorites);
            retargetTemplateTabs(path,std::nullopt);
            category=L"사용자";continue;
        }
        auto fragment=card.document;
        if(result->metal){const int elements[]={26,26,25,27,28,29,30};for(auto& a:fragment.atoms)if(a.label=="M"){a.element=elements[result->metal];a.label.clear();}}
        Vec at=center();if(!doc.atoms.empty()||!doc.objects.empty()||!doc.reactions.empty()||!doc.plots.empty()){
            auto old=scene().bounds,incoming=makeScene(fragment).bounds;at={old.x+old.w+incoming.w/2+30,old.y+old.h/2};
        }
        insert(fragment,at);tool=Select;status=L"미리보기에서 선택한 템플릿을 삽입했습니다.";fit();return;
    }
}

void transformSelected(int operation){
    if(selection.empty())throw std::runtime_error("회전·반전할 구조의 원자나 결합을 먼저 선택하세요.");Document before=doc;auto originalSelection=selection;
    if(operation==164){
        auto result=ui::form(win,L"선택 구조 회전",{{L"회전 각도 (도, +는 시계방향)","0"}},[&](const std::vector<std::string>& values)->std::wstring{
            try{Document preview=before;auto ids=transformStructures(preview,originalSelection,numeric(values[0],-36000,36000));doc=std::move(preview);selection=std::move(ids);redraw();UpdateWindow(win);return {};}catch(const std::exception& e){return ui::wide(e.what());}
        });
        if(!result){doc=before;selection=originalSelection;redraw();return;}if(toNative(doc)==toNative(before))return;history.checkpoint(before);
    }else{Document result=before;selection=transformStructures(result,originalSelection,operation==162?-90:operation==163?90:0,operation==160,operation==161);history.checkpoint(before);doc=std::move(result);}
    tool=Select;changed();fit();status=L"선택 구조를 회전·반전했습니다. Ctrl+Z로 되돌릴 수 있습니다.";
}

bool activeStructureTarget(){
    if(!targetContainer||selection.size()!=1||selection.front()!=targetContainer)return false;
    if(auto* r=reaction(targetContainer))return targetSlot<r->slots.size();
    if(auto* p=plot(targetContainer))return targetSlot<p->states.size();
    return false;
}
void insertStructureTarget(const Document& fragment){
    // Build before committing so invalid targets cannot partially change the document.
    Document candidate=doc;fillSlot(candidate,fragment,targetContainer,targetSlot,targetAbove);
    history.checkpoint(doc);doc=std::move(candidate);changed();status=L"선택한 영역에 구조를 삽입했습니다. Ctrl+Z로 되돌릴 수 있습니다.";
}
void pasteStructure(const Document& fragment){
    if(activeStructureTarget())insertStructureTarget(fragment);
    else insert(fragment,canvas(mouse)?world(mouse):center());
}
std::vector<int> resizeStructure(Document& candidate,const std::vector<int>& ids,double factor,int container=0,size_t slot=0,bool above=false){
    std::optional<Vec> anchor;
    for(auto& r:candidate.reactions)if(r.id==container)anchor=reactionSlot(r,slot);
    for(auto& p:candidate.plots)if(p.id==container)anchor=plotAnchor(p,slot,above);
    auto affected=scaleStructures(candidate,ids,factor,anchor);if(factor==1)return affected;auto bounds=makeScene(candidate.subset(affected)).bounds;
    for(auto& r:candidate.reactions)if(r.id==container){auto before=r;r.slotWidth=std::max(r.slotWidth,bounds.w+6);r.slotHeight=std::max(r.slotHeight,bounds.h+6);r.spacing=std::max(r.spacing,r.slotWidth+90);
        for(size_t i=0;i<r.slots.size();i++)candidate.translate(r.slots[i].members,reactionSlot(r,i)-reactionSlot(before,i));}
    for(auto& p:candidate.plots)if(p.id==container){auto before=p;p.width=std::max(p.width,(bounds.w+14)*std::max(size_t(1),p.states.size()-1)+114);
        for(size_t i=0;i<p.states.size();i++){candidate.translate(p.states[i].above,plotAnchor(p,i,true)-plotAnchor(before,i,true));candidate.translate(p.states[i].below,plotAnchor(p,i,false)-plotAnchor(before,i,false));}}
    return affected;
}
void resizeSelected(){
    Document before=doc;auto originalSelection=selection;bool inSlot=activeStructureTarget();auto ids=selection;
    int container=inSlot?targetContainer:0;size_t slot=targetSlot;bool above=targetAbove;
    if(inSlot){if(auto* r=reaction(container))ids=r->slots[slot].members;else if(auto* p=plot(container))ids=above?p->states[slot].above:p->states[slot].below;}
    if(ids.empty())throw std::runtime_error("크기를 조정할 구조·이미지를 선택하세요. 빈 삽입 영역에는 먼저 구조를 넣으세요.");
    auto update=[&](const std::vector<std::string>& values)->std::wstring{
        Document candidate=before;auto affected=resizeStructure(candidate,ids,numeric(values[0],1,1000)/100.,container,slot,above);
        doc=std::move(candidate);selection=inSlot?originalSelection:affected;redraw();UpdateWindow(win);return {};
    };
    auto result=ui::form(win,L"구조 크기 조정 · Ctrl+K",{{L"선택한 부분의 배율 (%, 100: 원래 크기)","100"}},update);
    if(!result){doc=before;selection=originalSelection;redraw();return;}
    update(*result);if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();status=L"선택한 부분의 크기를 조정했습니다. Ctrl+Z로 되돌릴 수 있습니다.";}
}
void pasteClipboard(){
    if(auto fragment=clipboardDocument())pasteStructure(*fragment);
    else {status=L"클립보드에 구조 또는 GIF 원본이 없습니다. GIF 파일을 복사하거나 이미지 삽입을 사용하세요.";redraw();}
}
void fillTarget(bool file){
    if(!targetContainer)throw std::runtime_error("먼저 구조 삽입 영역을 클릭하세요.");
    std::optional<Document> fragment;
    if(file){auto path=fileDialog(false,4);if(!path)return;auto bytes=readFile(*path);fragment=imageMime(bytes).empty()?fromCdxml(bytes):imageDocument(bytes);}else fragment=clipboardDocument();
    if(!fragment)throw std::runtime_error("구조 또는 GIF 파일을 복사한 뒤 붙여넣거나 구조·이미지 파일을 선택하세요.");
    insertStructureTarget(*fragment);
}
void atomColorPalette(){
    auto chosen=selectedDocument();if(chosen.atoms.empty()){status=L"색을 바꿀 원자를 먼저 선택하세요.";redraw();return;}
    auto initial=rgb(chosen.atoms.front().color);static COLORREF custom[16]={};CHOOSECOLORW picker{};
    picker.lStructSize=sizeof(picker);picker.hwndOwner=win;picker.lpCustColors=custom;picker.Flags=CC_FULLOPEN|CC_RGBINIT;picker.rgbResult=RGB(initial.GetR(),initial.GetG(),initial.GetB());
    if(!ChooseColorW(&picker))return;
    char value[8];sprintf_s(value,"#%02x%02x%02x",GetRValue(picker.rgbResult),GetGValue(picker.rgbResult),GetBValue(picker.rgbResult));
    bool different=false;for(auto& atom:chosen.atoms){different=different||atom.color!=value;for(auto& run:atom.labelRuns)different=different||run.color!=value;}if(!different)return;
    history.checkpoint(doc);
    for(auto& atom:chosen.atoms)if(auto* target=doc.atom(atom.id)){target->color=value;for(auto& run:target->labelRuns)run.color=value;}
    status=L"선택한 원자 "+std::to_wstring(chosen.atoms.size())+L"개의 색상을 변경했습니다. · Ctrl+Z: 되돌리기";changed();
}
bool applyTextColor(const std::string& value,const std::optional<InlineTarget>& target={}){
    colorValue(value);Document before=doc;bool found=false;
    if(target&&contains(selection,target->owner)&&!doc.isLocked(target->owner)){
        if(auto* r=reaction(target->owner)){
            auto it=std::find_if(r->captionColors.begin(),r->captionColors.end(),[&](const CaptionColor& c){return c.role==target->role&&c.index==target->index;});
            if(it==r->captionColors.end())r->captionColors.push_back({target->role,target->index,value});else it->color=value;found=true;
        }
    }
    if(!found)for(int id:selection)if(!doc.isLocked(id)){
        if(auto* r=reaction(id)){r->textColor=value;r->captionColors.clear();found=true;}
        if(auto* o=textObject(id)){o->color=value;for(auto& run:o->runs)run.color=value;found=true;}
    }
    if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}
    return found;
}
void textColorPalette(const std::optional<InlineTarget>& target){
    bool eligible=false;for(int id:selection)if(!doc.isLocked(id)&&(reaction(id)||textObject(id)))eligible=true;
    if(!eligible){status=L"색을 바꿀 단계 텍스트를 편집하거나 반응 블록·텍스트를 먼저 선택하세요.";redraw();return;}
    std::string current="#000000";for(auto& item:scene().items)if(item.type==Primitive::Text&&contains(selection,item.owner)){
        if(target&&(item.owner!=target->owner||item.textRole!=target->role||item.textIndex!=target->index))continue;
        current=item.color;break;
    }
    auto initial=rgb(current);static COLORREF custom[16]={};CHOOSECOLORW picker{};
    picker.lStructSize=sizeof(picker);picker.hwndOwner=win;picker.lpCustColors=custom;picker.Flags=CC_FULLOPEN|CC_RGBINIT;picker.rgbResult=RGB(initial.GetR(),initial.GetG(),initial.GetB());
    if(!ChooseColorW(&picker))return;
    char value[8];sprintf_s(value,"#%02x%02x%02x",GetRValue(picker.rgbResult),GetGValue(picker.rgbResult),GetBValue(picker.rgbResult));
    applyTextColor(value,target);status=L"텍스트 색상을 변경했습니다. · Ctrl+Z: 되돌리기";redraw();
}
void command(int id){std::optional<InlineTarget> colorTarget;if(id==156&&inlineEdit)colorTarget=inlineTarget;if(!finishInline(true))return;snapPlot=0;snapStart.reset();
 if(id==184){appearance::picker(win,[](){refreshAppearance();});status=std::wstring(L"작업실 · ")+appearance::palette().name+L" / 테마는 출력에 포함되지 않습니다.";redraw();return;}
 if(id==156){pruneLockedSelection();textColorPalette(colorTarget);return;}
 if(id==155){pruneLockedSelection();atomColorPalette();return;}
 if(id==198||id==199){groupSelected(id==199);return;}
 if(id>=190&&id<=197){alignSelection(id);return;}
 if(id==110||id==125||id==126||(id>=160&&id<=165))repeatAction=[id](){command(id);};
 if(id==182||id==183){sidebarPage=id-182;showLayers=false;redraw();return;}if(id==219){auto r=ui::form(win,L"오비탈 설정",{{L"모양",std::to_string(int(orbitalKind)),false,{L"s (원형)",L"p (두 로브)",L"d (네 로브)",L"혼성 (큰 로브 / 작은 로브)"}},{L"위상 표시",std::to_string(orbitalPhase),false,{L"흑백 (채움 / 윤곽)",L"윤곽선",L"색상 (청색 / 적색)"}}});if(r){orbitalKind=OrbitalKind(std::stoi((*r)[0]));orbitalPhase=std::stoi((*r)[1]);tool=Orbital;selection.clear();status=L"오비탈: 클릭하거나 중심에서 바깥으로 드래그하세요.";}redraw();return;}if(id==175||id==176){showLayers=id==176;redraw();return;}
 if(id>=177&&id<=181){
    bool images=doc.imagesLocked,graphs=doc.graphsLocked;
    if(id==177)images=!images;if(id==178)graphs=!graphs;
    if(id==179){images=false;graphs=true;}if(id==180){images=true;graphs=false;}if(id==181)images=graphs=false;
    if(images!=doc.imagesLocked||graphs!=doc.graphsLocked){history.checkpoint(doc);doc.imagesLocked=images;doc.graphsLocked=graphs;pruneLockedSelection();status=L"레이어 잠금 적용 · 잠긴 항목은 보이지만 선택·이동·삭제되지 않습니다.";changed();}redraw();return;
 }
 pruneLockedSelection();if(id>=200&&id<=218){if(id==217){auto before=doc;if(delocalizeBonds(doc,selection)){history.checkpoint(before);changed();}tool=Delocalized;}else{tool=Tool(id-200);selection.clear();}redraw();return;}if(id>=301&&id<=418){element=id-300;int aid=hoverAtom?hoverAtom:(doc.atom(primary())?primary():0);if(aid){history.checkpoint(doc);auto a=doc.atom(aid);setAtomElement(doc,aid,element);a->label.clear();a->labelRuns.clear();a->explicitH=-1;changed();}redraw();return;}
 switch(id){case 165:resizeSelected();break;case 160:case 161:case 162:case 163:case 164:transformSelected(id);break;case 170:library(L"전체");break;case 171:library(L"리간드");break;case 172:library(L"착물");break;case 173:library(L"기질");break;case 174:library(L"산·시약");break;case 100:newDocumentTab();break;case 109:closeDocumentTab(activeDocument);break;
 case 101:{for(const auto& path:openDocumentFiles()){
    bool found=false;for(size_t i=0;i<documents.size();i++){auto currentPath=i==activeDocument?filePath:documents[i].path;if(!currentPath.empty()&&_wcsicmp(fs::absolute(currentPath).lexically_normal().c_str(),fs::absolute(path).lexically_normal().c_str())==0){switchDocumentTab(i);found=true;break;}}
    if(!found){auto imported=fromCdxml(readFile(path));newDocumentTab(std::move(imported),path);status=L"문서를 새 탭에서 열었습니다.";
        if(!doc.importWarnings.empty()){std::wstring warnings=L"지원하지 않는 XML 객체는 CDXML 저장 시 보존됩니다. 미리보기와 SVG에는 일부 객체가 생략될 수 있습니다.\n\n";for(size_t i=0;i<std::min(size_t(12),doc.importWarnings.size());i++)warnings+=ui::wide(doc.importWarnings[i])+L"\n";MessageBoxW(win,warnings.c_str(),L"CDXML 호환성 보고서",MB_OK|MB_ICONINFORMATION);}}
    }break;}
 case 102:save();break;case 103:{auto p=fileDialog(true,1);if(p){writeFile(*p,toSvg(scene()));status=L"SVG를 내보냈습니다. 가져온 미지원 객체는 결과를 확인하세요.";}break;}case 108:{auto p=fileDialog(true,2);if(p){writeFile(*p,toCdxml(doc));status=L"CDXML을 내보냈습니다.";}break;}
 case 153:case 154:{auto chosen=selectedDocument();if(chosen.atoms.empty()&&chosen.bonds.empty())throw std::runtime_error("원자나 결합을 먼저 선택하세요.");history.checkpoint(doc);std::string color=id==153?"#a2a9b2":"#000000";for(auto& a:chosen.atoms)if(auto* target=doc.atom(a.id)){target->color=color;target->protect=true;for(auto& run:target->labelRuns)run.color=color;}for(auto& b:chosen.bonds)if(auto* target=doc.bond(b.id))target->color=color;changed();break;}
 case 104:if(history.undo(doc)){selection.clear();targetContainer=0;changed();}break;case 105:if(history.redo(doc)){selection.clear();targetContainer=0;changed();}break;case 107:fit();break;case 110:clean();break;case 111:library();break;
 case 112:saveCurrentTemplate();break;
 case 113:{auto p=fileDialog(false);if(p)insert(fromCdxml(readFile(*p)),center());break;}
 case 120:{sidebarPage=0;history.checkpoint(doc);Reaction r;r.id=doc.nextId++;r.p=center()-Vec{120,0};r.slots={{"Reactant",{}},{"Intermediate",{}},{"Product",{}}};r.above={"Step 1","Step 2"};r.below={"",""};doc.reactions.push_back(r);selection={r.id};tool=Select;changed();editReaction(r.id);break;}
 case 121:edit();break;case 122:case 128:{auto* r=reaction(primary());if(!r)throw std::runtime_error("반응 메커니즘을 선택하세요.");if(r->slots.size()>=30)throw std::runtime_error("메커니즘은 최대 30칸까지 추가할 수 있습니다.");history.checkpoint(doc);appendMechanismStage(*r,id==128);optimizeMechanism(doc,{r->id},r->cyclic);changed();fit();break;}case 123:fillTarget(false);break;case 124:fillTarget(true);break;
 case 130:{sidebarPage=1;history.checkpoint(doc);EnergyPlot p;p.id=doc.nextId++;p.p=center()-Vec{160,75};p.stages=defaultMechanismStages();p.states={{"R",0,{},{}},{"TS",20,{},{}},{"P",-10,{},{}}};p.series={{"Path 1","#000000",{0,20,-10},1,3.5}};doc.plots.push_back(p);selection={p.id};tool=Select;changed();editPlot(p.id);break;}
 case 131:if(plot(primary()))editPlot(primary());else throw std::runtime_error("에너지 그래프를 선택하세요.");break;case 132:if(plot(primary()))editSeries(primary(),true);else throw std::runtime_error("에너지 그래프를 선택하세요.");break;case 133:if(plot(primary()))editSeries(primary(),false);else throw std::runtime_error("에너지 그래프를 선택하세요.");break;
 case 125:case 126:{Document before=doc;try{optimizeMechanism(doc,selection,id==126);}catch(...){doc=before;throw;}history.checkpoint(before);selection.clear();changed();fit();break;}case 127:reactionSettings(primary());break;case 135:addBilateralBranch(primary());break;case 137:plotSettings(primary());break;case 138:publicationLayout(primary());break;case 139:editPlotStages(primary());break;case 136:snapPlotStages(primary());break;case 142:editPlotColors(primary());break;case 150:history.checkpoint(doc);applyHouseStyle(doc);status=L"Arial 굵게, 14 pt 라벨, 2 pt 결합선을 적용했습니다.";changed();fit();break;case 134:guides=!guides;break;case 140:case 141:sidebarPage=id==141?1:0;history.checkpoint(doc);doc=id==140?mechanismDemo():coordinateDemo();selection.clear();targetContainer=0;filePath.clear();tool=Select;status=L"배치 예제입니다. Ctrl+Z로 이전 문서를 복원합니다.";changed();fit();break;
 }redraw();}
double bondFraction(int id,Vec p){auto* b=doc.bond(id);if(!b)return .5;Vec a=doc.atom(b->a)->p,v=doc.atom(b->b)->p-a;double ll=v.x*v.x+v.y*v.y;return ll>1e-9?std::clamp(((p-a).x*v.x+(p-a).y*v.y)/ll,0.,1.):.5;}
Document draftOrbital(Vec pointer){Document preview;preview.style=doc.style;Vec at=startAtom&&doc.atom(startAtom)?doc.atom(startAtom)->p:world(down),tip=world(pointer);if((pointer-down).length()<5||(tip-at).length()<.01)tip=at+Vec{0,-doc.style.bondLength};else if(GetKeyState(VK_SHIFT)&0x8000)tip=snapped(at,tip,(tip-at).length());addOrbital(preview,at,tip,orbitalKind,orbitalPhase);return preview;}
Object draftArrow(Vec pointer){Object o;o.id=-1;Vec a=world(down),b=world(pointer);o.width=doc.style.lineWidth;o.color="#ff0000";
    if(tool==Straight||tool==Resonance){o.head=tool==Resonance?3:0;if(!o.head)o.color="#000000";if(GetKeyState(VK_SHIFT)&0x8000){Vec v=b-a;b=std::abs(v.x)>std::abs(v.y)?Vec{b.x,a.y}:Vec{a.x,b.y};}o.points={a,b};return o;}
    o.head=tool==Fishhook?2:1;o.sourceAtom=startAtom;o.targetAtom=doc.atomAt(b,10/zoom);
    o.sourceBond=o.sourceAtom?0:startBond;o.targetBond=o.targetAtom?0:doc.bondAt(b,5/zoom);
    o.sourceBondT=bondFraction(o.sourceBond,a);o.targetBondT=bondFraction(o.targetBond,b);
    if(o.targetAtom==o.sourceAtom)o.targetAtom=0;
    if(auto* atom=doc.atom(o.sourceAtom))a=atom->p;
    if(auto* atom=doc.atom(o.targetAtom))b=atom->p;else if(!o.targetBond&&(GetKeyState(VK_SHIFT)&0x8000)){Vec v=b-a;b=std::abs(v.x)>std::abs(v.y)?Vec{b.x,a.y}:Vec{a.x,b.y};}
    bool electron=tool==Curve||tool==Fishhook||((o.sourceAtom||o.sourceBond)&&(o.targetAtom||o.targetBond));o.kind=electron?ObjectKind::Curve:ObjectKind::Line;o.autoRoute=electron;
    if(electron){Vec v=b-a;double len=v.length();Vec n=len>.001?Vec{-v.y/len,v.x/len}:Vec{0,1};double bend=std::clamp(len*.3,6.,24.);o.points={a,a+v*.28+n*bend,a+v*.72+n*bend,b};}
    else{o.sourceAtom=o.targetAtom=o.sourceBond=o.targetBond=0;o.points={a,b};}return o;
}

void presentBuffer(Graphics& out,Bitmap& buffer){
    // The point-only DrawImage overload scales by the bitmap/device DPI ratio.
    // UI geometry and native child controls both use client pixels, so copy 1:1.
    out.SetPageUnit(UnitPixel);
    out.DrawImage(&buffer,Gdiplus::Rect(0,0,INT(buffer.GetWidth()),INT(buffer.GetHeight())),0,0,
                  INT(buffer.GetWidth()),INT(buffer.GetHeight()),UnitPixel);
}
Vec mechanismAddPosition(const Reaction& r,bool below=false);
void paint(HDC dc,bool reuseScene=false){const auto& palette=appearance::palette();ink=palette.text;muted=palette.muted;accent=palette.accent;border=palette.edge;
 const auto width=std::max(1,ww),height=std::max(1,wh);
 if(!paintBuffer||paintBuffer->GetWidth()!=UINT(width)||paintBuffer->GetHeight()!=UINT(height))
     paintBuffer=std::make_unique<Bitmap>(width,height,PixelFormat32bppARGB);
 Graphics g(paintBuffer.get());g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);g.Clear(palette.panel);hits.clear();fill(g,{0,0,float(ww),float(top)},palette.chrome);rule(g,0,87,float(ww),87,border);
 // One icon row beneath the native Korean menu; the same hit rectangles drive tooltips and commands.
 float toolbarX=12;
 auto group=[&](std::initializer_list<int> ids){
     for(int id:ids){RectF box{toolbarX,18,44,44};hits.push_back({id,box});
         bool hovered=box.Contains(float(mouse.x),float(mouse.y));
         if(hovered){appearance::rounded(g,box,8,palette.hover);}
         Color tint=(id==101||id==102)?palette.accent:ink;
         icon(g,id,{box.X+7,box.Y+7,30,30},tint);toolbarX+=48;
     }
 };
 auto separator=[&](){rule(g,toolbarX+2,22,toolbarX+2,58,border);toolbarX+=16;};
 group({100,101,102});separator();group({104,105});separator();group({107,125,155,156});separator();
 group({170,171,172,173,174});separator();group({160,161,162,163,164});button(g,184,L"배경 · 테마  ◇",{float(ww-168),23,152,34});text(g,L"YOUNGDRAW  /  SCIENTIFIC ATELIER",16,70,9,muted,true);
 text(g,L"그리기",16,101,11,muted,true);
 const std::vector<int> drawingTools={200,201,202,217,203,204,205,206,207,208,209,210,211,212,213,214,215,216,218,219};
 for(size_t i=0;i<drawingTools.size();i++)toolButton(g,drawingTools[i],{float(12+(i%2)*82),float(122+(i/2)*44),74,40},int(tool)==drawingTools[i]-200);
 text(g,L"원소",16,576,11,muted,true);std::vector<int> elements={6,7,8,16,15,9,17,35,53,1,5,14};
 for(int i=0;i<int(elements.size());i++)button(g,300+elements[i],ui::wide(symbol(elements[i])),{float(12+i%3*53),float(598+i/3*30),49,26},element==elements[i]);
 int shown=int(tool);for(auto& h:hits)if(h.id>=200&&h.id<=218&&h.box.Contains(float(mouse.x),float(mouse.y)))shown=h.id-200;
 text(g,toolNames[size_t(shown)],12,728,10,muted);
 button(g,150,L"Arial 굵게 / 2 pt",{12,750,156,29});

 auto cb=canvasBox();appearance::background(g,cb);auto state=g.Save();g.SetClip(cb);g.TranslateTransform(float(origin.x),float(origin.y));g.ScaleTransform(float(zoom),float(zoom));const Scene& sc=sceneForPaint(reuseScene);animationVisible=false;animationDelay=1000;render(g,sc,true,true);if(animationVisible)SetTimer(win,912,animationDelay,nullptr);else KillTimer(win,912);g.Restore(state);
 state=g.Save();g.SetClip(cb);auto guide=[&](Vec c,double w,double h,std::wstring label,int owner,size_t index,bool above){Vec p=screen(c);RectF r(float(p.x-w*zoom/2),float(p.y-h*zoom/2),float(w*zoom),float(h*zoom));bool active=targetContainer==owner&&targetSlot==index&&targetAbove==above;Pen pen(active?accent:Color(255,201,215,223),active?1.6f:1);pen.SetDashStyle(DashStyleDash);g.DrawRectangle(&pen,r);text(g,label,r.X+3,r.Y+3,10,appearance::palette().canvasInk);};
 if(snapPlot){
    auto hovered=nearestPlotSnap(sc,mouse);auto found=sc.plotSnapPoints.find(snapPlot);
    if(found!=sc.plotSnapPoints.end())for(auto& pt:found->second){Vec at=screen(pt.p);Pen pen(accent,1.5f);SolidBrush body(white);g.FillEllipse(&body,float(at.x-4),float(at.y-4),8.f,8.f);g.DrawEllipse(&pen,float(at.x-4),float(at.y-4),8.f,8.f);}
    auto highlight=[&](const PlotSnapPoint& pt){Vec at=screen(pt.p);Pen pen(accent,2.5f);g.DrawEllipse(&pen,float(at.x-7),float(at.y-7),14.f,14.f);pen.SetDashStyle(DashStyleDash);g.DrawLine(&pen,float(at.x),float(at.y+8),float(at.x),float(screen({0,sc.plotBaselines.at(snapPlot)}).y));};
    if(snapStart)highlight(*snapStart);
    if(hovered){highlight(*hovered);if(auto* graph=plot(snapPlot)){auto label=ui::wide(graph->states[size_t(hovered->state)].label)+L" · "+(hovered->edge<0?L"왼쪽 끝":hovered->edge>0?L"오른쪽 끝":L"중앙");Vec at=screen(hovered->p);text(g,label,float(at.x+10),float(at.y-25),11,accent,true);}}
 }
 if(guides&&!snapPlot){for(auto& r:doc.reactions)for(size_t i=0;i<r.slots.size();i++)guide(reactionSlot(r,i),r.slotWidth,r.slotHeight,r.slots[i].members.empty()?L"구조를 삽입하세요":L"",r.id,i,false);for(auto& p:doc.plots)if(!doc.isLocked(p.id))for(size_t i=0;i<p.states.size();i++){guide(plotAnchor(p,i,true),48,55,p.states[i].above.empty()?L"+ 구조 삽입":L"",p.id,i,true);guide(plotAnchor(p,i,false),48,55,p.states[i].below.empty()?L"+ 구조 삽입":L"",p.id,i,false);}}
 if(tool==Select&&!inlineEdit)for(auto& r:doc.reactions)if(!r.cyclic&&!r.slots.empty()&&!doc.isLocked(r.id)){for(bool below:{false,true}){if(below&&r.vertical)continue;Vec p=mechanismAddPosition(r,below);SolidBrush body(white);Pen outline(accent,1.8f);g.FillEllipse(&body,float(p.x-12),float(p.y-12),24.f,24.f);g.DrawEllipse(&outline,float(p.x-12),float(p.y-12),24.f,24.f);g.DrawLine(&outline,float(p.x-5),float(p.y),float(p.x+5),float(p.y));g.DrawLine(&outline,float(p.x),float(p.y-5),float(p.x),float(p.y+5));}}
 if(selectedCycleArrow>=0&&selection==std::vector<int>{selectedCycleOwner}){Scene highlight;for(auto item:sc.items)if(item.owner==selectedCycleOwner&&item.reactionArrow==selectedCycleArrow){item.color="#008778";item.width+=1;highlight.items.push_back(item);}auto saved=g.Save();g.TranslateTransform(float(origin.x),float(origin.y));g.ScaleTransform(float(zoom),float(zoom));render(g,highlight);g.Restore(saved);}
 if(!inlineEdit)for(int id:selection){auto it=sc.objectBounds.find(id);if(it!=sc.objectBounds.end()){auto r=it->second;Vec p=screen({r.x,r.y});Pen pen(Color(150,0,135,120),1.5);pen.SetDashStyle(DashStyleDash);g.DrawRectangle(&pen,float(p.x-4),float(p.y-4),float(r.w*zoom+8),float(r.h*zoom+8));}else if(auto a=doc.atom(id)){Vec p=screen(a->p);Pen pen(accent,1.5);g.DrawEllipse(&pen,float(p.x-8),float(p.y-8),16.f,16.f);}}
 if(selection.size()==1){auto found=sc.arrowPaths.find(selection.front());if(found!=sc.arrowPaths.end()&&found->second.size()==4){const auto& path=found->second;Pen guidePen(Color(170,0,135,120),1);guidePen.SetDashStyle(DashStyleDash);for(int i:{0,2}){Vec a=screen(path[i]),b=screen(path[i+1]);g.DrawLine(&guidePen,float(a.x),float(a.y),float(b.x),float(b.y));}Pen outline(accent,1.5);SolidBrush body(white),control(accent);for(int i=0;i<4;i++){Vec p=screen(path[i]);g.FillEllipse(i==1||i==2?&control:&body,float(p.x-5),float(p.y-5),10.f,10.f);g.DrawEllipse(&outline,float(p.x-5),float(p.y-5),10.f,10.f);}}}
 if(hoverAtom){Vec p=screen(doc.atom(hoverAtom)->p);Pen pen(accent,1);g.DrawEllipse(&pen,float(p.x-7),float(p.y-7),14.f,14.f);}if(boxing){Pen p(accent,1);p.SetDashStyle(DashStyleDash);g.DrawRectangle(&p,float(std::min(down.x,mouse.x)),float(std::min(down.y,mouse.y)),float(std::abs(down.x-mouse.x)),float(std::abs(down.y-mouse.y)));}if(drawing){if(tool==Orbital){auto transform=g.Save();g.TranslateTransform(float(origin.x),float(origin.y));g.ScaleTransform(float(zoom),float(zoom));render(g,makeScene(draftOrbital(mouse),measure),false,true);g.Restore(transform);}else if(tool==Arrow||tool==Curve||tool==Fishhook||tool==Straight||tool==Resonance){Document preview=doc;preview.objects.push_back(draftArrow(mouse));auto all=makeScene(preview,measure);Scene arrow;for(auto& item:all.items)if(item.owner==-1)arrow.items.push_back(item);auto transform=g.Save();g.TranslateTransform(float(origin.x),float(origin.y));g.ScaleTransform(float(zoom),float(zoom));render(g,arrow,false,true);g.Restore(transform);}else{Pen p(accent,1.5);p.SetDashStyle(DashStyleDash);g.DrawLine(&p,float(down.x),float(down.y),float(mouse.x),float(mouse.y));}}g.Restore(state);
 float rx=float(ww-right);fill(g,{rx,float(top),float(right),float(wh-top-bottom)},palette.panel);button(g,175,L"도구",{rx+12,91,114,28},!showLayers);button(g,176,L"레이어",{rx+132,91,116,28},showLayers);
 if(showLayers){
    text(g,L"레이어 잠금",rx+16,142,14,ink,true);
    text(g,L"잠긴 레이어는 클릭되지 않습니다.",rx+16,170,11,muted);
    size_t images=std::count_if(doc.objects.begin(),doc.objects.end(),[](const Object& o){return o.kind==ObjectKind::Image;});
    auto row=[&](float y,const std::wstring& label,size_t count,bool locked,int id){
        appearance::rounded(g,{rx+12,y,236,64},8,palette.raised);text(g,label,rx+24,y+10,13,ink,true);text(g,std::to_wstring(count)+L"개",rx+24,y+34,11,muted);
        button(g,id,locked?L"잠김":L"편집 가능",{rx+142,y+16,96,30},locked);
    };
    row(204,L"이미지",images,doc.imagesLocked,177);row(276,L"그래프",doc.plots.size(),doc.graphsLocked,178);
    text(g,L"빠른 전환",rx+16,372,11,muted,true);
    button(g,179,L"이미지만 편집",{rx+12,398,236,34},doc.graphsLocked&&!doc.imagesLocked);
    button(g,180,L"그래프만 편집",{rx+12,442,236,34},doc.imagesLocked&&!doc.graphsLocked);
    button(g,181,L"모두 잠금 해제",{rx+12,486,236,34});
    text(g,L"그림 편집 중 그래프가 눌리면",rx+16,554,11,muted);
    text(g,L"'이미지만 편집'을 누르세요.",rx+16,575,11,muted);
    text(g,L"이미지와 그래프는 자동 분류됩니다.",rx+16,616,11,muted);
    text(g,L"잠금 상태도 문서에 저장됩니다.",rx+16,637,11,muted);
    text(g,L"Ctrl+Z로 잠금 변경을 되돌립니다.",rx+16,658,11,muted);
 }else{button(g,110,L"구조 정리  Ctrl+Shift+K",{rx+12,124,236,30});button(g,111,L"템플릿",{rx+12,160,114,30});button(g,112,L"보관",{rx+132,160,116,30});
 rule(g,rx+12,204,rx+248,204,border);
 button(g,182,L"메커니즘",{rx+12,218,114,32},sidebarPage==0);button(g,183,L"그래프",{rx+132,218,116,32},sidebarPage==1);
 auto heading=[&](const wchar_t* label,float y){text(g,label,rx+16,y,11,muted,true);};
 auto full=[&](int id,const wchar_t* label,float y,bool active=false){button(g,id,label,{rx+12,y,236,30},active);};
 auto pair=[&](int a,const wchar_t* first,int b,const wchar_t* second,float y){button(g,a,first,{rx+12,y,114,30});button(g,b,second,{rx+132,y,116,30});};
 if(sidebarPage==0){
    heading(L"반응 편집",270);pair(120,L"반응 추가",121,L"단계 표 편집",292);pair(122,L"단계 추가",127,L"메커니즘 설정",328);
    heading(L"배치",378);full(125,L"크기·간격 자동 정렬",400);full(126,L"촉매 순환 배치",436);
    heading(L"구조 삽입",486);pair(123,L"붙여넣기",124,L"파일에서 삽입",508);full(134,L"구조 삽입 영역 표시",544,guides);
    heading(L"시작하기",594);full(140,L"메커니즘 예제",616);
 }else{
    heading(L"데이터·경로",270);pair(130,L"그래프 추가",131,L"데이터 표 편집",292);pair(132,L"경로 추가",133,L"경로 스타일",328);
    pair(135,L"양쪽 분기 추가",138,L"출판용 정돈",364);
    heading(L"그래프 편집",410);pair(139,L"이름·화살표 표",136,L"화살표 자석",432);pair(137,L"그래프·축 설정",142,L"구간별 색상",468);
    heading(L"구조 삽입",522);pair(123,L"붙여넣기",124,L"파일에서 삽입",544);full(134,L"구조 삽입 영역 표시",580,guides);
    heading(L"시작하기",630);full(141,L"에너지 그래프 예제",652);
 }
 rule(g,rx+12,704,rx+248,704,border);full(113,L"구조 파일 열어 삽입",718);
 }
 fill(g,{0,float(wh-bottom),float(ww),float(bottom)},palette.chrome);rule(g,0,float(wh-bottom),float(ww),float(wh-bottom),border);text(g,status,14,float(wh-bottom+9),11,muted);Graphics out(dc);presentBuffer(out,*paintBuffer);}
BondKind kind(){return tool==Double?BondKind::Double:tool==Triple?BondKind::Triple:tool==Wedge?BondKind::Wedge:tool==Hash?BondKind::Hash:tool==Delocalized?BondKind::Aromatic:BondKind::Single;}
Vec sproutPoint(int id){return bondEndpoint(doc,id,doc.atom(id)->p,kind());}
Vec endpoint(Vec p){Vec a=startAtom?doc.atom(startAtom)->p:world(down);int target=doc.atomAt(world(p),8/zoom);if(target&&target!=startAtom)return doc.atom(target)->p;if((p-down).length()<5)return startAtom?bondEndpoint(doc,startAtom,doc.atom(startAtom)->p,kind(),element):a+Vec{std::cos(-pi/6),std::sin(-pi/6)}*defaultBondLength(doc,0,kind(),element);if(startAtom&&!(GetKeyState(VK_MENU)&0x8000))return bondEndpoint(doc,startAtom,world(p),kind(),element);return snapped(a,world(p),defaultBondLength(doc,startAtom,kind(),element),(GetKeyState(VK_MENU)&0x8000)!=0);}int objectAt(Vec p){auto sc=scene();for(auto it=doc.objects.rbegin();it!=doc.objects.rend();++it){if(doc.isLocked(it->id))continue;auto r=sc.objectBounds[it->id];double t=5/zoom;if(p.x>=r.x-t&&p.y>=r.y-t&&p.x<=r.x+r.w+t&&p.y<=r.y+r.h+t){if(it->kind==ObjectKind::Line&&it->points.size()>=2){Vec a=it->points[0],v=it->points[1]-a;double ll=v.x*v.x+v.y*v.y;if(ll>.001){double u=std::clamp(((p-a).x*v.x+(p-a).y*v.y)/ll,0.,1.);if((p-a-v*u).length()>t)continue;}}return it->id;}}
 for(auto& r:doc.reactions){auto b=sc.objectBounds[r.id];if(p.x>=b.x-5&&p.x<=b.x+b.w+5&&p.y>=b.y-5&&p.y<=b.y+b.h+5)return r.id;}for(auto& plot:doc.plots){if(doc.isLocked(plot.id))continue;auto b=sc.objectBounds[plot.id];if(p.x>=b.x-5&&p.x<=b.x+b.w+5&&p.y>=b.y-5&&p.y<=b.y+b.h+5)return plot.id;}return 0;}
bool pickSlot(Vec p){if(!guides)return false;for(auto& r:doc.reactions)for(size_t i=0;i<r.slots.size();i++){Vec c=reactionSlot(r,i);if(std::abs(p.x-c.x)<r.slotWidth/2&&std::abs(p.y-c.y)<r.slotHeight/2){targetContainer=r.id;targetSlot=i;targetAbove=false;selection={r.id};status=L"구조 삽입 영역 선택: Ctrl+V로 복사한 구조를 삽입합니다.";redraw();return true;}}
 for(auto& plot:doc.plots)if(!doc.isLocked(plot.id))for(size_t i=0;i<plot.states.size();i++)for(bool above:{false,true}){Vec c=plotAnchor(plot,i,above);if(std::abs(p.x-c.x)<24&&std::abs(p.y-c.y)<27.5){targetContainer=plot.id;targetSlot=i;targetAbove=above;sidebarPage=1;selection={plot.id};status=L"그래프 삽입 영역 선택: Ctrl+V로 복사한 구조를 삽입합니다.";redraw();return true;}}return false;}
bool beginCurveHandle(Vec p){
    if(selection.size()!=1||(GetKeyState(VK_CONTROL)&0x8000))return false;
    auto sc=scene();auto found=sc.arrowPaths.find(selection.front());if(found==sc.arrowPaths.end()||found->second.size()!=4)return false;
    for(int i=0;i<4;i++)if((screen(found->second[i])-p).length()<=8){
        dragBefore=doc;for(auto& o:doc.objects)if(o.id==selection.front()){o.points=found->second;o.autoRoute=false;o.sourceAtom=o.targetAtom=o.sourceBond=o.targetBond=0;}
        dragBase=doc;curveHandle=i;curveOwner=selection.front();dragging=true;moved=false;copyDrag=false;SetCapture(win);status=L"조절점을 드래그하여 곡선과 양끝 위치를 수정합니다. Esc: 취소 / Ctrl+Z: 되돌리기";redraw();return true;
    }return false;
}
void updateDrag(){
    if(!dragging)return;Vec delta=(mouse-down)*(1/zoom);if(GetKeyState(VK_SHIFT)&0x8000)delta=std::abs(delta.x)>std::abs(delta.y)?Vec{delta.x,0}:Vec{0,delta.y};
    if((mouse-down).length()<=2&&!moved)return;moved=true;doc=dragBase;
    if(annotationDrag){auto* a=doc.atom(annotationDrag);a->annotationManual=true;a->annotationOffset=annotationStart+delta-a->p;return;}
    if(curveHandle>=0){for(auto& o:doc.objects)if(o.id==curveOwner){o.points[size_t(curveHandle)]=o.points[size_t(curveHandle)]+delta;}return;}
    if(copyDrag){auto fragment=selectedDocument();int first=doc.nextId;doc.append(fragment);selection.clear();for(int id:doc.allIds())if(id>=first)selection.push_back(id);copyDrag=false;dragBase=doc;}doc.translate(selection,delta);
}
void cancel(){selectedCycleOwner=0;selectedCycleArrow=-1;snapPlot=0;snapStart.reset();if(dragging)doc=dragBefore;drawing=dragging=boxing=panning=rightPanning=false;curveHandle=-1;annotationDrag=0;ReleaseCapture();redraw();}
Vec mechanismAddPosition(const Reaction& r,bool below){
    Vec last=reactionSlot(r,r.slots.size()-1);
    if(below||(r.vertical&&!r.cyclic))return screen(last+Vec{0,r.slotHeight/2+mechanismCaptionHeight(r,doc.style)})+Vec{0,28};
    double direction=reactionRow(r,r.slots.size()-1)%2?-1.:1.;
    return screen(last+Vec{direction*r.slotWidth/2,0})+Vec{direction*28,0};
}
std::optional<std::pair<int,int>> cycleArrowAt(Vec p){
    double tolerance=6/zoom;
    for(auto& item:scene().items){
        if(item.reactionArrow<0||doc.isLocked(item.owner))continue;
        auto closeTo=[&](Vec a,Vec b){Vec v=b-a;double ll=v.x*v.x+v.y*v.y;double t=ll>1e-9?std::clamp(((p-a).x*v.x+(p-a).y*v.y)/ll,0.,1.):0;return (p-a-v*t).length()<=tolerance;};
        if(item.type==Primitive::Bezier&&item.points.size()==4){Vec a=item.points[0];for(int j=1;j<=64;j++){double t=j/64.,u=1-t;Vec b=item.points[0]*(u*u*u)+item.points[1]*(3*u*u*t)+item.points[2]*(3*u*t*t)+item.points[3]*(t*t*t);if(closeTo(a,b))return std::make_pair(item.owner,item.reactionArrow);a=b;}}
        if(item.type==Primitive::Polygon)for(size_t j=0;j<item.points.size();j++)if(closeTo(item.points[j],item.points[(j+1)%item.points.size()]))return std::make_pair(item.owner,item.reactionArrow);
    }
    return {};
}
bool deleteSelectedCycleArrow(){
    if(selectedCycleArrow<0||selection!=std::vector<int>{selectedCycleOwner})return false;
    auto* r=reaction(selectedCycleOwner);if(!r||!r->cyclic||doc.isLocked(r->id)||size_t(selectedCycleArrow)>=r->slots.size())return false;
    if(contains(r->hiddenArrows,selectedCycleArrow))return false;
    history.checkpoint(doc);r->hiddenArrows.push_back(selectedCycleArrow);selectedCycleOwner=0;selectedCycleArrow=-1;selection.clear();
    status=L"순환 화살표를 삭제했습니다. Ctrl+Z: 되돌리기 · 메커니즘 설정에서 모두 복원";changed();return true;
}
void mouseDown(Vec p){hideTooltip();for(auto& h:hits)if(h.id==156&&h.box.Contains(float(p.x),float(p.y))){command(156);return;}if(!finishInline(true))return;SetFocus(win);for(auto& h:hits)if(h.box.Contains(float(p.x),float(p.y))){command(h.id);return;}if(!canvas(p))return;down=mouse=p;annotationDrag=0;selectedCycleOwner=0;selectedCycleArrow=-1;
 if(tool==Select||tool==Erase)if(auto hit=cycleArrowAt(world(p))){selectedCycleOwner=hit->first;selectedCycleArrow=hit->second;selection={hit->first};targetContainer=0;if(tool==Erase)deleteSelectedCycleArrow();else{status=L"순환 화살표 선택 · Delete: 이 화살표만 삭제 · Ctrl+Z: 되돌리기";redraw();}return;}
 if(snapPlot){
    auto* graph=plot(snapPlot);if(!graph||doc.isLocked(snapPlot)){snapPlot=0;snapStart.reset();return;}
    auto hit=nearestPlotSnap(scene(),p);if(!hit){status=L"표시된 준위의 점 가까이를 클릭하세요. Esc: 취소";redraw();return;}
    if(!snapStart){snapStart=hit;status=L"첫 점 고정. 두 번째 끝점·중앙점을 클릭하세요 (좌우 순서 무관). Esc: 취소";redraw();return;}
    auto label=snapLabel;label.start=std::min(snapStart->percent,hit->percent);label.end=std::max(snapStart->percent,hit->percent);
    if(label.end-label.start<1){status=L"두 점을 더 떨어뜨려 선택하세요 (최소 범위 1%). Esc: 취소";redraw();return;}
    history.checkpoint(doc);if(snapStage<0)graph->stages.push_back(label);else graph->stages.at(size_t(snapStage))=label;
    snapPlot=0;snapStart.reset();status=L"준위에 맞춰 반응 이름·양쪽 화살표를 배치했습니다. Ctrl+Z: 되돌리기";changed();return;
 }
 if(tool==Select)for(auto& r:doc.reactions)if(!r.cyclic&&!r.slots.empty()&&!doc.isLocked(r.id) ){for(bool below:{false,true}){if(below&&r.vertical)continue;if((p-mechanismAddPosition(r,below)).length()<=13){selection={r.id};command(below?128:122);return;}}}
 if(tool==Select){auto sc=scene();Vec w=world(p);for(auto i=sc.items.rbegin();i!=sc.items.rend();++i)if(i->textRole==13&&!groupedObject(i->owner)){Vec at=i->points[0];double width=measure(i->text,i->fontSize,i->font,i->face);if(w.x>=at.x-2&&w.x<=at.x+width+2&&w.y>=at.y-2&&w.y<=at.y+i->fontSize+2){annotationDrag=i->owner;annotationStart=at;dragBefore=dragBase=doc;selection={i->owner};curveHandle=-1;dragging=true;moved=copyDrag=false;SetCapture(win);status=L"전하·라디칼 표시 위치 이동 · Esc: 취소 · Ctrl+Z: 되돌리기";redraw();return;}}}
 if(beginCurveHandle(p))return;curveHandle=-1;Vec w=world(p);int aid=doc.atomAt(w,10/zoom),bid=doc.bondAt(w,5/zoom),oid=aid||bid?0:objectAt(w);hoverAtom=aid;hoverBond=aid?0:bid;
 if((GetKeyState(VK_CONTROL)&0x8000)&&!aid&&!bid&&!oid){panning=true;panOrigin=origin;SetCapture(win);return;}
 if(tool==Erase){int id=aid?aid:bid?bid:oid;if(id){history.checkpoint(doc);doc.erase({id});selection.clear();changed();}return;}
 if(tool==Select){if(auto target=textAt(w);target&&contains(selection,target->owner)&&!doc.atom(target->owner)&&!groupedObject(target->owner)){beginInline(target,w);return;}bool imageHit=false;for(auto& o:doc.objects)if(o.id==oid&&o.kind==ObjectKind::Image)imageHit=true;if(!aid&&!bid&&!imageHit&&!groupedObject(oid)&&pickSlot(w))return;targetContainer=0;int id=aid?aid:bid?bid:oid;bool shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;if(id){if(shift){auto members=groupedSelection({id});if(contains(selection,id))selection.erase(std::remove_if(selection.begin(),selection.end(),[&](int member){return contains(members,member);}),selection.end());else{selection.insert(selection.end(),members.begin(),members.end());selection=groupedSelection(selection);}}else if(!contains(selection,id)){selection=groupedSelection({id});}if(!selection.empty()){dragBefore=doc;dragBase=doc;dragging=true;moved=false;copyDrag=(GetKeyState(VK_CONTROL)&0x8000)!=0;SetCapture(win);}}else{if(!shift)selection.clear();boxing=true;SetCapture(win);}redraw();return;}
 if(tool==Text){beginInline(textAt(w),w);return;}
 if(tool==ImageTool){addImage(w);return;}if(tool==Ring5||tool==Ring6||tool==Benzene){history.checkpoint(doc);if(aid||bid)doc.attachRing(aid,bid,tool==Ring5?5:6,tool==Benzene);else doc.ring(w,tool==Ring5?5:6,tool==Benzene);changed();return;}
 if(tool==Arrow||tool==Curve||tool==Fishhook||tool==Straight||tool==Resonance||tool==Orbital){drawing=true;startAtom=aid;startBond=aid?0:bid;SetCapture(win);return;}
 if((GetKeyState(VK_MENU)&0x8000)&&aid){selection={aid};editObject(aid);return;}
 if(!aid&&bid){history.checkpoint(doc);auto b=doc.bond(bid);if(b->kind==BondKind::Aromatic&&kind()==BondKind::Aromatic){Vec a=doc.atom(b->a)->p,v=doc.atom(b->b)->p-a;for(auto& stroke:scene().items)if(stroke.owner==bid&&stroke.dash&&stroke.points.size()==2){Vec offset=(stroke.points[0]+stroke.points[1])*.5-a;b->doubleSide=(v.x*offset.y-v.y*offset.x)>0?-1:1;break;}}else if(b->kind==kind()&&(kind()==BondKind::Wedge||kind()==BondKind::Hash))std::swap(b->a,b->b);else setBondKind(doc,bid,kind());b->customOrder.clear();b->customDisplay.clear();selection={bid};changed();return;}
 startAtom=aid;drawing=true;SetCapture(win);redraw();}
void mouseUp(Vec p){if(rightPanning)return;if(panning){panning=false;ReleaseCapture();redraw();return;}if(dragging){dragging=false;if(moved){history.checkpoint(dragBefore);changed();}else doc=dragBefore;curveHandle=-1;annotationDrag=0;ReleaseCapture();}if(boxing){boxing=false;ReleaseCapture();Vec a=world(down),b=world(p);double l=std::min(a.x,b.x),t=std::min(a.y,b.y),r=std::max(a.x,b.x),z=std::max(a.y,b.y);for(auto& atom:doc.atoms)if(atom.p.x>=l&&atom.p.x<=r&&atom.p.y>=t&&atom.p.y<=z&&!contains(selection,atom.id))selection.push_back(atom.id);auto sc=scene();for(auto& box:sc.objectBounds)if(!doc.atom(box.first)&&!doc.bond(box.first)&&!doc.isLocked(box.first)){auto q=box.second;if(q.x>=l&&q.y>=t&&q.x+q.w<=r&&q.y+q.h<=z&&!contains(selection,box.first))selection.push_back(box.first);}selection=groupedSelection(selection);redraw();}
 if(drawing){if(tool==Orbital){auto fragment=draftOrbital(p);drawing=false;ReleaseCapture();history.checkpoint(doc);int first=doc.nextId;doc.append(fragment);selection.clear();for(auto& o:doc.objects)if(o.id>=first)selection.push_back(o.id);changed();return;}if(tool==Arrow||tool==Curve||tool==Fishhook||tool==Straight||tool==Resonance){drawing=false;ReleaseCapture();if((p-down).length()<5)return;Object o=draftArrow(p);history.checkpoint(doc);o.id=doc.nextId++;doc.objects.push_back(o);selection={o.id};changed();return;}
 Vec end=endpoint(p),begin=startAtom?doc.atom(startAtom)->p:world(down);int target=doc.atomAt(end,8/zoom);drawing=false;ReleaseCapture();if(target==startAtom&&startAtom)return;if((end-begin).length()<1)return;history.checkpoint(doc);int a=startAtom?startAtom:doc.addAtom(begin),b=target?target:doc.addAtom(end,element);doc.addBond(a,b,kind());selection={b};changed();}redraw();}
void hotkey(char c){
 if(snapPlot)return;
 if(c=='+'||c=='-'||c=='*'){
    int target=doc.atom(hoverAtom)?hoverAtom:selection.size()==1&&doc.atom(selection.front())?selection.front():0;
    if(target){history.checkpoint(doc);auto* a=doc.atom(target);if(c=='*')a->radical=a->radical?0:2;else{a->charge+=c=='+'?1:-1;a->explicitH=-1;}changed();return;}
 }
 int aid=hoverAtom,bid=aid?0:hoverBond;if(c=='a'&&!aid&&!bid){command(217);return;}if(aid){std::map<char,std::string> labels={{'b',"Br"},{'B',"B"},{'c',"C"},{'C',"Cl"},{'f',"F"},{'h',"H"},{'i',"I"},{'l',"Cl"},{'L',"Li"},{'n',"N"},{'w',"N"},{'o',"O"},{'q',"O"},{'p',"P"},{'s',"S"},{'S',"Si"},{'m',"Me"},{'e',"Et"},{'r',"R"},{'N',"NO2"},{'O',"OMe"},{'P',"Ph"},{'F',"CF3"},{'y',"Boc"},{'d',"D"}};auto label=labels.find(c);if(label!=labels.end()){history.checkpoint(doc);auto a=doc.atom(aid);int e=atomicNumber(label->second);if(e){setAtomElement(doc,a->id,e);a->label.clear();a->labelRuns.clear();a->explicitH=-1;}else{a->label=label->second;a->labelRuns.clear();}if(c=='d'){a->element=1;a->isotope=2;a->label.clear();a->labelRuns.clear();}selection={aid};changed();return;}if(c=='g'){selection=doc.component(aid);tool=Select;redraw();return;}
 if((c>='0'&&c<='9')||c=='a'||c=='z'||c=='v'||c=='u'){history.checkpoint(doc);if(c=='3'||c=='a'||c=='6'||c=='7'||c=='v'||c=='u'){bool aromatic=c=='a'||c=='3';Vec at=bondEndpoint(doc,aid,doc.atom(aid)->p,BondKind::Single,6);if(aromatic&&doc.atom(aid)->element==6){Vec v=at-doc.atom(aid)->p;at=doc.atom(aid)->p+v*(doc.style.bondLength*benzeneConnectorScale/v.length());}int attached=doc.addAtom(at);doc.addBond(aid,attached);doc.attachRing(attached,0,c=='7'?5:c=='v'?3:c=='u'?4:6,aromatic);}else{int e=c=='2'?8:6;BondKind k=c=='2'||c=='8'?BondKind::Double:c=='z'?BondKind::Triple:c=='4'?BondKind::Wedge:c=='5'?BondKind::Hash:BondKind::Single;Vec p=bondEndpoint(doc,aid,doc.atom(aid)->p,k,e);int b=doc.addAtom(p,e);doc.addBond(aid,b,k);if(c=='9'){int z=doc.addAtom(sproutPoint(aid));doc.addBond(aid,z);}}changed();return;}}
 if(bid){auto b=doc.bond(bid);if(c=='g'){selection=doc.component(b->a);tool=Select;redraw();return;}if(c=='1'||c=='2'||c=='3'||c=='w'||c=='h'||c=='W'||c=='d'||c=='b'||c=='y'){history.checkpoint(doc);BondKind k=c=='1'?BondKind::Single:c=='2'?BondKind::Double:c=='3'?BondKind::Triple:c=='w'?BondKind::Wedge:c=='h'||c=='W'?BondKind::Hash:c=='d'?BondKind::Dash:c=='b'?BondKind::Bold:BondKind::Wavy;if(b->kind==k&&(k==BondKind::Wedge||k==BondKind::Hash))std::swap(b->a,b->b);setBondKind(doc,bid,k);b->customOrder.clear();b->customDisplay.clear();changed();return;}
 if((c>='4'&&c<='8')||c=='a'||c=='v'){history.checkpoint(doc);doc.attachRing(0,bid,c=='a'?6:c=='v'?3:c-'0',c=='a');changed();return;}if(c=='l'||c=='r'||c=='c'){history.checkpoint(doc);b->doubleSide=c=='l'?1:c=='r'?-1:0;changed();return;}}
 switch(c){case ' ':tool=Select;break;case 'G':tool=Orbital;break;case 'x':tool=Single;break;case 'j':tool=Benzene;break;case 'e':tool=Arrow;break;case 't':tool=Text;break;case 'l':tool=Straight;break;case 'r':tool=Resonance;break;case 'a':command(217);return;default:break;}redraw();}
bool deleteSelectedCycleArrow();
#include "win_shortcuts.h"

void menuBar(HWND hwnd){HMENU bar=CreateMenu();auto menu=[&](const wchar_t* label,std::initializer_list<std::pair<int,const wchar_t*>> items){HMENU m=CreatePopupMenu();for(auto& item:items)AppendMenuW(m,MF_STRING,item.first,item.second);AppendMenuW(bar,MF_POPUP|MF_OWNERDRAW,(UINT_PTR)m,label);MENUITEMINFOW info{sizeof(info)};info.fMask=MIIM_STRING;info.dwTypeData=const_cast<wchar_t*>(label);SetMenuItemInfoW(bar,UINT(GetMenuItemCount(bar)-1),TRUE,&info);};menu(L"파일(&F)",{{100,L"새 문서\tCtrl+N"},{101,L"열기…\tCtrl+O"},{102,L"저장\tCtrl+S"},{112,L"템플릿으로 저장…"},{109,L"현재 탭 닫기\tCtrl+W"},{108,L"CDXML 내보내기…"},{103,L"SVG 내보내기…"}});menu(L"편집(&E)",{{104,L"실행 취소\tCtrl+Z"},{105,L"다시 실행\tCtrl+Shift+Z"},{198,L"그룹 만들기\tCtrl+G"},{199,L"그룹 해제\tCtrl+Shift+G"},{121,L"선택 항목 편집…"},{110,L"구조 정리\tCtrl+Shift+K"},{153,L"선택 부분을 뒤쪽으로 (옅은 회색)"},{154,L"선택 부분을 앞쪽으로 (검정)"},{160,L"선택 구조 좌우 반전"},{161,L"선택 구조 상하 반전"},{162,L"선택 구조 반시계 90° 회전"},{163,L"선택 구조 시계 90° 회전"},{164,L"선택 구조 각도 지정 회전…\tCtrl+R"},{165,L"선택 구조 크기 조정…\tCtrl+K"}});menu(L"삽입(&I)",{{111,L"템플릿…"},{113,L"구조 파일…"},{215,L"직선\tl"},{216,L"공명 화살표 ↔\tr"},{217,L"공명 결합 (실선+파선)\ta"},{218,L"오비탈 그리기"},{219,L"오비탈 설정…"},{120,L"반응 메커니즘…"},{130,L"에너지 그래프…"}});menu(L"배치(&L)",{{125,L"크기·간격 자동 정렬"},{126,L"촉매 순환 배치"},{127,L"메커니즘 설정…"},{137,L"그래프·축 설정…"},{138,L"출판용 비율·여백 정돈"},{139,L"반응 이름·화살표…"},{136,L"화살표 자석 배치…"},{142,L"그래프 구간별 색상…"},{135,L"그래프 양쪽 분기 추가…"},{190,L"선택 객체 왼쪽 맞춤\tCtrl+Shift+Alt+L"},{191,L"선택 객체 가로 중앙 맞춤\tCtrl+Shift+Alt+C"},{192,L"선택 객체 오른쪽 맞춤\tCtrl+Shift+Alt+R"},{193,L"선택 객체 위쪽 맞춤\tCtrl+Shift+Alt+T"},{194,L"선택 객체 상하 중앙 맞춤\tCtrl+Shift+Alt+M"},{195,L"선택 객체 아래쪽 맞춤\tCtrl+Shift+Alt+B"},{196,L"선택 객체 가로 간격 배분\tCtrl+Shift+Alt+H"},{197,L"선택 객체 세로 간격 배분\tCtrl+Shift+Alt+V"}});menu(L"보기(&V)",{{184,L"배경 · 테마…"},{176,L"레이어 패널"},{107,L"화면 맞춤"},{134,L"구조 삽입 영역 표시"},{140,L"메커니즘 예제"},{141,L"에너지 그래프 예제"}});SetMenu(hwnd,bar);refreshAppearance();}
void refreshAppearance(){const auto& p=appearance::palette();ink=p.text;muted=p.muted;accent=p.accent;border=p.edge;appearance::titlebar(win);HBRUSH old=menuBackground;menuBackground=CreateSolidBrush(RGB(p.chrome.GetR(),p.chrome.GetG(),p.chrome.GetB()));MENUINFO info{sizeof(info)};info.fMask=MIM_BACKGROUND;info.hbrBack=menuBackground;if(GetMenu(win))SetMenuInfo(GetMenu(win),&info);if(old)DeleteObject(old);DrawMenuBar(win);if(documentTabs)InvalidateRect(documentTabs,nullptr,FALSE);redraw();}
LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){try{switch(msg){case WM_CREATE:win=hwnd;appearance::load();appearance::titlebar(hwnd);createTooltip();menuBar(hwnd);createDocumentTabs();title();return 0;case WM_GETMINMAXINFO:{auto* p=(MINMAXINFO*)lp;p->ptMinTrackSize={1200,880};return 0;}case WM_SIZE:hideTooltip();if(!finishInline(true))return 0;ww=LOWORD(lp);wh=HIWORD(lp);if(documentTabs)MoveWindow(documentTabs,left,89,std::max(1,ww-left-right),30,TRUE);redraw();return 0;case WM_ERASEBKGND:return 1;case WM_SETCURSOR:if(LOWORD(lp)==HTCLIENT&&panning){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}if(LOWORD(lp)==HTCLIENT&&canvas(mouse)&&tool==Text){SetCursor(LoadCursorW(nullptr,IDC_IBEAM));return TRUE;}break;case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);paint(dc,true);EndPaint(hwnd,&ps);return 0;}
 case WM_MEASUREITEM:{auto* item=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);if(item->CtlType==ODT_MENU){auto label=reinterpret_cast<const wchar_t*>(item->itemData);item->itemWidth=UINT(wcslen(label)*8+16);item->itemHeight=24;return TRUE;}break;}
 case WM_DRAWITEM:{auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item->CtlType==ODT_MENU){const auto& p=appearance::palette();Graphics g(item->hDC);RectF box(float(item->rcItem.left),float(item->rcItem.top),float(item->rcItem.right-item->rcItem.left),float(item->rcItem.bottom-item->rcItem.top));fill(g,box,(item->itemState&ODS_SELECTED)?p.hover:p.chrome);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,RGB(p.text.GetR(),p.text.GetG(),p.text.GetB()));auto font=SelectObject(item->hDC,GetStockObject(DEFAULT_GUI_FONT));RECT rect=item->rcItem;rect.left+=8;DrawTextW(item->hDC,reinterpret_cast<const wchar_t*>(item->itemData),-1,&rect,DT_SINGLELINE|DT_VCENTER);SelectObject(item->hDC,font);return TRUE;}break;}
 case WM_CTLCOLOREDIT:if((HWND)lp==inlineEdit){SetTextColor((HDC)wp,inlineColor);SetBkColor((HDC)wp,RGB(255,255,255));return (LRESULT)GetStockObject(WHITE_BRUSH);}break;case WM_TIMER:if(wp==912){if(!IsIconic(hwnd)){auto box=canvasBox();RECT r{LONG(box.X),LONG(box.Y),LONG(box.GetRight()),LONG(box.GetBottom())};repaintOnly(&r);}return 0;}if(wp==911){showTooltip();return 0;}break;case WM_ACTIVATEAPP:if(!wp)hideTooltip();break;case WM_LBUTTONDOWN:mouseDown({double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))});return 0;case WM_LBUTTONUP:mouseUp({double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))});return 0;
 case WM_LBUTTONDBLCLK:{if(snapPlot){mouseDown({double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))});return 0;}Vec p=world({double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))});if(auto target=textAt(p);target&&!doc.atom(target->owner)){cancel();beginInline(target,p);return 0;}int aid=doc.atomAt(p,10/zoom);if(aid){selection=doc.component(aid);tool=Select;}else{int id=objectAt(p);if(id){selection={id};edit();}}redraw();return 0;}
 case WM_MBUTTONDOWN:if(canvas({double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))})){cancel();panning=true;down={double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))};panOrigin=origin;SetCapture(hwnd);}return 0;case WM_MBUTTONUP:if(panning&&!rightPanning){panning=false;ReleaseCapture();redraw();}return 0;
 case WM_RBUTTONDOWN:{Vec at{double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))};if((wp&MK_CONTROL)&&canvas(at)){
    hideTooltip();if(!finishInline(true))return 0;cancel();SetFocus(hwnd);down=mouse=at;panOrigin=origin;panning=rightPanning=true;SetCapture(hwnd);SetCursor(LoadCursorW(nullptr,IDC_HAND));
 }return 0;}
 case WM_RBUTTONUP:if(rightPanning){mouse={double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))};origin=panOrigin+mouse-down;panning=rightPanning=false;ReleaseCapture();SetCursor(LoadCursorW(nullptr,IDC_ARROW));redraw();}return 0;
 case WM_MOUSELEAVE:hideTooltip();hoverAtom=hoverBond=0;repaintOnly();return 0;case WM_MOUSEMOVE:{TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,hwnd,0};TrackMouseEvent(&tracking);mouse={double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))};hoverAtom=canvas(mouse)?doc.atomAt(world(mouse),10/zoom):0;hoverBond=canvas(mouse)&&!hoverAtom?doc.bondAt(world(mouse),5/zoom):0;updateDrag();if(panning)origin=panOrigin+mouse-down;updateTooltip();if(!inlineEdit){if(dragging)redraw();else repaintOnly();}return 0;}
 case WM_CAPTURECHANGED:if((drawing||dragging||boxing||panning)&&(HWND)lp!=hwnd){if(dragging)doc=dragBefore;drawing=dragging=boxing=panning=rightPanning=false;curveHandle=-1;annotationDrag=0;redraw();}return 0;
 case WM_MOUSEWHEEL:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hwnd,&p);Vec at{double(p.x),double(p.y)};if(canvas(at)){Vec anchor=world(at);zoom=std::clamp(zoom*(GET_WHEEL_DELTA_WPARAM(wp)>0?1.15:1/1.15),.12,12.);origin=at-anchor*zoom;repaintOnly();}return 0;}
 case WM_NOTIFY:if(documentTabs&&((NMHDR*)lp)->hwndFrom==documentTabs&&((NMHDR*)lp)->code==TCN_SELCHANGE){switchDocumentTab(size_t(TabCtrl_GetCurSel(documentTabs)));SetFocus(win);return 0;}break;case WM_APP+3:closeDocumentTab(size_t(wp));return 0;case WM_SYSKEYDOWN:if((GetKeyState(VK_CONTROL)&0x8000)||wp==VK_LEFT||wp==VK_RIGHT||wp==VK_UP||wp==VK_DOWN||wp=='K'){key(wp);return 0;}break;case WM_KEYDOWN:key(wp);return 0;case WM_CHAR:if(!(GetKeyState(VK_CONTROL)&0x8000)&&!dragging&&!drawing&&!boxing&&!panning&&wp<128)hotkey(char(wp));return 0;case WM_COMMAND:if(LOWORD(wp)>=100&&(LOWORD(wp)<=199||LOWORD(wp)==215||LOWORD(wp)==216||LOWORD(wp)==217||LOWORD(wp)==218||LOWORD(wp)==219)){command(LOWORD(wp));return 0;}if(LOWORD(wp)==900){if(HIWORD(wp)==EN_CHANGE)previewInline();else if(HIWORD(wp)==EN_KILLFOCUS&&!inlineClosing)PostMessageW(hwnd,WM_APP+1,0,0);return 0;}break;case WM_APP+1:if(inlineEdit&&GetFocus()!=inlineEdit)finishInline(true);return 0;case WM_CLOSE:if(!finishInline(true))return 0;if(closeAllDocuments()){if(office::editing()){office::close();dirty=false;}else DestroyWindow(hwnd);}return 0;case WM_DESTROY:KillTimer(hwnd,912);canvasSceneCache.reset();paintBuffer.reset();if(menuBackground){DeleteObject(menuBackground);menuBackground=nullptr;}if(tabFont){DeleteObject(tabFont);tabFont=nullptr;}PostQuitMessage(0);return 0;
 }}catch(const std::exception& e){MessageBoxW(hwnd,ui::wide(e.what()).c_str(),L"YoungDraw",MB_OK|MB_ICONWARNING);redraw();}return DefWindowProcW(hwnd,msg,wp,lp);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){SetProcessDPIAware();int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);OleInitialize(nullptr);bool embedding=argc>1&&(_wcsicmp(argv[1],L"/Embedding")==0||_wcsicmp(argv[1],L"-Embedding")==0);GdiplusStartupInput start;ULONG_PTR token;GdiplusStartup(&token,&start,nullptr);
 if(argc==2&&std::wstring(argv[1])==L"--copy-office-demo"){office::start({nullptr,[](const Document&){},[](){return coordinateDemo();},officePicture},false);auto result=office::copy(coordinateDemo());if(SUCCEEDED(result))result=OleFlushClipboard();LocalFree(argv);imageCache.clear();GdiplusShutdown(token);OleUninitialize();return FAILED(result)?int(result):0;}
 if(argc==2&&std::wstring(argv[1])==L"--test-office"){office::start({nullptr,[](const Document&){},[](){return coordinateDemo();},officePicture},false,false);auto result=office::selfTest();LocalFree(argv);imageCache.clear();GdiplusShutdown(token);OleUninitialize();return FAILED(result)?int(result):0;}
 if(argc==3&&std::wstring(argv[1])==L"--export-lab-templates"){try{fs::create_directories(argv[2]);size_t i=0;for(auto& entry:labTemplates){auto d=fromNative(entry.data);auto path=fs::path(argv[2])/std::to_wstring(i++);writePreview(path.wstring()+L".png",d);writeFile(path.wstring()+L".svg",toSvg(makeScene(d)));writeFile(path.wstring()+L".cdxml",toCdxml(d));}writePreview((fs::path(argv[2])/L"chair.png").wstring(),chairTemplate());writeFile((fs::path(argv[2])/L"chair.svg").wstring(),toSvg(makeScene(chairTemplate())));LocalFree(argv);GdiplusShutdown(token);OleUninitialize();return 0;}catch(...){LocalFree(argv);GdiplusShutdown(token);OleUninitialize();return 1;}}
 if(argc==3&&std::wstring(argv[1])==L"--export-demo"){try{writeFile(std::wstring(argv[2])+L".cdxml",toCdxml(doc));writeFile(std::wstring(argv[2])+L".svg",toSvg(makeScene(doc)));writeFile(std::wstring(argv[2])+L"-mechanism.ydraw",toNative(mechanismDemo()));writeFile(std::wstring(argv[2])+L"-mechanism.cdxml",toCdxml(mechanismDemo()));writeFile(std::wstring(argv[2])+L"-mechanism.svg",toSvg(makeScene(mechanismDemo())));writeFile(std::wstring(argv[2])+L"-coordinate.ydraw",toNative(coordinateDemo()));writeFile(std::wstring(argv[2])+L"-coordinate.cdxml",toCdxml(coordinateDemo()));writeFile(std::wstring(argv[2])+L"-coordinate.svg",toSvg(makeScene(coordinateDemo())));Document rigid=coordinateDemo();rigid.plots[0].mode=3;rigid.plots[0].series[0].dash=1;rigid.plots[0].series[0].color="#0e6d89";rigid.plots[0].states[1].marker=3;rigid.plots[0].states[3].marker=4;rigid.plots[0].series.push_back({"Path 2","#b44758",{0,26,10,19,-9.9},0,2,2,2.5});writeFile(std::wstring(argv[2])+L"-rigid.ydraw",toNative(rigid));writeFile(std::wstring(argv[2])+L"-rigid.cdxml",toCdxml(rigid));writeFile(std::wstring(argv[2])+L"-rigid.svg",toSvg(makeScene(rigid)));writePreview(std::wstring(argv[2])+L"-rigid.png",rigid);writePreview(std::wstring(argv[2])+L"-coordinate.png",coordinateDemo());writePreview(std::wstring(argv[2])+L"-mechanism.png",mechanismDemo());Document partial;int pa=partial.addAtom({30,45}),pb=partial.addAtom({56,30}),pc=partial.addAtom({82,45});partial.addBond(pa,pb);partial.addBond(pc,pb);delocalizeBonds(partial,partial.allIds());writeFile(std::wstring(argv[2])+L"-delocalized.ydraw",toNative(partial));writeFile(std::wstring(argv[2])+L"-delocalized.cdxml",toCdxml(partial));writeFile(std::wstring(argv[2])+L"-delocalized.svg",toSvg(makeScene(partial)));writePreview(std::wstring(argv[2])+L"-delocalized.png",partial);Document cyclic=mechanismDemo();optimizeMechanism(cyclic,{},true);writeFile(std::wstring(argv[2])+L"-cycle.ydraw",toNative(cyclic));writePreview(std::wstring(argv[2])+L"-cycle.png",cyclic);LocalFree(argv);GdiplusShutdown(token);OleUninitialize();return 0;}catch(...){LocalFree(argv);GdiplusShutdown(token);OleUninitialize();return 1;}}
 if(argc==4&&std::wstring(argv[1])==L"--convert"){try{doc=fromCdxml(readFile(argv[2]));writeFile(std::wstring(argv[3])+L".ydraw",toNative(doc));writeFile(std::wstring(argv[3])+L".cdxml",toCdxml(doc));writeFile(std::wstring(argv[3])+L".svg",toSvg(scene()));LocalFree(argv);GdiplusShutdown(token);OleUninitialize();return 0;}catch(const std::exception& e){try{writeFile(std::wstring(argv[3])+L".error.txt",e.what());}catch(...){} LocalFree(argv);GdiplusShutdown(token);OleUninitialize();return 1;}}
 StartupSplash splash(instance,!embedding && show!=SW_HIDE && show!=SW_SHOWMINIMIZED && show!=SW_SHOWMINNOACTIVE && show!=SW_MINIMIZE);
 std::vector<std::pair<Document,std::wstring>> startupDocuments;
 if(!embedding)for(int i=1;i<argc;i++){try{startupDocuments.push_back({fromCdxml(readFile(argv[i])),argv[i]});}catch(...){}}
 if(!startupDocuments.empty()){doc=std::move(startupDocuments[0].first);filePath=startupDocuments[0].second;Document beforeUpgrade=doc;dirty=upgradeBondGeometry(doc);if(dirty)history.checkpoint(beforeUpgrade);tool=Select;}

 LocalFree(argv);WNDCLASSW wc{};wc.style=CS_DBLCLKS;wc.lpfnWndProc=proc;wc.hInstance=instance;wc.lpszClassName=L"YoungDrawWindow";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));RegisterClassW(&wc);HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"YoungDraw",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1440,940,nullptr,nullptr,instance,nullptr);if(!hwnd)return 1;SendMessageW(hwnd,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED)));fit();for(size_t i=1;i<startupDocuments.size();i++)newDocumentTab(std::move(startupDocuments[i].first),startupDocuments[i].second);if(FAILED(office::start({hwnd,[](const Document& d){storeDocumentTab();for(auto& tab:documents)tab.office=false;officeDocument=false;newDocumentTab(d,L"",true);},[](){return officeCurrentDocument();},officePicture},embedding)))MessageBoxW(hwnd,L"Office 편집 개체를 등록하지 못했습니다.",L"YoungDraw",MB_OK|MB_ICONWARNING);if(!embedding)splash.finish(hwnd,show);fit();UpdateWindow(hwnd);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}office::stop();imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return int(msg.wParam);}

#include "win_editor_chrome.h"
#include "win_editor_commands.h"
#include "win_editor_shortcuts.h"
#include "win_editor_state.h"
#include "win_editor_tabs.h"
#include "win_dialogs.h"
#include "win_theme.h"
#include "win_ui.h"
#include <commctrl.h>

namespace yd::editor {

HBRUSH menuBackground=nullptr;
Color ink(255,28,36,49),muted(255,101,114,131),accent(255,0,123,112),border(255,222,229,234),white(255,255,255,255),black(255,0,0,0);
std::vector<Hit> hits;
void text(Graphics& g,const std::wstring& s,float x,float y,float size,Color c,bool bold){FontFamily ff(ui::interfaceFont());Font f(&ff,size,bold?FontStyleBold:FontStyleRegular,UnitPixel);SolidBrush b(c);StringFormat fmt(StringFormat::GenericTypographic());g.DrawString(s.c_str(),-1,&f,PointF(x,y),&fmt,&b);}
void fill(Graphics& g,RectF r,Color c){SolidBrush b(c);g.FillRectangle(&b,r);}
void rule(Graphics& g,float x,float y,float z,float w,Color c){Pen p(c,1);g.DrawLine(&p,x,y,z,w);}
void icon(Graphics& g,int id,RectF box,Color color){
    auto st=g.Save();float unit=std::min(box.Width,box.Height)/48;g.TranslateTransform(box.X+box.Width/2,box.Y+box.Height/2);g.ScaleTransform(unit,unit);Pen pen(color,1.9f);pen.SetStartCap(LineCapRound);pen.SetEndCap(LineCapRound);SolidBrush brush(color);
    auto line=[&](float x,float y,float a,float b){g.DrawLine(&pen,x,y,a,b);};
    auto ring=[&](int n,bool benzene){std::vector<PointF> p;for(int i=0;i<n;i++){double a=-pi/2+pi/n+2*pi*i/n;p.push_back({float(17*std::cos(a)),float(17*std::sin(a))});}g.DrawPolygon(&pen,p.data(),n);if(benzene)for(int i=0;i<6;i+=2)line(p[i].X*.72f,p[i].Y*.72f,p[(i+1)%6].X*.72f,p[(i+1)%6].Y*.72f);};
    auto arrow=[&](){line(-20,0,19,0);line(19,0,12,-5);line(19,0,12,5);};
    switch(id){
    case 100:g.DrawRectangle(&pen,-15.f,-20.f,30.f,40.f);line(3,-20,15,-8);line(3,-20,3,-8);line(3,-8,15,-8);break;
    case 185:case 101:{PointF folder[]={{-22,-10},{-5,-10},{0,-17},{18,-17},{18,15},{-22,15}};g.DrawPolygon(&pen,folder,6);line(-18,17,-11,-2);line(-11,-2,23,-2);line(23,-2,18,17);if(id==185){line(-9,5,8,5);line(-9,10,8,10);}break;}
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
    case dottedBondCommand:line(-23,0,-17,0);line(17,0,23,0);[[fallthrough]];
    case interactionToolCommand:for(int i=-2;i<=2;i++)g.FillEllipse(&brush,float(i*9-2),-2.f,4.f,4.f);break;
    case 215:line(-22,0,22,0);break;
    case 216:arrow();line(-20,0,-13,-5);line(-20,0,-13,5);break;
    case 217:line(-20,5,18,-16);pen.SetDashStyle(DashStyleDash);line(-16,14,22,-7);break;
    case 218:{GraphicsPath positive,negative;
        positive.AddBezier(0.f,0.f,-16.f,-23.f,-27.f,-12.f,-20.f,0.f);positive.AddBezier(-20.f,0.f,-27.f,12.f,-16.f,23.f,0.f,0.f);positive.CloseFigure();g.FillPath(&brush,&positive);
        negative.AddBezier(0.f,0.f,16.f,-23.f,27.f,-12.f,20.f,0.f);negative.AddBezier(20.f,0.f,27.f,12.f,16.f,23.f,0.f,0.f);negative.CloseFigure();g.DrawPath(&pen,&negative);break;}
    case 127:case 137:case 219:g.DrawEllipse(&pen,-12.f,-12.f,24.f,24.f);g.DrawEllipse(&pen,-4.f,-4.f,8.f,8.f);for(int i=0;i<8;i++){double a=i*pi/4;line(float(13*std::cos(a)),float(13*std::sin(a)),float(20*std::cos(a)),float(20*std::sin(a)));}break;
    case 131:
        g.DrawRectangle(&pen,-19.f,-16.f,38.f,32.f);
        line(-19,-5,19,-5);line(-19,6,19,6);line(-7,-5,-7,16);line(7,-5,7,16);break;
    case 130:line(-20,18,22,18);line(-20,18,-20,-19);g.DrawBezier(&pen,PointF(-17,11),PointF(-2,-27),PointF(7,20),PointF(20,-10));g.FillEllipse(&brush,-7.f,-5.f,5.f,5.f);break;
    case spinMultiplicityPlotCommand:
        line(-20,18,22,18);line(-20,18,-20,-19);
        line(-15,-11,-6,-11);line(-2,-2,7,-2);line(11,9,20,9);break;
    case 132:g.DrawBezier(&pen,PointF(-21,12),PointF(-9,-25),PointF(10,-18),PointF(21,11));line(-21,19,-4,-3);line(-4,-3,21,19);break;
    case 133:line(-22,-10,22,-10);pen.SetDashStyle(DashStyleDash);line(-22,9,22,9);g.FillEllipse(&brush,-4.f,-14.f,8.f,8.f);break;
    case 151:{
        line(-21,-15,-5,-4);line(5,4,21,15);line(-21,15,-5,4);line(5,-4,21,-15);
        PointF diamond[]={{0,-8},{8,0},{0,8},{-8,0}};g.DrawPolygon(&pen,diamond,4);break;}
    case 110:line(-16,14,14,-16);for(int i=0;i<6;i++){double a=i*pi/3;line(float(11+7*std::cos(a)),float(-11+7*std::sin(a)),float(11+12*std::cos(a)),float(-11+12*std::sin(a)));}break;
    case 111:case 112:case 113:g.DrawRectangle(&pen,-20.f,-16.f,35.f,34.f);ring(6,false);if(id==112){line(13,4,13,22);line(4,13,22,13);}break;
    case 120:arrow();g.DrawEllipse(&pen,-23.f,-18.f,10.f,10.f);g.DrawPolygon(&pen,std::vector<PointF>{{12,-15},{18,-22},{24,-15},{18,-8}}.data(),4);break;
    case recognizeMechanismCommand:
        for(float x:{-22.f,22.f})for(float y:{-19.f,19.f}){
            line(x,y,x+(x<0?8:-8),y);line(x,y,x,y+(y<0?7:-7));
        }
        line(-10,0,10,0);line(10,0,5,-5);line(10,0,5,5);
        g.DrawEllipse(&pen,-18.f,-3.f,6.f,6.f);g.DrawEllipse(&pen,12.f,-3.f,6.f,6.f);
        break;
    case 182:arrow();g.DrawEllipse(&pen,-19.f,-19.f,10.f,10.f);g.DrawEllipse(&pen,9.f,9.f,10.f,10.f);break;
    case 183:line(-19,-18,-19,18);line(-19,18,21,18);line(-15,9,-3,-9);line(-3,-9,7,3);line(7,3,19,-14);break;
    case 169:line(-22,0,-5,0);line(-5,0,15,-15);line(-5,0,15,15);line(15,-15,6,-15);line(15,-15,15,-6);line(15,15,6,15);line(15,15,15,6);break;
    case 143:case 144:
        g.DrawRectangle(&pen,-18.f,-15.f,36.f,30.f);line(-8,0,8,0);
        if(id==143)line(0,-8,0,8);
        break;
    case 145:case 147:
        g.DrawEllipse(&pen,-23.f,-4.f,8.f,8.f);g.DrawEllipse(&pen,15.f,-4.f,8.f,8.f);
        if(id==145){line(-15,0,15,0);line(15,0,8,-5);line(15,0,8,5);}
        else{line(-15,0,-7,0);line(7,0,15,0);line(-5,-5,5,5);line(-5,5,5,-5);}
        break;
    case 146:
        g.DrawRectangle(&pen,-21.f,-15.f,17.f,26.f);g.DrawRectangle(&pen,4.f,-15.f,17.f,26.f);
        line(-13,18,13,18);line(13,18,7,14);break;
    case 149:arrow();line(-19,-15,19,-15);line(-19,-20,-19,-10);line(19,-20,19,-10);break;
    case 135:line(20,0,1,0);line(1,0,-18,-14);line(1,0,-18,14);
        line(-18,-14,-10,-13);line(-18,-14,-15,-7);line(-18,14,-10,13);line(-18,14,-15,7);break;
    case 136:
        line(-22,13,-9,13);line(9,-13,22,-13);line(-9,13,9,-13);
        g.FillEllipse(&brush,-12.f,10.f,6.f,6.f);g.FillEllipse(&brush,6.f,-16.f,6.f,6.f);break;
    case 138:g.DrawRectangle(&pen,-15.f,-20.f,30.f,40.f);
        line(-8,-10,8,-10);line(-8,-2,8,-2);line(-8,6,5,6);line(-8,13,5,13);break;
    case 139:arrow();line(-10,-17,10,-17);line(0,-17,0,-6);break;
    case 142:{
        const Color colors[]={Color(221,76,65),Color(57,157,130),Color(66,118,222)};
        for(int i=0;i<3;i++){SolidBrush swatch(colors[i]);g.FillRectangle(&swatch,-20.f+14*i,-7.f,12.f,14.f);}
        break;}
    case 186:g.DrawRectangle(&pen,-21.f,-15.f,36.f,30.f);
        line(-21,-8,15,-8);line(-3,10,21,-14);line(-3,10,2,9);break;
    case bondAnimationCommand:{
        g.DrawEllipse(&pen,-22.f,-15.f,9.f,9.f);
        g.DrawEllipse(&pen,13.f,-15.f,9.f,9.f);
        pen.SetDashStyle(DashStyleDash);line(-13,-10.5f,13,-10.5f);
        PointF play[]={{-6,-1},{-6,19},{11,9}};g.FillPolygon(&brush,play,3);break;}
    case 140:case 141:{
        PointF folder[]={{-20,-14},{-5,-14},{0,-9},{20,-9},{20,16},{-20,16}};
        g.DrawPolygon(&pen,folder,6);
        if(id==140){line(-10,5,10,5);line(10,5,5,0);line(10,5,5,10);}
        else{line(-12,9,-5,1);line(-5,1,3,7);line(3,7,12,-2);}
        break;}
    case 121:line(-15,17,16,-15);line(-15,17,-18,22);line(-18,22,-10,20);line(12,-18,19,-11);break;
    case 122:arrow();line(0,-22,0,-8);line(-7,-15,7,-15);break;
    case 124:g.DrawRectangle(&pen,-18.f,-19.f,31.f,36.f);line(3,-19,13,-9);
        line(3,-19,3,-9);line(3,-9,13,-9);line(20,7,-7,7);line(-7,7,-1,1);line(-7,7,-1,13);break;
    case 123:g.DrawRectangle(&pen,-17.f,-16.f,30.f,35.f);g.DrawRectangle(&pen,-8.f,-21.f,14.f,8.f);line(-9,2,19,2);line(19,2,12,-4);line(19,2,12,8);break;
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
const std::vector<std::wstring> toolNames={L"선택·이동 [Space]",L"단일 결합 [x]",L"이중 결합",L"삼중 결합",L"쐐기 결합",L"해시 결합",L"5각 고리",L"6각 고리",L"벤젠 [j]",L"지우개",L"반응 화살표 [e]",L"전자쌍 이동 화살표",L"단전자 이동 화살표",L"텍스트 [t] · 클릭하여 입력",L"이미지 삽입",L"직선 [l]",L"공명 화살표 ↔ [r]",L"공명 결합 (실선+파선) [a]",L"오비탈 · 드래그: 방향·크기",L"상호작용 점선",L"점선 단일 결합"};
void toolButton(Graphics& g,int id,RectF r,bool active){
    hits.push_back({id,r});bool hovered=r.Contains(float(mouse.x),float(mouse.y));appearance::rounded(g,r,7,active?accent:hovered?appearance::palette().hover:appearance::palette().raised);
    std::wstring key=id==200?L"Space":id==201?L"x":id==208?L"j":id==210?L"e":id==213?L"t":id==215?L"l":id==216?L"r":id==217?L"a":id==218?L"오비탈":id==219?L"설정":id==dottedBondCommand?L"결합":id==interactionToolCommand?L"점선":L"";
    icon(g,id,{r.X+10,r.Y+1,r.Width-20,23},active?appearance::palette().onAccent:ink);
    if(!key.empty()){
        RectF strip{r.X+2,r.Y+r.Height-18,r.Width-4,18};
        ui::drawLabel(g,key,strip,id==218||id==219?12.f:13.f,active?appearance::palette().onAccent:appearance::palette().muted,true,true);
    }
}
void button(Graphics& g,int id,std::wstring label,RectF r,bool active){
    hits.push_back({id,r});
    appearance::rounded(g,r,6,active?accent:r.Contains(float(mouse.x),float(mouse.y))?appearance::palette().hover:appearance::palette().raised);
    const auto color=active?appearance::palette().onAccent:ink;
    // Narrow paired buttons need their full width for readable Korean labels.
    const bool pictogram=id>=110&&id<=150&&r.Width>=170;
    if(pictogram)icon(g,id,{r.X+8,r.Y+(r.Height-22)/2,22,22},color);
    RectF caption{r.X+(pictogram?36.f:6.f),r.Y,r.Width-(pictogram?42.f:12.f),r.Height};
    ui::drawLabel(g,label,caption,14.f,color,active,true);
}

HWND tooltip=nullptr;bool tipActive=false;RECT tipRect{};int tipCommand=0;std::wstring tipText;
std::wstring helpText(int id){
    if(id==131)return L"에너지 표 편집\n상태 이름과 에너지를 수정하거나 Excel 표를 붙여넣습니다. 경로 이름은 열 제목을 더블클릭해 바꿉니다.";
    if(id==151)return L"스핀 교차점\n교차 구간과 두 스핀 경로를 이름으로 고른 뒤 교차 에너지를 입력합니다. TS 양쪽에 각각 추가할 수 있습니다.";
    if(id==spinMultiplicityPlotCommand)return L"스핀 다중도 비교\n다중도(2S + 1)별 에너지를 독립 준위로 비교합니다. 에너지 표에서 다중도 이름과 값을 편집하세요.";
    if(id==recognizeMechanismCommand)return L"현재 파일 → 메커니즘 편집\n열린 그림에서 구조, 반응 화살표, 이름과 에너지를 인식합니다. 별도 창에서 원본과 비교하고 단계·연결·배치를 수정한 뒤 적용합니다.";
    if(id==143)return L"단계 추가\n기본값은 맨 끝입니다. 위치 번호를 바꾸면 기존 단계 앞에 삽입합니다.";
    if(id==144)return L"단계 삭제\n단계 번호를 지정하고 그 안의 구조를 캔버스에 남길지 선택합니다.";
    if(id==145)return L"드래그로 연결\n연결 모드를 켠 뒤 시작 칸에서 도착 칸으로 드래그합니다. 다시 누르거나 Esc를 눌러 종료합니다.";
    if(id==146)return L"두 단계 합치기\n지정한 단계의 구조와 연결을 다른 단계로 옮깁니다.";
    if(id==147)return L"선택 연결 삭제\n삭제할 메커니즘 화살표를 먼저 클릭하세요.";
    if(id==149)return L"화살표 길이\n별도 편집창의 슬라이더를 움직이면 길이와 배치가 즉시 바뀝니다. 자동 길이는 추가 설정에서 선택합니다.";
    if(id==140)return L"메커니즘 예제·도움말\n직선·촉매 순환·직선에서 순환·분기와 합류 예제를 설명과 함께 새 탭에 엽니다.";
    if(id==141)return L"에너지 그래프 예제\n현재 탭을 예제로 바꾸며 Ctrl+Z로 복원할 수 있습니다.";
    if(id==186)return L"구조 고치기\n선택한 단계의 구조만 독립 창에서 수정합니다. 적용하면 해당 구조만 교체하며 Ctrl+Z로 되돌릴 수 있습니다.";
    if(id==187)return L"취소\n독립 창의 수정을 버리고 메커니즘으로 돌아갑니다.";
    if(id==185)return L"내 프로젝트\n프로젝트 생성·열기·삭제, 구조·CDXML·이미지 보관, 이름·노트 편집과 드래그 순서 변경을 지원합니다.";
    if(id==184)return L"배경 · 테마\n6가지 작업실 분위기와 도트·격자·줄노트·등각·육각 문양을 선택합니다. 문서와 내보내기에는 영향을 주지 않습니다.";
    if(id==156)return L"텍스트 색상표\n단계 이름·설명을 편집하는 중에 누르면 해당 텍스트의 색을 바꿉니다. 반응 블록을 선택하면 블록 전체 텍스트에 적용합니다. 일반 텍스트도 지원합니다. Ctrl+Z: 되돌리기.";
    if(id==155)return L"원자 색상표\n원자를 선택한 뒤 색상표에서 색을 고르면 선택한 원자의 색을 바꿉니다. 여러 원자를 한 번에 변경할 수 있습니다. Ctrl+Z: 되돌리기.";
    if(id==218)return L"오비탈 그리기\n클릭: 기본 크기 / 드래그: 중심에서 방향·크기 지정\n원자 위에서 시작하면 원자 중심에 놓습니다. Shift: 30° 단위\n옆의 설정 버튼에서 s·p·d·혼성 로브와 위상 표시를 선택합니다.\nSpace로 선택 후 이동·회전·크기 조정, Ctrl+Z로 되돌리기를 지원합니다.";if(id==219)return L"오비탈 설정\ns·p·d·혼성 로브 / 흑백 위상·윤곽선·청색/적색 위상을 선택합니다.";if(id==182||id==183)return L"작업별 도구\n메커니즘과 그래프의 추가·편집·배치·예제를 각각 한곳에서 찾습니다.";if(id>=175&&id<=181)return L"이미지·그래프 레이어\n레이어를 잠그면 그림은 유지하면서 클릭·텍스트 편집·드래그·삭제를 막습니다. 이미지만 편집 / 그래프만 편집으로 빠르게 전환하세요. 잠금 상태는 문서에 저장되며 Ctrl+Z로 되돌릴 수 있습니다.";
    if(id==162)return L"반시계 방향 90° 회전\n선택한 구조를 왼쪽으로 90° 회전합니다. Ctrl+Z: 되돌리기.";
    if(id==163)return L"시계 방향 90° 회전\n선택한 구조를 오른쪽으로 90° 회전합니다. Ctrl+Z: 되돌리기.";
    if(id>=160&&id<=164)return L"선택 구조 회전·반전\n원자나 결합을 선택한 뒤 실행하세요. 구조의 길이와 입체 표시를 보존하며, 각도 회전은 숫자로 입력할 수 있습니다. Ctrl+Z: 되돌리기.";
    if(id>=170&&id<=174){static const wchar_t* names[]={L"템플릿 전체",L"리간드",L"배위 착물",L"기질",L"산·시약"};return std::wstring(names[id-170])+L"\n구조 그림을 살펴보고 큰 미리보기를 확인한 뒤 삽입할 수 있습니다.";}
    if(id==dottedBondCommand)return L"점선 단일 결합\n선택한 결합을 점선으로 바꿉니다. 기존 결합을 클릭하거나 드래그하여 새 결합을 그립니다. 결합 위 1: 실선으로 복원.";
    if(id==interactionToolCommand)return L"상호작용 점선\n수소결합 등을 표시할 두 지점 사이를 드래그하세요. Shift: 수평·수직.";
    if(id==217)return L"공명 결합 (실선+파선) [a]\n선택한 결합들을 변환하거나 클릭·드래그하여 그립니다. 같은 결합 재클릭: 파선 위치 전환.";if(id==216)return L"공명 화살표 ↔ [r]\n드래그하여 공명 구조 사이에 양방향 화살표를 그립니다. Shift: 수평·수직.";if(id==215)return L"직선 [l]\n드래그하여 화살촉 없는 직선을 그립니다. Shift: 수평·수직.";
    if(id>=200&&id<=214){static const std::vector<std::wstring> detail={L"클릭하거나 드래그하여 객체를 선택·이동합니다. Shift: 선택 추가, Ctrl+객체 드래그: 복제.",L"원자를 클릭하거나 드래그하여 단일 결합을 그립니다.",L"드래그하여 이중 결합을 그립니다. 기존 결합을 클릭하면 차수를 바꿉니다.",L"드래그하여 삼중 결합을 그립니다.",L"앞으로 나오는 입체 결합을 그립니다. 같은 결합을 다시 클릭하면 방향을 바꿉니다.",L"뒤로 들어가는 입체 결합을 그립니다. 같은 결합을 다시 클릭하면 방향을 바꿉니다.",L"빈 공간에 5각 고리를 삽입하거나 원자·결합에 고리를 연결합니다.",L"빈 공간에 6각 고리를 삽입하거나 원자·결합에 고리를 연결합니다.",L"방향족 벤젠 고리를 그립니다. 원자·결합 위에서는 고리를 연결합니다.",L"클릭한 원자·결합·그리기 객체를 삭제합니다. Ctrl+Z로 복원할 수 있습니다.",L"드래그하여 반응 화살표를 그립니다. 원자에서 원자로 연결하면 전자 이동 곡선으로 자동 전환됩니다. Shift: 수평·수직.",L"전자쌍의 이동을 곡선과 전체 화살촉으로 표시합니다. 원자·결합선과 여백을 두고 작게 휘도록 배치합니다. 선택 후 점을 드래그하면 곡선을 수정합니다.",L"전자 하나의 이동을 곡선과 반쪽 화살촉으로 표시합니다. 원자·결합선과 약간 떨어져 배치합니다. 선택 후 점을 드래그하면 곡선을 수정합니다.",L"캔버스나 기존 텍스트를 클릭하여 입력합니다. Ctrl+Enter: 완료, Esc: 취소.",L"GIF/PNG/JPEG 원본 이미지를 삽입합니다. GIF 애니메이션과 원본 데이터는 문서에 보관합니다."};return toolNames[id-200]+L"\n"+detail[id-200];}
    if(id>=301&&id<=418)return ui::wide(symbol(id-300))+L" 원소 선택\n다음 원자를 이 원소로 그립니다. 원자를 선택한 상태에서는 해당 원소로 바꿉니다.";
    static const std::map<int,std::wstring> help={
      {100,L"새 문서 (Ctrl+N)\n새 문서를 만듭니다."},{101,L"열기 (Ctrl+O)\nYoungDraw 또는 CDXML 문서를 엽니다."},{102,L"저장 (Ctrl+S)\n구조·그래프·이미지와 편집 정보를 저장합니다."},{103,L"SVG 내보내기\n논문용 벡터 그림으로 저장합니다."},{106,L"JACS 출판용 SVG\n240/504 pt 열 너비를 선택하고 출력 시 글자·선 두께·그림 높이를 검사합니다."},{108,L"CDXML 내보내기\nChemDraw에서 편집할 수 있는 문서로 저장합니다."},{104,L"실행 취소 (Ctrl+Z)\n직전 편집을 되돌립니다."},{105,L"다시 실행 (Ctrl+Shift+Z)\n취소한 편집을 다시 적용합니다."},{107,L"화면 맞춤\n전체 그림이 보이도록 확대·축소합니다."},
      {110,L"분자 구조 정리 (Ctrl+Shift+K)\n결합 길이와 고리 모양을 정돈합니다. 입체 배치는 보존합니다."},{111,L"템플릿\n기본 구조 또는 저장한 개인 템플릿을 삽입합니다."},{112,L"템플릿 보관\n선택한 객체를 재사용할 템플릿 파일로 저장합니다."},{113,L"구조 파일 삽입\nCDXML/.ydraw 파일을 현재 문서에 삽입합니다."},
      {bondAnimationCommand,L"결합 애니메이션 GIF 만들기\n메커니즘의 여러 단계 또는 두 구조의 결합 변화를 반복 GIF로 저장합니다."},
      {120,L"반응 메커니즘 추가\n구조·단계 이름·화살표 설명을 담는 반응 블록을 만듭니다."},{121,L"선택 항목 편집\n메커니즘은 표로, 일반 객체는 속성창으로 편집합니다."},{122,L"반응 단계 추가\n선택한 메커니즘의 마지막에 새 단계를 추가합니다."},{123,L"구조 붙여넣기\n선택한 구조 삽입 영역에 복사한 구조·이미지를 크기와 위치를 맞춰 넣습니다."},{124,L"구조 파일 삽입\n선택한 구조 삽입 영역을 CDXML/.ydraw 또는 GIF/PNG/JPEG 파일로 채웁니다."},
      {125,L"자동 정렬\n선택한 구조나 반응 블록의 크기·단계 간격·이름 위치를 정돈합니다."},{126,L"촉매 순환 배치\n반응 단계를 원형으로 배치하고 마지막에서 첫 단계로 연결합니다."},{127,L"메커니즘 설정\n단위, 가로·순환 배치, 정방향·평형 화살표를 바꿉니다."},
      {130,L"에너지 그래프 추가\n여러 경로를 비교하는 에너지 프로필을 만듭니다."},{132,L"경로 추가\n선택한 그래프에 비교할 에너지 경로를 추가합니다."},{135,L"양쪽 분기 추가\n첫 상태를 중앙에 두고 기존 경로는 오른쪽, 새 경로는 왼쪽으로 이어집니다."},{133,L"경로 스타일\n이름·색·선 종류·굵기·표식을 바꿉니다."},{134,L"그림 영역 표시\n메커니즘과 그래프의 구조 삽입 영역을 표시하거나 숨깁니다. 내보내기에는 포함되지 않습니다."},{138,L"출판용 비율·여백 정돈\n선택 그래프의 가로·세로 비율과 글자 크기를 조정합니다. 데이터·색·선·축 범위는 유지합니다."},{139,L"반응 이름·화살표\n그래프 x축 위에 표시할 반응 이름과 화살표의 시작·끝 범위(0~100%)를 편집합니다."},{136,L"화살표 자석 배치\n준위의 왼쪽 끝·중앙·오른쪽 끝에서 두 점을 클릭하여 화살표 범위를 정합니다."},{142,L"구간별 색상\n같은 경로 안에서 준위·표식과 연결선 색상을 구간별로 설정합니다."},{137,L"그래프·축 설정\n제목·단위·크기·그래프 형태·y축 범위·눈금 간격·글자 크기·축 글자색·첨자를 설정합니다."},
      {140,L"메커니즘 예제\n영어 문구가 들어간 반응 메커니즘 예제를 엽니다. Ctrl+Z로 이전 문서를 복원합니다."},{141,L"에너지 그래프 예제\n구조가 연결된 에너지 프로필 예제를 엽니다. Ctrl+Z로 이전 문서를 복원합니다."},{150,L"Arial 굵게 / 2 pt\n14 pt 라벨과 2 pt 결합선을 적용하고 구조 크기를 맞춥니다."}};auto found=help.find(id);return found==help.end()?L"":found->second;
}
void hideTooltip(){KillTimer(win,911);if(!tooltip||!tipActive)return;TOOLINFOW info{};info.cbSize=TTTOOLINFOW_V1_SIZE;info.hwnd=win;info.uId=1;SendMessageW(tooltip,TTM_TRACKACTIVATE,FALSE,(LPARAM)&info);SendMessageW(tooltip,TTM_DELTOOLW,0,(LPARAM)&info);tipActive=false;tipCommand=0;}
void showTooltip(){KillTimer(win,911);if(!tipActive||drawing||dragging||boxing||panning||GetForegroundWindow()!=win)return;POINT point{};GetCursorPos(&point);POINT client=point;ScreenToClient(win,&client);if(!PtInRect(&tipRect,client))return;TOOLINFOW info{};info.cbSize=TTTOOLINFOW_V1_SIZE;info.hwnd=win;info.uId=1;SendMessageW(tooltip,TTM_TRACKPOSITION,0,MAKELPARAM(point.x+16,point.y+22));SendMessageW(tooltip,TTM_TRACKACTIVATE,TRUE,(LPARAM)&info);}
void updateTooltip(){if(!tooltip)return;if(drawing||dragging||boxing||panning){hideTooltip();return;}const Hit* hit=nullptr;for(auto& h:hits)if(h.box.Contains(float(mouse.x),float(mouse.y))){hit=&h;break;}if(!hit){hideTooltip();return;}RECT rect{LONG(hit->box.X),LONG(hit->box.Y),LONG(hit->box.X+hit->box.Width),LONG(hit->box.Y+hit->box.Height)};if(tipActive&&tipCommand==hit->id&&EqualRect(&rect,&tipRect))return;hideTooltip();tipText=helpText(hit->id);if(tipText.empty())return;tipRect=rect;tipCommand=hit->id;TOOLINFOW info{};info.cbSize=TTTOOLINFOW_V1_SIZE;info.hwnd=win;info.uId=1;info.uFlags=TTF_TRACK|TTF_ABSOLUTE;info.rect=rect;info.lpszText=tipText.data();tipActive=SendMessageW(tooltip,TTM_ADDTOOLW,0,(LPARAM)&info)!=0;if(tipActive)SetTimer(win,911,450,nullptr);}
void createTooltip(){INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,win,nullptr,GetModuleHandleW(nullptr),nullptr);SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,360);SendMessageW(tooltip,TTM_SETDELAYTIME,TTDT_INITIAL,450);SendMessageW(tooltip,TTM_SETDELAYTIME,TTDT_AUTOPOP,15000);SendMessageW(tooltip,TTM_SETDELAYTIME,TTDT_RESHOW,80);}
void menuBar(HWND hwnd){HMENU bar=CreateMenu();auto menu=[&](const wchar_t* label,std::initializer_list<std::pair<int,const wchar_t*>> items){HMENU m=CreatePopupMenu();for(auto& item:items)AppendMenuW(m,MF_STRING,item.first,item.second);AppendMenuW(bar,MF_POPUP|MF_OWNERDRAW,(UINT_PTR)m,label);MENUITEMINFOW info{sizeof(info)};info.fMask=MIIM_STRING;info.dwTypeData=const_cast<wchar_t*>(label);SetMenuItemInfoW(bar,UINT(GetMenuItemCount(bar)-1),TRUE,&info);};menu(L"파일(&F)",{{185,L"내 프로젝트…"},{233,L"최근 프로젝트·문서…"},{100,L"새 문서\tCtrl+N"},{101,L"열기…\tCtrl+O"},{102,L"저장\tCtrl+S"},{112,L"템플릿으로 저장…"},{109,L"현재 탭 닫기\tCtrl+W"},{108,L"CDXML 내보내기…"},{103,L"SVG 내보내기…"},{106,L"JACS 출판용 SVG…"}});menu(L"편집(&E)",{{186,L"구조 고치기 · 독립 창…"},{104,L"실행 취소\tCtrl+Z"},{105,L"다시 실행\tCtrl+Shift+Z"},{198,L"그룹 만들기\tCtrl+G"},{199,L"그룹 해제\tCtrl+Shift+G"},{121,L"선택 항목 편집…"},{110,L"구조 정리\tCtrl+Shift+K"},{153,L"선택 부분을 뒤쪽으로 (옅은 회색)"},{154,L"선택 부분을 앞쪽으로 (검정)"},{160,L"선택 구조 좌우 반전"},{161,L"선택 구조 상하 반전"},{162,L"선택 구조 반시계 90° 회전"},{163,L"선택 구조 시계 90° 회전"},{164,L"선택 구조 각도 지정 회전…\tCtrl+R"},{165,L"선택 구조 크기 조정…\tCtrl+K"},{166,L"벤젠을 Ph로 축약\tCtrl+Shift+P"},{abbreviateGroupsCommand,L"선택 구조의 작용기 약어…"},{mullikenSpinCommand,L"선택 원자의 Mulliken 스핀 값…"}});menu(L"삽입(&I)",{{111,L"템플릿…"},{113,L"구조 파일…"},{dottedBondCommand,L"점선 단일 결합"},{215,L"직선\tl"},{interactionToolCommand,L"상호작용 점선"},{216,L"공명 화살표 ↔\tr"},{217,L"공명 결합 (실선+파선)\ta"},{218,L"오비탈 그리기"},{219,L"오비탈 설정…"},{externalMechanismCommand,L"첫 구조 없이 분기 메커니즘…"},{insertBranchArrowsCommand,L"세로선 분기 화살표…"},{120,L"반응 메커니즘…"},{bondAnimationCommand,L"결합 애니메이션 GIF…"},{130,L"에너지 그래프…"},{spinMultiplicityPlotCommand,L"스핀 다중도 비교…"}});menu(L"배치(&L)",{{143,L"단계 삽입…"},{144,L"단계 제거…"},{146,L"두 단계 합치기…"},{145,L"드래그로 단계 연결"},{147,L"선택한 연결 제거"},{148,L"단계·연결 표 편집…"},{149,L"화살표 길이…"},{recognizeMechanismCommand,L"현재 파일 → 메커니즘 편집…"},{167,L"가져온 메커니즘 정리 (구조 보존)"},{169,L"메커니즘 분기 추가…"},{168,L"화살표 중앙 X 설정…"},{125,L"자동 정렬"},{126,L"촉매 순환 배치"},{127,L"메커니즘 설정…"},{137,L"그래프·축 설정…"},{138,L"출판용 비율·여백 정돈"},{139,L"반응 이름·화살표…"},{136,L"화살표 자석 배치…"},{142,L"그래프 구간별 색상…"},{151,L"스핀 교차점…"},{135,L"그래프 양쪽 분기 추가…"},{190,L"선택 객체 왼쪽 맞춤\tCtrl+Shift+Alt+L"},{191,L"선택 객체 가로 중앙 맞춤\tCtrl+Shift+Alt+C"},{192,L"선택 객체 오른쪽 맞춤\tCtrl+Shift+Alt+R"},{193,L"선택 객체 위쪽 맞춤\tCtrl+Shift+Alt+T"},{194,L"선택 객체 상하 중앙 맞춤\tCtrl+Shift+Alt+M"},{195,L"선택 객체 아래쪽 맞춤\tCtrl+Shift+Alt+B"},{196,L"선택 객체 가로 간격 배분\tCtrl+Shift+Alt+H"},{197,L"선택 객체 세로 간격 배분\tCtrl+Shift+Alt+V"}});menu(L"보기(&V)",{{230,L"명령 검색…\tCtrl+Shift+F"},{231,L"도움말…\tF1"},{232,L"단축키 호환…"},{thirdPartyNoticesCommand,L"오픈소스 라이선스…"},{184,L"배경 · 테마…"},{176,L"레이어 패널"},{107,L"화면 맞춤"},{134,L"그림 영역 표시"},{140,L"메커니즘 예제"},{141,L"에너지 그래프 예제"}});if(isolatedStructureEditing){auto file=GetSubMenu(bar,0);while(GetMenuItemCount(file)>0)DeleteMenu(file,0,MF_BYPOSITION);AppendMenuW(file,MF_STRING,186,L"적용하고 돌아가기\tCtrl+S");AppendMenuW(file,MF_STRING,187,L"취소하고 돌아가기\tCtrl+W");for(int id:{120,130,spinMultiplicityPlotCommand,167,169,insertBranchArrowsCommand,externalMechanismCommand,bondAnimationCommand,recognizeMechanismCommand,186,140,141})EnableMenuItem(bar,id,MF_BYCOMMAND|MF_GRAYED);EnableMenuItem(file,186,MF_BYCOMMAND|MF_ENABLED);}
 SetMenu(hwnd,bar);refreshAppearance();}
void refreshAppearance(){const auto& p=appearance::palette();ink=p.text;muted=p.muted;accent=p.accent;border=p.edge;appearance::titlebar(win);HBRUSH old=menuBackground;menuBackground=CreateSolidBrush(RGB(p.chrome.GetR(),p.chrome.GetG(),p.chrome.GetB()));MENUINFO info{sizeof(info)};info.fMask=MIM_BACKGROUND;info.hbrBack=menuBackground;if(GetMenu(win))SetMenuInfo(GetMenu(win),&info);if(old)DeleteObject(old);DrawMenuBar(win);if(documentTabs)InvalidateRect(documentTabs,nullptr,FALSE);redraw();}

} // namespace yd::editor

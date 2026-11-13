#include "win_mechanism_editor.h"
#include "win_editor_render.h"
#include "win_editor_state.h"
#include "win_dialog_style.h"
#include "win_dialogs.h"
#include "win_ui.h"
#include "mechanism_data.h"
#include "cdxml.h"
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>

namespace yd::editor {
namespace {
struct Editor {
    Document draft,source;
    int owner=0,stage=-1,edge=-1,connectFrom=-1,dragStage=-1;
    bool comparing=false,connecting=false,updating=false,accepted=false,lengthGesture=false;
    HWND window=nullptr,list=nullptr,message=nullptr;
    HFONT font=nullptr;
    ui::DialogColors colors;
    RECT canvas{};
    double scale=1;
    Vec offset,down,start,dragPoint;
    std::vector<Document> undo,redo;
    struct Plus {Vec point;int stage,edge;bool branch;};
    std::vector<Plus> pluses;
    ~Editor(){if(font)DeleteObject(font);}
    Reaction& reaction(){auto it=std::find_if(draft.reactions.begin(),draft.reactions.end(),[&](const auto& r){return r.id==owner;});if(it==draft.reactions.end())throw std::runtime_error("편집할 메커니즘이 없습니다.");return *it;}
    Vec screen(Vec p)const{return p*scale+offset;}
    Vec world(Vec p)const{return (p-offset)*(1/scale);}
    void say(const wchar_t* text){SetWindowTextW(message,text);}
    HWND control(const wchar_t* type,const wchar_t* text,int id,DWORD style=0){
        HWND result=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,(HMENU)INT_PTR(id),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(result,WM_SETFONT,(WPARAM)font,TRUE);return result;
    }
    void checkpoint(){undo.push_back(draft);if(undo.size()>80)undo.erase(undo.begin());redo.clear();}
    void fit(){
        auto sc=makeScene(comparing?source:draft,measure);auto b=sc.bounds;
        scale=std::clamp(std::min((canvas.right-canvas.left-100)/std::max(1.,b.w),
                                 (canvas.bottom-canvas.top-100)/std::max(1.,b.h)),.03,3.);
        offset={double(canvas.left+canvas.right)/2-(b.x+b.w/2)*scale,
                double(canvas.top+canvas.bottom)/2-(b.y+b.h/2)*scale};
        InvalidateRect(window,nullptr,TRUE);
    }
    void layout(){
        RECT r{};GetClientRect(window,&r);int w=r.right,h=r.bottom;
        const int side=std::clamp(w/4,190,260),left=side+20;
        auto place=[&](int id,int x,int y,int width,int height){MoveWindow(GetDlgItem(window,id),x,y,std::max(1,width),std::max(1,height),TRUE);};
        place(10,10,10,side,23);place(11,10,35,side,220);
        place(12,10,71,side,23);place(13,10,96,side,220);
        place(14,10,132,side,23);place(15,10,157,side,220);
        place(16,10,193,side,23);place(17,10,218,side,30);
        place(18,10,252,side,23);place(19,10,278,side,28);
        place(30,10,315,side,27);place(31,10,348,side,27);
        place(32,10,381,side,27);place(33,10,414,side,27);
        place(34,10,447,side,27);place(35,10,480,side,27);
        int listTop=516;MoveWindow(list,10,listTop,side,std::max(1,h-listTop-60),TRUE);
        const int buttonWidth=80;int columns=std::max(1,(w-left-10)/(buttonWidth+6));
        const int buttons[]={40,41,42,43,44,45};
        for(int i=0;i<6;++i)place(buttons[i],left+(i%columns)*86,10+(i/columns)*34,80,28);
        int top=14+((6+columns-1)/columns)*34;
        canvas={left,top,w-10,h-60};
        place(50,left,h-52,w-left-190,42);place(IDOK,w-180,h-48,80,32);place(IDCANCEL,w-90,h-48,80,32);
        fit();
    }
    void sync(){
        updating=true;auto& r=reaction();
        SendMessageW(list,LB_RESETCONTENT,0,0);
        for(size_t i=0;i<r.slots.size();++i){auto label=std::to_wstring(i+1)+L" · "+ui::wide(r.slots[i].label);SendMessageW(list,LB_ADDSTRING,0,(LPARAM)label.c_str());}
        SendMessageW(list,LB_SETCURSEL,stage,0);
        int mode=r.circularMerge?5:r.explicitConnections()?4:r.cyclic?3:r.vertical?1:r.wrapColumns>0?2:0;
        SendDlgItemMessageW(window,11,CB_SETCURSEL,mode,0);
        SetDlgItemTextW(window,13,ui::wide(r.unit).c_str());
        SendDlgItemMessageW(window,15,CB_SETCURSEL,r.arrowType==1?1:0,0);
        SendDlgItemMessageW(window,17,TBM_SETPOS,TRUE,LONG(std::clamp(mechanismArrowLength(r,draft.style),20.,600.)));
        auto length=L"화살표 길이 · "+std::to_wstring(int(mechanismArrowLength(r,draft.style)))+L" pt";
        SetDlgItemTextW(window,16,length.c_str());
        SetDlgItemTextW(window,19,std::to_wstring(r.wrapColumns>0?r.wrapColumns:3).c_str());
        updating=false;InvalidateRect(window,nullptr,TRUE);
    }
    void changed(bool refit=true){sync();if(refit)fit();}
    void arrange(){auto& r=reaction();optimizeMechanism(draft,{owner},r.cyclic);}
    int stageAt(Vec point){auto& r=reaction();auto p=world(point);double best=1e30;int result=-1;for(size_t i=0;i<r.slots.size();++i){auto c=reactionSlot(r,i);if(std::abs(p.x-c.x)<=r.slotWidth/2+6/scale&&std::abs(p.y-c.y)<=r.slotHeight/2+6/scale&&(p-c).length()<best){best=(p-c).length();result=int(i);}}return result;}
    int edgeAt(Vec point){auto sc=makeScene(draft,measure);double best=9;int found=-1;
        auto segment=[&](Vec a,Vec b,int edge){a=screen(a);b=screen(b);auto v=b-a;double n=v.x*v.x+v.y*v.y;double t=n>1e-9?std::clamp(((point-a).x*v.x+(point-a).y*v.y)/n,0.,1.):0;double distance=(point-a-v*t).length();if(distance<best){best=distance;found=edge;}};
        for(const auto& p:sc.items)if(p.owner==owner&&p.reactionArrow>=0){
            if(p.type==Primitive::Line)for(size_t i=1;i<p.points.size();++i)segment(p.points[i-1],p.points[i],p.reactionArrow);
            if(p.type==Primitive::Bezier)for(size_t i=0;i+3<p.points.size();i+=3){Vec previous=p.points[i];for(int j=1;j<=40;++j){double t=j/40.,u=1-t;auto at=p.points[i]*(u*u*u)+p.points[i+1]*(3*u*u*t)+p.points[i+2]*(3*u*t*t)+p.points[i+3]*(t*t*t);segment(previous,at,p.reactionArrow);previous=at;}}
        }return found;
    }
    void add(int from,bool branch){
        if(reaction().slots.size()>=40)throw std::runtime_error("그림 영역은 최대 40개입니다.");
        checkpoint();auto& r=reaction();
        if(branch){makeMechanismGraph(r);r.circularMerge=false;addMechanismBranch(r,size_t(from));stage=int(r.slots.size()-1);}
        else{appendMechanismStage(r,false,size_t(from));stage=int(r.slots.size()-1);}
        edge=-1;arrange();changed();say(L"그림 영역을 추가했습니다. 더블클릭: 이름·에너지 · Delete: 삭제 · Ctrl+Z: 취소");
    }
    void remove(){
        if(comparing)return;
        if(edge>=0){checkpoint();removeMechanismConnection(reaction(),size_t(edge));edge=-1;say(L"선택한 연결을 삭제했습니다. Ctrl+Z로 복구할 수 있습니다.");changed(false);return;}
        if(stage>=0){if(reaction().slots.size()<=1)throw std::runtime_error("메커니즘에는 그림 영역이 한 개 이상 필요합니다.");checkpoint();removeMechanismStage(draft,owner,size_t(stage),-1,true);stage=edge=-1;arrange();changed();say(L"선택한 단계와 그림을 삭제했습니다. Ctrl+Z: 복구");}
    }
    void editText(){
        if(comparing)return;auto& r=reaction();
        if(stage<0&&edge<0){say(L"편집할 그림 영역이나 화살표를 먼저 클릭하세요.");return;}
        if(stage>=0){auto& slot=r.slots.at(size_t(stage));auto result=ui::form(window,L"단계 이름·에너지",{{L"이름",slot.label,false,{},false,4,true},{L"에너지 (S=0:값 / S=1:값)",slot.energy,false,{},false,4,true}});if(result){checkpoint();slot.label=(*result)[0];slot.energy=(*result)[1];arrange();changed();}}
        else{auto copy=r;makeMechanismGraph(copy);auto result=ui::form(window,L"연결 설명·에너지",{{L"화살표 위 설명 / TS 이름",size_t(edge)<copy.above.size()?copy.above[edge]:"",false,{},false,4,true},{L"화살표 아래 에너지",size_t(edge)<copy.below.size()?copy.below[edge]:"",false,{},false,4,true},{L"곡률 (−2~2, 0: 직선)",number(copy.links.at(size_t(edge)).bend)}});if(result){double bend=numeric((*result)[2],-2,2);checkpoint();makeMechanismGraph(r);r.above.resize(r.links.size());r.below.resize(r.links.size());r.above[edge]=(*result)[0];r.below[edge]=(*result)[1];r.links[edge].bend=bend;arrange();changed();}}
    }
    void table(){
        if(comparing)return;auto original=reaction();
        if(!original.explicitConnections()){
            auto rows=ui::table(window,L"이름·에너지 한 번에 편집",{L"구분 (중간체 / TS)",L"이름 / 설명",L"S",L"에너지"},mechanismRows(original),{},true,L"행 추가·삭제도 가능합니다. 그림에서는 + / Delete로 직접 편집합니다.",true);
            if(rows){auto result=mechanismGridData(original,*rows);checkpoint();reaction()=std::move(result);arrange();changed();}
        }else{
            makeMechanismGraph(original);ui::Table cells;
            for(size_t i=0;i<original.slots.size();++i)cells.push_back({"구조",std::to_string(i+1),number(original.slotGrid[i].x),number(original.slotGrid[i].y),original.slots[i].label,original.slots[i].energy,""});
            for(size_t i=0;i<original.links.size();++i)cells.push_back({"화살표",std::to_string(i+1),std::to_string(original.links[i].from+1),std::to_string(original.links[i].to+1),original.above[i],original.below[i],number(original.links[i].bend)});
            auto rows=ui::table(window,L"이름·에너지·연결 한 번에 편집",{L"구분",L"번호",L"열 / 시작",L"행 / 도착",L"이름 / 설명",L"에너지",L"곡률"},cells,{},true,L"좌표와 연결 번호를 함께 편집할 수 있습니다.",false,4);
            if(rows){auto result=mechanismGraphData(original,*rows);checkpoint();reaction()=std::move(result);arrange();changed();}
        }
        stage=edge=-1;sync();
    }
    void setting(int id){
        if(updating||comparing)return;auto old=draft;auto& r=reaction();
        try{
            if(id==11){int mode=int(SendDlgItemMessageW(window,11,CB_GETCURSEL,0,0));
                if(r.explicitConnections()&&mode!=4&&mode!=5){say(L"분기 연결이 있는 그림은 자유 배치를 유지합니다. 그림 영역을 직접 드래그하세요.");sync();return;}
                r.cyclic=mode==3;r.vertical=mode==1;r.wrapColumns=mode==2?3:0;
                if(mode==4||mode==5){makeMechanismGraph(r);r.fixedFrames=true;r.circularMerge=mode==5;if(mode==5&&!arrangeCircularMerge(r,draft.style))throw std::runtime_error("한 곳에서 갈라져 다시 합류하는 두 경로에 사용할 수 있습니다.");}else for(auto& slot:r.slots)slot.startsRow=false;
            }
            if(id==13){wchar_t unit[128]{};GetDlgItemTextW(window,13,unit,128);r.unit=ui::utf8(unit);}
            if(id==15)r.arrowType=int(SendDlgItemMessageW(window,15,CB_GETCURSEL,0,0));
            if(id==19){wchar_t value[16]{};GetDlgItemTextW(window,19,value,16);if(!*value)return;int n=std::stoi(value);if(n<1||n>40)return;if(!r.explicitConnections()&&!r.cyclic&&r.wrapColumns>0){r.wrapColumns=n;r.vertical=false;}}
            if(toNative(draft)!=toNative(old)){undo.push_back(std::move(old));redo.clear();arrange();changed();}
        }catch(...){draft=std::move(old);throw;}
    }
    void length(int value,bool save){
        if(updating||comparing)return;
        if(save)checkpoint();reaction().arrowLength=value;arrange();changed();
    }
    void draw(){
        PAINTSTRUCT ps{};auto dc=BeginPaint(window,&ps);RECT all{};GetClientRect(window,&all);FillRect(dc,&all,colors.panel);
        Graphics g(dc);g.SetSmoothingMode(SmoothingModeAntiAlias);
        SolidBrush white(Color::White);g.FillRectangle(&white,Gdiplus::Rect(INT(canvas.left),INT(canvas.top),INT(canvas.right-canvas.left),INT(canvas.bottom-canvas.top)));
        auto saved=g.Save();g.SetClip(Gdiplus::Rect(INT(canvas.left),INT(canvas.top),INT(canvas.right-canvas.left),INT(canvas.bottom-canvas.top)));
        auto transform=[&](){g.TranslateTransform(float(offset.x),float(offset.y));g.ScaleTransform(float(scale),float(scale));};
        auto sc=makeScene(comparing?source:draft,measure);auto local=g.Save();transform();render(g,sc,false,false);g.Restore(local);
        pluses.clear();
        if(!comparing){auto& r=reaction();Pen active(Color(255,0,139,128),2),frame(Color(150,90,110,125),1);frame.SetDashStyle(DashStyleDash);
            auto plus=[&](Vec p,int from,int link,bool branch){if(p.x<canvas.left+12||p.x>canvas.right-12||p.y<canvas.top+12||p.y>canvas.bottom-12)return;pluses.push_back({p,from,link,branch});g.FillEllipse(&white,float(p.x-9),float(p.y-9),18.f,18.f);g.DrawEllipse(&active,float(p.x-9),float(p.y-9),18.f,18.f);g.DrawLine(&active,float(p.x-4),float(p.y),float(p.x+4),float(p.y));g.DrawLine(&active,float(p.x),float(p.y-4),float(p.x),float(p.y+4));};
            for(size_t i=0;i<r.slots.size();++i){auto p=screen(reactionSlot(r,i));RectF box(float(p.x-r.slotWidth*scale/2),float(p.y-r.slotHeight*scale/2),float(r.slotWidth*scale),float(r.slotHeight*scale));g.DrawRectangle(stage==int(i)?&active:&frame,box);
                if(r.slots[i].members.empty())ui::drawLabel(g,L"그림 영역",{box.X+3,box.Y+3,std::max(1.f,box.Width-6),std::max(1.f,box.Height-6)},11,Color(255,100,110,120),false,true);
                if(r.slots.size()<40&&!connecting){bool terminal=!r.explicitConnections()?i+1==r.slots.size():std::none_of(r.links.begin(),r.links.end(),[&](auto& l){return l.from==int(i);});if(terminal&&!r.cyclic)plus({box.GetRight()+20,p.y},int(i),-1,false);plus({p.x,box.GetBottom()+32},int(i),-1,true);}
            }
            if(r.slots.size()<40&&!connecting){auto graph=r;makeMechanismGraph(graph);for(size_t i=0;i<graph.links.size();++i){if(contains(graph.hiddenArrows,int(i)))continue;auto path=mechanismLinkPath(graph,i,draft.style);if(path.size()<2)continue;auto mid=path.size()==4?path[0]*.125+path[1]*.375+path[2]*.375+path[3]*.125:(path[path.size()-2]+path.back())*.5;plus(screen(mid)+Vec{0,20},-1,int(i),false);}}
            if(edge>=0){auto localState=g.Save();transform();Scene selected;for(auto p:sc.items)if(p.owner==owner&&p.reactionArrow==edge){p.color="#008b80";p.width+=1;selected.items.push_back(p);}render(g,selected);g.Restore(localState);}
            if(connectFrom>=0){auto a=screen(reactionSlot(r,size_t(connectFrom)));g.DrawLine(&active,float(a.x),float(a.y),float(start.x),float(start.y));int target=stageAt(start);if(target>=0&&target!=connectFrom){auto p=screen(reactionSlot(r,size_t(target)));g.DrawEllipse(&active,float(p.x-12),float(p.y-12),24.f,24.f);}}
        }
        if(dragStage>=0&&(dragPoint-down).length()>3){auto& r=reaction();auto p=screen(reactionSlot(r,size_t(dragStage)))+dragPoint-down;Pen ghost(Color(255,0,139,128),2);ghost.SetDashStyle(DashStyleDash);g.DrawRectangle(&ghost,float(p.x-r.slotWidth*scale/2),float(p.y-r.slotHeight*scale/2),float(r.slotWidth*scale),float(r.slotHeight*scale));}
        g.Restore(saved);EndPaint(window,&ps);
    }
    void action(int id){
        if(id==IDOK){accepted=true;DestroyWindow(window);return;}if(id==IDCANCEL){DestroyWindow(window);return;}
        if(id==40){comparing=!comparing;SetDlgItemTextW(window,40,comparing?L"편집 결과":L"원본 보기");fit();return;}
        if(id==41){fit();return;}
        if(comparing){say(L"원본을 보고 있습니다. ‘편집 결과’로 돌아와 수정하세요.");return;}
        if(id==42){if(!undo.empty()){redo.push_back(draft);draft=std::move(undo.back());undo.pop_back();stage=edge=-1;changed();}return;}
        if(id==43){if(!redo.empty()){undo.push_back(draft);draft=std::move(redo.back());redo.pop_back();stage=edge=-1;changed();}return;}
        if(id==44){scale=std::min(6.,scale*1.2);InvalidateRect(window,nullptr,TRUE);return;}
        if(id==45){scale=std::max(.03,scale/1.2);InvalidateRect(window,nullptr,TRUE);return;}
        if(id==30){editText();return;}if(id==31){table();return;}
        if(id==32){connecting=!connecting;connectFrom=-1;say(connecting?L"시작 그림 영역 → 도착 그림 영역을 클릭하거나 드래그하세요. Esc: 종료":L"연결 모드를 종료했습니다.");InvalidateRect(window,nullptr,TRUE);return;}
        if(id==33){if(stage<0){say(L"분기를 시작할 그림 영역을 먼저 클릭하세요.");return;}add(stage,true);return;}
        if(id==35){auto& r=reaction();auto result=ui::form(window,L"여백·글자·자동 길이",{{L"줄 사이 여백 (pt)",number(r.rowGap)},{L"글자 크기 (pt, 0: 자동)",number(r.captionFontSize)},{L"화살표 길이",r.arrowLength==0?"0":"1",false,{L"설명에 맞춰 자동",L"현재 슬라이더 값 유지"}},{L"삭제한 순환 화살표","0",false,{L"현재 상태 유지",L"모두 복원"}}});if(result){double gap=numeric((*result)[0],0,4000),fontSize=numeric((*result)[1],0,100);checkpoint();r.rowGap=gap;r.captionFontSize=fontSize;if((*result)[2]=="0")r.arrowLength=0;if((*result)[3]=="1")r.hiddenArrows.clear();arrange();changed();}return;}
        if(id==34){checkpoint();arrange();changed();say(L"자동 정렬했습니다. Ctrl+Z: 되돌리기");return;}
    }
    static LRESULT CALLBACK procedure(HWND h,UINT msg,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<Editor*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<Editor*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)self);}
        if(!self)return DefWindowProcW(h,msg,wp,lp);
        try{switch(msg){
        case WM_CREATE:{
            self->font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,ui::interfaceFont());
            self->control(L"STATIC",L"배치 · 그림에서 직접 이동 가능",10);
            self->control(L"COMBOBOX",L"",11,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
            for(auto label:{L"→ 직선",L"↓ 세로",L"↪ 여러 줄",L"⟳ 촉매 순환",L"⑂ 분기 · 자유 배치",L"⤵ 원형 분기·합류"})SendDlgItemMessageW(h,11,CB_ADDSTRING,0,(LPARAM)label);
            self->control(L"STATIC",L"에너지 단위 · 표시만 변경",12);
            self->control(L"COMBOBOX",L"",13,CBS_DROPDOWN|WS_VSCROLL|WS_TABSTOP);
            for(auto label:{L"kcal mol^{-1}",L"kJ mol^{-1}",L"eV",L"단위 숨김"})SendDlgItemMessageW(h,13,CB_ADDSTRING,0,(LPARAM)label);
            self->control(L"STATIC",L"화살표 모양",14);self->control(L"COMBOBOX",L"",15,CBS_DROPDOWNLIST|WS_TABSTOP);
            for(auto label:{L"→ 정방향",L"⇌ 평형"})SendDlgItemMessageW(h,15,CB_ADDSTRING,0,(LPARAM)label);
            self->control(L"STATIC",L"화살표 길이",16);self->control(TRACKBAR_CLASSW,L"",17,TBS_HORZ|TBS_NOTICKS|WS_TABSTOP);SendDlgItemMessageW(h,17,TBM_SETRANGE,TRUE,MAKELONG(20,600));
            self->control(L"STATIC",L"여러 줄 · 한 줄의 그림 수 (1~40)",18);self->control(L"EDIT",L"3",19,WS_BORDER|ES_NUMBER|WS_TABSTOP);
            const wchar_t* tools[]={L"선택한 이름·에너지 편집",L"이름·에너지 표",L"연결하기 · 시작 → 도착",L"선택한 단계에서 분기",L"자동 정렬",L"길이 자동 · 여백·글자 설정"};for(int i=0;i<6;++i)self->control(L"BUTTON",tools[i],30+i,BS_OWNERDRAW|WS_TABSTOP);
            self->list=self->control(L"LISTBOX",L"",20,LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_TABSTOP);
            const wchar_t* tools2[]={L"원본 보기",L"화면 맞춤",L"되돌리기",L"다시 실행",L"확대 +",L"축소 −"};for(int i=0;i<6;++i)self->control(L"BUTTON",tools2[i],40+i,BS_OWNERDRAW|WS_TABSTOP);
            self->message=self->control(L"STATIC",L"+ : 추가 / 분기 · 그림 영역 드래그: 이동 · 더블클릭: 이름·에너지 · Delete: 단계·연결 삭제",50);
            self->control(L"BUTTON",L"적용",IDOK,BS_OWNERDRAW|WS_TABSTOP);self->control(L"BUTTON",L"취소",IDCANCEL,BS_OWNERDRAW|WS_TABSTOP);
            self->sync();self->layout();return 0;}
        case WM_SIZE:self->layout();return 0;
        case WM_GETMINMAXINFO:{MONITORINFO m{sizeof(m)};GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&m);reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={std::min(760L,m.rcWork.right-m.rcWork.left),std::min(620L,m.rcWork.bottom-m.rcWork.top)};return 0;}
        case WM_ERASEBKGND:return TRUE;
        case WM_PAINT:self->draw();return 0;
        case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:return self->colors.control(msg,(HDC)wp);
        case WM_DRAWITEM:self->colors.draw(*reinterpret_cast<DRAWITEMSTRUCT*>(lp),wp==IDOK);return TRUE;
        case WM_COMMAND:{int id=LOWORD(wp),code=HIWORD(wp);
            if(id==20&&code==LBN_SELCHANGE){self->stage=int(SendMessageW(self->list,LB_GETCURSEL,0,0));self->edge=-1;InvalidateRect(h,nullptr,TRUE);return 0;}
            if(id==20&&code==LBN_DBLCLK){self->editText();return 0;}
            if((id==11||id==13||id==15)&&(code==CBN_SELCHANGE||code==CBN_EDITCHANGE)){if(id==13&&code==CBN_SELCHANGE){int n=int(SendDlgItemMessageW(h,13,CB_GETCURSEL,0,0));wchar_t text[128]{};SendDlgItemMessageW(h,13,CB_GETLBTEXT,n,(LPARAM)text);SetDlgItemTextW(h,13,n==3?L"":text);}self->setting(id);return 0;}
            if(id==19&&code==EN_KILLFOCUS){self->setting(id);return 0;}
            if(code==BN_CLICKED)self->action(id);return 0;}
        case WM_HSCROLL:if((HWND)lp==GetDlgItem(h,17)){const int code=LOWORD(wp);if(!self->lengthGesture){self->checkpoint();self->lengthGesture=true;}self->length(int(SendDlgItemMessageW(h,17,TBM_GETPOS,0,0)),false);if(code==TB_ENDTRACK)self->lengthGesture=false;}return 0;
        case WM_LBUTTONDOWN:{Vec point{double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))};POINT pt{LONG(point.x),LONG(point.y)};if(!PtInRect(&self->canvas,pt)||self->comparing)return 0;
            for(auto button:self->pluses)if((button.point-point).length()<11){if(button.edge>=0){self->checkpoint();auto& r=self->reaction();self->stage=int(splitMechanismLink(r,size_t(button.edge)));self->arrange();self->changed();}else self->add(button.stage,button.branch);return 0;}
            int stage=self->stageAt(point);if(self->connecting){if(stage<0){self->say(L"그림 영역 안에서 시작·도착을 지정하세요.");return 0;}if(self->connectFrom>=0&&self->connectFrom!=stage){self->checkpoint();connectMechanismStages(self->reaction(),self->connectFrom,stage);self->connectFrom=-1;self->changed(false);self->say(L"연결했습니다. 다른 두 단계를 선택하거나 Esc로 연결 모드를 종료하세요.");return 0;}self->connectFrom=stage;self->start=point;SetCapture(h);InvalidateRect(h,nullptr,TRUE);return 0;}
            self->stage=stage;self->edge=stage<0?self->edgeAt(point):-1;self->sync();SetFocus(h);
            if(stage>=0){self->dragStage=stage;self->down=self->dragPoint=point;self->start=reactionSlot(self->reaction(),size_t(stage));SetCapture(h);}return 0;}
        case WM_MOUSEMOVE:{Vec point{double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))};if(self->dragStage>=0){self->dragPoint=point;InvalidateRect(h,nullptr,TRUE);}if(self->connectFrom>=0){self->start=point;InvalidateRect(h,nullptr,TRUE);}return 0;}
        case WM_LBUTTONUP:{Vec point{double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))};ReleaseCapture();
            if(self->connecting&&self->connectFrom>=0){int target=self->stageAt(point);if(target>=0&&target!=self->connectFrom){self->checkpoint();connectMechanismStages(self->reaction(),self->connectFrom,target);self->connectFrom=-1;self->changed(false);self->say(L"연결했습니다. 화살표 클릭 → Delete: 삭제 · 자동 정렬: 재배치");}return 0;}
            if(self->dragStage>=0){int moved=self->dragStage;self->dragStage=-1;if((point-self->down).length()>3){self->checkpoint();auto& r=self->reaction();makeMechanismGraph(r);r.fixedFrames=true;r.circularMerge=false;auto delta=(point-self->down)*(1/self->scale);double stepX=r.spacing;double stepY=r.slotHeight+std::max(r.rowGap,mechanismCaptionHeight(r,Style{})+80);auto originalPosition=reactionSlot(r,size_t(moved));r.slotGrid[moved]=r.slotGrid[moved]+Vec{delta.x/std::max(1.,stepX),delta.y/std::max(1.,stepY)};self->draft.translate(r.slots[moved].members,reactionSlot(r,size_t(moved))-originalPosition);self->arrange();self->changed(false);self->say(L"그림 영역을 이동했습니다. Ctrl+Z: 되돌리기");}}return 0;}
        case WM_LBUTTONDBLCLK:self->editText();return 0;
        case WM_MOUSEWHEEL:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(h,&p);if(PtInRect(&self->canvas,p)){Vec before=self->world({double(p.x),double(p.y)});self->scale=std::clamp(self->scale*(GET_WHEEL_DELTA_WPARAM(wp)>0?1.15:1/1.15),.03,6.);self->offset=Vec{double(p.x),double(p.y)}-before*self->scale;InvalidateRect(h,nullptr,TRUE);}return 0;}
        case WM_CLOSE:DestroyWindow(h);return 0;
        }}catch(const std::exception& error){MessageBoxW(h,ui::wide(error.what()).c_str(),L"메커니즘 편집",MB_OK|MB_ICONWARNING);}
        return DefWindowProcW(h,msg,wp,lp);
    }
};
}
std::optional<Document> mechanismEditor(HWND parent,const Document& draft,int owner,const Document* original){
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW wc{};wc.lpfnWndProc=Editor::procedure;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawMechanismEditor";wc.style=CS_DBLCLKS;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    Editor editor;editor.draft=draft;editor.source=original?*original:draft;editor.owner=owner;
    MONITORINFO m{sizeof(m)};GetMonitorInfoW(MonitorFromWindow(parent,MONITOR_DEFAULTTONEAREST),&m);
    int width=std::min(1280L,m.rcWork.right-m.rcWork.left),height=std::min(900L,m.rcWork.bottom-m.rcWork.top);auto r=ui::ownedWindowRect(parent,width,height);
    HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,original?L"메커니즘 변환 미리보기 · 적용 전 편집":L"메커니즘 편집 · 그림에서 직접 수정",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,r.left,r.top,width,height,parent,nullptr,wc.hInstance,&editor);
    if(!window)throw std::runtime_error("메커니즘 편집창을 열 수 없습니다.");appearance::titlebar(window);EnableWindow(parent,FALSE);ShowWindow(window,SW_SHOW);
    MSG msg{};while(IsWindow(window)){
        auto result=GetMessageW(&msg,nullptr,0,0);if(result<=0){if(result==0)PostQuitMessage(int(msg.wParam));break;}
        if(msg.message==WM_KEYDOWN){bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;bool edit=false;wchar_t cls[32]{};GetClassNameW(GetFocus(),cls,32);edit=wcscmp(cls,L"Edit")==0;
            if(!edit&&(msg.wParam==VK_DELETE||msg.wParam==VK_BACK)){try{editor.remove();}catch(const std::exception& e){MessageBoxW(window,ui::wide(e.what()).c_str(),L"메커니즘 편집",MB_OK|MB_ICONWARNING);}continue;}
            if(!edit&&ctrl&&(msg.wParam=='Z'||msg.wParam=='Y')){editor.action(msg.wParam=='Z'?42:43);continue;}
            if(msg.wParam==VK_ESCAPE&&(editor.connecting||editor.comparing)){editor.connecting=false;editor.connectFrom=-1;if(editor.comparing)editor.action(40);InvalidateRect(window,nullptr,TRUE);continue;}
            if(!edit&&msg.wParam==VK_F2){editor.editText();continue;}
        }
        if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    if(IsWindow(window))DestroyWindow(window);EnableWindow(parent,TRUE);SetActiveWindow(parent);
    if(editor.accepted)return std::move(editor.draft);return {};
}
}

#include "win_editor_commands.h"
#include "win_editor_pointer.h"
#include "win_editor_reaction.h"
#include "win_editor_state.h"
#include "win_editor_structure.h"
#include "win_editor_text.h"
#include "cdxml.h"
#include "mechanism_data.h"
#include "win_dialogs.h"
#include "win_ui.h"
#include <sstream>

namespace yd::editor {

bool mechanismConnecting=false,mechanismConnectionDrag=false;
int connectionOwner=0,connectionFrom=-1,connectionEdge=-1,connectionHandle=-1;
int selectedCycleOwner=0,selectedCycleArrow=-1;

void editBranchedReaction(int id){
    auto* ptr=reaction(id);if(!ptr)return;Document before=doc;Reaction original=*ptr;makeMechanismGraph(original);ui::Table rows;
    for(size_t i=0;i<original.slots.size();i++)rows.push_back({"구조",std::to_string(i+1),"","",original.slots[i].label,original.slots[i].energy});
    for(size_t i=0;i<original.links.size();i++)rows.push_back({"화살표",std::to_string(i+1),std::to_string(original.links[i].from+1),std::to_string(original.links[i].to+1),i<original.above.size()?original.above[i]:"",i<original.below.size()?original.below[i]:""});
    auto update=[&](const ui::Table& cells)->std::wstring{
        Reaction edited=mechanismGraphData(original,cells);
        doc=before;*reaction(id)=edited;optimizeMechanism(doc,{id},false);redraw();UpdateWindow(win);return {};
    };
    auto result=ui::table(win,L"메커니즘 분기·프레임 편집",{L"구분",L"번호",L"시작 구조",L"도착 구조",L"이름 / TS 설명",L"에너지 (S=0 / 1 / 2)"},rows,update,true,L"행 추가·삭제 가능 · 새 행의 번호는 비워 두세요 · 시작 0: 공통 세로선 · 삭제한 단계의 구조는 캔버스에 유지",false,4);
    if(result){if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();fit();}}else{doc=before;redraw();}
}
static bool editLinearReaction(const Document& base, const Reaction& original, Document& edited, bool preview){
    auto update=[&](const ui::Table& rows)->std::wstring{
        Document candidate=base;
        *std::find_if(candidate.reactions.begin(),candidate.reactions.end(),[&](const Reaction& r){return r.id==original.id;})
            =mechanismGridData(original,rows);
        optimizeMechanism(candidate,{original.id},original.cyclic);
        edited=std::move(candidate);
        if(preview){doc=edited;redraw();UpdateWindow(win);}
        return {};
    };
    return ui::table(win,L"메커니즘 단계 편집 · 중간체 → TS → 중간체",
        {L"구분 (중간체 / TS)",L"이름 / TS 설명 (예: TS1 / 산화)",L"S (예: 0 / 2)",L"에너지 (숫자 또는 ?.?)"},mechanismRows(original),update,true,L"TS1 / 설명: 이름은 에너지 위, 설명은 화살표 위 · S와 에너지는 같은 순서로 / 구분 · Ctrl+=: 아래첨자 · Ctrl+Shift+=: 위첨자 · 미정 에너지: ? 또는 ?.? · S=0 검정, S=1 빨강, S=2 파랑",true).has_value();
}
void editReaction(int id){
    auto* r=reaction(id);
    if(!r)return;
    if(r->explicitConnections()){editBranchedReaction(id);return;}
    Document before=doc, edited;
    Reaction original=*r;
    if(editLinearReaction(before,original,edited,true)){
        if(toNative(edited)!=toNative(before)){history.checkpoint(before);changed();fit();}
    }else{doc=std::move(before);redraw();}
}
void newReaction(){
    Document base=doc, edited;
    Reaction r;
    r.id=base.nextId++;
    r.p=center()-Vec{120,0};
    r.slots={{"Reactant",{}},{"Intermediate",{}},{"Product",{}}};
    r.above={"Step 1","Step 2"};
    r.below={"",""};
    base.reactions.push_back(r);
    if(!editLinearReaction(base,r,edited,false))return;
    history.checkpoint(doc);
    doc=std::move(edited);
    sidebarPage=0;
    selection={r.id};
    targetContainer=0;
    tool=Select;
    changed();
    fit();
}
void reactionSettings(int id){
    auto* ptr=reaction(id);if(!ptr)throw std::runtime_error("반응 블록을 선택하세요.");Document before=doc;Reaction old=*ptr;
    auto update=[&](const std::vector<std::string>& v)->std::wstring{try{
        int mode=std::stoi(v[1]);if(old.explicitConnections()&&mode!=0)return L"분기 연결과 프레임을 유지하려면 가로 배치를 사용하세요.";double columns=numeric(v[3],1,40),gap=numeric(v[4],0,4000);
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
void editArrowCross(){
    int owner=primary();auto* r=reaction(owner);Object* object=nullptr;for(auto& o:doc.objects)if(o.id==owner&&o.head)object=&o;
    if(!r&&!object)throw std::runtime_error("반응 화살표 또는 메커니즘을 선택하세요.");
    int count=r?int(!r->explicitConnections()?(r->cyclic?r->slots.size():r->slots.size()-1):r->links.size()):1;if(count<1)return;
    int index=r&&selectedCycleOwner==owner&&selectedCycleArrow>=0?selectedCycleArrow:0;ArrowCross old=object?object->cross:ArrowCross{};
    if(r)for(auto m:r->crosses)if(m.index==index)old=m;
    Document before=doc;
    auto update=[&](const std::vector<std::string>& v)->std::wstring{
        double n=numeric(v[0],1,count);if(n!=std::floor(n))return L"화살표 번호는 정수입니다.";
        ArrowCross mark;mark.index=int(n)-1;mark.enabled=v[1]=="1";mark.width=numeric(v[2],.1,30);mark.size=numeric(v[3],2,200);mark.color=colorValue(v[4]);
        doc=before;if(auto* rr=reaction(owner)){rr->crosses.erase(std::remove_if(rr->crosses.begin(),rr->crosses.end(),[&](const ArrowCross& m){return m.index==mark.index;}),rr->crosses.end());if(mark.enabled)rr->crosses.push_back(mark);}else for(auto& o:doc.objects)if(o.id==owner)o.cross=mark;
        redraw();UpdateWindow(win);return {};
    };
    auto result=ui::form(win,L"반응 화살표 중앙 X",{{L"화살표 번호 (1부터)",std::to_string(index+1)},{L"X 표시", "1",false,{L"숨김",L"표시"}},{L"X 선 굵기 (pt)",number(old.width)},{L"X 크기 (pt)",number(old.size)},{L"X 색상",old.color,false,{},true}},update);
    if(result){update(*result);if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();}}else{doc=before;redraw();}
}
void branchMechanism(){
    auto* r=reaction(primary());if(!r)throw std::runtime_error("분기를 추가할 메커니즘을 선택하세요.");
    auto value=ui::form(win,L"메커니즘 분기 추가",{{L"분기 시작 구조 번호 (1부터)",std::to_string(targetContainer==r->id?targetSlot+1:1)},{L"추가할 분기 수","2"}});if(!value)return;
    double source=numeric((*value)[0],1,double(r->slots.size()));if(source!=std::floor(source))throw std::runtime_error("구조 번호는 정수입니다.");
    Document candidate=doc;double count=numeric((*value)[1],1,20);if(count!=std::floor(count))throw std::runtime_error("분기 수는 정수입니다.");for(auto& item:candidate.reactions)if(item.id==r->id)for(int i=0;i<int(count);i++)addMechanismBranch(item,size_t(source)-1);optimizeMechanism(candidate,{r->id},false);history.checkpoint(doc);doc=std::move(candidate);changed();fit();
}
void externalMechanism(){
    auto values=ui::form(win,L"첫 구조 없이 세로선에서 분기",{{L"분기 수 (2~20)","3"}});if(!values)return;
    double count=numeric((*values)[0],2,20);if(count!=std::floor(count))throw std::runtime_error("분기 수는 정수입니다.");
    Document candidate=doc;Reaction r;r.id=candidate.nextId++;r.p=world({double(ww)/2,double(wh)/2});r.unit.clear();
    for(int i=0;i<int(count);i++){r.slots.push_back({"",{},""});r.slotGrid.push_back({0,double(i)});r.links.push_back({-1,i,{}});r.above.push_back("");r.below.push_back("");}
    candidate.reactions.push_back(r);optimizeMechanism(candidate,{r.id},false);history.checkpoint(doc);doc=std::move(candidate);selection={r.id};targetContainer=0;changed();fit();
}
void insertBranchArrows(){
    Document before=doc;auto originalSelection=selection;int stemId=0;Vec anchor=world({double(ww)/2,double(wh)/2});double height=160;
    for(auto& o:doc.objects)if(contains(selection,o.id)&&o.kind==ObjectKind::Line&&o.points.size()==2&&std::abs(o.points[0].x-o.points[1].x)<.01){stemId=o.id;anchor={o.points[0].x,std::min(o.points[0].y,o.points[1].y)};height=std::abs(o.points[0].y-o.points[1].y);break;}
    auto update=[&](const std::vector<std::string>& values)->std::wstring{
        double count=numeric(values[0],2,20);if(count!=std::floor(count))return L"분기 수는 정수입니다.";
        Document candidate=before;auto ids=addBranchArrows(candidate,anchor,numeric(values[1],10,10000),numeric(values[2],10,10000),size_t(count),values[3]=="1",stemId);
        doc=std::move(candidate);selection=ids;redraw();UpdateWindow(win);return {};
    };
    auto result=ui::form(win,L"하나의 세로선에서 여러 화살표 분기 · 미리보기",{{L"분기 화살표 수 (2~20)","3"},{L"세로선 높이 (pt)",number(height)},{L"가로 화살표 길이 (pt)","140"},{L"화살표 방향","0",false,{L"오른쪽",L"왼쪽"}}},update);
    if(!result){doc=before;selection=originalSelection;redraw();return;}
    auto error=update(*result);if(!error.empty())throw std::runtime_error(ui::utf8(error));
    if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();fit();status=L"세로선 분기 화살표를 만들었습니다. 그룹으로 이동 · Ctrl+Shift+G로 개별 편집 · Ctrl+Z로 되돌리기";}
}
Reaction* selectedMechanism();
void arrangeImported(bool editAfter){
    if(editAfter){
        auto* existing=selectedMechanism();
        const bool hasArrow=std::any_of(doc.objects.begin(),doc.objects.end(),[](const Object& item){
            return item.kind==ObjectKind::Line&&item.head&&item.points.size()>=2;
        });
        if(!existing&&!hasArrow&&!doc.reactions.empty()){
            if(doc.reactions.size()!=1)throw std::runtime_error("편집할 메커니즘을 캔버스에서 선택하세요.");
            existing=&doc.reactions.front();
        }
        if(existing){
            const int id=existing->id;
            sidebarPage=0;
            selection={id};
            redraw();
            editReaction(id);
            return;
        }
    }
    Document before=doc;
    auto originalSelection=selection;
    auto ids=editAfter?std::vector<int>{}:groupedSelection(selection);
    auto update=[&](const std::vector<std::string>& values)->std::wstring{
        Document candidate=before;int owner=arrangeImportedMechanism(candidate,ids,values[4]=="0");
        auto slots=candidate.reactions.back().slots;double degrees=numeric(values[1],-36000,36000),factor=numeric(values[2],1,1000)/100.;
        for(auto& slot:slots){
            bool image=false;for(auto& o:candidate.objects)if(contains(slot.members,o.id)&&o.kind==ObjectKind::Image)image=true;
            if(values[0]=="1"&&!image)cleanStructure(candidate,slot.members,false);
            if((degrees||values[3]!="0")&&!image)transformStructures(candidate,slot.members,degrees,values[3]=="1",values[3]=="2");
            if(factor!=1)scaleStructures(candidate,slot.members,factor);
        }
        if(values[4]!="0"||values[0]!="0"||degrees||factor!=1||values[3]!="0")optimizeMechanism(candidate,{owner},false);doc=std::move(candidate);selection={owner};redraw();UpdateWindow(win);return {};
    };
    auto result=ui::form(win,L"화살표 사이 구조 묶음 정리 · 미리보기",{{L"구조 모양","0",false,{L"원본 좌표 유지",L"2D 결합 각도·길이 정돈"}},{L"구조 묶음 회전 (도)","0"},{L"구조 묶음 배율 (%)","100"},{L"반전","0",false,{L"없음",L"좌우",L"상하"}},{L"배치","0",false,{L"원본 구조 위치·묶음 유지",L"간격 자동 재배치"}}},update);
    if(!result){doc=before;selection=originalSelection;redraw();return;}
    auto error=update(*result);if(!error.empty())throw std::runtime_error(ui::utf8(error));
    if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();fit();status=L"화살표 사이의 구조들을 반응 상태로 묶었습니다. 구조 칸에서 Ctrl+Shift+K: 모양 정돈 · Ctrl+K: 크기 조정";}
    if(editAfter){
        sidebarPage=0;
        targetContainer=0;
        tool=Select;
        redraw();
        editReaction(primary());
    }
}
Reaction* selectedMechanism(){
    if(auto* r=reaction(primary()))return r;
    if(auto* r=reaction(targetContainer))return r;
    return reaction(selectedCycleOwner);
}
void editMechanismControls(int id){
    auto* r=selectedMechanism();if(!r)throw std::runtime_error("메커니즘 또는 단계 영역을 먼저 선택하세요.");const int owner=r->id;
    if(id==145){mechanismConnecting=!mechanismConnecting;connectionOwner=owner;selection={owner};tool=Select;guides=true;status=mechanismConnecting?L"연결 모드: 시작 구조 칸에서 도착 구조 칸으로 드래그하세요. Esc: 종료 · 자동 정렬: 연결에 맞춰 배치":L"연결 모드를 종료했습니다.";redraw();return;}
    if(id==148){editBranchedReaction(owner);return;}
    if(id==149){auto value=ui::form(win,L"화살표 길이",{{L"길이 (pt, 0: 설명에 맞춰 자동)",number(r->arrowLength)}});if(!value)return;double length=numeric((*value)[0],0,10000);if(length>0&&length<20)throw std::runtime_error("길이는 20 pt 이상 또는 0(자동)으로 입력하세요.");Document candidate=doc;for(auto& row:candidate.reactions)if(row.id==owner)row.arrowLength=length;optimizeMechanism(candidate,{owner},r->cyclic);history.checkpoint(doc);doc=std::move(candidate);changed();fit();return;}
    if(id==147){if(selectedCycleOwner!=owner||selectedCycleArrow<0)throw std::runtime_error("제거할 화살표를 먼저 클릭하세요.");Document candidate=doc;for(auto& row:candidate.reactions)if(row.id==owner)removeMechanismConnection(row,size_t(selectedCycleArrow));history.checkpoint(doc);doc=std::move(candidate);selectedCycleArrow=-1;selection={owner};changed();return;}
    size_t selected=targetContainer==owner?std::min(targetSlot,r->slots.size()-1):0;
    if(id==143){
        const size_t end=r->slots.size();
        auto value=ui::form(win,L"단계 추가",{{L"삽입할 위치 (1부터, 마지막 뒤: 단계 수+1)",std::to_string(end+1)}});
        if(!value)return;
        double n=numeric((*value)[0],1,double(end+1));
        if(n!=std::floor(n))throw std::runtime_error("정수 위치를 입력하세요.");
        const size_t position=size_t(n)-1;
        Document candidate=doc;
        for(auto& row:candidate.reactions)if(row.id==owner){
            if(position==end){
                if(end>=40)throw std::runtime_error("단계는 최대 40개까지 추가할 수 있습니다.");
                appendMechanismStage(row);
            }else insertMechanismStage(row,position);
        }
        optimizeMechanism(candidate,{owner},position==end&&r->cyclic);
        history.checkpoint(doc);doc=std::move(candidate);
        targetContainer=owner;targetSlot=position;selection={owner};changed();fit();return;
    }
    if(id==144||id==146){std::vector<ui::Field> fields={{L"제거하거나 합칠 단계 번호 (1부터)",std::to_string(selected+1)}};
        if(id==146)fields.push_back({L"구조와 연결을 받을 단계 번호 (1부터)",std::to_string(selected?selected:std::min(size_t(2),r->slots.size()))});
        else fields.push_back({L"해당 단계의 구조", "0",false,{L"캔버스에 유지 (단계·연결만 제거)",L"구조도 함께 삭제"}});
        auto value=ui::form(win,id==146?L"잘못 나뉜 단계 합치기":L"단계 제거",fields);if(!value)return;
        double stage=numeric((*value)[0],1,double(r->slots.size()));if(stage!=std::floor(stage))throw std::runtime_error("정수 단계 번호를 입력하세요.");int merge=-1;
        if(id==146){double n=numeric((*value)[1],1,double(r->slots.size()));if(n!=std::floor(n))throw std::runtime_error("정수 단계 번호를 입력하세요.");merge=int(n)-1;}
        Document candidate=doc;removeMechanismStage(candidate,owner,size_t(stage)-1,merge,id==144&&(*value)[1]=="1");
        if(id==146&&std::any_of(candidate.reactions.begin(),candidate.reactions.end(),[&](const Reaction& row){return row.id==owner;}))optimizeMechanism(candidate,{owner});
        history.checkpoint(doc);doc=std::move(candidate);selection.clear();targetContainer=0;selectedCycleOwner=0;selectedCycleArrow=-1;changed();fit();return;}
}
Vec mechanismAddPosition(const Reaction& r,bool below,size_t slot){
    if(slot==size_t(-1))slot=r.slots.size()-1;
    Vec last=reactionSlot(r,slot);
    if(below||(r.vertical&&!r.cyclic))return screen(last+Vec{0,r.slotHeight/2+mechanismCaptionHeight(r,doc.style)})+Vec{0,28};
    double direction=!r.explicitConnections()&&reactionRow(r,slot)%2?-1.:1.;
    return screen(last+Vec{direction*r.slotWidth/2,0})+Vec{direction*28,0};
}
std::optional<size_t> mechanismSlotAt(int owner,Vec p){
    auto* r=reaction(owner);if(!r||doc.isLocked(owner))return {};std::optional<size_t> best;double distance=1e30;
    for(size_t i=0;i<r->slots.size();i++){Vec c=reactionSlot(*r,i);if(std::abs(p.x-c.x)<=r->slotWidth/2+8/zoom&&std::abs(p.y-c.y)<=r->slotHeight/2+8/zoom){double d=(p-c).length();if(d<distance){best=i;distance=d;}}}return best;
}
std::vector<Vec> mechanismConnectionPath(int owner,int edge){
    auto* r=reaction(owner);if(!r||edge<0)return {};auto copy=*r;makeMechanismGraph(copy);if(size_t(edge)>=copy.links.size())return {};return mechanismLinkPath(copy,size_t(edge),doc.style);
}
bool beginMechanismConnection(Vec point){
    if(isolatedStructureEditing||tool!=Select)return false;
    if(selectedCycleOwner&&selectedCycleArrow>=0){auto path=mechanismConnectionPath(selectedCycleOwner,selectedCycleArrow);if(!path.empty())for(int handle:{0,1})if((point-screen(handle?path.back():path.front())).length()<=10){
        connectionOwner=selectedCycleOwner;connectionEdge=selectedCycleArrow;connectionHandle=handle;connectionFrom=-1;mechanismConnectionDrag=true;dragBefore=doc;SetCapture(win);status=L"끝점을 다른 단계에 놓으면 연결을 변경합니다. 화살촉을 빈 곳에 가로로 끌면 길이를 조절합니다. Esc: 취소";return true;}}
    if(!mechanismConnecting)return false;auto slot=mechanismSlotAt(connectionOwner,world(point));
    if(slot){connectionFrom=int(*slot);connectionEdge=-1;connectionHandle=1;mechanismConnectionDrag=true;dragBefore=doc;selection={connectionOwner};SetCapture(win);status=L"도착 구조 칸까지 드래그하세요. 놓은 뒤 크기·간격 자동 정렬로 재배열합니다.";}
    else status=L"연결 모드: 시작 구조 칸 안에서 드래그를 시작하세요. Esc: 종료";redraw();return true;
}
void finishMechanismConnection(Vec point){
    const int owner=connectionOwner,edge=connectionEdge,handle=connectionHandle,from=connectionFrom;
    mechanismConnectionDrag=false;ReleaseCapture();auto target=mechanismSlotAt(owner,world(point));Document candidate=dragBefore;
    auto it=std::find_if(candidate.reactions.begin(),candidate.reactions.end(),[&](const Reaction& r){return r.id==owner;});if(it==candidate.reactions.end())return;
    makeMechanismGraph(*it);int selected=edge;
    bool lengthDrag=edge>=0&&handle==1&&(point-down).length()>3&&(!target||int(*target)==it->links.at(size_t(edge)).to);
    if(lengthDrag){auto path=mechanismConnectionPath(owner,edge);if(path.empty())return;double direction=path.back().x>=path[path.size()-2].x?1.:-1.;it->arrowLength=std::clamp(mechanismArrowLength(*it,candidate.style)+direction*(point.x-down.x)/zoom,20.,10000.);optimizeMechanism(candidate,{owner});}
    else if(target){if(edge>=0){auto link=it->links.at(size_t(edge));selected=int(connectMechanismStages(*it,handle?link.from:int(*target),handle?int(*target):link.to,edge));}else selected=int(connectMechanismStages(*it,from,int(*target)));}
    else{status=L"도착 단계를 지정하지 않아 연결을 변경하지 않았습니다.";redraw();return;}
    if(toNative(candidate)==toNative(doc)){redraw();return;}history.checkpoint(doc);doc=std::move(candidate);selection={owner};selectedCycleOwner=owner;selectedCycleArrow=selected;targetContainer=0;
    status=L"연결·길이를 변경했습니다. 크기·간격 자동 정렬: 연결에 맞춰 재배열 · Ctrl+Z: 되돌리기";changed();
}
std::optional<std::pair<int,int>> cycleArrowAt(Vec p){
    double tolerance=6/zoom;
    for(auto& item:scene().items){
        if(item.reactionArrow<0||doc.isLocked(item.owner))continue;
        auto closeTo=[&](Vec a,Vec b){Vec v=b-a;double ll=v.x*v.x+v.y*v.y;double t=ll>1e-9?std::clamp(((p-a).x*v.x+(p-a).y*v.y)/ll,0.,1.):0;return (p-a-v*t).length()<=tolerance;};
        if(item.type==Primitive::Bezier&&item.points.size()==4){Vec a=item.points[0];for(int j=1;j<=64;j++){double t=j/64.,u=1-t;Vec b=item.points[0]*(u*u*u)+item.points[1]*(3*u*u*t)+item.points[2]*(3*u*t*t)+item.points[3]*(t*t*t);if(closeTo(a,b))return std::make_pair(item.owner,item.reactionArrow);a=b;}}
        if(item.type==Primitive::Line&&item.points.size()>=2)for(size_t j=1;j<item.points.size();j++)if(closeTo(item.points[j-1],item.points[j]))return std::make_pair(item.owner,item.reactionArrow);
        if(item.type==Primitive::Polygon)for(size_t j=0;j<item.points.size();j++)if(closeTo(item.points[j],item.points[(j+1)%item.points.size()]))return std::make_pair(item.owner,item.reactionArrow);
    }
    return {};
}
void mechanismContextMenu(Vec at){
    HWND hwnd=win;
    if(!finishInline(true))return;SetFocus(hwnd);if(auto hit=cycleArrowAt(world(at))){selection={hit->first};selectedCycleOwner=hit->first;selectedCycleArrow=hit->second;}
    else if(!pickSlot(world(at))){int objectHit=doc.atomAt(world(at),10/zoom);if(!objectHit)objectHit=doc.bondAt(world(at),5/zoom);if(!objectHit)objectHit=objectAt(world(at));if(objectHit&&!contains(selection,objectHit))selection=groupedSelection({objectHit});}
    HMENU menu=CreatePopupMenu();AppendMenuW(menu,activeStructureTarget()?MF_STRING:MF_STRING|MF_GRAYED,186,L"구조 고치기 · 독립 창…");for(auto entry:std::initializer_list<std::pair<int,const wchar_t*>>{{143,L"단계 삽입…"},{144,L"단계 제거…"},{146,L"두 단계 합치기…"},{145,L"드래그로 단계 연결"},{148,L"단계·연결 표 편집…"},{149,L"화살표 길이…"},{147,L"선택한 연결 제거"}})AppendMenuW(menu,MF_STRING,entry.first,entry.second);AppendMenuW(menu,MF_STRING,166,L"벤젠을 Ph로 축약 (Ctrl+Shift+P)");AppendMenuW(menu,MF_STRING,167,L"가져온 메커니즘 정리 (구조 보존)");AppendMenuW(menu,MF_STRING,168,L"화살표 중앙 X 설정…");AppendMenuW(menu,MF_STRING,169,L"메커니즘 분기 추가…");AppendMenuW(menu,MF_STRING,220,L"세로선 분기 화살표…");POINT point{LONG(at.x),LONG(at.y)};ClientToScreen(hwnd,&point);int chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,hwnd,nullptr);DestroyMenu(menu);if(chosen)command(chosen);redraw();

}
bool deleteSelectedCycleArrow(){
    if(selectedCycleArrow<0||selection!=std::vector<int>{selectedCycleOwner})return false;
    auto* r=reaction(selectedCycleOwner);if(!r||doc.isLocked(r->id)||size_t(selectedCycleArrow)>=(!r->explicitConnections()?(r->cyclic?r->slots.size():r->slots.size()-1):r->links.size()))return false;
    if(contains(r->hiddenArrows,selectedCycleArrow))return false;
    if(!r->cyclic){history.checkpoint(doc);removeMechanismConnection(*r,size_t(selectedCycleArrow));selectedCycleOwner=0;selectedCycleArrow=-1;selection.clear();changed();return true;}
    history.checkpoint(doc);r->hiddenArrows.push_back(selectedCycleArrow);selectedCycleOwner=0;selectedCycleArrow=-1;selection.clear();
    status=L"순환 화살표를 삭제했습니다. Ctrl+Z: 되돌리기 · 메커니즘 설정에서 모두 복원";changed();return true;
}

} // namespace yd::editor

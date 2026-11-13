#include "win_theme.h"
#include "win_editor_render.h"
#include "win_editor_commands.h"
#include "win_editor_pointer.h"
#include "win_editor_reaction.h"
#include "win_mechanism_editor.h"
#include "win_editor_state.h"
#include "win_editor_structure.h"
#include "win_editor_text.h"
#include "cdxml.h"
#include "mechanism_data.h"
#include "office.h"
#include "win_dialogs.h"
#include "win_ui.h"
#include <sstream>

namespace yd::editor {

bool mechanismConnecting=false,mechanismConnectionDrag=false;
int connectionOwner=0,connectionFrom=-1,connectionEdge=-1,connectionHandle=-1;
int selectedCycleOwner=0,selectedCycleArrow=-1;
int stageCopyOwner=0;
std::set<size_t> stagesToCopy;

void cancelStageCopy(){
    if(stageCopyOwner)status=L"단계 선택 복사를 취소했습니다.";
    stageCopyOwner=0;
    stagesToCopy.clear();
}
void toggleStageCopy(Vec at){
    if(auto stage=mechanismSlotAt(stageCopyOwner,world(at))){
        if(!stagesToCopy.erase(*stage))stagesToCopy.insert(*stage);
    }
    status=std::to_wstring(stagesToCopy.size())+L"단계 선택 · 클릭: 선택/해제 · Ctrl+C 또는 Enter: 복사 · Esc: 취소";
    redraw();
}
void finishStageCopy(){
    if(stagesToCopy.empty()){
        status=L"복사할 구조 칸을 하나 이상 클릭하세요.";
    }else{
        auto fragment=copyMechanismStages(doc,stageCopyOwner,stagesToCopy);
        if(FAILED(office::copy(fragment)))throw std::runtime_error("클립보드 복사에 실패했습니다. 잠시 후 다시 시도하세요.");
        const auto count=stagesToCopy.size();
        cancelStageCopy();
        status=std::to_wstring(count)+L"단계를 복사했습니다. 빈 캔버스나 다른 문서에서 Ctrl+V로 붙여넣으세요.";
    }
    redraw();
}

static bool editReactionGraph(const Document& base,Reaction original){
    const int id=original.id;
    makeMechanismGraph(original);
    ui::Table rows;
    const bool editablePositions=original.fixedFrames&&!original.circularMerge;
    for(size_t i=0;i<original.slots.size();i++){
        const auto p=original.slotGrid[i];
        rows.push_back({"구조",std::to_string(i+1),editablePositions?number(p.x):"",editablePositions?number(p.y):"",original.slots[i].label,original.slots[i].energy});
        if(editablePositions)rows.back().push_back("");
    }
    for(size_t i=0;i<original.links.size();i++){
        const auto& link=original.links[i];
        rows.push_back({"화살표",std::to_string(i+1),std::to_string(link.from+1),std::to_string(link.to+1),i<original.above.size()?original.above[i]:"",i<original.below.size()?original.below[i]:""});
        if(editablePositions)rows.back().push_back(number(link.bend));
    }
    auto update=[&](const ui::Table& cells)->std::wstring{
        Document candidate=base;
        auto entry=std::find_if(candidate.reactions.begin(),candidate.reactions.end(),[&](const Reaction& r){return r.id==id;});
        *entry=mechanismGraphData(original,cells);
        optimizeMechanism(candidate,{id},false);
        doc=std::move(candidate);
        if(original.fixedFrames)fit();else redraw();
        UpdateWindow(win);
        return {};
    };
    std::vector<std::wstring> columns={L"구분",L"번호",L"시작 구조",L"도착 구조",L"이름 / TS 설명",L"에너지 (S=0 / 1 / 2)"};
    std::wstring hint=L"행 추가·삭제 가능 · 새 행의 번호는 비워 두세요 · 시작 0: 공통 세로선 · 삭제한 단계의 구조는 캔버스에 유지";
    if(editablePositions){
        columns[2]=L"열 / 시작 구조";
        columns[3]=L"행 / 도착 구조";
        columns.push_back(L"곡률 (−2~2)");
        hint=L"구조 행: 열·행 좌표 (오른쪽·아래가 +) · 화살표 행: 시작·도착 구조 번호 및 곡률 (0: 직선) · 새 행의 번호는 비워 두세요";
    }
    if(original.circularMerge)hint=L"원형 경로의 좌표·곡률은 연결에 맞춰 자동 배치됩니다 · 캔버스의 +: 해당 화살표에 중간체 삽입";
    return ui::table(win,L"메커니즘 분기·프레임 편집",columns,rows,update,true,hint,false,4).has_value();
}
void editBranchedReaction(int id){
    auto* r=reaction(id);if(!r)return;
    Document before=doc;
    double previousZoom=zoom;
    Vec previousOrigin=origin;
    if(editReactionGraph(before,*r)){
        if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();fit();}
    }else{
        doc=std::move(before);
        zoom=previousZoom;origin=previousOrigin;redraw();
    }
}
void editReaction(int id){
    if(!reaction(id))return;
    const Document before=doc;
    if(auto edited=mechanismEditor(win,before,id))if(toNative(*edited)!=toNative(before)){history.checkpoint(before);doc=std::move(*edited);selection={id};targetContainer=0;changed();fit();}
}
void newReaction(){
    auto values=ui::form(win,L"반응 메커니즘 만들기",{{L"경로 양식","0",false,{
        L"직선",L"가로 → 아래 → 촉매 순환",L"가로 → 아래 → 좌우 분기·합류",L"촉매 순환"}}});
    if(!values)return;
    Document before=doc,base=doc;
    const auto layout=static_cast<MechanismTemplate>(std::stoi(values->front()));
    int horizontal=1,leftCount=2,rightCount=1;
    if(layout==MechanismTemplate::SplitAndMerge){
        auto counts=ui::form(win,L"원형 분기·합류 단계 수",{
            {L"아래로 꺾이기 전 가로 화살표 수","1"},
            {L"왼쪽 경로 중간체 수","2"},
            {L"오른쪽 경로 중간체 수","1"}},[](const std::vector<std::string>& v)->std::wstring{
                try{
                    double horizontal=numeric(v[0],1,37),left=numeric(v[1],0,37),right=numeric(v[2],0,37);
                    if(horizontal!=std::floor(horizontal)||left!=std::floor(left)||right!=std::floor(right))return L"개수는 정수로 입력하세요.";
                    if(left+right==0)return L"좌우 경로 중 한 곳에 중간체를 한 개 이상 지정하세요.";
                    if(horizontal+left+right+3>40)return L"반응물·분기점·생성물을 포함해 구조는 최대 40개입니다.";
                    return {};
                }catch(const std::exception& error){return ui::wide(error.what());}
            });
        if(!counts)return;
        horizontal=std::stoi((*counts)[0]);leftCount=std::stoi((*counts)[1]);rightCount=std::stoi((*counts)[2]);
    }
    Reaction r=mechanismTemplate(layout,horizontal,leftCount,rightCount);
    r.id=base.nextId++;
    r.p=r.cyclic||r.fixedFrames?center():center()-Vec{120,0};
    base.reactions.push_back(r);
    double previousZoom=zoom;
    Vec previousOrigin=origin;
    auto result=mechanismEditor(win,base,r.id);
    if(!result){doc=std::move(before);zoom=previousZoom;origin=previousOrigin;redraw();return;}
    history.checkpoint(before);doc=std::move(*result);
    sidebarPage=0;
    selection={r.id};
    targetContainer=0;
    tool=Select;
    changed();
    fit();
}
void reactionSettings(int id){editReaction(id);}
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
    auto* r=reaction(primary());if(!r)r=reaction(targetContainer);if(!r)return;
    editReaction(r->id);
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
            return (item.kind==ObjectKind::Line||item.kind==ObjectKind::Curve)&&item.head&&item.points.size()>=2;
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
    if(editAfter){
        const Document before=doc;Document draft=before;
        const int owner=arrangeImportedMechanism(draft,{},true);
        if(auto result=mechanismEditor(win,draft,owner,&before)){
            history.checkpoint(before);doc=std::move(*result);sidebarPage=0;selection={owner};targetContainer=0;tool=Select;changed();fit();
            status=L"변환 결과를 적용했습니다. 메커니즘 편집에서 + / Delete / 연결·배치를 수정할 수 있습니다.";
        }
        return;
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
    if(id==149){editReaction(owner);return;}
    if(id==147){if(selectedCycleOwner!=owner||selectedCycleArrow<0)throw std::runtime_error("제거할 화살표를 먼저 클릭하세요.");Document candidate=doc;for(auto& row:candidate.reactions)if(row.id==owner)removeMechanismConnection(row,size_t(selectedCycleArrow));history.checkpoint(doc);doc=std::move(candidate);selectedCycleArrow=-1;selection={owner};changed();return;}
    size_t selected=targetContainer==owner?std::min(targetSlot,r->slots.size()-1):0;
    auto stagePreview=[&](HDC dc,const RECT& box,const std::vector<std::string>& values){
        const double n=numeric(values.at(0),1,double(r->slots.size()+1));
        if(n!=std::floor(n))return;
        const bool append=size_t(n)>r->slots.size();
        const size_t index=append?selected:size_t(n)-1;
        const auto& palette=appearance::palette();
        Graphics g(dc);
        SolidBrush background(palette.raised);
        g.FillRectangle(&background,RectF(float(box.left),float(box.top),float(box.right-box.left),float(box.bottom-box.top)));
        const auto label=(append?L"끝에 삽입  ·  현재 ":L"")+std::wstring(L"단계 ")+
            std::to_wstring(index+1)+L" · "+ui::wide(r->slots[index].label);
        ui::drawLabel(g,label,RectF(float(box.left+10),float(box.top+8),600,23),14,palette.text,true,true);
        auto structure=makeScene(structureInSlot(doc,owner,index),measure);
        const auto& b=structure.bounds;
        const auto scale=float(std::min(600./std::max(1.,b.w),80./std::max(1.,b.h)));
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.TranslateTransform(float(box.left+box.right)/2-float(b.x+b.w/2)*scale,float(box.top+35)-float(b.y)*scale);
        g.ScaleTransform(scale,scale);render(g,structure,false,true);
    };
    if(id==143){
        if(r->circularMerge&&selectedCycleOwner==owner&&selectedCycleArrow>=0){
            insertMechanismLinkStage(owner,size_t(selectedCycleArrow));
            return;
        }
        const size_t end=r->slots.size();
        auto value=ui::form(win,L"단계 추가",{{L"삽입할 위치 (1부터, 마지막 뒤: 단계 수+1)",std::to_string(end+1)}},{},{},stagePreview);
        if(!value)return;
        double n=numeric((*value)[0],1,double(end+1));
        if(n!=std::floor(n))throw std::runtime_error("정수 위치를 입력하세요.");
        const size_t position=size_t(n)-1;
        size_t newStage=position;
        Document candidate=doc;
        for(auto& row:candidate.reactions)if(row.id==owner){
            if(position==end){
                if(end>=40)throw std::runtime_error("단계는 최대 40개까지 추가할 수 있습니다.");
                appendMechanismStage(row);
            }else if(row.circularMerge){
                std::vector<size_t> incoming;
                for(size_t i=0;i<row.links.size();i++)if(row.links[i].to==int(position))incoming.push_back(i);
                if(incoming.size()>1)throw std::runtime_error("삽입할 TS 연결을 선택한 뒤 중간체 삽입을 사용하세요.");
                if(incoming.empty())insertMechanismStage(row,position);
                else newStage=splitMechanismLink(row,incoming.front());
            }else insertMechanismStage(row,position);
        }
        optimizeMechanism(candidate,{owner},position==end&&r->cyclic);
        history.checkpoint(doc);doc=std::move(candidate);
        targetContainer=owner;targetSlot=newStage;selection={owner};changed();fit();return;
    }
    if(id==144||id==146){std::vector<ui::Field> fields={{L"제거하거나 합칠 단계 번호 (1부터)",std::to_string(selected+1)}};
        if(id==146)fields.push_back({L"구조와 연결을 받을 단계 번호 (1부터)",std::to_string(selected?selected:std::min(size_t(2),r->slots.size()))});
        else fields.push_back({L"해당 단계의 구조", "0",false,{L"캔버스에 유지 (단계·연결만 제거)",L"구조도 함께 삭제"}});
        auto value=ui::form(win,id==146?L"잘못 나뉜 단계 합치기":L"단계 제거",fields,{},{},stagePreview);if(!value)return;
        double stage=numeric((*value)[0],1,double(r->slots.size()));if(stage!=std::floor(stage))throw std::runtime_error("정수 단계 번호를 입력하세요.");int merge=-1;
        if(id==146){double n=numeric((*value)[1],1,double(r->slots.size()));if(n!=std::floor(n))throw std::runtime_error("정수 단계 번호를 입력하세요.");merge=int(n)-1;}
        Document candidate=doc;removeMechanismStage(candidate,owner,size_t(stage)-1,merge,id==144&&(*value)[1]=="1");
        if((id==146||r->circularMerge)&&std::any_of(candidate.reactions.begin(),candidate.reactions.end(),[&](const Reaction& row){return row.id==owner;}))optimizeMechanism(candidate,{owner});
        history.checkpoint(doc);doc=std::move(candidate);selection.clear();targetContainer=0;selectedCycleOwner=0;selectedCycleArrow=-1;changed();fit();return;}
}
Vec mechanismAddPosition(const Reaction& r,bool below,size_t slot){
    if(slot==size_t(-1))slot=r.slots.size()-1;
    Vec last=reactionSlot(r,slot);
    if(below||(r.vertical&&!r.cyclic))return screen(last+Vec{0,r.slotHeight/2+mechanismCaptionHeight(r,doc.style)})+Vec{0,28};
    double direction=!r.explicitConnections()&&reactionRow(r,slot)%2?-1.:1.;
    return screen(last+Vec{direction*r.slotWidth/2,0})+Vec{direction*(r.fixedFrames?48:28),0};
}
std::vector<std::pair<size_t,Vec>> mechanismSplitButtons(const Reaction& r){
    // Transition states are selected as connections; insertion is an explicit editor action.
    return {};
}
void insertMechanismLinkStage(int owner,size_t edge){
    auto* original=reaction(owner);
    if(!original||doc.isLocked(owner))return;
    Document candidate=doc;
    auto edited=std::find_if(candidate.reactions.begin(),candidate.reactions.end(),[=](const Reaction& r){return r.id==owner;});
    const auto stage=splitMechanismLink(*edited,edge);
    optimizeMechanism(candidate,{owner});
    history.checkpoint(doc);
    doc=std::move(candidate);
    selection={owner};targetContainer=owner;targetSlot=stage;
    selectedCycleOwner=0;selectedCycleArrow=-1;
    status=L"선택한 화살표에 중간체를 삽입했습니다. Ctrl+Z: 되돌리기";
    changed();fit();
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
    if(slot){connectionFrom=int(*slot);connectionEdge=-1;connectionHandle=1;mechanismConnectionDrag=true;dragBefore=doc;selection={connectionOwner};SetCapture(win);status=L"도착 구조 칸까지 드래그하세요. 놓은 뒤 자동 정렬로 재배열합니다.";}
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
    status=L"연결·길이를 변경했습니다. 자동 정렬: 연결에 맞춰 재배열 · Ctrl+Z: 되돌리기";changed();
}
std::optional<std::pair<int,int>> cycleArrowAt(Vec p){
    double tolerance=6/zoom;
    for(auto& item:scene().items){
        if(item.reactionArrow<0||doc.isLocked(item.owner))continue;
        auto closeTo=[&](Vec a,Vec b){Vec v=b-a;double ll=v.x*v.x+v.y*v.y;double t=ll>1e-9?std::clamp(((p-a).x*v.x+(p-a).y*v.y)/ll,0.,1.):0;return (p-a-v*t).length()<=tolerance;};
        if(item.type==Primitive::Bezier)for(size_t i=0;i+3<item.points.size();i+=3){Vec a=item.points[i];for(int j=1;j<=64;j++){double t=j/64.,u=1-t;Vec b=item.points[i]*(u*u*u)+item.points[i+1]*(3*u*u*t)+item.points[i+2]*(3*u*t*t)+item.points[i+3]*(t*t*t);if(closeTo(a,b))return std::make_pair(item.owner,item.reactionArrow);a=b;}}
        if(item.type==Primitive::Line&&item.points.size()>=2)for(size_t j=1;j<item.points.size();j++)if(closeTo(item.points[j-1],item.points[j]))return std::make_pair(item.owner,item.reactionArrow);
        if(item.type==Primitive::Polygon)for(size_t j=0;j<item.points.size();j++)if(closeTo(item.points[j],item.points[(j+1)%item.points.size()]))return std::make_pair(item.owner,item.reactionArrow);
    }
    return {};
}
void mechanismContextMenu(Vec at){
    HWND hwnd=win;
    if(stageCopyOwner){cancelStageCopy();redraw();return;}
    if(!finishInline(true))return;
    constexpr int copyStagesItem=224;
    int copyOwner=0;
    const Vec pointInDocument=world(at);
    for(const auto& r:doc.reactions)
        if(r.explicitConnections()&&!r.slots.empty()&&!doc.isLocked(r.id)&&mechanismSlotAt(r.id,pointInDocument)){
            copyOwner=r.id;
            break;
        }
    SetFocus(hwnd);if(auto hit=cycleArrowAt(world(at))){selection={hit->first};selectedCycleOwner=hit->first;selectedCycleArrow=hit->second;if(!copyOwner)copyOwner=hit->first;}
    else if(!pickSlot(world(at))){int objectHit=doc.atomAt(world(at),10/zoom);if(!objectHit)objectHit=doc.bondAt(world(at),5/zoom);if(!objectHit)objectHit=objectAt(world(at));if(objectHit&&!contains(selection,objectHit))selection=groupedSelection({objectHit});}
    if(!copyOwner)copyOwner=objectAt(pointInDocument);
    auto* copyReaction=reaction(copyOwner);
    HMENU menu=CreatePopupMenu();
    if(!isolatedStructureEditing&&copyReaction&&copyReaction->explicitConnections()&&!copyReaction->slots.empty()&&!doc.isLocked(copyOwner))
        AppendMenuW(menu,MF_STRING,copyStagesItem,L"원하는 단계만 복사…");
    AppendMenuW(menu,activeStructureTarget()?MF_STRING:MF_STRING|MF_GRAYED,186,L"구조 고치기 · 독립 창…");for(auto entry:std::initializer_list<std::pair<int,const wchar_t*>>{{143,L"단계 삽입…"},{144,L"단계 제거…"},{146,L"두 단계 합치기…"},{145,L"드래그로 단계 연결"},{148,L"단계·연결 표 편집…"},{149,L"화살표 길이…"},{147,L"선택한 연결 제거"}})AppendMenuW(menu,MF_STRING,entry.first,entry.second);AppendMenuW(menu,MF_STRING,166,L"벤젠을 Ph로 축약 (Ctrl+Shift+P)");AppendMenuW(menu,MF_STRING,167,L"가져온 메커니즘 정리 (구조 보존)");AppendMenuW(menu,MF_STRING,168,L"화살표 중앙 X 설정…");AppendMenuW(menu,MF_STRING,169,L"메커니즘 분기 추가…");AppendMenuW(menu,MF_STRING,220,L"세로선 분기 화살표…");POINT point{LONG(at.x),LONG(at.y)};ClientToScreen(hwnd,&point);int chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,hwnd,nullptr);DestroyMenu(menu);if(chosen==copyStagesItem){
        stageCopyOwner=copyOwner;
        stagesToCopy.clear();
        selection.clear();
        targetContainer=0;
        selectedCycleOwner=0;
        selectedCycleArrow=-1;
        mechanismConnecting=false;
        tool=Select;
        status=L"복사할 구조 칸을 클릭하세요. Ctrl+C 또는 Enter: 복사 · Esc: 취소";
    }else if(chosen)command(chosen);
    redraw();

}
bool deleteSelectedMechanismStage(){
    auto* r=reaction(targetContainer);if(!activeStructureTarget()||!r||targetSlot>=r->slots.size()||doc.isLocked(r->id))return false;
    Document candidate=doc;removeMechanismStage(candidate,r->id,targetSlot,-1,true);
    history.checkpoint(doc);doc=std::move(candidate);selection.clear();targetContainer=0;selectedCycleOwner=0;selectedCycleArrow=-1;
    status=L"선택한 단계와 그림을 삭제했습니다. Ctrl+Z: 복구";changed();return true;
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

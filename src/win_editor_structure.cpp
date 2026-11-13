#include "win_editor_clipboard.h"
#include "win_editor_files.h"
#include "win_editor_images.h"
#include "win_editor_plot.h"
#include "win_editor_reaction.h"
#include "win_editor_render.h"
#include "structure_templates.h"
#include "win_editor_state.h"
#include "win_editor_structure.h"
#include "win_editor_text.h"
#include "cdxml.h"
#include "win_dialogs.h"
#include "win_dialog_style.h"
#include "win_scripts.h"
#include "win_ui.h"
#include <commdlg.h>
#include <sstream>

namespace yd::editor {

void editMullikenSpin(){
    if(selection.empty()||activeStructureTarget())
        throw std::runtime_error("스핀 값을 표시할 원자를 선택하세요. 구조 칸은 독립 편집 창에서 원자를 선택할 수 있습니다.");
    auto selected=doc.subset(groupedSelection(selection));
    ui::Table rows;
    std::vector<int> ids;
    for(const auto& a:selected.atoms){
        if(doc.isLocked(a.id))continue;
        rows.push_back({std::to_string(a.id),symbol(a.element),a.mullikenSpin?number(*a.mullikenSpin):""});
        ids.push_back(a.id);
    }
    if(rows.empty())throw std::runtime_error("스핀 값을 표시할 원자를 선택하세요.");
    auto read=[](const std::string& value)->std::optional<double>{
        const auto first=value.find_first_not_of(" \\t\\r\\n");
        if(first==std::string::npos)return {};
        const auto last=value.find_last_not_of(" \\t\\r\\n");
        return numeric(value.substr(first,last-first+1),-1000,1000);
    };
    auto result=ui::table(win,L"Mulliken 스핀 밀도", {L"원자 번호",L"원소",L"스핀 값"},rows,
        [&](const ui::Table& data)->std::wstring{
            if(data.size()!=rows.size())return L"선택된 원자 행을 유지하세요.";
            for(size_t i=0;i<data.size();++i){
                if(data[i][0]!=rows[i][0]||data[i][1]!=rows[i][1])return L"스핀 값 열만 편집하세요.";
                read(data[i][2]);
            }
            return {};
        },false,L"선택 원자에 계산 결과를 입력하거나 값 열에 붙여넣으세요. 빈칸이면 해당 원자의 표시를 지웁니다.");
    if(!result)return;
    Document candidate=doc;
    bool updated=false;
    for(size_t i=0;i<ids.size();++i){
        auto& spin=candidate.atom(ids[i])->mullikenSpin;
        auto value=read((*result)[i][2]);
        if((*result)[i][2]==rows[i][2])continue;
        updated|=spin!=value;
        spin=value;
    }
    if(updated){history.checkpoint(doc);doc=std::move(candidate);changed();}
}

void abbreviateGroups(){
    if(selection.empty())throw std::runtime_error("약어로 바꿀 작용기가 들어 있는 구조를 선택하세요.");
    auto ids=groupedSelection(selection);
    if(activeStructureTarget()){
        if(auto* r=reaction(targetContainer))ids=r->slots.at(targetSlot).members;
        if(auto* p=plot(targetContainer))ids=targetAbove?p->states.at(targetSlot).above:p->states.at(targetSlot).below;
    }
    std::vector<std::wstring> names={L"모든 지원 작용기",L"Ph · Phenyl"};
    for(const auto& entry:functionalGroups())names.push_back(ui::wide(entry.abbreviation)+L" · "+ui::wide(entry.name));
    const Document before=doc;
    Document candidate=before;
    int count=0;
    auto preview=[&](const auto& values)->std::wstring{
        const size_t index=std::stoul(values.front());
        candidate=before;
        count=0;
        if(index<2)count+=collapsePhenyl(candidate,ids);
        if(index!=1)count+=collapseFunctionalGroups(candidate,ids,index==0?"":functionalGroups().at(index-2).abbreviation);
        doc=candidate;
        hoverAtom=hoverBond=0;
        redraw();UpdateWindow(win);
        return count?L"":L"선택한 범위에서 일치하는 작용기를 찾지 못했습니다.";
    };
    auto result=ui::form(win,L"작용기 약어 · 미리보기",{{L"바꿀 작용기","0",false,names}},preview);
    doc=before;
    if(result){
        preview(*result);
        doc=before;
        if(count){
            history.checkpoint(before);
            doc=std::move(candidate);
            auto remaining=doc.allIds();
            selection.erase(std::remove_if(selection.begin(),selection.end(),[&](int id){return !contains(remaining,id);}),selection.end());
            status=L"작용기 "+std::to_wstring(count)+L"개를 약어로 바꿨습니다. Ctrl+Z: 되돌리기";
            changed();
        }
    }
    redraw();
}

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
        Document candidate=before;double degrees=numeric(values[1],-36000,36000),factor=numeric(values[2],1,1000)/100.;
        if(values[4]=="0"&&!chosen.empty()){
            report=cleanSelectedStructure(candidate,chosen,values[0]=="1",values[0]=="0",degrees,factor,values[3]=="1",values[3]=="2");
            doc=std::move(candidate);redraw();UpdateWindow(win);return {};
        }
        normalizeChemicalLabels(candidate,chosen);report=cleanStructure(candidate,chosen,values[0]=="1",values[0]=="0");
        std::vector<int> members;for(auto& ids:components)members.insert(members.end(),ids.begin(),ids.end());
        if(degrees||values[3]!="0")transformStructures(candidate,members,degrees,values[3]=="1",values[3]=="2");
        if(factor!=1)scaleStructures(candidate,members,factor);
        std::vector<std::pair<int,bool>> affected;
        for(auto& r:candidate.reactions){bool changedSlot=false;for(auto& slot:r.slots)for(int id:slot.members)if(auto* a=candidate.atom(id);a&&before.atom(id)&&(a->p-before.atom(id)->p).length()>.001)changedSlot=true;if(changedSlot)affected.push_back({r.id,r.cyclic});}
        for(auto& r:affected)optimizeMechanism(candidate,{r.first},r.second);
        doc=std::move(candidate);redraw();UpdateWindow(win);return {};
    };
    std::vector<int> compared;
    for(const auto& comp:components)compared.insert(compared.end(),comp.begin(),comp.end());
    auto drawComparison=[&](HDC dc,const RECT& box,const auto&){
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        const auto& palette=appearance::palette();
        const float width=float(box.right-box.left)/2, height=float(box.bottom-box.top)-28;
        const auto original=makeScene(before.subset(compared),measure);
        const auto revised=makeScene(doc.subset(compared),measure);
        const float scale=float(std::min({2.,(width-28)/std::max({1.,original.bounds.w,revised.bounds.w}),
                                            (height-12)/std::max({1.,original.bounds.h,revised.bounds.h})}));
        const auto draw=[&](const Scene& sc,float x,const std::wstring& title){
            ui::drawLabel(g,title,{x,float(box.top),width,24},12,palette.text,true);
            auto saved=g.Save();
            const auto& b=sc.bounds;
            g.TranslateTransform(x+width/2,float(box.top)+26+height/2);
            g.ScaleTransform(scale,scale);
            g.TranslateTransform(float(-b.x-b.w/2),float(-b.y-b.h/2));
            render(g,sc,false,true);
            g.Restore(saved);
        };
        draw(original,float(box.left),L"정리 전");
        auto title=report.referenceComponents?L"적용 미리보기 · 기준 그림 "+std::to_wstring(report.referenceComponents)+L"개":L"적용 미리보기";
        draw(revised,float(box.left)+width,title);
    };
    auto result=ui::form(win,L"구조 정리 · 미리보기",{
        {L"정리 방식","0",false,{L"권장 · 일치하는 고리 기준 그림 + 2D 정리",L"원본 형태·방향 우선 / 크기 맞춤",L"2D 결합·고리 정리"}},
        {L"추가 회전 (도, +: 시계방향)","0"},{L"배율 (%)","100"},{L"반전","0",false,{L"없음",L"좌우",L"상하"}},
        {L"정리 범위",chosen.empty()?"1":"0",false,{L"선택한 부분만 · 연결 경계와 나머지 원자 고정",L"선택한 부분이 속한 분자 전체"}}
    },update,{},drawComparison);
    if(!result){doc=before;selection=originalSelection;redraw();return;}
    update(*result);if(toNative(doc)!=toNative(before)){history.checkpoint(before);changed();fit();}
    status=L"구조 "+std::to_wstring(report.components)+L"개 정리 · 기준 그림 "+std::to_wstring(report.referenceComponents)+L"개 적용 · 원본 배치 "+std::to_wstring(report.protectedComponents)+L"개 보호 · Ctrl+Z: 되돌리기";redraw();
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
void edit(){if(activeStructureTarget()){editTargetStructure();return;}int id=primary();if(auto* o=textObject(id)){if(tool!=Text)return;beginInline(InlineTarget{id,0,-1,o->points[0],o->fontSize},o->points[0]);return;}if(reaction(id))editReaction(id);else if(plot(id))editPlot(id);else editObject(id);}
void addImage(Vec p){auto path=fileDialog(false,3);if(!path)return;insert(imageDocument(readFile(*path)),p);tool=Select;}


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
std::optional<Document> runStructureEditor(const Document& fragment,HWND owner){
    wchar_t temporary[32768]{},name[32768]{},exe[32768]{};
    if(!GetTempPathW(32768,temporary)||!GetTempFileNameW(temporary,L"YDS",0,name)||!GetModuleFileNameW(nullptr,exe,32768))throw std::runtime_error("구조 편집용 임시 파일을 만들 수 없습니다.");
    fs::path folder(name);fs::remove(folder);fs::create_directory(folder);
    struct Cleanup {fs::path path;~Cleanup(){std::error_code error;fs::remove_all(path,error);}} cleanup{folder};
    auto input=folder/L"structure.ydraw",result=folder/L"result.ydraw";writeFile(input.wstring(),toNative(fragment));
    std::wstring commandLine=L"\""+std::wstring(exe)+L"\" --edit-structure \""+input.wstring()+L"\" \""+result.wstring()+L"\"";
    MONITORINFO monitor{sizeof(monitor)};
    int width=1440,height=940;
    if(GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor)){
        width=std::min(width,int(monitor.rcWork.right-monitor.rcWork.left));
        height=std::min(height,int(monitor.rcWork.bottom-monitor.rcWork.top));
    }
    const RECT placement=ui::ownedWindowRect(owner,width,height);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    startup.dwFlags=STARTF_USESHOWWINDOW|STARTF_USEPOSITION|STARTF_USESIZE;
    startup.wShowWindow=SW_SHOWNORMAL;startup.dwX=placement.left;startup.dwY=placement.top;
    startup.dwXSize=width;startup.dwYSize=height;
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe,commandLine.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process))throw std::runtime_error("독립 구조 편집 창을 열 수 없습니다.");
    CloseHandle(process.hThread);EnableWindow(owner,FALSE);
    bool quit=false;int quitCode=0;
    while(WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT){
        if(MsgWaitForMultipleObjects(1,&process.hProcess,FALSE,100,QS_ALLINPUT)==WAIT_OBJECT_0+1){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
            if(message.message==WM_QUIT){quit=true;quitCode=int(message.wParam);continue;}
            TranslateMessage(&message);DispatchMessageW(&message);
        }}
    }
    DWORD exitCode=0;GetExitCodeProcess(process.hProcess,&exitCode);CloseHandle(process.hProcess);EnableWindow(owner,TRUE);SetForegroundWindow(owner);SetFocus(owner);
    if(quit)PostQuitMessage(quitCode);
    if(exitCode)throw std::runtime_error("구조 편집 창이 종료되었습니다. 원래 메커니즘은 유지됩니다.");
    if(!fs::exists(result))return {};return fromNative(readFile(result.wstring()));
}
std::function<std::optional<Document>(const Document&)> structureEditor=[](const Document& fragment){return runStructureEditor(fragment,win);};
void editTargetStructure(){
    if(isolatedStructureEditing)return;
    if(!activeStructureTarget()){
        auto chosen=selection;bool found=false;
        for(auto& r:doc.reactions)for(size_t i=0;i<r.slots.size()&&!found;i++)if(std::any_of(r.slots[i].members.begin(),r.slots[i].members.end(),[&](int id){return contains(chosen,id);})){targetContainer=r.id;targetSlot=i;targetAbove=false;selection={r.id};found=true;}
        if(!found)for(auto& p:doc.plots)for(size_t i=0;i<p.states.size()&&!found;i++)for(bool above:{false,true}){auto& members=above?p.states[i].above:p.states[i].below;if(std::any_of(members.begin(),members.end(),[&](int id){return contains(chosen,id);})){targetContainer=p.id;targetSlot=i;targetAbove=above;selection={p.id};found=true;break;}}
    }
    if(!activeStructureTarget())throw std::runtime_error("먼저 수정할 단계의 구조 영역을 선택하세요.");
    if(!finishInline(true))return;
    const int container=targetContainer;const size_t slot=targetSlot;const bool above=targetAbove;
    const Document before=doc;const auto fragment=structureInSlot(before,container,slot,above);
    auto result=structureEditor(fragment);if(!result||toNative(*result)==toNative(fragment))return;
    Document candidate=before;replaceSlotStructure(candidate,*result,container,slot,above);
    history.checkpoint(before);doc=std::move(candidate);selection={container};targetContainer=container;targetSlot=slot;targetAbove=above;
    status=L"이 단계의 구조만 수정했습니다. 다른 단계와 배치는 유지됩니다. Ctrl+Z: 되돌리기";changed();
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
std::vector<int> resizeStructure(Document& candidate,const std::vector<int>& ids,double factor,int container,size_t slot,bool above){
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
    constexpr double minPercent=1,maxPercent=1000;
    auto update=[&](const std::vector<std::string>& values)->std::wstring{
        Document candidate=before;auto affected=resizeStructure(candidate,ids,numeric(values[0],minPercent,maxPercent)/100.,container,slot,above);
        doc=std::move(candidate);selection=inSlot?originalSelection:affected;redraw();UpdateWindow(win);return {};
    };
    ui::Field scale{L"배율 (100%: 원래 크기)\n+ / − : 5%p씩 조절","100"};
    scale.stepValue=[=](const std::string& text,int direction){
        double percent=100;
        try{percent=numeric(text,minPercent,maxPercent);}catch(const std::exception&){}
        return number(std::clamp(percent+direction*5,minPercent,maxPercent));
    };
    auto result=ui::form(win,L"구조 크기 조정 · Ctrl+K",{scale},update);
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

} // namespace yd::editor

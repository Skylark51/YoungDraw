#include "win_editor_animation.h"
#include "win_editor_chrome.h"
#include "win_editor_clipboard.h"
#include "win_editor_commands.h"
#include "win_editor_files.h"
#include "win_editor_plot.h"
#include "win_editor_pointer.h"
#include "win_plot_manager.h"
#include "win_editor_reaction.h"
#include "win_editor_render.h"
#include "win_mechanism_editor.h"
#include "win_editor_shortcuts.h"
#include "win_editor_state.h"
#include "win_editor_structure.h"
#include "win_editor_tabs.h"
#include "win_editor_templates.h"
#include "win_editor_text.h"
#include "cdxml.h"
#include "mechanism_data.h"
#include "office.h"
#include "plot_data.h"
#include "win_dialogs.h"
#include "win_theme.h"
#include "win_ui.h"
#include "third_party_notices.h"

namespace yd::editor {
SelectionFormatting selectionFormatting(){
    SelectionFormatting result;
    if(selection.empty()){result.commonFace=0;return result;}
    const auto ids=groupedSelection(selection);
    auto face=[&](int value,const std::vector<TextRun>& runs){
        result.commonFace&=value;
        for(const auto& run:runs)result.commonFace&=run.face;
    };
    for(const auto& atom:doc.atoms)if(contains(ids,atom.id)&&!doc.isLocked(atom.id)){
        result.atoms=true;
        if(atom.id==primary())result.properties=true;
        face(atom.labelFace.value_or(doc.style.labelFace),atom.labelRuns);
    }
    for(const auto& bond:doc.bonds)if(!doc.isLocked(bond.id)&&
        (contains(ids,bond.id)||(contains(ids,bond.a)&&contains(ids,bond.b))))result.stroke=true;
    for(const auto& object:doc.objects)if(contains(ids,object.id)&&!doc.isLocked(object.id)){
        if(object.id==primary())result.properties=true;
        if(object.kind==ObjectKind::Text){result.text=true;face(object.face,object.runs);}
        if((object.kind==ObjectKind::Line||object.kind==ObjectKind::Curve)&&object.head)result.arrows=true;
        if(!object.filled&&(object.kind==ObjectKind::Line||object.kind==ObjectKind::Curve||
            object.kind==ObjectKind::Ellipse||object.kind==ObjectKind::Rectangle))result.stroke=true;
    }
    if(!result.labels())result.commonFace=0;
    return result;
}
std::vector<int> selectionFormattingCommands(const SelectionFormatting& formatting){
    std::vector<int> ids;
    if(formatting.properties)ids.push_back(121);
    if(formatting.labels())for(int id=selectedBoldCommand;id<=selectedFontLargerCommand;++id)ids.push_back(id);
    if(formatting.atoms)ids.push_back(155);
    if(formatting.text)ids.push_back(156);
    if(formatting.arrows)ids.push_back(arrowColorCommand);
    if(formatting.stroke){ids.push_back(selectedStrokeThinnerCommand);ids.push_back(selectedStrokeThickerCommand);}
    return ids;
}
void appendSelectionFormattingMenu(HMENU menu){
    const auto formatting=selectionFormatting();
    const auto ids=selectionFormattingCommands(formatting);
    bool separator=false;
    for(int id:ids){
        if(id==121)continue;
        if(!separator){AppendMenuW(menu,MF_SEPARATOR,0,nullptr);separator=true;}
        auto label=helpText(id);
        if(const auto line=label.find(L'\n');line!=std::wstring::npos)label.resize(line);
        UINT flags=MF_STRING;
        if(id>=selectedBoldCommand&&id<=selectedUnderlineCommand&&
            (formatting.commonFace&(1<<(id-selectedBoldCommand))))flags|=MF_CHECKED;
        AppendMenuW(menu,flags,id,label.c_str());
    }
}
namespace {
void formatSelection(int id){
    if(id==selectedStrokeThinnerCommand||id==selectedStrokeThickerCommand){
        if(adjustSelectedStrokeWidths(id==selectedStrokeThickerCommand))
            status=L"선택한 결합·도형의 선 굵기를 조절했습니다. Ctrl+Z: 되돌리기";
        redraw();return;
    }
    const auto formatting=selectionFormatting();
    if(!formatting.labels())return;
    const auto ids=groupedSelection(selection);
    const Document before=doc;
    const bool toggle=id<=selectedUnderlineCommand;
    const int mask=toggle?1<<(id-selectedBoldCommand):0;
    const bool enable=!(formatting.commonFace&mask);
    auto updateFace=[&](int face){return enable?face|mask:face&~mask;};
    auto nextSize=[&](double size){
        if(id==selectedFontLargerCommand)return size<144?std::clamp(size+1.,4.,144.):size;
        return size>4?std::max(4.,size-1.):size;
    };
    for(auto& atom:doc.atoms)if(contains(ids,atom.id)&&!doc.isLocked(atom.id)){
        if(toggle){
            const int old=atom.labelFace.value_or(doc.style.labelFace),next=updateFace(old);
            if(old!=next)atom.labelFace=next;
            for(auto& run:atom.labelRuns)run.face=updateFace(run.face);
        }else{
            const double old=atom.labelSize.value_or(doc.style.fontSize),next=nextSize(old);
            if(old!=next){atom.labelSize=next;for(auto& run:atom.labelRuns)run.size*=next/old;}
        }
    }
    for(auto& object:doc.objects)if(object.kind==ObjectKind::Text&&contains(ids,object.id)&&!doc.isLocked(object.id)){
        if(toggle){object.face=updateFace(object.face);for(auto& run:object.runs)run.face=updateFace(run.face);}
        else{
            const double old=object.fontSize,next=nextSize(old);
            if(old!=next){object.fontSize=next;for(auto& run:object.runs)run.size*=next/old;}
        }
    }
    if(toNative(doc)!=toNative(before)){
        history.checkpoint(before);changed();
        status=L"선택한 원자·텍스트의 서식을 변경했습니다. Ctrl+Z: 되돌리기";
    }
    redraw();
}
void editDocumentStyle(){
    const Document before=doc;
    const auto& old=before.style;
    const auto fields=std::vector<ui::Field>{
        {L"글꼴",old.labelFont,false,ui::fontFamilies(win),false,4,false,true},
        {L"굵게",(old.labelFace&1)?"1":"0",false,{L"보통",L"굵게"}},
        ui::numericField(L"글자 크기 (pt)\n휠 또는 ↑ / ↓ 키",old.fontSize,4,144,1),
        ui::numericField(L"선 굵기 (pt)\n휠 또는 ↑ / ↓ 키",old.lineWidth,.1,30,.1)
    };
    auto preview=[&](const std::vector<std::string>& values)->std::wstring{
        const auto family=ui::wide(values[0]);
        FontFamily font(family.c_str());
        if(family.empty()||font.GetLastStatus()!=Ok)return L"설치된 글꼴을 선택하세요.";
        Style updated=old;
        updated.labelFont=values[0];
        updated.labelFace=(old.labelFace&~1)|(values[1]=="1"?1:0);
        updated.fontSize=values[2]==number(old.fontSize)?old.fontSize:numeric(values[2],4,144);
        updated.lineWidth=values[3]==number(old.lineWidth)?old.lineWidth:numeric(values[3],.1,30);
        const double textScale=updated.fontSize/old.fontSize;
        const double inkScale=updated.lineWidth/old.lineWidth;
        updated.boldWidth=old.boldWidth*inkScale;
        auto candidate=before;
        candidate.style=updated;
        auto restyleText=[&](std::string& name,int& face,double& size){
            if(updated.labelFont!=old.labelFont)name=updated.labelFont;
            if(updated.labelFace!=old.labelFace)face=(face&~1)|(updated.labelFace&1);
            size*=textScale;
        };
        for(auto& atom:candidate.atoms)if(!candidate.isLocked(atom.id)){
            if(atom.labelSize)*atom.labelSize*=textScale;
            if(atom.labelFace&&updated.labelFace!=old.labelFace)*atom.labelFace=(*atom.labelFace&~1)|(updated.labelFace&1);
            for(auto& run:atom.labelRuns)restyleText(run.font,run.face,run.size);
        }
        for(auto& bond:candidate.bonds)if(!candidate.isLocked(bond.id)){
            if(bond.lineWidth)*bond.lineWidth*=inkScale;
            if(bond.boldWidth)*bond.boldWidth*=inkScale;
        }
        for(auto& object:candidate.objects)if(!candidate.isLocked(object.id)){
            if(object.kind==ObjectKind::Text){
                restyleText(object.font,object.face,object.fontSize);
                for(auto& run:object.runs)restyleText(run.font,run.face,run.size);
            }else if(object.kind!=ObjectKind::Image)object.width*=inkScale;
        }
        doc=std::move(candidate);redraw();UpdateWindow(win);
        return {};
    };
    const auto result=ui::form(win,L"문서 서식",fields,preview);
    if(!result){doc=before;redraw();return;}
    if(toNative(doc)!=toNative(before)){
        history.checkpoint(before);changed();
        status=L"문서 서식을 적용했습니다. Ctrl+Z로 되돌릴 수 있습니다.";
    }
}
}

void command(int id){cancelStageCopy();if(isolatedStructureEditing){if(id==102)id=186;if(id==109)id=187;if((id>=143&&id<=149)||id==100||id==101||id==185||id==140||id==141||id==120||id==130||id==spinMultiplicityPlotCommand||id==reactionProfilePlotCommand||id==167||id==insertBranchArrowsCommand||id==externalMechanismCommand||id==bondAnimationCommand||id==recognizeMechanismCommand)return;}std::optional<InlineTarget> colorTarget;if(id==156&&inlineEdit)colorTarget=inlineTarget;if(!finishInline(true))return;snapPlot=0;snapStart.reset();pendingEnergyGap.reset();
 if(id==thirdPartyNoticesCommand){appearance::messageBox(win,ui::wide(thirdPartyNotices).c_str(),L"오픈소스 라이선스",MB_OK|MB_ICONINFORMATION);return;}
 if(id==184){appearance::picker(win,[](){refreshAppearance();});status=std::wstring(L"테마 · ")+appearance::palette().name+L" / 테마는 출력에 포함되지 않습니다.";redraw();return;}
 if(id==156){pruneLockedSelection();textColorPalette(colorTarget);return;}
 if(id==155){pruneLockedSelection();atomColorPalette();return;}
 if(id==arrowColorCommand){pruneLockedSelection();arrowColorPalette();return;}
 if(id==ligandSpatialCommand){pruneLockedSelection();editLigandSpatial();return;}
 if(id==rotateToolCommand){
    cancel();pruneLockedSelection();
    tool=tool==Rotate?Select:Rotate;
    targetContainer=0;
    status=tool==Rotate?L"직접 회전 · 구조에서 드래그 · Shift: 15° 단위 · Space: 선택 도구":L"선택·이동";
    redraw();return;
 }
 if(id==198||id==199){groupSelected(id==199);return;}
 if(id>=190&&id<=197){alignSelection(id);return;}
 if(id==110||id==125||id==126||(id>=160&&id<=165))repeatAction=[id](){command(id);};
 if(id==includeMechanismCommand){includeSelectedMechanism();return;}
  if(id==mechanismManagerCommand){if(!reaction(primary()))throw std::runtime_error("관리할 메커니즘을 선택하세요.");editReaction(primary());return;}
  if(id==plotManagerCommand){managePlot(primary());return;}
  if(id==plotFontCommand){editPlotFont(primary());return;}
  if(id==energyGapStyleCommand){editEnergyGapStyle(primary());return;}
  if(id==plotWaveWidthCommand||id==plotWaveGapCommand){editPlotAppearance(primary(),7);return;}
  if(id==plotLegendCommand){openPlotManagerTool(primary(),id);return;}
  if(id>=plotAppearanceFirstCommand&&id<=plotAppearanceLastCommand){editPlotAppearance(primary(),id-plotAppearanceFirstCommand);return;}
 if(id==182||id==183){sidebarPage=id-182;showLayers=false;redraw();return;}if(id==219){auto r=ui::form(win,L"오비탈 설정",{{L"모양",std::to_string(int(orbitalKind)),false,{L"s (원형)",L"p (두 로브)",L"d (네 로브)",L"혼성 (큰 로브 / 작은 로브)"}},{L"위상 표시",std::to_string(orbitalPhase),false,{L"흑백 (채움 / 윤곽)",L"윤곽선",L"색상 (청색 / 적색)"}}});if(r){orbitalKind=OrbitalKind(std::stoi((*r)[0]));orbitalPhase=std::stoi((*r)[1]);tool=Orbital;selection.clear();status=L"오비탈: 클릭하거나 중심에서 바깥으로 드래그하세요.";}redraw();return;}if(id==175||id==176){showLayers=id==176;redraw();return;}
 if(id>=177&&id<=181){
    bool images=doc.imagesLocked,graphs=doc.graphsLocked;
    if(id==177)images=!images;if(id==178)graphs=!graphs;
    if(id==179){images=false;graphs=true;}if(id==180){images=true;graphs=false;}if(id==181)images=graphs=false;
    if(images!=doc.imagesLocked||graphs!=doc.graphsLocked){history.checkpoint(doc);doc.imagesLocked=images;doc.graphsLocked=graphs;pruneLockedSelection();status=L"레이어 잠금 적용 · 잠긴 항목은 보이지만 선택·이동·삭제되지 않습니다.";changed();}redraw();return;
 }
 pruneLockedSelection();
 if(id>=selectedBoldCommand&&id<=selectedStrokeThickerCommand){formatSelection(id);return;}
 if(id==dottedBondCommand){
    const auto ids=groupedSelection(selection);
    auto before=doc;bool edited=false;
    for(auto& bond:doc.bonds)if(contains(ids,bond.id)||(contains(ids,bond.a)&&contains(ids,bond.b))){
        if(bond.kind!=BondKind::Dot||!bond.customOrder.empty()||!bond.customDisplay.empty()){
            setBondKind(doc,bond.id,BondKind::Dot);bond.customOrder.clear();bond.customDisplay.clear();edited=true;
        }
    }
    if(edited){history.checkpoint(before);changed();}
    tool=DottedBond;status=L"점선 단일 결합: 기존 결합을 클릭하거나 원자 사이를 드래그하세요. 1 키: 실선 단일 결합.";redraw();return;
 }
 if(id==interactionToolCommand){tool=Interaction;selection.clear();redraw();return;}if(id>=200&&id<=218){if(id==217){auto before=doc;if(delocalizeBonds(doc,selection)){history.checkpoint(before);changed();}tool=Delocalized;}else{tool=Tool(id-200);selection.clear();}redraw();return;}if(id>=301&&id<=418){element=id-300;int aid=hoverAtom?hoverAtom:(doc.atom(primary())?primary():0);if(aid){history.checkpoint(doc);applyAtomElement(aid,element);changed();}redraw();return;}
 firstUseGuide(id);
 switch(id){
 case 230:searchCommands();break;
 case 231:showHelp();break;
 case 232:shortcutSettings();break;
 case 233:showRecent();break;
 case reopenClosedDocumentCommand:reopenClosedDocumentTab();break;
 case bondAnimationCommand:createBondAnimation();break;
 case 143:case 144:case 145:case 146:case 147:case 148:case 149:editMechanismControls(id);break;
 case 186:if(isolatedStructureEditing){writeFile(isolatedStructureResult,toNative(doc));dirty=false;DestroyWindow(win);}else editTargetStructure();break;
 case 187:if(isolatedStructureEditing)DestroyWindow(win);break;
 case 185:manageProjects();break;
 case mullikenSpinCommand:editMullikenSpin();break;
 case molecularAnnotationsCommand:editMolecularAnnotations();break;
 case showMolecularAnnotationsCommand:
    history.checkpoint(doc);
    doc.showMolecularAnnotations=!doc.showMolecularAnnotations;
    status=doc.showMolecularAnnotations?L"분자 주석을 표시합니다.":L"분자 주석을 숨겼습니다.";
    changed();break;
 case abbreviateGroupsCommand:abbreviateGroups();break;
 case 166:{auto candidate=doc;auto ids=groupedSelection(selection);if(activeStructureTarget()){if(auto* r=reaction(targetContainer))ids=r->slots.at(targetSlot).members;if(auto* p=plot(targetContainer))ids=targetAbove?p->states.at(targetSlot).above:p->states.at(targetSlot).below;}if(ids.empty())throw std::runtime_error("벤젠 고리, 연결 결합 또는 구조를 선택하세요.");int count=collapsePhenyl(candidate,ids);if(!count){status=L"Ph로 축약할 고리를 찾지 못했습니다. 벤젠 고리 또는 연결 결합을 선택하세요. 치환기 두 개 이상·융합 고리·주석 원자는 유지됩니다.";redraw();break;}history.checkpoint(doc);doc=std::move(candidate);const auto all=doc.allIds();selection.erase(std::remove_if(selection.begin(),selection.end(),[&](int id){return !contains(all,id);}),selection.end());status=L"벤젠 고리 "+std::to_wstring(count)+L"개를 Ph로 바꿨습니다. Ctrl+Z: 실행 취소";changed();break;}
 case 167:arrangeImported();break;
 case recognizeMechanismCommand:arrangeImported(true);break;
 case insertBranchArrowsCommand:insertBranchArrows();break;case externalMechanismCommand:externalMechanism();break;
 case 168:editArrowCross();break;case 169:branchMechanism();break;
 case 165:resizeSelected();break;case 160:case 161:case 162:case 163:case 164:transformSelected(id);break;case 170:library(L"전체");break;case 171:library(L"리간드");break;case 172:library(L"착물");break;case 173:library(L"기질");break;case 174:library(L"산·시약");break;case 100:newDocumentTab();break;case 109:closeDocumentTab(activeDocument);break;
 case 101:openDocuments(openDocumentFiles());break;
 case copyPngCommand:copyPng();break;
  case exportPngCommand:{auto path=fileDialog(true,5);if(path){writePreview(*path,doc,true);status=L"투명 PNG 그림을 내보냈습니다.";}break;}
  case exportTiffCommand:{auto path=fileDialog(true,6);if(path){writeTiff(*path,selection.empty()?doc:selectedDocument());status=L"고해상도 TIFF 그림을 저장했습니다.";}break;}
  case 102:save();break;case 103:{auto p=fileDialog(true,1);if(p){writeFile(*p,toSvg(scene()));status=L"SVG를 내보냈습니다. 가져온 미지원 객체는 결과를 확인하세요.";}break;}case 108:{auto p=fileDialog(true,2);if(p){writeFile(*p,toCdxml(doc));status=L"CDXML을 내보냈습니다.";}break;}
  case 106:{
      int choice=appearance::messageBox(win,L"JACS 출판용 SVG\n\n예: 단일 열 (240 pt)\n아니요: 두 열 (504 pt)\n취소: 내보내기 중단",
                             L"JACS Publication",MB_YESNOCANCEL|MB_ICONQUESTION);
      if(choice==IDCANCEL)break;
      const double widthPt=choice==IDYES?240.:504.;
      const Scene& prepared=scene();
      auto report=publicationCheck(prepared,widthPt);
      if(!report.warnings.empty()){
          std::wstring details=L"최종 출력: "+std::to_wstring(int(report.widthPt))+L" × "
                              +std::to_wstring(int(std::round(report.heightPt)))+L" pt\n\n";
          for(const auto& warning:report.warnings)details+=L"• "+ui::wide(warning)+L"\n";
          details+=L"\n경고를 검토했습니다. 그래도 SVG를 내보내시겠습니까?";
          if(appearance::messageBox(win,details.c_str(),L"JACS 출력 사전 검사",MB_OKCANCEL|MB_ICONWARNING)!=IDOK)break;
      }
      if(auto path=fileDialog(true,1)){
          writeFile(*path,toSvg(prepared,widthPt));
          status=L"JACS 출판 크기로 SVG를 내보냈습니다. 제출 전 최종 시각 검사를 수행하세요.";
      }
      break;
  }
 case 153:case 154:{auto chosen=selectedDocument();if(chosen.atoms.empty()&&chosen.bonds.empty())throw std::runtime_error("원자나 결합을 먼저 선택하세요.");history.checkpoint(doc);std::string color=id==153?"#a2a9b2":"#000000";for(auto& a:chosen.atoms)if(auto* target=doc.atom(a.id)){target->color=color;target->protect=true;for(auto& run:target->labelRuns)run.color=color;}for(auto& b:chosen.bonds)if(auto* target=doc.bond(b.id)){target->color=color;if(!target->depthColor.empty())target->depthColor=color;}changed();break;}
 case 104:if(history.undo(doc)){selection.clear();targetContainer=0;changed();}break;case 105:if(history.redo(doc)){selection.clear();targetContainer=0;changed();}break;case 107:fit();break;case 110:clean();break;case 111:library();break;
 case 112:saveCurrentTemplate();break;
 case 113:{auto p=fileDialog(false);if(p)insert(fromCdxml(readFile(*p)),center());break;}
 case 120:newReaction();break;
 case 121:if(reaction(primary()))editReaction(primary());else if(textObject(primary())&&tool!=Text)editObject(primary());else edit();break;case 122:case 128:{
     auto* r=reaction(primary());if(!r||doc.isLocked(r->id)||r->slots.empty()||r->slots.size()>=40||r->links.size()>=100)break;
     const int owner=r->id;const size_t source=targetContainer==owner?std::min(targetSlot,r->slots.size()-1):r->slots.size()-1;
     const bool branch=!r->cyclic&&source+1!=r->slots.size();
     auto details=branch?mechanismAdditionDetails(win,*r,source,true):std::optional<std::vector<std::string>>{};
     if(branch&&!details)break;
     Document candidate=doc;auto& edited=*std::find_if(candidate.reactions.begin(),candidate.reactions.end(),[&](auto& row){return row.id==owner;});
     if(edited.cyclic&&!edited.explicitConnections())splitMechanismLink(edited,source);else appendMechanismStage(edited,id==128,source);
     if(details){edited.slots[source].label=(*details)[0];edited.slots[source].energy=(*details)[1];edited.slots.back().label=(*details)[2];edited.slots.back().energy=(*details)[3];edited.above.back()=(*details)[4];edited.below.back()=(*details)[5];}
     optimizeMechanism(candidate,{owner},edited.cyclic);history.checkpoint(doc);doc=std::move(candidate);changed();fit();break;
 }case 123:fillTarget(false);break;case 124:fillTarget(true);break;
 case 130:case spinMultiplicityPlotCommand:case reactionProfilePlotCommand:{
     const auto choice=ui::graphChoice(win,id==spinMultiplicityPlotCommand?4:0);if(!choice)break;
     sidebarPage=1;history.checkpoint(doc);EnergyPlot p=newEnergyPlot(choice->mode,choice->freeEnergy);
     p.id=doc.nextId++;
     Document graph;graph.style=doc.style;graph.plots.push_back(p);
     const auto bounds=makeScene(graph,measure).bounds;
     const auto view=canvasBox();
     const Vec target=center();
     zoom=std::min({zoom,std::max(1.,view.Width-65.)/bounds.w,std::max(1.,view.Height-110.)/bounds.h});
     const Vec midpoint{bounds.x+bounds.w/2,bounds.y+bounds.h/2};
     origin={view.X+view.Width/2-target.x*zoom,view.Y+view.Height/2-target.y*zoom};
     p.p=target-midpoint;
     doc.plots.push_back(p);selection={p.id};tool=Select;changed();editPlot(p.id);break;}
 case 131:if(plot(primary()))editPlot(primary());else throw std::runtime_error("에너지 그래프를 선택하세요.");break;case 132:if(plot(primary()))openPlotManagerTool(primary(),132);else throw std::runtime_error("에너지 그래프를 선택하세요.");break;case 133:if(plot(primary()))openPlotManagerTool(primary(),133);else throw std::runtime_error("에너지 그래프를 선택하세요.");break;
 case 125:case 126:{Document before=doc;try{optimizeMechanism(doc,selection,id==126);}catch(...){doc=before;throw;}history.checkpoint(before);selection.clear();changed();fit();break;}case 127:reactionSettings(primary());break;case 135:addBilateralBranch(primary());break;case 137:openPlotManagerTool(primary(),137);break;case changePlotStyleCommand:changePlotStyle(primary());break;case plotAxisLengthsCommand:editPlotAxisLengths(primary());break;case 138:publicationLayout(primary());break;case 139:openPlotManagerTool(primary(),139);break;case 136:snapPlotStages(primary());break;case 142:editPlotColors(primary());break;case 151:editPlotCrossings(primary());break;case plotAnnotationsCommand:editPlotAnnotations(primary());break;case addEnergyGapCommand:manageEnergyGaps(primary());break;case 150:editDocumentStyle();break;case 134:guides=!guides;break;case 140:newDocumentTab(mechanismGuide());sidebarPage=0;tool=Select;status=L"메커니즘 예제·도움말입니다. 확대하여 직선·촉매 순환·분기·합류 예제를 편집하고 CDXML로 저장할 수 있습니다.";changed();fit();break;case 141:sidebarPage=1;history.checkpoint(doc);doc=coordinateDemo();selection.clear();targetContainer=0;filePath.clear();tool=Select;status=L"배치 예제입니다. Ctrl+Z로 이전 문서를 복원합니다.";changed();fit();break;
 }redraw();}

} // namespace yd::editor

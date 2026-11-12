#include "win_editor_animation.h"
#include "win_editor_chrome.h"
#include "win_editor_clipboard.h"
#include "win_editor_commands.h"
#include "win_editor_files.h"
#include "win_editor_plot.h"
#include "win_editor_reaction.h"
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

namespace yd::editor {

void command(int id){cancelStageCopy();if(isolatedStructureEditing){if(id==102)id=186;if(id==109)id=187;if((id>=143&&id<=149)||id==100||id==101||id==185||id==140||id==141||id==120||id==130||id==spinMultiplicityPlotCommand||id==167||id==insertBranchArrowsCommand||id==externalMechanismCommand||id==bondAnimationCommand||id==recognizeMechanismCommand)return;}std::optional<InlineTarget> colorTarget;if(id==156&&inlineEdit)colorTarget=inlineTarget;if(!finishInline(true))return;snapPlot=0;snapStart.reset();
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
 pruneLockedSelection();
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
 if(id==interactionToolCommand){tool=Interaction;selection.clear();redraw();return;}if(id>=200&&id<=218){if(id==217){auto before=doc;if(delocalizeBonds(doc,selection)){history.checkpoint(before);changed();}tool=Delocalized;}else{tool=Tool(id-200);selection.clear();}redraw();return;}if(id>=301&&id<=418){element=id-300;int aid=hoverAtom?hoverAtom:(doc.atom(primary())?primary():0);if(aid){history.checkpoint(doc);auto a=doc.atom(aid);setAtomElement(doc,aid,element);a->label.clear();a->labelRuns.clear();a->explicitH=-1;changed();}redraw();return;}
 firstUseGuide(id);
 switch(id){
 case 230:searchCommands();break;
 case 231:showHelp();break;
 case 232:shortcutSettings();break;
 case 233:showRecent();break;
 case bondAnimationCommand:createBondAnimation();break;
 case 143:case 144:case 145:case 146:case 147:case 148:case 149:editMechanismControls(id);break;
 case 186:if(isolatedStructureEditing){writeFile(isolatedStructureResult,toNative(doc));dirty=false;DestroyWindow(win);}else editTargetStructure();break;
 case 187:if(isolatedStructureEditing)DestroyWindow(win);break;
 case 185:manageProjects();break;
 case mullikenSpinCommand:editMullikenSpin();break;
 case abbreviateGroupsCommand:abbreviateGroups();break;
 case 166:{auto candidate=doc;auto ids=groupedSelection(selection);if(ids.empty())throw std::runtime_error("벤젠이 포함된 그룹이나 고리를 선택하세요.");int count=collapsePhenyl(candidate,ids);if(!count)throw std::runtime_error("단일 결합 하나로 연결된 비치환 벤젠을 찾지 못했습니다.");history.checkpoint(doc);doc=std::move(candidate);selection.erase(std::remove_if(selection.begin(),selection.end(),[&](int id){auto all=doc.allIds();return !contains(all,id);}),selection.end());changed();break;}
 case 167:arrangeImported();break;
 case recognizeMechanismCommand:arrangeImported(true);break;
 case insertBranchArrowsCommand:insertBranchArrows();break;case externalMechanismCommand:externalMechanism();break;
 case 168:editArrowCross();break;case 169:branchMechanism();break;
 case 165:resizeSelected();break;case 160:case 161:case 162:case 163:case 164:transformSelected(id);break;case 170:library(L"전체");break;case 171:library(L"리간드");break;case 172:library(L"착물");break;case 173:library(L"기질");break;case 174:library(L"산·시약");break;case 100:newDocumentTab();break;case 109:closeDocumentTab(activeDocument);break;
 case 101:openDocuments(openDocumentFiles());break;
 case 102:save();break;case 103:{auto p=fileDialog(true,1);if(p){writeFile(*p,toSvg(scene()));status=L"SVG를 내보냈습니다. 가져온 미지원 객체는 결과를 확인하세요.";}break;}case 108:{auto p=fileDialog(true,2);if(p){writeFile(*p,toCdxml(doc));status=L"CDXML을 내보냈습니다.";}break;}
  case 106:{
      int choice=MessageBoxW(win,L"JACS 출판용 SVG\n\n예: 단일 열 (240 pt)\n아니요: 두 열 (504 pt)\n취소: 내보내기 중단",
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
          if(MessageBoxW(win,details.c_str(),L"JACS 출력 사전 검사",MB_OKCANCEL|MB_ICONWARNING)!=IDOK)break;
      }
      if(auto path=fileDialog(true,1)){
          writeFile(*path,toSvg(prepared,widthPt));
          status=L"JACS 출판 크기로 SVG를 내보냈습니다. 제출 전 최종 시각 검사를 수행하세요.";
      }
      break;
  }
 case 153:case 154:{auto chosen=selectedDocument();if(chosen.atoms.empty()&&chosen.bonds.empty())throw std::runtime_error("원자나 결합을 먼저 선택하세요.");history.checkpoint(doc);std::string color=id==153?"#a2a9b2":"#000000";for(auto& a:chosen.atoms)if(auto* target=doc.atom(a.id)){target->color=color;target->protect=true;for(auto& run:target->labelRuns)run.color=color;}for(auto& b:chosen.bonds)if(auto* target=doc.bond(b.id))target->color=color;changed();break;}
 case 104:if(history.undo(doc)){selection.clear();targetContainer=0;changed();}break;case 105:if(history.redo(doc)){selection.clear();targetContainer=0;changed();}break;case 107:fit();break;case 110:clean();break;case 111:library();break;
 case 112:saveCurrentTemplate();break;
 case 113:{auto p=fileDialog(false);if(p)insert(fromCdxml(readFile(*p)),center());break;}
 case 120:newReaction();break;
 case 121:if(reaction(primary()))editReaction(primary());else edit();break;case 122:case 128:{auto* r=reaction(primary());if(!r)throw std::runtime_error("반응 메커니즘을 선택하세요.");if(r->slots.size()>=40)throw std::runtime_error("메커니즘은 최대 40칸까지 추가할 수 있습니다.");history.checkpoint(doc);appendMechanismStage(*r,id==128,targetContainer==r->id?targetSlot:size_t(-1));optimizeMechanism(doc,{r->id},r->cyclic);changed();fit();break;}case 123:fillTarget(false);break;case 124:fillTarget(true);break;
 case 130:case spinMultiplicityPlotCommand:{
     sidebarPage=1;history.checkpoint(doc);
     EnergyPlot p;
     if(id==spinMultiplicityPlotCommand)p=spinMultiplicityPlot();
     else{
         p.stages=defaultMechanismStages();
         p.states={{"R",0,{},{}},{"TS",20,{},{}},{"P",-10,{},{}}};
         p.series={{"Path 1","#000000",{0,20,-10},1,3.5}};
     }
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
 case 131:if(plot(primary()))editPlot(primary());else throw std::runtime_error("에너지 그래프를 선택하세요.");break;case 132:if(plot(primary()))editSeries(primary(),true);else throw std::runtime_error("에너지 그래프를 선택하세요.");break;case 133:if(plot(primary()))editSeries(primary(),false);else throw std::runtime_error("에너지 그래프를 선택하세요.");break;
 case 125:case 126:{Document before=doc;try{optimizeMechanism(doc,selection,id==126);}catch(...){doc=before;throw;}history.checkpoint(before);selection.clear();changed();fit();break;}case 127:reactionSettings(primary());break;case 135:addBilateralBranch(primary());break;case 137:plotSettings(primary());break;case 138:publicationLayout(primary());break;case 139:editPlotStages(primary());break;case 136:snapPlotStages(primary());break;case 142:editPlotColors(primary());break;case 151:editPlotCrossings(primary());break;case 150:history.checkpoint(doc);applyHouseStyle(doc);status=L"Arial 굵게, 14 pt 라벨, 2 pt 결합선을 적용했습니다.";changed();fit();break;case 134:guides=!guides;break;case 140:case 141:sidebarPage=id==141?1:0;history.checkpoint(doc);doc=id==140?mechanismDemo():coordinateDemo();selection.clear();targetContainer=0;filePath.clear();tool=Select;status=L"배치 예제입니다. Ctrl+Z로 이전 문서를 복원합니다.";changed();fit();break;
 }redraw();}

} // namespace yd::editor

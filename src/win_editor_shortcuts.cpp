#include "win_editor_clipboard.h"
#include "win_editor_commands.h"
#include "win_editor_plot.h"
#include "win_editor_pointer.h"
#include "win_editor_reaction.h"
#include "win_editor_shortcuts.h"
#include "win_editor_state.h"
#include "win_editor_structure.h"
#include "win_editor_tabs.h"
#include "cdxml.h"
#include "office.h"
#include "win_dialogs.h"
#include "win_scripts.h"
#include "win_ui.h"

namespace yd::editor {

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

void alignSelection(int id){
    Document candidate=doc;
    if(alignObjects(candidate,selection,AlignMode(id-190))){history.checkpoint(doc);doc=std::move(candidate);changed();status=L"선택한 구조·객체를 정렬했습니다. Ctrl+Z로 되돌릴 수 있습니다.";}
    else status=L"정렬은 두 개 이상, 간격 배분은 세 개 이상의 구조·객체를 선택하세요.";
    repeatAction=[id](){alignSelection(id);};redraw();
}
void nudgeSelection(Vec delta){
    if(selection.empty())return;auto before=doc;doc.translate(selection,delta);
    if(toNative(before)!=toNative(doc)){history.checkpoint(before);changed();repeatAction=[delta](){nudgeSelection(delta);};}
}
void rotateSelection(double degrees){
    if(selection.empty())return;auto candidate=doc;auto ids=transformStructures(candidate,selection,degrees);
    history.checkpoint(doc);doc=std::move(candidate);selection=std::move(ids);changed();repeatAction=[degrees](){rotateSelection(degrees);};
}
void copyCdxmlText(){
    if(selection.empty())return;auto value=ui::wide(toCdxml(selectedDocument()));SIZE_T bytes=(value.size()+1)*sizeof(wchar_t);
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes);if(!memory)throw std::runtime_error("클립보드 메모리를 만들 수 없습니다.");
    void* data=GlobalLock(memory);if(!data){GlobalFree(memory);throw std::runtime_error("클립보드 메모리를 열 수 없습니다.");}memcpy(data,value.c_str(),bytes);GlobalUnlock(memory);
    if(!OpenClipboard(win)){GlobalFree(memory);throw std::runtime_error("클립보드를 열 수 없습니다.");}
    bool ok=EmptyClipboard()&&SetClipboardData(CF_UNICODETEXT,memory);CloseClipboard();if(!ok){GlobalFree(memory);throw std::runtime_error("CDXML 복사에 실패했습니다.");}
    status=L"선택한 구조를 CDXML 텍스트로 복사했습니다.";
}
void duplicateReaction(Vec direction){
    if(selection.empty())return;Document fragment=selectedDocument();auto bounds=makeScene(fragment).bounds;
    if(fragment.allIds().empty())return;Vec delta{direction.x*(bounds.w+100),direction.y*(bounds.h+100)};
    history.checkpoint(doc);int first=doc.nextId;doc.append(fragment,delta);selection.clear();for(int id:doc.allIds())if(id>=first)selection.push_back(id);
    Vec center{bounds.x+bounds.w/2,bounds.y+bounds.h/2};double half=direction.x?bounds.w/2:bounds.h/2;
    Object arrow;arrow.id=doc.nextId++;arrow.head=1;arrow.width=doc.style.lineWidth;arrow.color="#ff0000";
    arrow.points={center+direction*(half+15),center+delta-direction*(half+15)};doc.objects.push_back(arrow);changed();
    repeatAction=[direction](){duplicateReaction(direction);};
}
void formatSelectedText(int mask,bool plain=false){
    auto before=doc;for(auto& o:doc.objects)if(contains(selection,o.id)&&o.kind==ObjectKind::Text){o.face=plain?0:o.face^mask;for(auto& run:o.runs)run.face=plain?0:run.face^mask;}
    if(toNative(before)!=toNative(doc)){history.checkpoint(before);changed();repeatAction=[mask,plain](){formatSelectedText(mask,plain);};}
}
void key(WPARAM k){
    if(mechanismConnectionDrag){if(k==VK_ESCAPE)cancel();return;}
    if(mechanismConnecting&&k==VK_ESCAPE){cancel();return;}
    if(isolatedStructureEditing&&(GetKeyState(VK_CONTROL)&0x8000)){if(k==VK_TAB)return;if(k=='S'){command(186);return;}}
    if(snapPlot){if(k==VK_ESCAPE){cancel();status=L"화살표 자석 배치를 취소했습니다.";}return;}
    pruneLockedSelection();bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0,alt=(GetKeyState(VK_MENU)&0x8000)!=0;
    if(dragging||drawing||boxing||panning){if(k==VK_ESCAPE)cancel();return;}
    // Test all modifiers before ordinary clipboard/file shortcuts. Alt combinations
    // arrive as WM_SYSKEYDOWN on Windows and must never fall through to Ctrl+C/V.
    if(ctrl&&alt){
        if(shift){std::string keys="LCR TMBHV";keys.erase(std::remove(keys.begin(),keys.end(),' '),keys.end());auto index=keys.find(char(k));
            if(index!=std::string::npos){command(190+int(index));return;}
            if(k=='P'){pasteClipboard();return;}}
        status=L"이 ChemDraw 단축키에 해당하는 기능은 아직 지원하지 않습니다.";redraw();return;
    }
    if(alt){
        if(k=='K'){auto candidate=doc;if(toggleAromaticDisplay(candidate,selection)){history.checkpoint(doc);doc=std::move(candidate);changed();}redraw();return;}
        if(k==VK_LEFT||k==VK_RIGHT||k==VK_UP||k==VK_DOWN){rotateSelection(k==VK_LEFT?-1:k==VK_RIGHT?1:k==VK_UP?-15:15);redraw();return;}
        return;
    }
    if(ctrl){
        if(shift&&k=='P'){command(166);return;}
        if(shift&&(k=='H'||k=='V')){command(k=='H'?160:161);return;}
        if(shift&&k=='X'){command(125);return;}
        if(shift&&(k=='C'||k=='L')){auto before=doc;for(auto& o:doc.objects)if(contains(selection,o.id)&&o.kind==ObjectKind::Text)o.centerText=k=='C';if(toNative(before)!=toNative(doc)){history.checkpoint(before);changed();}redraw();return;}
        if(shift&&(k=='N'||k=='M'||k=='J'||k=='R'||k=='D')){status=L"이 ChemDraw 단축키에 해당하는 기능은 아직 지원하지 않습니다.";redraw();return;}
        switch(k){
        case VK_TAB:if(!documents.empty())switchDocumentTab((activeDocument+documents.size()+(shift?-1:1))%documents.size());break;
        case 'W':command(109);break;case 'N':command(100);break;case 'O':command(101);break;case 'S':save(shift);break;
        case 'Z':command(shift?105:104);break;case 'Y':if(repeatAction){auto repeat=repeatAction;repeat();}break;
        case 'A':selection=doc.editableIds();tool=Select;break;case 'C':copy();break;case 'D':copyCdxmlText();break;
        case 'X':copy();if(!selection.empty()){history.checkpoint(doc);doc.erase(selection);selection.clear();changed();}break;
        case 'V':pasteClipboard();break;case 'K':command(shift?110:165);break;case 'R':command(164);break;
        case 'B':formatSelectedText(1);break;case 'I':formatSelectedText(2);break;case 'U':formatSelectedText(4);break;case 'T':formatSelectedText(0,true);break;
        case 'G':command(shift?199:198);break;
        case VK_LEFT:duplicateReaction({-1,0});break;case VK_RIGHT:duplicateReaction({1,0});break;case VK_UP:duplicateReaction({0,-1});break;case VK_DOWN:duplicateReaction({0,1});break;
        case VK_OEM_PLUS:zoom=std::min(12.,zoom*1.15);break;case VK_OEM_MINUS:zoom=std::max(.12,zoom/1.15);break;
        }redraw();return;
    }
    if(k==VK_LEFT||k==VK_RIGHT||k==VK_UP||k==VK_DOWN){double step=shift?10:1;nudgeSelection({k==VK_LEFT?-step:k==VK_RIGHT?step:0,k==VK_UP?-step:k==VK_DOWN?step:0});redraw();return;}
    if(k==VK_ESCAPE){selectedCycleOwner=0;selectedCycleArrow=-1;tool=Select;selection.clear();targetContainer=0;}
    if(k==VK_DELETE||k==VK_BACK){if(activeStructureTarget()&&reaction(targetContainer)){command(144);return;}if(deleteSelectedCycleArrow())return;if(!selection.empty()){history.checkpoint(doc);doc.erase(selection);selection.clear();changed();}}
    if(k==VK_F1)MessageBoxW(win,L"Ctrl+K: 크기 / Ctrl+R: 회전 / Ctrl+Shift+K: 구조 정리\nCtrl+Shift+Alt+L/C/R: 왼쪽 / 가로 중앙 / 오른쪽 정렬\nCtrl+Shift+Alt+T/M/B: 위 / 세로 중앙 / 아래 정렬\nCtrl+Shift+Alt+H/V: 가로 / 세로 간격 배분\nCtrl+Shift+H/V: 좌우 / 상하 반전\n방향키: 1 pt 이동 / Shift+방향키: 10 pt 이동\nAlt+좌우: 1° 회전 / Alt+상하: 15° 회전\nCtrl+방향키: 반응 화살표와 복제 구조 추가\nCtrl+G / Ctrl+Shift+G: 그룹 / 그룹 해제\nCtrl+D: CDXML 복사 / Ctrl+Shift+X: 반응 배치\nCtrl+Z: 실행 취소 / Ctrl+Shift+Z: 다시 실행 / Ctrl+Y: 마지막 작업 반복\nShift+G: 오비탈 도구 / Space: 선택 도구\n텍스트 편집 F9/F10: 아래 / 위첨자\nCtrl+우클릭 드래그: 화면 이동",L"YoungDraw · ChemDraw 호환 단축키",MB_OK);
    if(k==VK_RETURN){if(hoverAtom){selection={hoverAtom};editObject(hoverAtom);}else edit();}redraw();
}

} // namespace yd::editor

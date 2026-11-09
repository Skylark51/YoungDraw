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

namespace {
bool adjustSelectedStrokeWidths(bool increase){
    if(selection.empty())return false;
    const auto ids=groupedSelection(selection);
    const auto adjusted=[increase](double width){
        constexpr double factor=1.15;
        if(increase)return width<12. ? std::clamp(width*factor,.1,12.) : width;
        return width>.1 ? std::max(.1,width/factor) : width;
    };
    bool found=false,edited=false;
    for(auto& bond:doc.bonds){
        if(!contains(ids,bond.id)&&!(contains(ids,bond.a)&&contains(ids,bond.b)))continue;
        found=true;
        const double line=bond.lineWidth.value_or(doc.style.lineWidth);
        const double bold=bond.boldWidth.value_or(doc.style.boldWidth);
        const double nextLine=adjusted(line),nextBold=adjusted(bold);
        if(nextLine==line&&nextBold==bold)continue;
        if(!edited)history.checkpoint(doc);
        if(nextLine!=line)bond.lineWidth=nextLine;
        if(nextBold!=bold)bond.boldWidth=nextBold;
        edited=true;
    }
    for(auto& object:doc.objects){
        if((object.kind!=ObjectKind::Ellipse&&object.kind!=ObjectKind::Curve)||
           object.filled||!contains(ids,object.id))continue;
        found=true;
        const double nextWidth=adjusted(object.width);
        if(nextWidth==object.width)continue;
        if(!edited)history.checkpoint(doc);
        object.width=nextWidth;
        edited=true;
    }
    if(edited)changed();
    return found;
}
}

void hotkey(char c){
    if(stageCopyOwner)return;
 if(snapPlot)return;
 // The main minus key produces '_' with Shift, while the keypad produces '-'.
 if((c=='+'||c=='-'||c=='_')&&(GetKeyState(VK_SHIFT)&0x8000)&&
    !mechanismConnectionDrag&&adjustSelectedStrokeWidths(c=='+'))return;
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
namespace {
constexpr auto preferences=L"Software\\YoungDraw\\Workflow";
DWORD preference(const wchar_t* name){
    DWORD value=0,size=sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER,preferences,name,RRF_RT_REG_DWORD,nullptr,&value,&size);
    return value;
}
void setPreference(const wchar_t* name,DWORD value){
    RegSetKeyValueW(HKEY_CURRENT_USER,preferences,name,REG_DWORD,&value,sizeof(value));
}
struct Guide {int command;const wchar_t* title;const wchar_t* steps;};
const Guide guidesList[]={
    {bondAnimationCommand,L"GIF · 분기 경로와 저장",L"1. 메커니즘을 선택하고 GIF 명령을 실행합니다. 분기에서는 구조를 화살표 순서대로 클릭합니다.\r\n2. 썸네일을 드래그해 순서를 바꾸고 반복·출력 설정을 지정합니다.\r\n3. GIF 저장을 눌러 진행률, 예상 용량과 남은 시간을 확인합니다."},
    {137,L"그래프 · 에너지 단위 변환",L"1. 그래프를 선택하고 그래프·축 설정을 엽니다.\r\n2. 기본 탭에서 kJ/mol 등을 고르고 에너지와 축 수치도 변환을 선택합니다."},
    {185,L"프로젝트 · 검색과 ZIP 이동",L"1. 내 프로젝트에서 검색을 눌러 프로젝트, 구조 또는 노트의 단어를 입력합니다.\r\n2. 내보내기로 ZIP을 저장하고 다른 PC의 불러오기에서 그 ZIP을 선택합니다."},
    {145,L"메커니즘 · 단계 연결",L"1. 메커니즘을 선택하고 단계 연결을 실행합니다.\r\n2. 시작 구조 칸에서 도착 구조 칸까지 드래그합니다."}
};
}
void firstUseGuide(int id){
    if(!IsWindowVisible(win))return;
    static std::vector<int> shown;
    for(const auto& guide:guidesList)if(guide.command==id){
        const auto name=L"Guide"+std::to_wstring(id);
        if(preference(name.c_str())||std::find(shown.begin(),shown.end(),id)!=shown.end())return;
        shown.push_back(id);
        const auto text=std::wstring(guide.steps)+L"\r\n\r\n이 안내를 다시 보지 않으시겠습니까?";
        if(MessageBoxW(win,text.c_str(),guide.title,MB_YESNO|MB_DEFBUTTON2|MB_ICONINFORMATION)==IDYES)
            setPreference(name.c_str(),1);
        return;
    }
}
void shortcutSettings(){
    auto choice=ui::form(win,L"단축키 호환",{{L"Ctrl+Y 동작",std::to_string(preference(L"WindowsKeys")?1:0),false,
        {L"ChemDraw형 · 마지막 작업 반복",L"Windows형 · 다시 실행"}}});
    if(choice)setPreference(L"WindowsKeys",choice->front()=="1");
}
void showHelp(){
    std::vector<ui::SearchItem> entries;
    for(const auto& guide:guidesList)entries.push_back({guide.title,guide.steps});
    entries.push_back({L"편집 · 실행 취소와 다시 실행",
        L"Ctrl+Z 실행 취소 / Ctrl+Shift+Z 다시 실행. Ctrl+Y는 보기 → 단축키 호환에서 작업 반복 또는 다시 실행으로 지정합니다."});
    entries.push_back({L"구조 · 정리, 크기, 회전",
        L"예: 구조를 선택하고 Ctrl+Shift+K로 정리합니다. Ctrl+K 크기 / Ctrl+R 회전 / Ctrl+G 그룹 / Ctrl+Shift+G 그룹 해제. 결합·타원·곡선을 선택하고 Shift와 + 또는 - 키를 눌러 선 두께를 조정합니다."});
    entries.push_back({L"배치 · 정렬, 이동, 반전",
        L"Ctrl+Shift+Alt+L/C/R 가로 정렬, T/M/B 세로 정렬, H/V 간격 배분. Ctrl+Shift+H/V 반전. 방향키 1 pt, Shift+방향키 10 pt, Alt+방향키 회전, Ctrl+방향키 반응 복제."});
    entries.push_back({L"그리기 · 도구, 첨자, 클립보드",
        L"Space 선택 도구, Shift+G 오비탈, F9/F10 아래·위첨자, Ctrl+D CDXML 복사, Ctrl+Shift+X 반응 배치. Ctrl+우클릭 드래그로 화면을 이동합니다."});
    entries.push_back({L"작업 공간 · 스크롤, 도구 고정, 명령 검색",
        L"작은 창에서는 도구 영역에 마우스를 두고 휠을 돌립니다. 그리기 도구를 우클릭해 상단에 고정합니다. Ctrl+Shift+F를 누르고 명령을 검색해 Enter로 실행합니다."});
    entries.push_back({L"메커니즘 · 에너지 직접 편집과 이동",
        L"에너지를 더블클릭해 값을 바꿉니다. 드래그하면 붙여넣을 수 있는 구조 영역이 청록색으로 표시됩니다."});
    ui::search(win,L"YoungDraw 도움말 · F1",entries,L"확인",true);
}
void searchCommands(){
    std::vector<ui::SearchItem> entries;
    std::vector<int> commands;
    HMENU bar=GetMenu(win);
    for(int i=0;i<GetMenuItemCount(bar);++i){
        wchar_t group[96]{};GetMenuStringW(bar,i,group,96,MF_BYPOSITION);
        auto menu=GetSubMenu(bar,i);
        for(int j=0;j<GetMenuItemCount(menu);++j){
            const UINT id=GetMenuItemID(menu,j);
            if(!id||id==UINT(-1)||(GetMenuState(menu,j,MF_BYPOSITION)&(MF_GRAYED|MF_DISABLED)))continue;
            wchar_t label[240]{};GetMenuStringW(menu,j,label,240,MF_BYPOSITION);
            if(id==230)continue;
            std::wstring title(label);
            if(const auto pos=title.find(L'\t');pos!=std::wstring::npos)title.replace(pos,1,L"   ·   ");
            entries.push_back({std::move(title),group});commands.push_back(int(id));
        }
    }
    if(auto selected=ui::search(win,L"명령 검색 · Ctrl+Shift+F",entries,L"실행"))command(commands[*selected]);
}
void key(WPARAM k){
    if(stageCopyOwner){
        if(k==VK_ESCAPE)cancel();
        else if(k==VK_RETURN||(k=='C'&&(GetKeyState(VK_CONTROL)&0x8000)))finishStageCopy();
        return;
    }
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
        if(shift&&k=='F'){searchCommands();return;}
        if(shift&&k=='P'){command(166);return;}
        if(shift&&(k=='H'||k=='V')){command(k=='H'?160:161);return;}
        if(shift&&k=='X'){command(125);return;}
        if(shift&&(k=='C'||k=='L')){auto before=doc;for(auto& o:doc.objects)if(contains(selection,o.id)&&o.kind==ObjectKind::Text)o.centerText=k=='C';if(toNative(before)!=toNative(doc)){history.checkpoint(before);changed();}redraw();return;}
        if(shift&&(k=='N'||k=='M'||k=='J'||k=='R'||k=='D')){status=L"이 ChemDraw 단축키에 해당하는 기능은 아직 지원하지 않습니다.";redraw();return;}
        switch(k){
        case VK_TAB:if(!documents.empty())switchDocumentTab((activeDocument+documents.size()+(shift?-1:1))%documents.size());break;
        case 'W':command(109);break;case 'N':command(100);break;case 'O':command(101);break;case 'S':save(shift);break;
        case 'Z':command(shift?105:104);break;case 'Y':if(preference(L"WindowsKeys"))command(105);else if(repeatAction){auto repeat=repeatAction;repeat();}break;
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
    if(k==VK_F1)showHelp();
    if(k==VK_RETURN){if(hoverAtom){selection={hoverAtom};editObject(hoverAtom);}else edit();}redraw();
}

} // namespace yd::editor

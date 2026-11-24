#include "win_button_help.h"
#include "win_theme.h"
#include "win_editor_chrome.h"
#include "win_editor_commands.h"
#include <commctrl.h>
#include <map>
#include <algorithm>
#include <cwctype>
#include <set>
namespace ui {
namespace {
constexpr wchar_t contextKey[]=L"YoungDraw.ButtonHelp.Context";
constexpr UINT attachMessage=WM_APP+177;
HWND dispatcher=nullptr;HHOOK hook=nullptr;
struct Entry{std::wstring text;HWND tip=nullptr,control=nullptr;RECT rect{};bool native=false;};
struct Owner{HWND window=nullptr,tip=nullptr;std::map<UINT_PTR,Entry> entries;};
std::wstring textOf(HWND h){int length=GetWindowTextLengthW(h);std::wstring text(size_t(length)+1,L'\0');GetWindowTextW(h,text.data(),length+1);text.resize(length);return text;}
std::wstring classOf(HWND h){wchar_t name[128]{};GetClassNameW(h,name,128);return name;}
LRESULT CALLBACK ownerProcedure(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){auto* owner=reinterpret_cast<Owner*>(data);if(message==WM_NCDESTROY){RemovePropW(h,contextKey);RemoveWindowSubclass(h,ownerProcedure,id);auto result=DefSubclassProc(h,message,wp,lp);delete owner;return result;}return DefSubclassProc(h,message,wp,lp);}
Owner* context(HWND h){auto* owner=static_cast<Owner*>(GetPropW(h,contextKey));if(owner)return owner;owner=new Owner;owner->window=h;if(!SetPropW(h,contextKey,owner)||!SetWindowSubclass(h,ownerProcedure,177,reinterpret_cast<DWORD_PTR>(owner))){RemovePropW(h,contextKey);delete owner;return nullptr;}return owner;}
HWND ownTip(Owner& owner){if(owner.tip&&IsWindow(owner.tip))return owner.tip;owner.tip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,owner.window,nullptr,GetModuleHandleW(nullptr),nullptr);SendMessageW(owner.tip,TTM_SETMAXTIPWIDTH,0,380);SendMessageW(owner.tip,TTM_SETDELAYTIME,TTDT_INITIAL,450);SendMessageW(owner.tip,TTM_SETDELAYTIME,TTDT_AUTOPOP,15000);return owner.tip;}
HWND existingTip(HWND root,HWND button){struct Search{HWND root,button,found;};Search search{root,button,nullptr};EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM data)->BOOL{auto& s=*reinterpret_cast<Search*>(data);wchar_t type[64]{};GetClassNameW(h,type,64);if(_wcsicmp(type,TOOLTIPS_CLASSW))return TRUE;TOOLINFOW info{sizeof(info)};info.hwnd=s.root;info.uId=UINT_PTR(s.button);if(SendMessageW(h,TTM_GETTOOLINFOW,0,(LPARAM)&info)){s.found=h;return FALSE;}return TRUE;},(LPARAM)&search);return search.found;}
void attach(HWND button){if(!IsWindow(button)||_wcsicmp(classOf(button).c_str(),L"Button"))return;HWND root=GetAncestor(button,GA_ROOT);auto* owner=context(root);if(!owner)return;UINT_PTR key=UINT_PTR(button);auto text=buttonHelpText(button);auto& entry=owner->entries[key];bool update=!entry.text.empty();entry.text=std::move(text);entry.control=button;entry.native=true;
    HWND tip=existingTip(root,button);if(!tip){tip=ownTip(*owner);update=false;}entry.tip=tip;TOOLINFOW info{sizeof(info)};info.hwnd=root;info.uId=key;info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.lpszText=entry.text.data();if(update)SendMessageW(tip,TTM_UPDATETIPTEXTW,0,(LPARAM)&info);else{if(!SendMessageW(tip,TTM_ADDTOOLW,0,(LPARAM)&info))SendMessageW(tip,TTM_UPDATETIPTEXTW,0,(LPARAM)&info);}
}
LRESULT CALLBACK dispatchProcedure(HWND h,UINT message,WPARAM wp,LPARAM lp){if(message==attachMessage){try{appearance::nativeControl(reinterpret_cast<HWND>(wp));attach(reinterpret_cast<HWND>(wp));}catch(...){}return 0;}return DefWindowProcW(h,message,wp,lp);}
LRESULT CALLBACK observe(int code,WPARAM wp,LPARAM lp){if(code>=0&&dispatcher){const auto& message=*reinterpret_cast<CWPRETSTRUCT*>(lp);if(message.message==WM_CREATE||message.message==WM_SETTEXT){wchar_t type[32]{};GetClassNameW(message.hwnd,type,32);if(message.message==WM_CREATE||!_wcsicmp(type,L"Button"))PostMessageW(dispatcher,attachMessage,reinterpret_cast<WPARAM>(message.hwnd),0);}}return CallNextHookEx(hook,code,wp,lp);}
}
std::wstring labelHelp(const std::wstring& source){std::wstring label;for(auto c:source)if(c!=L'&')label+=c;auto match=[&](const wchar_t* key){return label.find(key)!=std::wstring::npos;};
    if(label==L"ΔE"||label==L"ΔG")return L"Y축 제목을 "+label+L" 표기로 바꿉니다. 에너지 수치는 변환하지 않습니다. 제목을 직접 입력하면 해당 문구를 사용합니다.";
    if(label==L"ΔE / (단위)")return L"축 제목과 단위를 나눗셈으로 표시합니다. 예: ΔE / (kcal mol⁻¹), IUPAC 표기입니다. 에너지 수치는 바뀌지 않습니다.";
    if(label==L"ΔE (단위)")return L"축 제목 뒤에 단위를 괄호로 표시합니다. 예: ΔE (kcal mol⁻¹). 에너지 수치는 바뀌지 않습니다.";
    if(label==L"직접")return L"에너지 표기를 직접 입력한 축 제목으로 설정합니다. 옆 제목 칸에서 원하는 문구를 입력하세요.";
    if(label==L"표시 중"||label==L"숨김 상태")return L"클릭하여 이 축을 표시하거나 숨깁니다. 굵기와 화살촉 설정은 유지됩니다.";
    if(label==L"..."||label==L"…")return L"색상 선택창을 열어 옆 항목의 색을 바꿉니다.";
    if(match(L"빠른 이름 변경"))return L"목록에서 선택한 항목의 이름만 바로 바꿉니다. Ctrl+F2로도 실행할 수 있습니다. 이름과 노트는 F2로 함께 편집합니다.";
    if(match(L"x²")||match(L"x2"))return L"선택한 글자를 위첨자로 바꿉니다. 선택이 없으면 위첨자 입력 모드를 전환합니다.";
    if(match(L"x₂"))return L"선택한 글자를 아래첨자로 바꿉니다. 선택이 없으면 아래첨자 입력 모드를 전환합니다.";
    const std::vector<std::pair<std::wstring,std::wstring>> rules={
        {L"되돌리",L"직전 편집을 되돌립니다. 적용 전 미리보기 창에서는 그 창의 편집만 되돌립니다."},{L"실행 취소",L"직전 편집을 한 단계 되돌립니다."},{L"다시 실행",L"되돌렸던 편집을 다시 적용합니다."},
        {L"행 추가",L"표에 새 행을 추가합니다. 새 행에 이름이나 값을 입력할 수 있습니다."},{L"행 복제",L"선택한 행을 복제하여 같은 형식의 새 항목을 만듭니다."},{L"행 삭제",L"선택한 행을 표에서 제거합니다."},
        {L"취소",L"현재 작업을 취소하고 창을 닫습니다."},{L"적용",L"현재 선택과 입력값을 적용합니다."},{L"닫기",L"현재 창을 닫습니다."},
        {L"저장",L"현재 내용과 출력 설정을 선택한 파일에 저장합니다."},{L"열기",L"선택한 항목을 열어 내용을 확인하거나 편집합니다."},{L"만들기",L"선택한 종류와 설정으로 새 항목을 만듭니다."},{L"삽입",L"선택한 구조나 항목을 현재 문서의 지정 영역에 넣습니다."},
        {L"붙여넣",L"클립보드의 구조·이미지·표 내용을 현재 선택 영역에 넣습니다."},{L"복제",L"선택한 항목을 복제합니다. 원본은 유지됩니다."},{L"복사",L"선택한 내용을 클립보드에 복사하여 다른 위치에 붙여넣을 수 있게 합니다."},
        {L"위로",L"선택한 항목을 목록에서 한 칸 위로 옮깁니다."},{L"아래로",L"선택한 항목을 목록에서 한 칸 아래로 옮깁니다."},{L"이전",L"앞 페이지나 이전 단계로 이동합니다."},{L"다음",L"다음 페이지나 다음 단계로 이동합니다."},
        {L"재생",L"현재 설정으로 애니메이션을 재생하거나 일시 정지합니다."},{L"정지",L"애니메이션 재생을 멈춥니다."},{L"처음",L"첫 프레임 또는 첫 단계로 이동합니다."},{L"마지막",L"마지막 프레임 또는 마지막 단계로 이동합니다."},
        {L"확대",L"미리보기의 배율을 높입니다. 문서의 실제 크기는 바뀌지 않습니다."},{L"축소",L"미리보기의 배율을 낮춥니다. 문서의 실제 크기는 바뀌지 않습니다."},{L"배율 초기화",L"미리보기 배율과 위치를 처음 상태로 되돌립니다."},{L"화면 맞춤",L"전체 그림이 미리보기 영역에 들어오도록 배율을 맞춥니다."},
        {L"즐겨찾기",L"선택한 템플릿을 즐겨찾기에 추가하거나 해제합니다."},{L"이름",L"선택한 항목의 이름이나 설명을 편집합니다."},{L"노트",L"선택한 항목의 설명과 작업 메모를 편집합니다."},{L"검색",L"파일·폴더·기능 이름을 검색하여 원하는 항목을 찾습니다."},
        {L"상위 폴더",L"현재 폴더의 한 단계 위 폴더로 이동합니다."},{L"새 폴더",L"현재 위치에 새 폴더를 만듭니다."},{L"이동",L"선택한 항목의 위치나 분류를 바꿉니다."},{L"분류",L"표시할 항목의 분류를 선택하거나 바꿉니다."},{L"삭제",L"선택한 항목을 제거합니다. 실행 전 대상과 보존 여부를 확인하세요."},
        {L"선택",L"목록이나 미리보기에서 고른 항목으로 작업을 진행합니다."},{L"파일 추가",L"외부 파일을 현재 프로젝트에 추가합니다."},{L"새 구조",L"새 구조 문서를 만들어 편집합니다."},{L"새 프로젝트",L"새 프로젝트 폴더와 목록을 만듭니다."}
    };for(const auto& rule:rules)if(match(rule.first.c_str()))return rule.second;
    if(label==L"확인"||label==L"예")return L"현재 선택과 입력을 확인하고 다음 작업으로 진행합니다.";
    if(label==L"아니요")return L"질문에 아니요로 응답합니다.";
    return label.empty()?L"현재 창에서 선택한 항목의 동작을 실행합니다.":label+L" 기능을 선택합니다. 관련 설정이나 선택 대상에 이 동작을 적용합니다.";
}
std::wstring buttonHelpText(HWND button){auto root=GetAncestor(button,GA_ROOT);auto type=classOf(root);int id=GetDlgCtrlID(button);auto label=textOf(button);
    const auto parentType=classOf(GetParent(button));
    if(parentType==L"YoungDrawForm"||parentType==L"YoungDrawEmbeddedForm"){
        if(label==L"+")return L"옆 입력값을 한 단계 늘립니다. 입력란에서 ↑ 키나 마우스 휠로도 조절할 수 있습니다.";
        if(label==L"−")return L"옆 입력값을 한 단계 줄입니다. 입력란에서 ↓ 키나 마우스 휠로도 조절할 수 있습니다.";
        if(label==L"𝑰")return L"옆 입력란의 선택한 글자를 기울임꼴로 바꿉니다. Ctrl+I로도 전환할 수 있습니다.";
        if(label==L"x²")return labelHelp(label)+L" 입력란에서 F10 또는 Ctrl+Shift+=로도 전환할 수 있습니다.";
        if(label==L"x₂")return labelHelp(label)+L" 입력란에서 F9 또는 Ctrl+=로도 전환할 수 있습니다.";
    }
    if(type==L"YoungDrawMechanismPicker"&&id>=100&&id<104){
        const wchar_t* paths[]={L"단계를 왼쪽에서 오른쪽으로 차례로 연결합니다.",L"가로 경로가 아래로 꺾여 촉매 순환에 연결됩니다.",
            L"가로 경로가 좌우로 분기된 뒤 마지막 단계에서 합류합니다.",L"마지막 단계에서 첫 단계로 돌아오는 닫힌 경로를 만듭니다."};
        return paths[id-100];
    }
    if(parentType==L"YoungDrawGraphPicker"){
        if(id>=100&&id<105){const wchar_t* detail[]={L"봉우리와 중간체를 부드러운 곡선으로 연결합니다.",L"상태 사이를 직선으로 연결합니다.",L"평평한 에너지 준위를 점선으로 연결합니다.",L"평평한 에너지 준위를 실선으로 연결합니다.",L"상태들을 연결선 없이 독립 에너지 준위로 비교합니다."};return std::wstring(detail[id-100])+L" 카드를 누르면 선택한 형태를 미리 볼 수 있습니다.";}
        if(id==200)return L"그래프를 전자 에너지 차이 ΔE 표기로 만듭니다.";if(id==201)return L"그래프를 자유 에너지 차이 ΔG 표기로 만듭니다.";
    }
    if(type==L"YoungDrawAnimationPreview"){const std::map<int,std::wstring> meanings={{101,L"애니메이션을 재생하거나 일시 정지합니다."},{102,L"첫 프레임부터 애니메이션을 다시 재생합니다."},{103,L"이전 구조 단계로 이동합니다."},{104,L"다음 구조 단계로 이동합니다."},{105,L"프레임 속도와 단계별 정지 시간을 설정합니다."},{106,L"각 단계의 구조명·단계명·에너지 자막을 표로 편집합니다."},{107,L"현재 단계의 구조를 다른 구조·이미지로 교체합니다."},{108,L"현재 애니메이션을 GIF 파일로 저장합니다."},{109,L"애니메이션 미리보기를 닫습니다."},{112,L"출력 크기·배경·반복과 애니메이션 출력 설정을 조정합니다."},{113,L"새 구조 단계를 애니메이션에 추가합니다."},{114,L"현재 애니메이션 단계를 삭제합니다."},{115,L"각 단계 사이의 전환 시간과 정지 시간을 따로 설정합니다."},{116,L"두 구조 사이에 중간 프레임을 설계하여 결합 변화를 조정합니다."}};auto found=meanings.find(id);if(found!=meanings.end())return found->second;}
    if(type==L"YoungDrawPlotManager"){
        if(id==4406)return L"한 편집 화면에서 X축·Y축의 축선, 제목, 상태 이름, 범위·눈금과 단위를 아이콘으로 선택합니다. 축을 바꿔도 입력값이 유지되며 한 번에 적용하거나 취소할 수 있습니다.";
        if(id==4405)return L"두 인접 준위 사이의 일반 연결을 광여기 물결 화살표로 바꿉니다. hν와 화살촉은 낮은 준위에서 높은 준위로의 전이를 나타냅니다.";
        if(id==4404)return L"그래프 상태 이름·에너지 숫자 크기와 데이터 표 화면의 글자 크기를 각각 바꿉니다.";
        if(id==4403)return L"첫 물질과 마지막 물질 표시선의 중앙 사이를 같은 길이로 나눠 반응 화살표를 배치합니다. 기존 이동값은 초기화합니다.";
        if(id==4402)return L"원본 경로와 원하는 분기점을 선택하면 데이터 표에 새 경로 열이 생깁니다. 분기 뒤 에너지는 빈칸에 각각 입력하며, 이후에도 모든 값을 수정할 수 있습니다. 공통 분기점의 에너지는 연결된 경로가 함께 공유합니다.";
        if(id==250)return L"한 창에서 물결 획의 굵기와 물결 주변 곡선의 여백을 각각 조절합니다.";
        if(id==242)return L"X축의 길이, 굵기, 화살촉, 제목과 글자색을 관리합니다.";
        if(id==243)return L"Y축의 길이, 굵기, 화살촉, 제목과 간격, 눈금, 범위, 에너지 단위를 관리합니다.";
        if(id>=4000&&id<4005){const wchar_t* meanings[]={L"상태·에너지 표, 경로 추가·스타일·색상 도구를 표시합니다.",L"축 범위·길이·제목과 그래프 글꼴·글자 크기 도구를 표시합니다.",L"반응 이름·화살표·TS·갭·교차점 도구를 표시합니다.",L"상태와 위·아래 영역을 선택해 해당 구조만 독립 편집합니다.",L"축·곡선·삽입 그림·물결의 굵기와 여백 도구를 표시합니다."};return meanings[id-4000];}
        if(id==4305)return L"선택한 상태의 위·아래 구조에 사각형 테두리를 표시합니다. 색상, 구조와의 여백, 선 굵기를 미리 보며 조절합니다. 구조를 편집하거나 크기를 바꾸면 테두리도 따라갑니다.";
        if(id>=4300&&id<=4304){const wchar_t* meanings[]={L"선택한 상태의 구조만 별도 창에서 편집합니다. 다른 상태와 에너지 값은 유지됩니다.",L"클립보드의 구조나 이미지를 선택한 상태의 위·아래 영역에 넣습니다.",L"구조·이미지 파일을 선택한 상태의 위·아래 영역에 넣습니다.",L"선택한 상태에 삽입된 구조의 배율만 조절합니다.",L"선택한 구조 영역을 비웁니다. 실행 취소로 복원할 수 있습니다."};return meanings[id-4300];}
        auto help=yd::editor::helpText(id);if(!help.empty())return help;
    }
    if(type==L"YoungDrawMechanismEditor"){
        const std::map<int,std::wstring> meanings={{30,L"선택한 단계의 이름·에너지 또는 연결의 TS 설명·에너지·곡률을 편집합니다."},{31,L"전체 단계·연결을 표로 편집합니다. 적용 전까지 원본은 유지됩니다."},{32,L"연결 모드를 켜고 미리보기의 시작 단계와 도착 단계를 클릭해 화살표로 연결합니다. Esc로 종료합니다."},{33,L"선택한 단계에서 새 경로로 분기하고 단계 이름과 TS 에너지를 입력합니다."},{34,L"현재 연결을 유지하며 단계·화살표·설명의 간격을 정돈합니다."},{35,L"줄 여백, 글자 크기, 자동 화살표 길이, 순환 화살표 복원을 설정합니다."},{40,L"편집 결과와 원본을 번갈아 비교합니다. 원본 비교 중에는 수정하지 않습니다."},{41,L"전체 메커니즘이 미리보기 영역에 들어오도록 맞춥니다."},{42,L"이 편집 창에서 수행한 직전 변경을 되돌립니다. 원본 문서는 아직 바뀌지 않습니다."},{43,L"이 편집 창에서 취소한 변경을 다시 적용합니다."},{44,L"미리보기를 확대합니다. 구조의 실제 크기는 바뀌지 않습니다."},{45,L"미리보기를 축소합니다. 구조의 실제 크기는 바뀌지 않습니다."},{60,L"선택한 단계의 분자 구조를 독립 편집 창에서 그리거나 수정합니다."},{61,L"복사한 구조·이미지를 선택한 단계에 삽입합니다."},{62,L"CDXML·YoungDraw·이미지 파일의 구조를 선택한 단계에 삽입합니다."},{63,L"선택한 연결 사이에 중간체 단계를 삽입합니다."},{64,L"순환 메커니즘의 마지막 단계와 처음 단계 사이 연결을 켜거나 끕니다."},{65,L"선택한 단계 또는 연결을 제거합니다. 이 창의 되돌리기로 복원할 수 있습니다."},{66,L"선택한 단계의 구조와 연결을 지정한 다른 단계로 합칩니다."},{70,L"구조 편집·입력·중간체·단계·분기·연결 도구를 표시합니다."},{71,L"자동 배치, 전체 표, 글자·여백 설정 도구를 표시합니다."},{72,L"원본 비교, 화면 맞춤, 확대·축소 도구를 표시합니다."},{IDOK,L"배치와 구조·단계·연결 편집 결과를 원본 문서에 한 번에 적용합니다."},{IDCANCEL,L"이 창의 편집 결과를 적용하지 않고 원본 구조와 배치를 유지합니다."}};auto found=meanings.find(id);if(found!=meanings.end())return found->second;
    }
    if(type==L"YoungDrawProjectManager"){
        const std::map<int,std::wstring> meanings={{10,L"새 프로젝트를 만듭니다."},{11,L"선택한 프로젝트의 이름과 설명을 편집합니다."},{12,L"선택한 프로젝트를 삭제합니다. 확인 창에서 대상을 검토하세요."},{13,L"다른 위치의 프로젝트 폴더나 ZIP 파일을 불러옵니다."},{14,L"선택한 프로젝트의 문서와 이미지를 폴더 또는 ZIP으로 내보냅니다."},{15,L"선택한 프로젝트 폴더를 탐색기에서 엽니다."},{16,L"프로젝트의 파일·폴더·노트를 검색합니다."},{17,L"선택한 파일 항목을 복제하고 새 항목으로 등록합니다."},{18,L"선택한 파일·폴더만 ZIP으로 묶어 내보냅니다."},{20,L"새 구조 문서를 프로젝트에 추가합니다."},{21,L"기존 구조·이미지 파일을 프로젝트로 가져옵니다."},{22,L"현재 열려 있는 문서를 프로젝트에 추가합니다."},{23,L"선택한 문서나 구조를 편집 탭에서 엽니다."},{24,L"선택 항목의 이름과 노트를 편집합니다."},{25,L"선택 항목을 프로젝트에서 삭제합니다. 확인 창에서 대상을 검토하세요."},{26,L"선택 항목을 목록에서 위로 옮깁니다."},{27,L"선택 항목을 목록에서 아래로 옮깁니다."},{28,L"노트 내용을 저장합니다."},{29,L"선택한 위치에 새 하위 폴더를 만듭니다."},{30,L"현재 폴더의 상위 폴더로 이동합니다."},{31,L"선택 항목을 다른 폴더로 이동합니다."},{32,L"선택 항목의 미리보기를 크게 보거나 목록 보기로 돌아갑니다."},{33,L"선택 항목 미리보기를 확대합니다."},{34,L"선택 항목 미리보기를 축소합니다."},{35,L"미리보기 배율과 위치를 초기화합니다."}};auto found=meanings.find(id);if(found!=meanings.end())return found->second;
    }
    auto help=labelHelp(label);auto style=GetWindowLongPtrW(button,GWL_STYLE)&BS_TYPEMASK;if(style==BS_AUTOCHECKBOX||style==BS_CHECKBOX||style==BS_AUTO3STATE||style==BS_3STATE)return label+L" 설정을 켜거나 끕니다.";if(style==BS_AUTORADIOBUTTON||style==BS_RADIOBUTTON)return label+L" 항목을 선택합니다.";return help;
}
void enableButtonHelp(){if(dispatcher)return;INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawButtonHelpDispatcher";wc.lpfnWndProc=dispatchProcedure;RegisterClassW(&wc);dispatcher=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);if(dispatcher)hook=SetWindowsHookExW(WH_CALLWNDPROCRET,observe,nullptr,GetCurrentThreadId());}
void attachWindowHelp(HWND window){enableButtonHelp();EnumChildWindows(window,[](HWND h,LPARAM)->BOOL{try{attach(h);}catch(...){}return TRUE;},0);}
bool hasButtonHelp(HWND button){auto* owner=static_cast<Owner*>(GetPropW(GetAncestor(button,GA_ROOT),contextKey));if(!owner)return false;auto found=owner->entries.find(UINT_PTR(button));return found!=owner->entries.end()&&!found->second.text.empty()&&IsWindow(found->second.tip);}
void setRegionHelp(HWND window,const std::vector<HelpRegion>& regions){enableButtonHelp();auto* owner=context(window);if(!owner)return;std::set<UINT_PTR> keep;for(const auto& region:regions){const UINT_PTR key=~UINT_PTR(0)-region.id;keep.insert(key);auto& entry=owner->entries[key];bool fresh=entry.text.empty();bool changed=entry.text!=region.text;entry.text=region.text;entry.native=false;entry.rect=region.rect;entry.tip=ownTip(*owner);TOOLINFOW info{sizeof(info)};info.hwnd=window;info.uId=key;info.uFlags=TTF_SUBCLASS;info.rect=region.rect;info.lpszText=entry.text.data();if(fresh)SendMessageW(entry.tip,TTM_ADDTOOLW,0,(LPARAM)&info);else{SendMessageW(entry.tip,TTM_NEWTOOLRECTW,0,(LPARAM)&info);if(changed)SendMessageW(entry.tip,TTM_UPDATETIPTEXTW,0,(LPARAM)&info);}}
    for(auto it=owner->entries.begin();it!=owner->entries.end();){if(!it->second.native&&!keep.count(it->first)){TOOLINFOW info{sizeof(info)};info.hwnd=window;info.uId=it->first;if(IsWindow(it->second.tip))SendMessageW(it->second.tip,TTM_DELTOOLW,0,(LPARAM)&info);it=owner->entries.erase(it);}else ++it;}
}
size_t regionHelpCount(HWND window){auto* owner=static_cast<Owner*>(GetPropW(window,contextKey));if(!owner)return 0;return size_t(std::count_if(owner->entries.begin(),owner->entries.end(),[](const auto& entry){return !entry.second.native;}));}
}

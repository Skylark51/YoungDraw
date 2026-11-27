#include "win_editor_tool_options.h"
#include "win_editor_chrome.h"
#include "win_editor_commands.h"
#include "win_editor_pointer.h"
#include "win_editor_state.h"
#include "win_editor_structure.h"
#include "win_theme.h"

namespace yd::editor {
int reactionArrowHead=1;
bool partialTripleBond=false;

bool showDrawingToolOptions(int id,Vec at){
    if(id!=210&&id!=217)return false;
    HMENU menu=CreatePopupMenu();std::vector<HBITMAP> bitmaps;
    const int first=id==210?irreversibleArrowCommand:delocalizedBondOptionCommand,last=id==210?lowerFavoredArrowCommand:partialTripleBondOptionCommand;
    const wchar_t* labels[]={L"비가역 반응",L"평형 · 같은 길이",L"평형 · 위쪽 화살표 우세",L"평형 · 아래쪽 화살표 우세",L"공명 결합 · 실선 + 파선",L"부분 삼중 결합 · 실선 2개 + 파선"};
    for(int command=first;command<=last;++command){
        const bool active=id==210?reactionArrowHead==(command==irreversibleArrowCommand?1:command-equilibriumArrowCommand+4):partialTripleBond==(command==partialTripleBondOptionCommand);
        AppendMenuW(menu,MF_STRING|(active?MF_CHECKED:0),command,labels[command-irreversibleArrowCommand]);
        Bitmap preview(48,32,PixelFormat32bppARGB);Graphics graphics(&preview);
        graphics.Clear(Color(255,255,255,255));graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        icon(graphics,command,{4,2,40,26},Color(255,30,40,50));HBITMAP bitmap=nullptr;
        preview.GetHBITMAP(Color(255,255,255,255),&bitmap);bitmaps.push_back(bitmap);
        MENUITEMINFOW item{};item.cbSize=sizeof(item);item.fMask=MIIM_BITMAP;item.hbmpItem=bitmap;
        SetMenuItemInfoW(menu,command,FALSE,&item);
    }
    POINT point{LONG(at.x),LONG(at.y)};ClientToScreen(win,&point);
    const int chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,win,nullptr);
    DestroyMenu(menu);for(auto bitmap:bitmaps)DeleteObject(bitmap);
    if(chosen)command(chosen);return true;
}

bool drawingOptionCommand(int id){
    if(id<irreversibleArrowCommand||id>partialTripleBondOptionCommand)return false;
    cancel();
    if(id<=lowerFavoredArrowCommand){reactionArrowHead=id==irreversibleArrowCommand?1:id-equilibriumArrowCommand+4;tool=Arrow;status=L"반응 화살표 · 드래그: 길이와 방향 · 아이콘 클릭: 종류 선택";}
    else {
        partialTripleBond=id==partialTripleBondOptionCommand;tool=Delocalized;
        const auto ids=groupedSelection(selection);bool edited=false;
        for(auto& bond:doc.bonds)if(!doc.isLocked(bond.id)&&(contains(ids,bond.id)||(contains(ids,bond.a)&&contains(ids,bond.b)))){
            const auto kind=partialTripleBond?BondKind::PartialTriple:BondKind::Aromatic;
            if(bond.kind==kind&&bond.customOrder.empty()&&bond.customDisplay.empty())continue;
            if(!edited)history.checkpoint(doc);edited=true;
            setBondKind(doc,bond.id,kind);bond.customOrder.clear();bond.customDisplay.clear();
        }
        if(edited)changed();
        status=partialTripleBond?L"부분 삼중 결합 · 실선 2개 + 파선 · 결합 차수 2.5의 도식 표기":L"공명 결합 · 실선 + 파선";
    }
    redraw();return true;
}

void drawLayerPanel(Graphics& g,float x,int width){
    const auto& colors=appearance::palette();
    text(g,L"레이어",x+16,138,15,colors.text,true);
    text(g,L"행: 선택   눈: 표시   자물쇠: 잠금",x+16,166,11,colors.muted);
    auto row=[&](float y,const wchar_t* label,int symbol,const std::vector<int>& ids,bool visible,bool locked,int select,int eye,int lock){
        const bool active=std::any_of(ids.begin(),ids.end(),[](int id){return contains(selection,id);});
        appearance::rounded(g,{x+12,y,float(width-24),68},7,active?colors.hover:colors.raised);
        hits.push_back({select,{x+48,y+2,float(width-138),64}});
        icon(g,symbol,{x+55,y+12,30,30},visible?colors.text:colors.muted);
        text(g,label,x+92,y+10,12,colors.text,true);
        text(g,std::to_wstring(ids.size())+L"개"+(active?L" · 선택됨":L""),x+92,y+35,10,colors.muted);
        auto toggle=[&](int id,RectF box,bool active){
            hits.push_back({id,box});
            const bool hovered=box.Contains(float(mouse.x),float(mouse.y));
            appearance::rounded(g,box,4,active?colors.accent:hovered?colors.hover:colors.raised);
            icon(g,id,{box.X+3,box.Y+3,22,22},active?colors.onAccent:colors.muted);
            if(id==eye&&!visible)rule(g,box.X+4,box.Y+24,box.X+24,box.Y+4,colors.muted);
        };
        toggle(eye,{x+17,y+19,28,28},visible);
        toggle(lock,{x+float(width-49),y+19,28,28},locked);
    };
    std::vector<int> images,graphs;
    for(const auto& object:doc.objects)if(object.kind==ObjectKind::Image)images.push_back(object.id);
    for(const auto& graph:doc.plots)graphs.push_back(graph.id);
    row(200,L"그래프",130,graphs,doc.graphsVisible,doc.graphsLocked,selectGraphLayerCommand,graphVisibilityCommand,178);
    row(276,L"이미지",214,images,doc.imagesVisible,doc.imagesLocked,selectImageLayerCommand,imageVisibilityCommand,177);
    text(g,L"빠른 편집 대상",x+16,368,11,colors.muted,true);
    button(g,179,L"이미지만 편집",{x+12,394,float(width-24),32},doc.graphsLocked&&!doc.imagesLocked);
    button(g,180,L"그래프만 편집",{x+12,434,float(width-24),32},doc.imagesLocked&&!doc.graphsLocked);
    button(g,181,L"모두 잠금 해제",{x+12,474,float(width-24),32});
    text(g,L"표시·잠금 상태는 문서에 저장됩니다.",x+16,530,11,colors.muted);
    text(g,L"Ctrl+Z: 표시·잠금 변경 되돌리기",x+16,552,11,colors.muted);
}

bool layerPanelCommand(int id){
    if(id<imageVisibilityCommand||id>selectGraphLayerCommand)return false;
    if(id<=graphVisibilityCommand){history.checkpoint(doc);if(id==imageVisibilityCommand)doc.imagesVisible=!doc.imagesVisible;else doc.graphsVisible=!doc.graphsVisible;changed();}
    else {
        cancel();tool=Select;selection.clear();targetContainer=0;
        if(id==selectImageLayerCommand)for(const auto& object:doc.objects)if(object.kind==ObjectKind::Image&&!doc.isLocked(object.id))selection.push_back(object.id);
        if(id==selectGraphLayerCommand)for(const auto& graph:doc.plots)if(!doc.isLocked(graph.id))selection.push_back(graph.id);
        status=selection.empty()?L"선택할 항목이 없거나 레이어가 숨김·잠금 상태입니다.":L"레이어 항목 선택 · 캔버스에서 이동하거나 편집하세요.";
    }
    redraw();return true;
}
} // namespace yd::editor

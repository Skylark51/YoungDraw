#include "win_editor_canvas.h"
#include "win_editor_chrome.h"
#include "win_editor_commands.h"
#include "win_editor_plot.h"
#include "win_editor_pointer.h"
#include "win_editor_reaction.h"
#include "win_editor_shortcuts.h"
#include "win_editor_state.h"
#include "win_editor_structure.h"
#include "win_editor_tabs.h"
#include "win_editor_text.h"
#include "win_editor_window.h"
#include "office.h"
#include "win_dialogs.h"
#include "win_theme.h"
#include "win_ui.h"
#include <commctrl.h>
#include <windowsx.h>

namespace yd::editor {

LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){try{switch(msg){case WM_CREATE:win=hwnd;appearance::load();appearance::titlebar(hwnd);createTooltip();menuBar(hwnd);createDocumentTabs();title();return 0;case WM_GETMINMAXINFO:{auto* p=(MINMAXINFO*)lp;p->ptMinTrackSize={740,480};return 0;}case WM_SIZE:hideTooltip();if(!finishInline(true))return 0;ww=LOWORD(lp);wh=HIWORD(lp);if(documentTabs)MoveWindow(documentTabs,left,89,std::max(1,ww-left-right),30,TRUE);redraw();return 0;case WM_ERASEBKGND:return 1;case WM_SETCURSOR:if(LOWORD(lp)==HTCLIENT&&panning){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}if(LOWORD(lp)==HTCLIENT&&canvas(mouse)&&tool==Text){SetCursor(LoadCursorW(nullptr,IDC_IBEAM));return TRUE;}break;case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);paint(dc,true);EndPaint(hwnd,&ps);return 0;}
 case WM_MEASUREITEM:{auto* item=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);if(item->CtlType==ODT_MENU){auto label=reinterpret_cast<const wchar_t*>(item->itemData);item->itemWidth=UINT(wcslen(label)*8+16);item->itemHeight=24;return TRUE;}break;}
 case WM_DRAWITEM:{auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item->CtlType==ODT_MENU){const auto& p=appearance::palette();Graphics g(item->hDC);RectF box(float(item->rcItem.left),float(item->rcItem.top),float(item->rcItem.right-item->rcItem.left),float(item->rcItem.bottom-item->rcItem.top));fill(g,box,(item->itemState&ODS_SELECTED)?p.hover:p.chrome);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,RGB(p.text.GetR(),p.text.GetG(),p.text.GetB()));auto font=SelectObject(item->hDC,GetStockObject(DEFAULT_GUI_FONT));RECT rect=item->rcItem;rect.left+=8;DrawTextW(item->hDC,reinterpret_cast<const wchar_t*>(item->itemData),-1,&rect,DT_SINGLELINE|DT_VCENTER);SelectObject(item->hDC,font);return TRUE;}break;}
 case WM_CTLCOLOREDIT:if((HWND)lp==inlineEdit){SetTextColor((HDC)wp,inlineColor);SetBkColor((HDC)wp,RGB(255,255,255));return (LRESULT)GetStockObject(WHITE_BRUSH);}break;case WM_TIMER:if(wp==912){if(!IsIconic(hwnd)){auto box=canvasBox();RECT r{LONG(box.X),LONG(box.Y),LONG(box.GetRight()),LONG(box.GetBottom())};repaintOnly(&r);}return 0;}if(wp==911){showTooltip();return 0;}break;case WM_ACTIVATEAPP:if(!wp)hideTooltip();break;case WM_LBUTTONDOWN:mouseDown({double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))});return 0;case WM_LBUTTONUP:mouseUp({double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))});return 0;
 case WM_LBUTTONDBLCLK:{if(snapPlot){mouseDown({double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))});return 0;}Vec at{double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))};if(!canvas(at))return 0;Vec p=world(at);
 if(auto energy=textAt(p);energy&&(energy->role==3||energy->role==9)&&!doc.isLocked(energy->owner)){cancel();beginInline(energy,p);return 0;}
 if(!isolatedStructureEditing&&tool==Select&&pickSlot(p)){cancel();editTargetStructure();return 0;}if(auto target=textAt(p);target&&!doc.atom(target->owner)){if(tool==Text){cancel();beginInline(target,p);}else if(tool==Select)mouseDown({double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))});return 0;}int aid=doc.atomAt(p,10/zoom);if(aid){selection=doc.component(aid);tool=Select;}else{int id=objectAt(p);if(id){selection={id};edit();}}redraw();return 0;}
 case WM_MBUTTONDOWN:if(canvas({double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))})){cancel();panning=true;down={double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))};panOrigin=origin;SetCapture(hwnd);}return 0;case WM_MBUTTONUP:if(panning&&!rightPanning){panning=false;ReleaseCapture();redraw();}return 0;
 case WM_RBUTTONDOWN:{Vec at{double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))};if((wp&MK_CONTROL)&&canvas(at)){
    hideTooltip();if(!finishInline(true))return 0;cancel();SetFocus(hwnd);down=mouse=at;panOrigin=origin;panning=rightPanning=true;SetCapture(hwnd);SetCursor(LoadCursorW(nullptr,IDC_HAND));
 }return 0;}
 case WM_RBUTTONUP:if(rightPanning){mouse={double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))};origin=panOrigin+mouse-down;panning=rightPanning=false;ReleaseCapture();SetCursor(LoadCursorW(nullptr,IDC_ARROW));redraw();}else{Vec at{double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))};if(!pinTool(at)&&canvas(at))mechanismContextMenu(at);}return 0;
 case WM_MOUSELEAVE:hideTooltip();hoverAtom=hoverBond=0;repaintOnly();return 0;case WM_MOUSEMOVE:{TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,hwnd,0};TrackMouseEvent(&tracking);mouse={double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))};hoverAtom=canvas(mouse)?doc.atomAt(world(mouse),10/zoom):0;hoverBond=canvas(mouse)&&!hoverAtom?doc.bondAt(world(mouse),5/zoom):0;updateDrag();if(panning)origin=panOrigin+mouse-down;updateTooltip();if(!inlineEdit){if(dragging)redraw();else repaintOnly();}return 0;}
 case WM_CAPTURECHANGED:if(mechanismConnectionDrag&&(HWND)lp!=hwnd){mechanismConnectionDrag=false;redraw();}if((drawing||dragging||boxing||panning)&&(HWND)lp!=hwnd){if(dragging)doc=dragBefore;drawing=dragging=boxing=panning=rightPanning=false;curveHandle=-1;annotationDrag=0;redraw();}return 0;
 case WM_MOUSEWHEEL:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hwnd,&p);Vec at{double(p.x),double(p.y)};if(scrollTools(at,-GET_WHEEL_DELTA_WPARAM(wp)*2/3))return 0;if(canvas(at)){Vec anchor=world(at);zoom=std::clamp(zoom*(GET_WHEEL_DELTA_WPARAM(wp)>0?1.15:1/1.15),.12,12.);origin=at-anchor*zoom;repaintOnly();}return 0;}
 case WM_NOTIFY:if(documentTabs&&((NMHDR*)lp)->hwndFrom==documentTabs&&((NMHDR*)lp)->code==TCN_SELCHANGE){switchDocumentTab(size_t(TabCtrl_GetCurSel(documentTabs)));SetFocus(win);return 0;}break;case WM_APP+3:closeDocumentTab(size_t(wp));return 0;case WM_SYSKEYDOWN:if((GetKeyState(VK_CONTROL)&0x8000)||wp==VK_LEFT||wp==VK_RIGHT||wp==VK_UP||wp==VK_DOWN||wp=='K'){key(wp);return 0;}break;case WM_KEYDOWN:key(wp);return 0;case WM_CHAR:if(!(GetKeyState(VK_CONTROL)&0x8000)&&!dragging&&!drawing&&!boxing&&!panning&&wp<128)hotkey(char(wp));return 0;case WM_COMMAND:if(LOWORD(wp)>=100&&(LOWORD(wp)<=199||(LOWORD(wp)>=230&&LOWORD(wp)<=233)||LOWORD(wp)==215||LOWORD(wp)==216||LOWORD(wp)==217||LOWORD(wp)==218||LOWORD(wp)==219||LOWORD(wp)==insertBranchArrowsCommand||LOWORD(wp)==externalMechanismCommand||LOWORD(wp)==bondAnimationCommand||LOWORD(wp)==recognizeMechanismCommand)){command(LOWORD(wp));return 0;}if(LOWORD(wp)==900){if(HIWORD(wp)==EN_CHANGE)previewInline();else if(HIWORD(wp)==EN_KILLFOCUS&&!inlineClosing)PostMessageW(hwnd,WM_APP+1,0,0);return 0;}break;case WM_APP+1:if(inlineEdit&&GetFocus()!=inlineEdit)finishInline(true);return 0;case WM_CLOSE:if(!finishInline(true))return 0;if(isolatedStructureEditing){DestroyWindow(hwnd);return 0;}if(closeAllDocuments()){if(office::editing()){office::close();dirty=false;}else DestroyWindow(hwnd);}return 0;case WM_DESTROY:KillTimer(hwnd,912);canvasSceneCache.reset();paintBuffer.reset();if(menuBackground){DeleteObject(menuBackground);menuBackground=nullptr;}if(tabFont){DeleteObject(tabFont);tabFont=nullptr;}PostQuitMessage(0);return 0;
 }}catch(const std::exception& e){MessageBoxW(hwnd,ui::wide(e.what()).c_str(),L"YoungDraw",MB_OK|MB_ICONWARNING);redraw();}return DefWindowProcW(hwnd,msg,wp,lp);}

} // namespace yd::editor

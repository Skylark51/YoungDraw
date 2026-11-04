// Included inside winmain.cpp's window namespace, after drawing commands.
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
    if(k==VK_DELETE||k==VK_BACK){if(deleteSelectedCycleArrow())return;if(!selection.empty()){history.checkpoint(doc);doc.erase(selection);selection.clear();changed();}}
    if(k==VK_F1)MessageBoxW(win,L"Ctrl+K: 크기 / Ctrl+R: 회전 / Ctrl+Shift+K: 구조 정리\nCtrl+Shift+Alt+L/C/R: 왼쪽 / 가로 중앙 / 오른쪽 정렬\nCtrl+Shift+Alt+T/M/B: 위 / 세로 중앙 / 아래 정렬\nCtrl+Shift+Alt+H/V: 가로 / 세로 간격 배분\nCtrl+Shift+H/V: 좌우 / 상하 반전\n방향키: 1 pt 이동 / Shift+방향키: 10 pt 이동\nAlt+좌우: 1° 회전 / Alt+상하: 15° 회전\nCtrl+방향키: 반응 화살표와 복제 구조 추가\nCtrl+G / Ctrl+Shift+G: 그룹 / 그룹 해제\nCtrl+D: CDXML 복사 / Ctrl+Shift+X: 반응 배치\nCtrl+Z: 실행 취소 / Ctrl+Shift+Z: 다시 실행 / Ctrl+Y: 마지막 작업 반복\nShift+G: 오비탈 도구 / Space: 선택 도구\n텍스트 편집 F9/F10: 아래 / 위첨자\nCtrl+우클릭 드래그: 화면 이동",L"YoungDraw · ChemDraw 호환 단축키",MB_OK);
    if(k==VK_RETURN){if(hoverAtom){selection={hoverAtom};editObject(hoverAtom);}else edit();}redraw();
}

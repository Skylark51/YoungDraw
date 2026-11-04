// Exercise the production document switching and saving code in a hidden window.
#include "../src/winmain.cpp"
#include <iostream>
#include "mechanism_layout_fixture.h"
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static int formAction=0;static bool formDriven=false,previewObserved=false;static std::string formBefore;
static bool paletteDriven=false;
static void CALLBACK driveTextPalette(HWND parent,UINT,UINT_PTR timer,DWORD){
    HWND dialog=GetLastActivePopup(parent);wchar_t name[64]{};GetClassNameW(dialog,name,64);if(std::wstring(name)!=L"#32770")return;
    KillTimer(parent,timer);paletteDriven=true;
    SendMessageW(dialog,RegisterWindowMessageW(L"commdlg_SetRGBColor"),0,RGB(35,90,225));
    PostMessageW(dialog,WM_COMMAND,IDOK,0);
}
static void CALLBACK driveForm(HWND parent,UINT,UINT_PTR timer,DWORD){
    HWND dialog=GetLastActivePopup(parent);wchar_t name[64]{};GetClassNameW(dialog,name,64);if(std::wstring(name)!=L"YoungDrawForm")return;
    KillTimer(parent,timer);formDriven=true;
    if(formAction==11||formAction==12){
        SetDlgItemTextW(dialog,100,L"4");previewObserved=toNative(doc)!=formBefore;
    }else if(formAction==13||formAction==14){
        SetDlgItemTextW(dialog,101,L"15");SetDlgItemTextW(dialog,102,L"85");previewObserved=toNative(doc)!=formBefore;
    }else if(formAction==9||formAction==10){
        SendDlgItemMessageW(dialog,101,CB_SETCURSEL,1,0);SendDlgItemMessageW(dialog,102,CB_SETCURSEL,2,0);
        SetDlgItemTextW(dialog,104,L"#cc2255");previewObserved=toNative(doc)!=formBefore;
    }else if(formAction==8){SetDlgItemTextW(dialog,100,L"30");
    }else if(formAction==7){
        SendDlgItemMessageW(dialog,100,CB_SETCURSEL,2,0);SendDlgItemMessageW(dialog,101,CB_SETCURSEL,1,0);
        SetDlgItemTextW(dialog,102,L"40");SetDlgItemTextW(dialog,103,L"45");
    }else if(formAction==5||formAction==6){
        SetDlgItemTextW(dialog,100,L"50");previewObserved=toNative(doc)!=formBefore;
    }else if(formAction==3||formAction==4){
        SendDlgItemMessageW(dialog,101,CB_SETCURSEL,2,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(101,CBN_SELCHANGE),(LPARAM)GetDlgItem(dialog,101));
        SetDlgItemTextW(dialog,103,L"2");SetDlgItemTextW(dialog,104,L"180");previewObserved=toNative(doc)!=formBefore;
    }else if(formAction==0){
        HWND edit=GetDlgItem(dialog,100);SetWindowTextW(edit,L"H2");SendMessageW(edit,EM_SETSEL,1,2);
        BYTE before[256]{};GetKeyboardState(before);BYTE keys[256];memcpy(keys,before,sizeof(keys));keys[VK_CONTROL]=0x80;keys[VK_SHIFT]=0;SetKeyboardState(keys);
        SendMessageW(edit,WM_KEYDOWN,VK_OEM_PLUS,0);SetKeyboardState(before);
    }else{
        SetDlgItemTextW(dialog,101,L"30");previewObserved=toNative(doc)!=formBefore;
    }
    SendMessageW(dialog,WM_COMMAND,(formAction==1||formAction==3||formAction==6||formAction==10||formAction==12||formAction==14)?IDCANCEL:IDOK,0);
}
int main(int argc,char** argv){
    OleInitialize(nullptr);GdiplusStartupInput startup;ULONG_PTR token;GdiplusStartup(&token,&startup,nullptr);
    int result=0;
    try{
        // User templates live in an isolated LOCALAPPDATA root so this test never
        // changes a real user's templates, favorites, or active gallery data.
        {
            wchar_t previous[32768]{};DWORD size=GetEnvironmentVariableW(L"LOCALAPPDATA",previous,32768);
            std::wstring oldAppData=size&&size<32768?std::wstring(previous,size):L"";
            auto root=fs::temp_directory_path()/(L"YoungDrawTemplateTests-"+std::to_wstring(GetCurrentProcessId()));
            fs::create_directories(root);
            check(SetEnvironmentVariableW(L"LOCALAPPDATA",root.wstring().c_str())!=0,"isolate template storage");
            try{
                check(validTemplatePart(L"Fe-peroxo")&&!validTemplatePart(L"../escape")&&!validTemplatePart(L"CON"),"template path validation");
                auto path=templateLocation(L"test-molecule",L"catalysis");
                fs::create_directories(path.parent_path());
                Document initial;initial.addAtom({0,0},8);
                writeFile(path.wstring(),toNative(initial));
                size_t skipped=0;
                auto& cards=templateCards(skipped);
                auto match=[&](const std::vector<ui::TemplateCard>& items){
                    return std::find_if(items.begin(),items.end(),[&](const ui::TemplateCard& card){return card.path==path.wstring();});
                };
                auto item=match(cards);
                check(item!=cards.end()&&item->group==L"catalysis"&&item->document.atoms.size()==1,"user template category and load");
                const auto* preview=&item->preview;
                auto& reused=templateCards(skipped);
                check(&match(reused)->preview==preview,"unchanged template preview is cached");
                auto favorites=loadFavorites();favorites.insert(templateKey(*item));saveFavorites(favorites);
                auto& marked=templateCards(skipped);
                check(match(marked)->favorite,"favorite survives gallery reload");
                initial.addAtom({50,0},6);writeFile(path.wstring(),toNative(initial));
                auto& refreshed=templateCards(skipped);
                check(match(refreshed)->document.atoms.size()==2,"edited template invalidates preview cache");
                fs::remove(path);auto& removed=templateCards(skipped);
                check(match(removed)==removed.end(),"deleted user template disappears from gallery");
            }catch(...){
                SetEnvironmentVariableW(L"LOCALAPPDATA",oldAppData.empty()?nullptr:oldAppData.c_str());
                fs::remove_all(root);throw;
            }
            SetEnvironmentVariableW(L"LOCALAPPDATA",oldAppData.empty()?nullptr:oldAppData.c_str());
            fs::remove_all(root);
        }
        // Reproduce differing bitmap/window DPI (100%, 125%, 150%, 200%).
        // The painted UI must fill the client area and retain hit-test positions.
        for(float sourceDpi:{96.f,120.f,144.f,192.f})for(float targetDpi:{96.f,120.f,144.f,192.f}){
            Bitmap buffer(160,100,PixelFormat32bppARGB),output(180,120,PixelFormat32bppARGB);
            buffer.SetResolution(sourceDpi,sourceDpi);output.SetResolution(targetDpi,targetDpi);
            {Graphics source(&buffer);source.Clear(Color(255,255,255,255));
             SolidBrush marker(Color(255,0,123,112));source.FillRectangle(&marker,40,30,20,20);}
            {Graphics target(&output);target.Clear(Color(255,255,0,255));presentBuffer(target,buffer);}
            Color pixel;
            output.GetPixel(158,98,&pixel);check(pixel.GetValue()==Color(255,255,255,255).GetValue(),"DPI-independent paint covers bottom-right client area");
            output.GetPixel(50,40,&pixel);check(pixel.GetValue()==Color(255,0,123,112).GetValue(),"DPI-independent paint preserves button hit-test position");
            output.GetPixel(162,102,&pixel);check(pixel.GetValue()==Color(255,255,0,255).GetValue(),"DPI-independent paint does not enlarge beyond client area");
        }
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawTabRegression";
        check(RegisterClassW(&wc)!=0,"register window");
        auto hwnd=CreateWindowExW(0,wc.lpszClassName,L"Tabs",WS_OVERLAPPEDWINDOW,0,0,1440,940,nullptr,nullptr,wc.hInstance,nullptr);
        check(hwnd&&documentTabs,"create native tab bar");
        {
            // Hover-only paint reuse must keep geometry, while any document redraw
            // must invalidate the cached scene (including Undo and tab switches).
            auto previousDoc=doc;
            doc={};doc.addAtom({0,0});redraw();
            auto first=sceneForPaint(true).bounds;
            doc.atoms[0].p={200,100};
            auto retained=sceneForPaint(true).bounds;
            check(first.x==retained.x&&first.y==retained.y,"overlay paint reuses the existing scene");
            redraw();
            auto updated=sceneForPaint(true).bounds;
            check(updated.x>first.x+100&&updated.y>first.y+50,"document redraw rebuilds the scene");
            doc=previousDoc;redraw();
        }
        {
            auto originalDoc=doc;auto originalHistory=history;auto originalSelection=selection;auto originalOrigin=origin;double originalZoom=zoom;bool originalDirty=dirty;
            doc={};history.clear();selection.clear();origin={left+200.,top+180.};zoom=2;tool=Select;dirty=false;
            int a=doc.addAtom({0,0}),b=doc.addAtom({25,0});int bond=doc.addBond(a,b);
            Object label;label.id=doc.nextId++;label.kind=ObjectKind::Text;label.text="Catalyst";label.points={{0,50}};label.fontSize=12;doc.objects.push_back(label);
            Object arrow;arrow.id=doc.nextId++;arrow.points={{70,0},{120,0}};doc.objects.push_back(arrow);
            int outsider=doc.addAtom({170,100});
            selection={bond,label.id,arrow.id};
            BYTE previous[256]{};GetKeyboardState(previous);BYTE keys[256];memcpy(keys,previous,sizeof(keys));keys[VK_CONTROL]=0x80;keys[VK_SHIFT]=0;SetKeyboardState(keys);key('G');SetKeyboardState(previous);
            check(doc.groups.size()==1&&contains(selection,a)&&contains(selection,b),"Ctrl+G groups bonds with endpoints, text and arrows");
            auto grouped=toNative(doc);command(198);check(doc.groups.size()==1&&toNative(doc)==grouped,"repeated grouping creates no duplicate group");
            selection.clear();Vec at=screen({0,50});mouseDown(at);mouseUp(at);
            check(contains(selection,a)&&contains(selection,label.id)&&contains(selection,arrow.id)&&!contains(selection,outsider),"clicking grouped text selects the entire group");
            mouseDown(at);check(!inlineEdit&&dragging,"grouped text starts a group drag instead of inline editing");mouse=at+Vec{40,20};updateDrag();mouseUp(mouse);
            check((doc.atom(a)->p-Vec{20,10}).length()<.001&&(doc.atom(outsider)->p-Vec{170,100}).length()<.001,"dragging a group moves all members and leaves other objects fixed");
            check((doc.objects[0].points[0]-Vec{20,60}).length()<.001&&(doc.objects[1].points[0]-Vec{90,10}).length()<.001,"group drag preserves relative text and arrow positions");
            check(history.undo(doc)&&toNative(doc)==grouped,"group movement can be undone");
            auto loaded=fromNative(grouped),xml=fromCdxml(toCdxml(doc));check(loaded.groups.size()==1&&xml.groups.size()==1,"native and CDXML round trips retain user groups");
            auto fragment=doc.subset({a});Document pasted;pasted.append(fragment,{10,10});check(pasted.groups.size()==1&&pasted.objects.size()==2,"copying a member preserves and remaps its whole group");
            selection={a};memcpy(keys,previous,sizeof(keys));keys[VK_CONTROL]=keys[VK_SHIFT]=0x80;SetKeyboardState(keys);key('G');SetKeyboardState(previous);
            check(doc.groups.empty(),"Ctrl+Shift+G ungroups a selected member");check(history.undo(doc)&&doc.groups.size()==1,"ungroup can be undone");
            check(history.undo(doc)&&doc.groups.empty(),"group creation can be undone");
            selection.clear();dirty=false;command(198);check(doc.groups.empty()&&!dirty,"empty grouping does not dirty the document");
            doc=originalDoc;history=originalHistory;selection=originalSelection;origin=originalOrigin;zoom=originalZoom;dirty=originalDirty;tool=Select;title();
        }
        {
            auto originalDoc=doc;auto originalHistory=history;auto originalSelection=selection;bool originalDirty=dirty;auto originalOrigin=origin;double originalZoom=zoom;
            Bitmap snapshot(ww,wh,PixelFormat32bppARGB);Graphics graphics(&snapshot);
            auto repaint=[&](){HDC dc=graphics.GetHDC();paint(dc);graphics.ReleaseHDC(dc);};
            auto hit=[&](int id)->RectF{for(auto& h:hits)if(h.id==id)return h.box;return {};};
            auto present=[&](int id){return hit(id).Width>0;};
            sidebarPage=0;showLayers=false;repaint();
            check(present(127)&&present(126)&&present(143)&&present(144)&&present(145)&&present(146)&&present(148)&&present(149)&&!present(137)&&!present(141),"mechanism tab groups settings, arrangement and stage/connection controls");
            for(int id:{100,101,102,104,105,107,125,155,156,170,171,172,173,174,160,161,162,163,164}){
                check(present(id)&&hit(id).Y==hit(100).Y&&hit(id).Width==44&&hit(id).GetBottom()<89,"icon toolbar fits one row above document tabs");
                check(!helpText(id).empty(),"each toolbar icon has Korean help");
            }
            check(hit(156).X==hit(155).X+48,"text palette is immediately right of atom palette");
            check(hit(164).GetRight()<1200,"all toolbar groups fit minimum window width");
            check(present(218)&&present(219)&&hit(150).GetBottom()<=wh-bottom,"orbital tools and style button fit the client height");
            auto before=toNative(doc);command(183);repaint();
            check(present(137)&&present(139)&&present(138)&&present(141)&&!present(127)&&!present(126),"graph tab groups graph-only actions");
            check(before==toNative(doc)&&dirty==originalDirty,"switching sidebar tabs never modifies the document");
            doc=Document{};selection.clear();history.clear();origin={left+160.,top+180.};zoom=2;dirty=false;
            command(218);orbitalKind=OrbitalKind::P;orbitalPhase=2;
            Vec at=origin;mouseDown(at);mouse=at+Vec{60,0};mouseUp(mouse);
            check(doc.objects.size()==2&&doc.groups.size()==1&&selection.size()==2,"orbital tool draws two grouped phase lobes on drag");
            check(orbitalMembers(selection.front())==selection,"orbital hit resolves to the full editable orbital");
            check(std::abs((doc.objects[0].points[3]-doc.objects[0].points[0]).length()-30)<1e-9,"orbital drag maps screen size to document units");
            auto orbital=toNative(doc);command(104);check(doc.objects.empty(),"orbital insertion is one undo action");command(105);check(toNative(doc)==orbital,"orbital insertion redo restores shape and phases");
            selection=doc.groups[0].members;formAction=7;formDriven=false;SetTimer(hwnd,920,25,driveForm);editObject(selection.back());KillTimer(hwnd,920);
            check(formDriven&&doc.objects.size()==4&&selection.size()==4,"orbital properties edit the whole object into a d orbital");
            for(auto& o:doc.objects)check(!o.filled&&std::abs((o.points[3]-o.points[0]).length()-40)<1e-7,"orbital properties apply size and outline phase to every lobe");
            command(104);check(toNative(doc)==orbital,"orbital property edit is one undo action");
            for(int k=0;k<4;k++){orbitalKind=OrbitalKind(k);orbitalPhase=k==1?2:0;down={origin.x+150*k,origin.y+120};auto fragment=draftOrbital(down+Vec{0,-45});doc.append(fragment);}
            selection.clear();sidebarPage=0;fit();repaint();
            CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
            check(snapshot.Save(L"orbital-sidebar-preview.png",&png,nullptr)==Ok,"render orbital drawing tools and mechanism sidebar");
            sidebarPage=1;repaint();check(snapshot.Save(L"graph-sidebar-preview.png",&png,nullptr)==Ok,"render organized graph sidebar");
            doc=originalDoc;history=originalHistory;selection=originalSelection;dirty=originalDirty;origin=originalOrigin;zoom=originalZoom;tool=Select;orbitalKind=OrbitalKind::P;orbitalPhase=0;sidebarPage=0;
        }
        {
            auto originalDoc=doc;auto originalHistory=history;auto originalSelection=selection;bool originalDirty=dirty;auto originalRepeat=repeatAction;
            BYTE originalKeys[256]{};GetKeyboardState(originalKeys);
            auto press=[&](WPARAM k,bool ctrl,bool shift,bool alt){BYTE keys[256]{};keys[VK_CONTROL]=ctrl?0x80:0;keys[VK_SHIFT]=shift?0x80:0;keys[VK_MENU]=alt?0x80:0;SetKeyboardState(keys);SendMessageW(hwnd,alt?WM_SYSKEYDOWN:WM_KEYDOWN,k,0);SetKeyboardState(originalKeys);};
            doc=Document{};history.clear();int a=doc.addAtom({0,0}),b=doc.addAtom({30,0});doc.addBond(a,b);int c=doc.addAtom({100,80}),d=doc.addAtom({130,80});doc.addBond(c,d);selection={a,c};
            auto before=toNative(doc);press('M',true,true,true);
            check(std::abs(doc.atom(a)->p.y-doc.atom(c)->p.y)<1e-8&&std::abs(doc.atom(b)->p.y-doc.atom(d)->p.y)<1e-8,"Ctrl+Shift+Alt+M via WM_SYSKEYDOWN aligns whole molecules to middle");
            check(std::abs((doc.atom(a)->p-doc.atom(b)->p).length()-30)<1e-8,"keyboard alignment preserves bond geometry");
            press('Z',true,false,false);check(toNative(doc)==before,"alignment keyboard command is undoable");
            press('Z',true,true,false);check(std::abs(doc.atom(a)->p.y-doc.atom(c)->p.y)<1e-8,"Ctrl+Shift+Z redoes alignment");
            selection=doc.editableIds();Vec initial=doc.atom(a)->p;press(VK_RIGHT,false,true,false);check((doc.atom(a)->p-initial-Vec{10,0}).length()<1e-8,"Shift+arrow nudges ten document points");
            press('Y',true,false,false);check((doc.atom(a)->p-initial-Vec{20,0}).length()<1e-8,"Ctrl+Y repeats the last edit instead of invoking redo");
            Vec bond=doc.atom(b)->p-doc.atom(a)->p;press(VK_RIGHT,false,false,true);Vec rotated=doc.atom(b)->p-doc.atom(a)->p;
            check(std::abs(std::atan2(rotated.y,rotated.x)-std::atan2(bond.y,bond.x)-pi/180)<1e-8,"Alt+Right rotates by one degree");
            formAction=8;formDriven=false;SetTimer(hwnd,921,20,driveForm);press('R',true,false,false);KillTimer(hwnd,921);check(formDriven,"Ctrl+R opens the rotation dialog");
            press('G',true,false,false);check(!doc.groups.empty(),"Ctrl+G groups selection");press('G',true,true,false);check(doc.groups.empty(),"Ctrl+Shift+G ungroups selection");
            auto count=documents.size();press('N',true,true,false);check(documents.size()==count,"unsupported Ctrl+Shift+N never opens an unrelated new document");
            hoverAtom=hoverBond=0;hotkey('G');check(tool==Orbital,"Shift+G selects orbital tool");
            doc=Document{};int root=doc.addAtom({0,0});hoverAtom=root;hotkey('a');check(doc.atoms.size()==7&&doc.bonds.size()==7,"a on atom sprouts phenyl with an exocyclic connector");
            check(std::abs((doc.atoms[1].p-doc.atoms[0].p).length()-25.5)<1e-8,"phenyl hotkey uses compact connector length");
            selection=doc.editableIds();press('K',false,false,true);check(std::count_if(doc.bonds.begin(),doc.bonds.end(),[](const Bond& edge){return edge.kind==BondKind::Aromatic;})==6,"Alt+K converts phenyl display to aromatic bonds");
            press('K',false,false,true);check(std::count_if(doc.bonds.begin(),doc.bonds.end(),[](const Bond& edge){return edge.kind==BondKind::Double;})==3,"Alt+K restores Kekule display");
            writePreview(L"benzene-connector-preview.png",demo());
            doc=originalDoc;history=originalHistory;selection=originalSelection;dirty=originalDirty;repeatAction=originalRepeat;tool=Select;hoverAtom=hoverBond=0;
        }
        {
            Document fixture;fixture.style.bondLength=18;fixture.ring({50,50},5);int co=fixture.atoms[0].id;fixture.atom(co)->element=27;fixture.atom(co)->label="LCo^{III}";for(int i:{1,2,4})fixture.atoms[i].element=8;
            int carbon=fixture.atoms[3].id;fixture.addBond(carbon,fixture.addAtom(fixture.atom(carbon)->p+Vec{18,0}));fixture.addBond(carbon,fixture.addAtom(fixture.atom(carbon)->p+Vec{0,18},1));writePreview(L"chelate-before.png",fixture);cleanStructure(fixture);writePreview(L"chelate-after.png",fixture);
            doc={};doc.ring({0,0},6,true);int a=doc.atoms[0].id;int c=doc.addAtom(bondEndpoint(doc,a,{100,0}));doc.addBond(a,c);writePreview(L"benzene-80-default.png",doc);doc={};
        }

        {
            doc={};int a=doc.addAtom({0,0}),b=doc.addAtom({30,0}),c=doc.addAtom({60,0}),d=doc.addAtom({90,0});doc.addBond(a,b);doc.addBond(b,c);doc.addBond(c,d);selection={b,c};targetContainer=0;history.clear();
            formBefore=toNative(doc);formAction=6;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);command(165);check(formDriven&&previewObserved&&toNative(doc)==formBefore&&selection==std::vector<int>({b,c}),"canceling partial resize restores the document and original selection");
            formAction=5;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);BYTE old[256]{},keys[256]{};GetKeyboardState(old);memcpy(keys,old,sizeof keys);keys[VK_CONTROL]=0x80;SetKeyboardState(keys);key('K');SetKeyboardState(old);
            check(formDriven&&previewObserved&&std::abs(doc.atom(b)->p.x-37.5)<1e-8&&std::abs(doc.atom(c)->p.x-52.5)<1e-8,"Ctrl+K scales only selected connected atoms");
            check(doc.atom(a)->p.x==0&&doc.atom(d)->p.x==90&&!contains(selection,a)&&!contains(selection,d),"Ctrl+K leaves unselected connected atoms and selection unchanged");
            check(history.undo(doc)&&toNative(doc)==formBefore,"partial Ctrl+K is one undo operation");
            doc={};selection.clear();history.clear();
        }

        {
            doc=coordinateDemo();history.clear();selection={doc.plots[0].id};targetContainer=selection.front();tool=Text;dirty=false;doc.graphsLocked=true;
            auto before=toNative(doc);auto chosen=selection;auto target=targetContainer;
            for(double scale:{.6,1.,3.}){
                zoom=scale;origin={double(left+90),double(top+100)};Vec initial=origin,at{double(left+160),double(top+180)};
                SendMessageW(hwnd,WM_RBUTTONDOWN,MK_CONTROL|MK_RBUTTON,MAKELPARAM(int(at.x),int(at.y)));
                check(panning&&rightPanning&&GetCapture()==hwnd,"Ctrl+right drag starts viewport pan over drawing in any tool");
                SendMessageW(hwnd,WM_MOUSEMOVE,MK_RBUTTON|MK_CONTROL,MAKELPARAM(int(at.x+70),int(at.y-30)));
                check((origin-initial-Vec{70,-30}).length()<1e-8,"right drag pans by screen pixels at every zoom");
                SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(int(at.x),int(at.y)));check(rightPanning,"other mouse button release does not end right drag");
                SendMessageW(hwnd,WM_RBUTTONUP,0,MAKELPARAM(-20,int(at.y+40)));
                check(!panning&&!rightPanning&&GetCapture()!=hwnd&&(origin-initial-Vec{-20-at.x,40}).length()<1e-8,"right release outside canvas ends pan at final mouse location even after Ctrl released");
                check(toNative(doc)==before&&!dirty&&selection==chosen&&targetContainer==target,"viewport pan preserves document, selection and insertion target");
            }
            check(!history.undo(doc),"viewport movement does not consume document undo history");
            Vec at{double(left+160),double(top+180)},initial=origin;SendMessageW(hwnd,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(int(at.x),int(at.y)));check(!panning,"plain right click does not pan");
            SendMessageW(hwnd,WM_RBUTTONDOWN,MK_RBUTTON|MK_CONTROL,MAKELPARAM(20,20));check(!panning,"Ctrl+right click on toolbar does not pan");
            SendMessageW(hwnd,WM_RBUTTONDOWN,MK_RBUTTON|MK_CONTROL,MAKELPARAM(int(at.x),int(at.y)));SendMessageW(hwnd,WM_KEYDOWN,VK_ESCAPE,0);check(!panning&&!rightPanning&&GetCapture()!=hwnd,"Escape ends viewport pan");
            SendMessageW(hwnd,WM_RBUTTONDOWN,MK_RBUTTON|MK_CONTROL,MAKELPARAM(int(at.x),int(at.y)));ReleaseCapture();check(!panning&&!rightPanning,"capture loss ends viewport pan");
            doc={};selection.clear();targetContainer=0;tool=Select;history.clear();
        }

        {
            doc={};history.clear();EnergyPlot p;p.id=doc.nextId++;p.p={30,60};p.title="Layer test";p.states={{"R",0},{"P",10}};p.series={{"Path 1","#167d9a",{0,10}}};doc.plots.push_back(p);
            Vec caption;for(auto& item:scene().items)if(item.owner==p.id&&item.textRole==4)caption=item.points[0]+Vec{3,3};
            auto image=imageDocument(readFile(L"animated.gif"));doc.append(image);int imageId=doc.objects.back().id;Vec overlap=p.p+Vec{140,110};doc.objects.back().points={overlap-Vec{30,30},overlap+Vec{30,30}};
            selection={p.id};targetContainer=p.id;hoverAtom=hoverBond=0;tool=Select;zoom=.6;origin={double(left+60),double(top+150)};
            command(176);check(showLayers,"layer panel opens");command(179);check(doc.graphsLocked&&!doc.imagesLocked&&selection.empty()&&!targetContainer,"image-only mode clears locked graph selection and insertion target");
            check(!textAt(caption)&&objectAt(overlap)==imageId,"locked graph title clicks pass through to image");
            check(!pickSlot(plotAnchor(doc.plots[0],0,true)),"locked graph insertion slots cannot intercept clicks");
            mouseDown(screen(overlap));check(selection==std::vector<int>{imageId},"clicking over graph selects editable image");mouseUp(screen(overlap));
            command(180);check(doc.imagesLocked&&!doc.graphsLocked&&selection.empty(),"graph-only mode removes locked image from selection");
            check(objectAt(overlap)==p.id,"locked image allows graph beneath to be selected");
            for(auto& item:scene().items)if(item.owner==p.id&&item.textRole==4)caption=item.points[0]+Vec{3,3};
            check(textAt(caption)&&textAt(caption)->owner==p.id,"unlocked graph title is editable");
            command(177);command(178);check(!doc.imagesLocked&&doc.graphsLocked,"each layer lock can be toggled independently");
            command(179);mouseDown(screen({-20,-40}));check(boxing,"start box selection around overlapping graph and image");mouseUp(screen({700,600}));check(contains(selection,imageId)&&!contains(selection,p.id),"box selection skips locked graphs");
            command(180);BYTE old[256]{},keys[256]{};GetKeyboardState(old);memcpy(keys,old,sizeof keys);keys[VK_CONTROL]=0x80;SetKeyboardState(keys);key('A');SetKeyboardState(old);check(!contains(selection,imageId)&&contains(selection,p.id),"Ctrl+A skips locked images");
            command(181);check(!doc.imagesLocked&&!doc.graphsLocked,"unlock all restores both layers");command(104);check(doc.imagesLocked&&!doc.graphsLocked,"layer lock changes can be undone");command(105);check(!doc.imagesLocked&&!doc.graphsLocked,"layer lock changes can be redone");
            command(179);Bitmap panel(ww,wh,PixelFormat32bppARGB);{Graphics graphics(&panel);auto dc=graphics.GetHDC();paint(dc);graphics.ReleaseHDC(dc);}CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};check(panel.Save(L"layer-panel.png",&png,nullptr)==Ok,"render layer panel preview");
            auto restored=fromNative(toNative(doc));check(restored.graphsLocked&&!restored.imagesLocked,"image-only mode persists in project");command(175);selection.clear();targetContainer=0;doc={};history.clear();
        }

        {
            doc={};history.clear();int a=doc.addAtom({20,20},27),b=doc.addAtom({70,20},8);doc.atom(a)->label="CoIII";selection={a};hoverAtom=hoverBond=0;tool=Select;
            hotkey('-');hotkey('*');check(doc.atom(a)->charge==-1&&doc.atom(a)->radical==2&&doc.atom(a)->label=="CoIII","selected atom accepts radical anion without clearing label");
            hotkey('*');check(!doc.atom(a)->radical&&doc.atom(a)->charge==-1,"radical shortcut toggles independently of charge");
            hoverAtom=b;hotkey('+');hotkey('*');check(doc.atom(b)->charge==1&&doc.atom(b)->radical==2&&doc.atom(a)->charge==-1,"hover target gets radical cation before selected atom");
            hoverAtom=0;zoom=3;origin={double(left+200),double(top+200)};
            Vec at;for(auto& item:scene().items)if(item.owner==b&&item.textRole==13)at=item.points[0]+Vec{measure(item.text,item.fontSize,item.font,item.face)/2,item.fontSize/2};
            Vec original=doc.atom(b)->p;mouseDown(screen(at));check(dragging&&annotationDrag==b,"annotation can be grabbed directly in select tool");mouse=down+Vec{45,30};updateDrag();mouseUp(mouse);
            check(doc.atom(b)->annotationManual&&(doc.atom(b)->p-original).length()<1e-8,"annotation drag leaves atom fixed");
            auto saved=fromNative(toNative(doc));check(saved.atom(b)->annotationManual,"dragged annotation persists");
            check(history.undo(doc)&&!doc.atom(b)->annotationManual,"annotation drag is one undo operation");
            setInlineValue({a,0},"CoIII");check(doc.atom(a)->label=="Co^{III}","typed atom oxidation state is normalized");
            Object text;text.id=doc.nextId++;text.kind=ObjectKind::Text;text.points={{20,70}};doc.objects.push_back(text);setInlineValue({text.id,0},"CoIII");check(doc.objects.back().text=="Co^{III}","typed free text oxidation state is normalized");
            writePreview(L"atom-annotations.png",doc);selection.clear();hoverAtom=hoverBond=0;annotationDrag=0;
        }

        {
            doc={};selection.clear();history.clear();formBefore=toNative(doc);formAction=12;formDriven=previewObserved=false;
            SetTimer(hwnd,901,10,driveForm);command(220);
            check(formDriven&&previewObserved&&toNative(doc)==formBefore,"branch arrow preview cancels without changes");
            formAction=11;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);command(220);
            check(formDriven&&doc.objects.size()==5&&doc.groups.size()==1,"branch insert dialog creates four arrows on one spine");
            writePreview(L"shared-spine-arrows.png",doc);
            check(history.undo(doc)&&toNative(doc)==formBefore,"branch insertion undoes as one action");
            doc={};selection.clear();history.clear();targetContainer=0;
            for(Vec p:std::vector<Vec>{{0,0},{240,0},{240,160}}){int a=doc.addAtom(p,27),b=doc.addAtom(p+Vec{22,14},8);doc.atom(a)->label="CoIII";doc.addBond(a,b);}
            Object stem;stem.id=doc.nextId++;stem.points={{60,0},{60,160}};doc.objects.push_back(stem);
            for(double y:{0.,160.}){Object arrow;arrow.id=doc.nextId++;arrow.points={{60,y},{200,y}};arrow.head=1;doc.objects.push_back(arrow);}
            formBefore=toNative(doc);formAction=14;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);command(167);
            check(formDriven&&previewObserved&&toNative(doc)==formBefore,"import arrangement preview cancels without changing original chemistry");
            formAction=13;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);command(167);
            check(formDriven&&doc.reactions.size()==1&&doc.reactions[0].links.size()==2&&doc.reactions[0].links[1].from==0,"import dialog retains a source-connected shared fork");
            check(doc.atoms[0].label=="Co^{III}","import dialog normalizes oxidation state");
            writePreview(L"shared-spine-arranged.png",doc);
            check(history.undo(doc)&&toNative(doc)==formBefore,"import geometry, labels and topology undo together");
            selection.clear();targetContainer=0;doc={};history.clear();
        }

        {
            doc={};Object label;label.id=doc.nextId++;label.kind=ObjectKind::Text;label.points={{30,30}};label.text="H2";doc.objects.push_back(label);
            formAction=0;SetTimer(hwnd,901,10,driveForm);editObject(label.id);check(formDriven&&doc.objects[0].text=="H_{2}","Word subscript shortcut works in the actual text properties dialog");
            doc={};int a=doc.addAtom({0,0}),b=doc.addAtom({13,3}),c=doc.addAtom({26,0});doc.addBond(a,b);doc.addBond(b,c);selection={a};history.clear();dirty=false;
            formBefore=toNative(doc);formAction=1;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);clean();
            check(formDriven&&previewObserved&&toNative(doc)==formBefore,"cleanup previews the chosen rotation and Cancel restores the exact original");
            formAction=2;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);clean();
            check(formDriven&&previewObserved&&toNative(doc)!=formBefore&&history.undo(doc)&&toNative(doc)==formBefore,"cleanup Apply commits once and can be completely undone");
            doc=mechanismDemo();formBefore=toNative(doc);history.clear();formAction=3;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);reactionSettings(doc.reactions[0].id);
            check(formDriven&&previewObserved&&toNative(doc)==formBefore,"serpentine settings preview and cancel restore the document exactly");
            formAction=4;formDriven=previewObserved=false;SetTimer(hwnd,901,10,driveForm);reactionSettings(doc.reactions[0].id);
            check(formDriven&&previewObserved&&doc.reactions[0].wrapColumns==2&&doc.reactions[0].rowGap==180,"serpentine settings apply from the real dialog");
            check(history.undo(doc)&&toNative(doc)==formBefore,"serpentine settings undo restores original structures and positions");
            doc=mechanismDemo();for(int i=0;i<4;i++){doc.reactions[0].slots.push_back({"State "+std::to_string(i+4),{},"0.0"});doc.reactions[0].above.push_back("TS / Step");doc.reactions[0].below.push_back("12.3");}
            doc.reactions[0].wrapColumns=3;for(size_t i=0;i<7;i++)if(doc.reactions[0].slots[i].members.empty())fillSlot(doc,demo(),doc.reactions[0].id,i);optimizeMechanism(doc);writePreview(L"mechanism-serpentine.png",doc);
            doc={};EnergyPlot staged;staged.id=doc.nextId++;staged.states={{"A",0},{"B",20},{"C",-10}};staged.series={{"Path 1","#167d9a",{0,20,-10}}};staged.stages=defaultMechanismStages();staged.stages[1].fontSize=24;doc.plots.push_back(staged);
            setInlineValue({staged.id,12,1},"Transition State");check(inlineValue({staged.id,12,1})=="Transition State","plot stage supports direct text editing");writePreview(L"plot-mechanism-stages.png",doc);
            doc={};selection.clear();history.clear();dirty=false;
        }
        {
            doc=mechanismDemo();history.clear();tool=Select;selection={doc.reactions[0].id};fit();
            auto original=toNative(doc);int id=selection[0];
            beginInline(InlineTarget{id,1,0,reactionSlot(doc.reactions[0],0),14},reactionSlot(doc.reactions[0],0));
            Bitmap toolbar(ww,wh,PixelFormat32bppARGB);{Graphics graphics(&toolbar);HDC dc=graphics.GetHDC();paint(dc);graphics.ReleaseHDC(dc);}
            Vec palettePoint;for(auto& h:hits)if(h.id==156)palettePoint={h.box.X+22,h.box.Y+22};
            paletteDriven=false;SetTimer(hwnd,930,10,driveTextPalette);mouseDown(palettePoint);KillTimer(hwnd,930);
            check(paletteDriven&&!inlineEdit&&doc.reactions[0].captionColors.size()==1,"toolbar text palette commits editing and opens the native color picker");
            bool blue=false,black=false;for(auto& item:scene().items)if(item.owner==id&&item.type==Primitive::Text){if(item.textRole==1&&item.textIndex==0)blue=item.color=="#235ae1";if(item.textRole==1&&item.textIndex==1)black=item.color=="#000000";}
            check(blue&&black,"palette changes only the edited stage label");
            auto saved=toNative(doc);for(auto restored:{fromNative(saved),fromCdxml(toCdxml(doc))})check(restored.reactions[0].captionColors[0].color=="#235ae1","caption color survives native and CDXML reopen");
            check(toSvg(scene()).find("#235ae1")!=std::string::npos,"SVG includes the chosen caption color");
            check(history.undo(doc)&&toNative(doc)==original,"text palette can be undone exactly");check(history.redo(doc)&&toNative(doc)==saved,"text palette redo retains chosen caption");
            auto grid=mechanismGridData(doc.reactions[0],mechanismRows(doc.reactions[0]));check(grid.captionColors.size()==1,"table editing preserves caption colors");
            applyTextColor("#009650");for(auto& item:scene().items)if(item.owner==id&&item.type==Primitive::Text)check(item.color=="#009650","selected block text color applies to every caption");
            check(history.undo(doc)&&toNative(doc)==saved,"whole block color can be undone");
            writePreview(L"mechanism-text-colors.png",doc);
            doc={};selection.clear();history.clear();dirty=false;
        }
        {
            doc=mechanismDemo();history.clear();selection.clear();tool=Select;fit();
            int id=doc.reactions[0].id;auto count=doc.reactions[0].slots.size();auto before=toNative(doc);
            mouseDown(mechanismAddPosition(doc.reactions[0]));
            check(doc.reactions[0].slots.size()==count+1&&doc.reactions[0].above.size()==count,"circular plus adds a structure and transition");
            check(history.undo(doc)&&toNative(doc)==before,"circular plus can be undone exactly");
            mouseDown(mechanismAddPosition(doc.reactions[0],true));
            check(doc.reactions[0].slots.size()==count+1&&doc.reactions[0].slots.back().startsRow,"bottom plus starts a manual row");
            auto lower=reactionSlot(doc.reactions[0],count);
            check(mechanismAddPosition(doc.reactions[0]).x<screen(lower).x,"second row displays the horizontal plus on the left");
            mouseDown(mechanismAddPosition(doc.reactions[0]));
            check(reactionSlot(doc.reactions[0],count+1).x<lower.x,"left plus extends the second row leftward");
            mouseDown(mechanismAddPosition(doc.reactions[0],true));
            auto bottom=reactionSlot(doc.reactions[0],count+2);
            check(bottom.y>lower.y&&mechanismAddPosition(doc.reactions[0]).x>screen(bottom).x,"third row displays the plus on the right");
            mouseDown(mechanismAddPosition(doc.reactions[0]));
            check(reactionSlot(doc.reactions[0],count+3).x>bottom.x,"right plus extends the third row rightward");
            writePreview(L"mechanism-manual-zigzag.png",doc);
            for(int i=0;i<4;i++)check(history.undo(doc),"manual plus click is undoable");
            check(toNative(doc)==before,"undoing manual rows restores the original exactly");
            doc.reactions[0].vertical=true;optimizeMechanism(doc,{id});fit();before=toNative(doc);
            mouseDown(mechanismAddPosition(doc.reactions[0]));
            check(doc.reactions[0].vertical&&doc.reactions[0].slots.size()==count+1&&reactionSlot(doc.reactions[0],count).y>reactionSlot(doc.reactions[0],count-1).y,"vertical plus extends downward");
            check(history.undo(doc)&&toNative(doc)==before,"vertical plus can be undone exactly");
            writePreview(L"mechanism-vertical.png",doc);
            doc.reactions[0].vertical=false;optimizeMechanism(doc,{id});writePreview(L"mechanism-readable.png",doc);
            doc={};selection.clear();history.clear();dirty=false;
        }
        {
            doc=mechanismDemo();history.clear();selection={doc.reactions[0].id};targetContainer=selection[0];targetSlot=1;targetAbove=false;dirty=false;
            const auto original=doc;const auto bytes=toNative(doc);auto originalRunner=structureEditor;
            bool isolated=false;
            structureEditor=[&](const Document& source)->std::optional<Document>{
                isolated=source.reactions.empty()&&source.plots.empty()&&source.atoms.size()<doc.atoms.size();
                check(toNative(doc)==bytes,"opening isolated editor never mutates parent");
                auto result=source;result.atoms.front().element=9;return result;
            };
            editTargetStructure();check(isolated&&dirty,"structure edit receives only the selected stage");
            auto& current=doc.reactions[0];auto edited=structureInSlot(doc,current.id,1);check(edited.atoms.front().element==9,"applying edit replaces selected structure");
            check(current.spacing==original.reactions[0].spacing&&current.slotWidth==original.reactions[0].slotWidth,"isolated apply preserves mechanism layout");
            for(size_t i:{size_t(0),size_t(2)})for(int id:original.reactions[0].slots[i].members)if(auto atom=original.atom(id))check(doc.atom(id)&&(doc.atom(id)->p-atom->p).length()<1e-9,"other stages keep atom identities and positions");
            check(history.undo(doc)&&toNative(doc)==bytes,"one undo restores complete original mechanism");
            history.clear();dirty=false;structureEditor=[](const Document&)->std::optional<Document>{return {};};editTargetStructure();check(toNative(doc)==bytes&&!dirty&&!history.undo(doc),"cancel leaves parent and undo history unchanged");
            structureEditor=[](const Document& source)->std::optional<Document>{return source;};editTargetStructure();check(toNative(doc)==bytes&&!history.undo(doc),"apply without editing is a no-op");
            structureEditor=originalRunner;doc={};history.clear();selection.clear();targetContainer=0;dirty=false;
        }
        {
            doc=mechanismDemo();makeMechanismGraph(doc.reactions[0]);while(!doc.reactions[0].links.empty())removeMechanismConnection(doc.reactions[0],0);
            history.clear();hits.clear();tool=Select;selection={doc.reactions[0].id};targetContainer=0;selectedCycleOwner=0;selectedCycleArrow=-1;
            origin={left+130.,top+130.};zoom=1;int owner=doc.reactions[0].id;command(145);
            auto source=screen(reactionSlot(doc.reactions[0],0)),destination=screen(reactionSlot(doc.reactions[0],1));
            mouseDown(source);check(mechanismConnectionDrag,"connect mode starts a native frame-to-frame drag");mouse=destination;mouseUp(destination);
            check(doc.reactions[0].links.size()==1&&doc.reactions[0].links[0].from==0&&doc.reactions[0].links[0].to==1,"releasing over target creates a real mechanism connection");
            fit();mechanismConnecting=false;selectedCycleOwner=owner;selectedCycleArrow=0;selection={owner};auto path=mechanismConnectionPath(owner,0);auto head=screen(path.back());destination=screen(reactionSlot(doc.reactions[0],2));
            mouseDown(head);check(mechanismConnectionDrag,"selected arrowhead exposes a draggable reconnection handle");mouse=destination;mouseUp(destination);
            check(doc.reactions[0].links[0].to==2,"dragging the arrowhead to another stage changes its endpoint");
            fit();path=mechanismConnectionPath(owner,0);head=screen(path.back());auto extended=head+Vec{-30,0};mouseDown(head);check(mechanismConnectionDrag,"length drag starts on arrowhead handle");check(!mechanismSlotAt(owner,world(extended)),"length drag ends in empty corridor");mouse=extended;mouseUp(extended);
            check(doc.reactions[0].arrowLength>0,"dragging arrowhead into empty space sets manual arrow length");
            fit();command(145);source=screen(reactionSlot(doc.reactions[0],0));destination=screen(reactionSlot(doc.reactions[0],1));mouseDown(source);mouse=destination;mouseUp(destination);key(VK_ESCAPE);
            check(doc.reactions[0].links.size()==2&&!mechanismConnecting,"second branch is added and Escape exits connection mode");selection={owner};command(125);
            check(doc.reactions[0].slotGrid[1].x==doc.reactions[0].slotGrid[2].x&&doc.reactions[0].slotGrid[1].y!=doc.reactions[0].slotGrid[2].y,"alignment reorganizes the manually connected branch targets");
            writePreview(L"mechanism-manual-connections.png",doc);
            auto before=toNative(doc);selectedCycleOwner=owner;selectedCycleArrow=0;selection={owner};check(deleteSelectedCycleArrow()&&doc.reactions[0].links.size()==1,"Delete removes the selected graph connection");
            check(history.undo(doc)&&toNative(doc)==before,"connection deletion can be undone completely");
            doc={};history.clear();selection.clear();targetContainer=0;selectedCycleOwner=0;selectedCycleArrow=-1;mechanismConnecting=mechanismConnectionDrag=false;dirty=false;
        }
        {
            doc=parallelMechanismFixture(true);history.clear();selection={doc.reactions[0].id};targetContainer=0;auto before=toNative(doc);
            writePreview(L"mechanism-parallel-before.png",doc);command(125);
            auto& r=doc.reactions[0];for(size_t i=4;i<10;i++)check(r.slotGrid[i].y==r.slotGrid[4].y,"native automatic alignment keeps the lower path horizontal across unequal pathway lengths");
            writePreview(L"mechanism-parallel-after.png",doc);check(history.undo(doc)&&toNative(doc)==before,"parallel pathway alignment is exactly undoable");
            doc={};history.clear();selection.clear();dirty=false;
        }
        if(argc>1&&std::string(argv[1])=="--live-chemdraw"){
            auto fragment=clipboardDocument();check(fragment&&fragment->atoms.size()==15&&fragment->bonds.size()==16,"actual Windows ChemDraw clipboard imports all atoms and bonds");
            doc={};history.clear();selection.clear();pasteStructure(*fragment);
            check(doc.atoms.size()==15&&doc.bonds.size()==16,"actual ChemDraw clipboard pastes as editable canvas objects");
            doc={};Reaction r;r.id=doc.nextId++;r.slots={{"R",{}},{"P",{}}};doc.reactions.push_back(r);
            selection={r.id};targetContainer=r.id;targetSlot=1;targetAbove=false;history.clear();auto before=toNative(doc);
            fillTarget(false);check(doc.atoms.size()==15&&doc.bonds.size()==16&&doc.reactions[0].slots[1].members.size()==31,"structure paste button accepts the actual ChemDraw clipboard in a mechanism slot");
            check(history.undo(doc)&&toNative(doc)==before,"ChemDraw clipboard insertion is undoable");
            doc={};history.clear();selection.clear();targetContainer=0;dirty=false;
            std::cout<<"Live ChemDraw clipboard paste passed\n";
        }
        {
            auto source=demo();auto native=clipboardStructureText(toNative(source));
            check(native&&native->atoms.size()==source.atoms.size(),"native clipboard structure is editable");
            std::string xml=R"(<CDXML><page id="1"><fragment id="2"><n id="3" p="0 0" Element="6"/><n id="4" p="30 0" Element="8"/><b id="5" B="3" E="4" Order="1"/></fragment></page></CDXML>)";
            auto external=clipboardStructureText(xml);check(external&&external->atoms.size()==2&&external->bonds.size()==1,"external CDXML clipboard data imports as atoms and bonds");
            check(!clipboardStructureText("ordinary text")&&!clipboardStructureText("<CDXML>broken"),"nonstructure or malformed clipboard content is ignored");
            auto wide=ui::wide(xml);HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(wide.size()+1)*sizeof(wchar_t));auto* data=GlobalLock(memory);memcpy(data,wide.c_str(),(wide.size()+1)*sizeof(wchar_t));GlobalUnlock(memory);
            auto decoded=clipboardStructureText(clipboardMemoryText(memory,true));GlobalFree(memory);check(decoded&&decoded->atoms.size()==2,"Unicode XML clipboard payload decodes correctly");
            doc={};Reaction r;r.id=doc.nextId++;r.slots={{"R",{}},{"P",{}}};doc.reactions.push_back(r);selection={r.id};targetContainer=r.id;targetSlot=1;targetAbove=false;history.clear();
            auto before=toNative(doc);pasteStructure(*native);
            check(doc.reactions[0].slots[0].members.empty()&&!doc.reactions[0].slots[1].members.empty()&&doc.atoms.size()==source.atoms.size(),"paste fills only the selected mechanism structure slot");
            auto pasted=doc.subset(doc.reactions[0].slots[1].members);auto box=makeScene(pasted).bounds;auto anchor=reactionSlot(doc.reactions[0],1);
            check(std::abs(box.x+box.w/2-anchor.x)<1e-6&&std::abs(box.y+box.h/2-anchor.y)<1e-6,"clipboard structure uses the same slot centering as file insertion");
            check(history.undo(doc)&&toNative(doc)==before,"slot paste can be completely undone");
            doc={};EnergyPlot p;p.id=doc.nextId++;p.states={{"R",0,{},{}},{"P",-10,{},{}}};doc.plots.push_back(p);selection={p.id};targetContainer=p.id;targetSlot=0;targetAbove=true;history.clear();
            pasteStructure(*external);check(!doc.plots[0].states[0].above.empty()&&doc.plots[0].states[0].below.empty()&&doc.plots[0].states[1].above.empty(),"paste targets the selected graph state above its energy level");
            targetSlot=1;targetAbove=false;pasteStructure(*external);check(!doc.plots[0].states[1].below.empty(),"graph lower insertion slot also accepts clipboard structures");
            {
                auto scaled=doc;auto members=scaled.plots[0].states[0].above;auto originalPoint=scaled.atom(members[0])->p;auto oldAnchor=plotAnchor(scaled.plots[0],0,true);
                auto lowerMembers=scaled.plots[0].states[1].below;int lowerA=lowerMembers[0],lowerB=lowerMembers[1];double lowerDistance=(scaled.atom(lowerA)->p-scaled.atom(lowerB)->p).length();
                resizeStructure(scaled,members,1.5,p.id,0,true);auto newAnchor=plotAnchor(scaled.plots[0],0,true);
                check((scaled.atom(members[0])->p-newAnchor-(originalPoint-oldAnchor)*1.5).length()<1e-6,"resizing a graph slot keeps its structure at the updated insertion anchor");
                check(std::abs((scaled.atom(lowerA)->p-scaled.atom(lowerB)->p).length()-lowerDistance)<1e-6,"slot resize does not scale another state structure");
                auto unchanged=toNative(scaled);resizeStructure(scaled,members,1,p.id,0,true);check(toNative(scaled)==unchanged,"100 percent resize is a true no-op");
            }
            selection.clear();auto membership=doc.plots[0].states[1].below;size_t count=doc.atoms.size();pasteStructure(*external);
            check(doc.atoms.size()==count+2&&doc.plots[0].states[1].below==membership,"ordinary canvas paste does not use an inactive insertion target");
            targetContainer=999999;before=toNative(doc);bool failed=false;try{insertStructureTarget(*external);}catch(...){failed=true;}
            check(failed&&toNative(doc)==before,"invalid insertion target leaves the document unchanged");
            doc={};history.clear();selection.clear();targetContainer=0;targetSlot=0;targetAbove=false;dirty=false;
        }
        const auto firstPath=fs::absolute(L"tab-first.ydraw").wstring(),secondPath=fs::absolute(L"tab-second.ydraw").wstring();
        doc={};history.clear();doc.addAtom({10,20},6);history.checkpoint(doc);doc.addAtom({40,20},8);
        selection={doc.atoms[1].id};filePath=firstPath;dirty=true;zoom=2.75;origin={33,44};tool=Double;targetContainer=17;targetSlot=2;targetAbove=true;
        newDocumentTab();check(documents.size()==2&&doc.atoms.empty()&&!dirty,"new tab preserves an unsaved document");
        doc.addAtom({1,2},7);filePath=secondPath;dirty=true;zoom=1.25;origin={90,100};history.checkpoint(doc);doc.addAtom({3,4},16);
        TabCtrl_SetCurSel(documentTabs,0);NMHDR notification{documentTabs,950,TCN_SELCHANGE};SendMessageW(hwnd,WM_NOTIFY,950,(LPARAM)&notification);
        check(activeDocument==0,"native tab selection switches the active document");
        check(doc.atoms.size()==2&&doc.atoms[1].element==8&&filePath==firstPath&&dirty,"first tab retains document, path, and modified state");
        check(zoom==2.75&&origin.x==33&&selection.size()==1&&tool==Double&&targetContainer==17&&targetSlot==2&&targetAbove,"view, tool, selection, and insertion target remain per document");
        check(history.undo(doc)&&doc.atoms.size()==1&&doc.atoms[0].element==6,"first tab has its own undo history");
        check(save()&&!dirty,"save first tab");
        switchDocumentTab(1);check(dirty&&doc.atoms.size()==2&&doc.atoms[0].element==7&&filePath==secondPath&&zoom==1.25,"saving first tab does not overwrite or clear second tab");
        check(history.undo(doc)&&doc.atoms.size()==1&&doc.atoms[0].element==7,"second tab has independent undo history");check(save(),"save second tab");
        check(fromNative(readFile(firstPath)).atoms[0].element==6&&fromNative(readFile(secondPath)).atoms[0].element==7,"both files save their own content");
        {
            auto saved=doc;doc=mechanismDemo();auto rows=mechanismRows(doc.reactions[0]);rows[1][1]="TS1 / Nucleophilic attack";rows[1][2]="0 / 1 / 2";rows[1][3]="0.0 / 8.7 / 13.4";rows[0][2]="0 / 2";rows[0][3]="-3.0 / 4.4";
            doc.reactions[0]=mechanismGridData(doc.reactions[0],rows);
            int reactionId=doc.reactions[0].id;
            auto oldName=doc.reactions[0].slots[0].label;beginInline(InlineTarget{reactionId,1,0,reactionSlot(doc.reactions[0],0),14},{});
            SetWindowTextW(inlineEdit,L"H2");SendMessageW(inlineEdit,EM_SETSEL,1,2);
            BYTE previousKeys[256]{};GetKeyboardState(previousKeys);BYTE scriptKeys[256];memcpy(scriptKeys,previousKeys,sizeof(scriptKeys));scriptKeys[VK_CONTROL]=0x80;scriptKeys[VK_SHIFT]=0;SetKeyboardState(scriptKeys);
            SendMessageW(inlineEdit,WM_KEYDOWN,VK_OEM_PLUS,0);SetKeyboardState(previousKeys);check(finishInline(true)&&doc.reactions[0].slots[0].label=="H_{2}","Word subscript shortcut applies to inline mechanism text");doc.reactions[0].slots[0].label=oldName;
            check(inlineValue({reactionId,11,0})=="TS1"&&inlineValue({reactionId,2,0})=="Nucleophilic attack","inline editing reads the name and description independently");
            setInlineValue({reactionId,11,0},"TS1 edited");check(transitionCaption(doc.reactions[0].above[0]).description=="Nucleophilic attack","editing TS name preserves arrow description");
            setInlineValue({reactionId,2,0},"Attack");check(transitionCaption(doc.reactions[0].above[0]).name=="TS1 edited","editing arrow description preserves TS name");
            doc.reactions[0].above[0]="TS1 / Nucleophilic attack";optimizeMechanism(doc);fit();
            Bitmap snapshot(ww,wh,PixelFormat32bppARGB);Graphics graphics(&snapshot);HDC dc=graphics.GetHDC();paint(dc);
            int state=SaveDC(dc);SetViewportOrgEx(dc,left,89,nullptr);SendMessageW(documentTabs,WM_PRINT,(WPARAM)dc,PRF_CLIENT|PRF_NONCLIENT|PRF_CHILDREN);RestoreDC(dc,state);graphics.ReleaseHDC(dc);
            CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
            check(snapshot.Save(L"document-tabs-preview.png",&png,nullptr)==Ok,"render document tab preview");
            auto previewDocument=doc;Document balanced;Reaction example;example.id=balanced.nextId++;example.slots={{"A",{}},{"C",{}},{"E",{}},{"G",{}},{"I",{}}};
            example.above={"B / Ring Opening","D / C–C bond cleavage","F / C–OH bond formation","H / 1,2-hydrogen atom transfer"};example.below={"?.? / 8.7 / 13.4","?.? / 17.1 / 22.5","?.? / 11.1 / 18.4","?.? / 11.1 / 18.4"};for(auto& slot:example.slots)slot.energy="-36.0 / ?.? / 22.3";balanced.reactions.push_back(example);
            auto source=demo();if(fs::exists(L"../chemdraw-clipboard.cdx"))source=fromCdx(readFile(L"../chemdraw-clipboard.cdx"));
            for(size_t i=0;i<5;i++)fillSlot(balanced,source,example.id,i);doc=balanced;fit();dc=graphics.GetHDC();paint(dc);graphics.ReleaseHDC(dc);
            check(snapshot.Save(L"mechanism-layout-preview.png",&png,nullptr)==Ok,"render five-stage mechanism with ChemDraw structures and readable captions");
            auto picture=scene();auto bounds=picture.bounds;Bitmap output(1800,420,PixelFormat32bppARGB);Graphics outputGraphics(&output);outputGraphics.Clear(Color(255,255,255,255));
            double outputScale=std::min(1740./bounds.w,360./bounds.h);outputGraphics.TranslateTransform(float(30-bounds.x*outputScale),float(210-(bounds.y+bounds.h/2)*outputScale));outputGraphics.ScaleTransform(float(outputScale),float(outputScale));render(outputGraphics,picture);
            check(output.Save(L"mechanism-balanced-export.png",&png,nullptr)==Ok,"render clean balanced-mechanism export");writeFile(L"mechanism-balanced-export.svg",toSvg(picture));doc=previewDocument;

            auto savedCycleDocument=doc;doc=balanced;optimizeMechanism(doc,{},true);fit();
            doc.reactions[0].captionFontSize=24;optimizeMechanism(doc,{},true);fit();
            writePreview(L"cycle-layout-preview.png",doc);writeFile(L"cycle-layout-preview.svg",toSvg(scene()));
            auto cycleBefore=toNative(doc);int cycleOwner=doc.reactions[0].id;Primitive picked;
            for(auto& item:scene().items)if(item.type==Primitive::Bezier&&item.reactionArrow==0){picked=item;break;}
            check(picked.points.size()==4,"cycle connector exposes selectable geometry");
            Vec onArc=(picked.points[0]+picked.points[3]+(picked.points[1]+picked.points[2])*3)*.125;
            auto hit=cycleArrowAt(onArc);check(hit&&hit->first==cycleOwner&&hit->second==0,"cycle arrow is hit tested along its curve");
            tool=Select;mouseDown(screen(onArc));check(selectedCycleArrow==0&&selectedCycleOwner==cycleOwner,"click selects just the cycle connector");
            key(VK_DELETE);check(doc.reactions.size()==1&&doc.reactions[0].hiddenArrows==std::vector<int>{0},"Delete preserves structures and removes only the picked connector");
            auto openedCycle=fromNative(toNative(doc));check(openedCycle.reactions[0].hiddenArrows==std::vector<int>{0},"arrow zero omission survives save and reopen");
            command(104);check(toNative(doc)==cycleBefore,"undo restores deleted cycle arrow");
            command(105);check(doc.reactions[0].hiddenArrows==std::vector<int>{0},"redo removes the same cycle arrow");
            doc=savedCycleDocument;selectedCycleArrow=-1;selectedCycleOwner=0;selection.clear();fit();
            selection={reactionId};auto atomCount=doc.atoms.size();
            for(int i=0;i<7;i++){
                command(122);
                auto bounds=scene().bounds;auto first=screen({bounds.x,bounds.y}),last=screen({bounds.x+bounds.w,bounds.y+bounds.h});
                check(first.x>=left&&last.x<=ww-right&&first.y>=top&&last.y<=wh-bottom,"toolbar stage addition fits the expanded mechanism inside the canvas");
            }
            check(doc.reactions[0].slots.size()==10&&doc.atoms.size()==atomCount,"toolbar adds more than five stages without losing structures");
            check(history.undo(doc)&&doc.reactions[0].slots.size()==9,"stage addition remains undoable");doc=std::move(saved);dirty=false;
        }
        {
            auto saved=doc;auto savedHistory=history;auto savedZoom=zoom;auto savedOrigin=origin;auto savedSelection=selection;auto savedDirty=dirty;
            doc=coordinateDemo();fit();hits.clear();history=History{};int id=doc.plots[0].id;
            auto points=scene().plotSnapPoints.at(id);check(points.size()>=6,"canvas exposes plateau snap points");
            selection={id};formAction=9;formBefore=toNative(doc);previewObserved=false;formDriven=false;SetTimer(win,994,20,driveForm);editPlotColors(id);
            check(formDriven&&previewObserved&&doc.plots[0].series[0].levelColors[1]=="#cc2255"&&doc.plots[0].series[0].segmentColors[1]=="#cc2255","range color form previews and commits selected levels and connectors");
            check(history.undo(doc)&&toNative(doc)==formBefore,"range color edit is undoable");
            formAction=10;formBefore=toNative(doc);previewObserved=false;formDriven=false;SetTimer(win,995,20,driveForm);editPlotColors(id);
            check(formDriven&&previewObserved&&toNative(doc)==formBefore,"cancel restores colors after preview");
            snapPlot=id;snapStage=-1;snapLabel={"Spin crossing",0,100};snapStart.reset();
            auto before=toNative(doc);auto first=points.front(),last=points.back();
            mouseDown(screen(last.p));check(snapStart.has_value()&&toNative(doc)==before,"first snap click is nonmutating");
            sidebarPage=1;mouse=screen(first.p);
            {Bitmap snapshot(ww,wh,PixelFormat32bppARGB);Graphics graphics(&snapshot);HDC dc=graphics.GetHDC();paint(dc);graphics.ReleaseHDC(dc);
            CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};snapshot.Save(L"graph-snap-preview.png",&png,nullptr);}
            mouseDown(screen(first.p));check(!snapPlot&&!snapStart&&doc.plots[0].stages.back().label=="Spin crossing","reverse-order clicks create mechanism stage");
            check(std::abs(doc.plots[0].stages.back().start-first.percent)<1e-8&&std::abs(doc.plots[0].stages.back().end-last.percent)<1e-8,"stage arrow projects exactly to clicked plateau endpoints");
            check(history.undo(doc)&&toNative(doc)==before,"snap placement is one undoable action");
            snapPlot=id;snapStart=first;key(VK_ESCAPE);check(!snapPlot&&!snapStart&&toNative(doc)==before,"Esc cancels pending snap without changing the document");
            doc=std::move(saved);history=std::move(savedHistory);zoom=savedZoom;origin=savedOrigin;selection=savedSelection;dirty=savedDirty;
        }
        check(closeDocumentTab(1)&&documents.size()==1&&doc.atoms[0].element==6,"closing a tab restores its neighbor");
        check(closeDocumentTab(0)&&documents.size()==1&&doc.atoms.empty()&&!dirty,"closing last tab leaves a clean empty document");
        fs::remove(firstPath);fs::remove(secondPath);DestroyWindow(hwnd);
        std::cout<<"Document tab regression tests passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    imageCache.clear();appearance::shutdown();GdiplusShutdown(token);OleUninitialize();return result;
}

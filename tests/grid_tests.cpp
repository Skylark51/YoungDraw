// Exercise the real Win32 grid and its message handlers, including uncommitted editors.
#include "../src/win_grid.cpp"
#include "plot_data.h"
#include "mechanism_data.h"
#include "cdxml.h"
#include "scene.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>
using namespace yd;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Fixture {
    ui::Grid grid;EnergyPlot applied;
    Fixture(const EnergyPlot& original,const ui::Table& rows):applied(original){
        grid.rows=rows;grid.original=rows;grid.columns={L"상태 이름",L"Path 1",L"Path 2"};
        grid.cellError=[](size_t column,const std::string& value)->std::wstring{
            return column!=0&&!validEnergyGridValue(value)?L"Invalid energy":L"";
        };
        grid.preview=[&,original](const ui::Table& data){try{auto candidate=energyGridData(original,data);applied=std::move(candidate);return std::wstring{};}catch(const std::exception& e){return ui::wide(e.what());}};
        auto hwnd=CreateWindowExW(0,L"YoungDrawGridRegression",L"Grid test",WS_OVERLAPPED,0,0,990,640,nullptr,nullptr,GetModuleHandleW(nullptr),&grid);
        check(hwnd!=nullptr,"create grid");
    }
    ~Fixture(){DestroyWindow(grid.win);DeleteObject(grid.font);}
    void edit(int row,int col,const wchar_t* text){ui::selectCell(grid,row,col);ui::editCell(grid);SetWindowTextW(grid.edit,text);}
    void apply(){SendMessageW(grid.win,WM_COMMAND,MAKEWPARAM(22,BN_CLICKED),(LPARAM)grid.apply);}
};
int main(){try{
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW wc{};wc.lpfnWndProc=ui::gridProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YoungDrawGridRegression";check(RegisterClassW(&wc)!=0,"register grid");
    {
        auto mechanism=mechanismDemo().reactions[0];auto applied=mechanism;ui::Grid grid;
        grid.rows=mechanismRows(mechanism);grid.original=grid.rows;grid.stages=true;grid.scripts=true;
        grid.columns={L"구분",L"이름",L"S",L"에너지"};
        grid.preview=[&](const ui::Table& data){applied=mechanismGridData(mechanism,data);return std::wstring{};};
        auto hwnd=CreateWindowExW(0,L"YoungDrawGridRegression",L"Mechanism",WS_OVERLAPPED,0,0,990,640,nullptr,nullptr,GetModuleHandleW(nullptr),&grid);
        check(hwnd!=nullptr,"create mechanism grid");
        check(GetDlgItem(hwnd,25)&&GetDlgItem(hwnd,26),"mechanism grid exposes subscript and superscript buttons");
        ui::selectCell(grid,0,1);ui::editCell(grid);SetWindowTextW(grid.edit,L"I2");SendMessageW(grid.edit,EM_SETSEL,1,2);
        SendMessageW(hwnd,WM_COMMAND,25,0);check(ui::value(grid.edit)==L"I_{2}","name subscript button formats selected characters");
        SendMessageW(hwnd,WM_COMMAND,26,0);check(ui::value(grid.edit)==L"I^{2}","name superscript button switches selected script");
        SetWindowTextW(grid.edit,L"Fe^{III}");SendMessageW(grid.edit,EM_SETSEL,8,8);
        BYTE beforeAlt[256]{};GetKeyboardState(beforeAlt);BYTE altKeys[256];memcpy(altKeys,beforeAlt,sizeof(altKeys));altKeys[VK_MENU]=0x80;SetKeyboardState(altKeys);
        SendMessageW(grid.edit,WM_SYSKEYDOWN,VK_RETURN,0);SendMessageW(grid.edit,WM_SYSCHAR,VK_RETURN,0);SetKeyboardState(beforeAlt);
        SendMessageW(grid.edit,EM_REPLACESEL,TRUE,(LPARAM)L"Catalyst");
        check(grid.edit&&grid.row==0&&ui::value(grid.edit)==L"Fe^{III}\r\nCatalyst","Alt+Enter inserts exactly one newline without leaving the stage name");
        ui::commit(grid);check(applied.slots[0].label=="Fe^{III}\r\nCatalyst","multiline scripted stage name applies");
        auto originalName=grid.rows[2][1];ui::copy(grid);ui::selectCell(grid,2,1);ui::paste(grid);
        check(grid.rows[2][1]==grid.rows[0][1]&&grid.rows.size()==5,"copy and paste keeps a multiline name in one cell");
        ui::editCell(grid);SetWindowTextW(grid.edit,ui::wide(originalName).c_str());ui::commit(grid);
        ui::selectCell(grid,0,1);
        ui::editCell(grid);check(SendMessageW(grid.edit,EM_GETLINECOUNT,0,0)==2,"stage name reopens with both lines");
        RECT rowRect{};ListView_GetSubItemRect(grid.list,0,2,LVIR_BOUNDS,&rowRect);check(rowRect.bottom-rowRect.top>=40,"table rows show multiline names");
        auto multilineDoc=mechanismDemo();multilineDoc.reactions[0]=applied;
        for(auto restored:{fromNative(toNative(multilineDoc)),fromCdxml(toCdxml(multilineDoc))})check(restored.reactions[0].slots[0].label==applied.slots[0].label,"scripted multiline names survive save and reopen");
        SetWindowTextW(grid.edit,L"Reactant");ui::commit(grid);
        ui::selectCell(grid,1,2);ui::editCell(grid);SetWindowTextW(grid.edit,L"0 / 1 / 2");ui::moveCell(grid,VK_TAB,false);
        ui::editCell(grid);SetWindowTextW(grid.edit,L"0.0 / 8.7 / 13.4");ui::commit(grid);
        ui::selectCell(grid,4,2);ui::editCell(grid);SetWindowTextW(grid.edit,L"2");ui::moveCell(grid,VK_TAB,false);
        ui::editCell(grid);SetWindowTextW(grid.edit,L"21.5");
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(22,BN_CLICKED),(LPARAM)grid.apply);
        check(grid.ok&&applied.below[0]=="0.0 / 8.7 / 13.4"&&reactionEnergies(applied.slots[2].energy)[0].value=="21.5","mechanism Apply commits the active editor and separates TS from intermediate energy");
        check(mechanismRows(applied)==grid.rows,"mechanism cells reopen without resetting");
        grid.done=grid.ok=false;
        for(WPARAM key:{WPARAM(VK_RETURN),WPARAM(VK_TAB)}){
            ui::selectCell(grid,4,3);ui::editCell(grid);SetWindowTextW(grid.edit,L"21.5");
            ui::moveCell(grid,key,false);
            check(grid.rows.size()==5&&GetFocus()==grid.apply,"last mechanism cell focuses Apply without adding a stage");
        }
        grid.rows.push_back({"","","",""});
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(22,BN_CLICKED),(LPARAM)grid.apply);
        check(grid.ok&&grid.rows.size()==5&&applied.slots.size()==3&&applied.above.size()==2,"Apply ignores accidental empty tail without inventing another TS or intermediate");
        grid.done=grid.ok=false;ui::selectCell(grid,0,1);ui::insertStage(grid,false);
        check(grid.rows.size()==7&&grid.rows[2][0]=="중간체"&&grid.rows[3][0]=="TS","add stage inserts an intermediate and its TS together");
        auto inserted=mechanismGridData(mechanism,grid.rows);
        check(inserted.slots[2].members==mechanism.slots[1].members&&inserted.slots[1].members.empty(),"adding a stage does not steal the next intermediate's structure");
        ui::deleteStage(grid);check(grid.rows.size()==5,"delete stage removes its intermediate and TS together");
        ui::selectCell(grid,0,1);ui::duplicate(grid);check(grid.rows.size()==7&&grid.rows[2][0]=="중간체"&&grid.rows[2][1].find("Reactant")==0&&grid.rows[3][0]=="TS","duplicate stage keeps valid row types and copies its TS energies");
        while(grid.rows.size()<21){
            ui::selectCell(grid,int(grid.rows.size())-1,1);
            SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(20,BN_CLICKED),0);
        }
        check(grid.rows.size()==21&&applied.slots.size()==11,"real Add Stage button grows a mechanism beyond five stages");
        ui::selectCell(grid,20,3);ui::editCell(grid);SetWindowTextW(grid.edit,L"?.?");
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(22,BN_CLICKED),(LPARAM)grid.apply);
        check(grid.ok&&applied.slots.size()==11&&applied.slots.back().energy=="?.?","expanded mechanism applies unknown energies from the active editor");
        auto expanded=mechanismDemo();expanded.reactions[0]=applied;
        for(auto restored:{fromNative(toNative(expanded)),fromCdxml(toCdxml(expanded))})check(mechanismRows(restored.reactions[0])==mechanismRows(applied),"more than five stages survive native and CDXML reopen");
        grid.scripts=true;ui::selectCell(grid,0,1);ui::editCell(grid);SetWindowTextW(grid.edit,L"FeIII");SendMessageW(grid.edit,EM_SETSEL,2,5);
        BYTE savedScriptKeys[256]{};GetKeyboardState(savedScriptKeys);BYTE scriptKeys[256];memcpy(scriptKeys,savedScriptKeys,sizeof(scriptKeys));scriptKeys[VK_CONTROL]=0x80;scriptKeys[VK_SHIFT]=0x80;SetKeyboardState(scriptKeys);
        SendMessageW(grid.edit,WM_KEYDOWN,VK_OEM_PLUS,0);SetKeyboardState(savedScriptKeys);ui::commit(grid);
        check(grid.rows[0][1]=="Fe^{III}"&&applied.slots[0].label=="Fe^{III}","Word superscript shortcut works in the mechanism name column");
        DestroyWindow(hwnd);DeleteObject(grid.font);
    }
    {
        // Unlike same-thread timer tests, synchronous UI automation can complete
        // a dialog inside GetMessage. It must return without a later input event.
        auto parent=CreateWindowExW(0,L"STATIC",L"Grid owner",WS_OVERLAPPED,0,0,990,640,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        for(bool accept:{true,false}){
            std::atomic<bool> closed{false},neededWake{false};
            std::thread driver([&]{
                HWND dialog=nullptr;
                for(int i=0;i<200&&!dialog;i++){dialog=FindWindowW(L"YoungDrawGrid",L"Modal close regression");if(!dialog)Sleep(5);}
                if(!dialog){neededWake=true;return;}
                Sleep(50);DWORD_PTR ignored=0;
                SendMessageTimeoutW(dialog,accept?WM_COMMAND:WM_CLOSE,accept?22:0,0,SMTO_ABORTIFHUNG,2000,&ignored);
                for(int i=0;i<100&&!closed;i++)Sleep(5);
                if(!closed){neededWake=true;PostMessageW(dialog,WM_NULL,0,0);}
            });
            auto table=ui::table(parent,L"Modal close regression",{L"구분",L"이름",L"S",L"에너지"},{{"중간체","I","",""}},[](const ui::Table&){return std::wstring{};},false,L"",true);
            closed=true;driver.join();
            check(!neededWake,"Apply and Close end the real modal loop without another input event");
            check(bool(table)==accept&&IsWindowEnabled(parent),"modal result and owner input are restored");
        }
        DestroyWindow(parent);
    }
    {
        ui::Grid grid;grid.scripts=true;grid.scriptColumn=4;
        grid.columns={L"Type",L"Number",L"From",L"To",L"Name",L"Energy"};grid.rows={{"Structure","1","","","FeIII","0"}};
        auto hwnd=CreateWindowExW(0,L"YoungDrawGridRegression",L"Branch",WS_OVERLAPPED,0,0,990,640,nullptr,nullptr,GetModuleHandleW(nullptr),&grid);
        ui::selectCell(grid,0,4);ui::editCell(grid);SendMessageW(grid.edit,EM_SETSEL,2,5);
        SendMessageW(grid.edit,WM_KEYDOWN,VK_F10,0);check(ui::value(grid.edit)==L"Fe^{III}","branched mechanism shortcut formats the name column");
        ui::selectCell(grid,0,0);SendMessageW(hwnd,WM_COMMAND,25,0);
        check(grid.col==4&&grid.edit,"branch script button targets the name instead of row type");
        ui::commit(grid);check(grid.rows[0][0]=="Structure","branch formatting preserves row type");
        DestroyWindow(hwnd);DeleteObject(grid.font);
    }
    EnergyPlot original;original.id=1;original.states={{"R",0,{},{}},{"TS",20,{},{}},{"P",-10,{},{}}};
    original.series={{"Path 1","#167d9a",{0,20,-10},0,2},{"Path 2","#b44758",{0,22,-12},1,2}};
    ui::Table rows={{"R","0","0"},{"TS","20","22"},{"P","-10","-12"}};
    {
        Fixture f(original,rows);
        f.edit(0,1,L"1.25");ui::moveCell(f.grid,VK_RETURN,false);
        f.edit(1,1,L"31.5");ui::moveCell(f.grid,VK_RETURN,false);
        f.edit(2,1,L"-15");ui::moveCell(f.grid,VK_RETURN,false);
        check(f.grid.rows.size()==4&&f.grid.row==3&&f.grid.col==1,"Enter on last row creates and selects the next row in the same column");
        f.edit(0,2,L"2.5");ui::moveCell(f.grid,VK_TAB,false);
        f.edit(1,2,L"42.75");ui::moveCell(f.grid,VK_RETURN,false);
        f.edit(2,2,L"-9.9");
        BYTE keys[256]{};GetKeyboardState(keys);BYTE ctrlKeys[256];memcpy(ctrlKeys,keys,sizeof(keys));ctrlKeys[VK_CONTROL]=0x80;SetKeyboardState(ctrlKeys);
        SendMessageW(f.grid.edit,WM_KEYDOWN,'A',0);SetKeyboardState(keys);
        SendMessageW(f.grid.edit,EM_REPLACESEL,TRUE,(LPARAM)L"-23.5");
        check(ui::value(f.grid.edit)==L"-23.5","Ctrl+A replaces whole numeric value instead of appending");
        f.apply();
        check(f.grid.done&&f.grid.ok,"Apply commits last active editor and closes");
        check(f.applied.series[0].values==std::vector<double>({1.25,31.5,-15,0}),"first path applied");
        check(f.applied.series[1].values==std::vector<double>({2.5,42.75,-23.5,0}),"second path applied, including last editor");
        Fixture reopened(f.applied,f.grid.rows);
        check(reopened.grid.rows==f.grid.rows,"reopen retains two paths");
        Document saved;saved.nextId=2;saved.plots.push_back(f.applied);
        auto restored=fromNative(toNative(saved));
        check(restored.plots[0].series[1].values==f.applied.series[1].values,"two paths persist through save and reopen");
    }
    {
        Fixture f(original,rows);f.edit(2,2,L"oops");ui::commit(f.grid);
        check(IsWindowEnabled(f.grid.apply),"invalid preview must not disable Apply");
        f.apply();check(!f.grid.done&&!f.grid.ok,"invalid Apply keeps grid open");
        check(f.grid.rows[2][2]=="oops","invalid Apply preserves entered table");
        check(ui::value(f.grid.error).find(L"3행 · Path 2")!=std::wstring::npos,"error identifies row and pathway");
        f.edit(2,2,L"-30.25");f.apply();
        check(f.grid.done&&f.applied.series[1].values[2]==-30.25,"one click applies correction still in editor");
    }
    {
        Fixture f(original,rows);
        ui::selectCell(f.grid,1,0);
        ui::paste(f.grid,L"\"TS\r\n2\"\tbad\t42.5\r\nP\t\tinf\r\nNew\t5\t-6\r\n");
        check(f.grid.rows.size()==4&&f.grid.rows[1][0]=="TS\r\n2"&&f.grid.rows[2][1].empty(),"Excel range preserves quoted lines and blanks and grows the table");
        check(f.grid.invalidCells==std::set<std::pair<int,int>>{{1,1},{2,2}},"all invalid energies are marked");
        f.apply();
        check(!f.grid.done&&f.grid.row==1&&f.grid.col==1,"invalid Apply selects the first bad cell");
        auto pasted=f.grid.rows;
        ui::paste(f.grid,L"1\t2\t3");
        check(f.grid.rows==pasted,"out-of-bounds paste leaves the table intact");
        f.edit(1,1,L"24");ui::commit(f.grid);
        check(f.grid.invalidCells==std::set<std::pair<int,int>>{{2,2}},"correcting one cell clears its mark only");
        f.edit(2,2,L"-12");f.apply();
        check(f.grid.ok&&f.applied.series[1].values[3]==-6&&!f.applied.series[0].hasValue(2),"corrected paste applies with missing energies intact");
    }
    {
        auto withBlank=rows;withBlank.push_back({"","",""});Fixture f(original,withBlank);f.apply();
        check(f.grid.ok&&f.grid.rows.size()==4&&!f.applied.series[0].hasValue(3)&&!f.applied.series[1].hasValue(3),"trailing blank row applies and stays blank");
    }
    {
        Fixture f(original,rows);ui::selectCell(f.grid,0,0);SendMessageW(f.grid.list,WM_KEYDOWN,VK_ESCAPE,0);
        check(!f.grid.done,"Escape outside editor must not discard the table");
    }
    {
        auto repeated=rows;repeated[2][0]="R";auto p=energyGridData(original,repeated);
        check(p.states[2].label=="R"&&p.series[1].values[2]==-12,"repeated chemical state names remain valid");
    }
    {
        Fixture f(original,rows);f.edit(0,1,L"18.25");
        RECT rect{};ListView_GetSubItemRect(f.grid.list,2,3,LVIR_BOUNDS,&rect);
        LPARAM point=MAKELPARAM((rect.left+rect.right)/2,(rect.top+rect.bottom)/2);
        SendMessageW(f.grid.list,WM_LBUTTONDOWN,MK_LBUTTON,point);
        SendMessageW(f.grid.list,WM_LBUTTONUP,0,point);
        check(f.grid.row==2&&f.grid.col==2,"single click selects target cell while another cell is being edited");
        check(!f.grid.edit&&f.grid.rows[0][1]=="18.25","single click commits old editor once");
        SendMessageW(f.grid.list,WM_LBUTTONDBLCLK,MK_LBUTTON,point);SendMessageW(f.grid.list,WM_LBUTTONUP,0,point);
        check(f.grid.edit!=nullptr,"double click still opens editor after mouse up");
    }
    {
        Fixture f(original,rows);ui::selectCell(f.grid,2,2);ui::moveCell(f.grid,VK_TAB,false);
        check(f.grid.rows.size()==4&&f.grid.row==3&&f.grid.col==0,"Tab at bottom right creates and selects next row");
        f.apply();check(f.grid.ok,"newly created empty row does not block Apply");
    }
    {
        ui::Table sparse={{"R","10","11"},{"","",""},{"TS","","28"},{"","-5",""},{"P","-12",""}};
        Fixture f(original,sparse);f.apply();
        check(f.grid.ok&&f.grid.rows==sparse,"partially filled and middle blank rows apply without discarding cells");
        check(!f.applied.series[0].hasValue(2)&&f.applied.series[1].hasValue(2),"blank in one path does not suppress the other path");
        check(f.applied.states[3].label.empty()&&f.applied.series[0].values[3]==-5,"energy with blank state name applies");
        Document saved;saved.nextId=2;saved.plots.push_back(f.applied);
        for(auto restored:{fromNative(toNative(saved)),fromCdxml(toCdxml(saved))}){
            const auto& plot=restored.plots[0];check(plot.series[0].present==f.applied.series[0].present&&plot.series[1].present==f.applied.series[1].present,"blank cells persist in native and CDXML");
            check(plot.states.size()==5&&plot.states[1].label.empty(),"middle empty row persists");
        }
        auto drawing=makeScene(saved);int plateaus=0,connections=0;
        for(auto& item:drawing.items)if(item.owner==1&&item.type==Primitive::Line&&item.color=="#167d9a"&&item.points.size()==2){
            if(item.textRole==7||item.points[0].y<saved.plots[0].p.y)continue;
            if(std::abs(item.points[1].y-item.points[0].y)<1e-6)plateaus++;else connections++;
        }
        check(plateaus==3&&connections==1,"blank energies have no plateau or connection across the gap");
        for(int mode:{0,1,2,3}){
            saved.plots[0].mode=mode;auto gapScene=makeScene(saved);int segments=0;
            for(auto& item:gapScene.items)if(item.owner==1&&item.color=="#167d9a"&&(item.type==Primitive::Line||item.type==Primitive::Bezier)&&item.points.size()>=2){
                double dy=item.points.back().y-item.points.front().y;if(std::abs(dy)>1e-6)segments++;
            }
            check(segments==1,"all graph modes leave missing-value gaps disconnected");
        }
        auto high=energyGridData(original,{{"R","100","110"},{"","",""},{"P","120","130"}});
        check(energyAxis(high).low>0,"blank numeric placeholders cannot change the y axis");
    }
    {
        for(ui::Table sparse:{ui::Table{},ui::Table{{"","",""}},ui::Table{{"Only","5",""}}}){
            Fixture f(original,sparse);f.apply();
            check(!f.grid.done&&!f.grid.ok,"empty and single-row energy grids cannot replace saved data");
            check(!ui::value(f.grid.error).empty(),"invalid energy grid explains why Apply was rejected");
        }
    }
    {
        auto sub=ui::scriptChange(L"TS1",2,3,false);check(sub.text==L"TS_{1}","selected state text becomes subscript");
        auto plain=ui::scriptChange(sub.text,sub.start,sub.end,false);check(plain.text==L"TS1","repeating subscript shortcut restores normal selected text");
        auto super=ui::scriptChange(L"TS+",2,3,true);check(super.text==L"TS^{+}","selected charge becomes superscript");
        auto insertion=ui::scriptChange(L"TS",2,2,false);check(insertion.text==L"TS_{}"&&insertion.start==4,"empty selection starts script input mode");
        auto emptyExit=ui::scriptChange(insertion.text,4,4,false);check(emptyExit.text==L"TS"&&emptyExit.start==2,"exiting unused script input removes empty formatting");
        auto exit=ui::scriptChange(L"TS_{12}",6,6,false);check(exit.text==L"TS_{12}"&&exit.start==7,"same shortcut exits a typed script run");
        auto change=ui::scriptChange(L"TS_{12}",6,6,true);check(change.text==L"TS^{12}"&&change.start==6,"opposite shortcut switches the current script run");
        Fixture f(original,rows);f.grid.scripts=true;f.edit(0,0,L"TS1");SendMessageW(f.grid.edit,EM_SETSEL,2,3);
        BYTE savedKeys[256]{};GetKeyboardState(savedKeys);BYTE keys[256];memcpy(keys,savedKeys,sizeof(keys));keys[VK_CONTROL]=0x80;keys[VK_SHIFT]=0;SetKeyboardState(keys);
        SendMessageW(f.grid.edit,WM_KEYDOWN,VK_OEM_PLUS,0);SetKeyboardState(savedKeys);
        check(ui::value(f.grid.edit)==L"TS_{1}","Ctrl+= formats selected text in the actual state editor");
        SendMessageW(f.grid.edit,EM_SETSEL,5,5);memcpy(keys,savedKeys,sizeof(keys));keys[VK_CONTROL]=0x80;keys[VK_SHIFT]=0x80;SetKeyboardState(keys);
        SendMessageW(f.grid.edit,WM_KEYDOWN,VK_OEM_PLUS,0);SetKeyboardState(savedKeys);
        check(ui::value(f.grid.edit)==L"TS^{1}","Ctrl+Shift+= switches actual editor input to superscript");
        f.apply();check(f.grid.ok&&f.applied.states[0].label=="TS^{1}","formatted names apply without losing their formatting");
    }
    std::cout<<"Grid regression checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

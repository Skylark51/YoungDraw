#include "mechanism_data.h"
#include "scene.h"
#include "cdxml.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
using namespace yd;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    auto document=mechanismDemo();auto original=document.reactions[0];auto rows=mechanismRows(original);
    check(rows.size()==5&&rows[0][0]=="중간체"&&rows[1][0]=="TS"&&rows[2][0]=="중간체","rows explicitly distinguish intermediates and transition states");
    rows[0]={"중간체","Reactant","0 / 2","-3.0 / 4.4"};
    rows[1]={"TS","TS1 / Nucleophilic attack","2 / 0 / 1","13.4 / 0.0 / 8.7"};
    rows[2]={"중간체","Intermediate","1","-1.2"};
    rows[3]={"TS","TS2","0.5 / 1.5","5.0 / 6.0"};
    rows.push_back({"TS","Final TS","2","21.5"});rows.push_back({"중간체","","",""});
    auto edited=mechanismGridData(original,rows);
    auto caption=transitionCaption(edited.above[0]);check(caption.name=="TS1"&&caption.description=="Nucleophilic attack","slash separates the TS name from its arrow explanation");
    auto legacy=transitionCaption(original.above[0]);check(legacy.name.empty()&&legacy.description==original.above[0],"existing descriptions without a slash retain their original meaning");
    check(edited.below[0]=="0.0 / 8.7 / 13.4","TS spin energies sort by S with the matching energies");
    auto state=reactionEnergies(edited.slots[1].energy);check(state.size()==1&&state[0].spin==1&&state[0].value=="-1.2","each intermediate chooses its own spin set");
    auto ts=reactionEnergies(edited.below[1]);check(ts.size()==2&&ts[0].spin==.5&&ts[1].spin==1.5,"half-integer spin combinations are preserved");
    check(edited.above[2]=="Final TS"&&reactionEnergies(edited.below[2])[0].value=="21.5","last TS row is retained");
    check(edited.slots[0].members==original.slots[0].members&&edited.slots.size()==4&&edited.slots.back().energy.empty(),"structure associations and incomplete rows survive");
    auto invalid=rows;invalid[1][3]="oops";bool rejected=false;try{mechanismGridData(original,invalid);}catch(...){rejected=true;}check(rejected&&original.below[0]=="37.2","invalid candidate leaves original unchanged");
    auto mismatched=rows;mismatched[1][2]="0 / 1";rejected=false;try{mechanismGridData(original,mismatched);}catch(...){rejected=true;}check(rejected,"mismatched S and energy counts cannot silently shift values");
    auto incomplete=rows;incomplete[0][2]="0 / 2";incomplete[0][3]="";auto partial=mechanismGridData(original,incomplete);check(mechanismRows(partial)[0][2]=="0 / 2","selected spins survive even before energies are filled");
    {
        auto unknown=rows;unknown[0][2]="0 / 2";unknown[0][3]="?.? / ?";
        unknown[1][2]="2 / 0 / 1";unknown[1][3]="?.? / 12.5 / ?";
        unknown[3][3]="? / 6.0";
        Document pending=document;pending.reactions[0]=mechanismGridData(original,unknown);
        auto values=reactionEnergies(pending.reactions[0].slots[0].energy);
        check(values.size()==2&&values[0].value=="?.?"&&values[1].spin==2&&values[1].value=="?","unknown energies retain their exact text and spins");
        check(pending.reactions[0].below[0]=="12.5 / ? / ?.?","mixed numeric and unknown TS energies sort with their spins");
        for(bool cycle:{false,true}){
            optimizeMechanism(pending,{},cycle);auto picture=makeScene(pending);bool stateUnknown=false,tsUnknown=false;
            for(auto& item:picture.items)if(item.text=="?.?"){if(item.textRole==9)stateUnknown=true;if(item.textRole==3)tsUnknown=true;}
            check(stateUnknown&&tsUnknown,"unknown intermediate and TS energies render in linear and cyclic mechanisms");
            for(auto restored:{fromNative(toNative(pending)),fromCdxml(toCdxml(pending))})check(mechanismRows(restored.reactions[0])==mechanismRows(pending.reactions[0]),"unknown energies survive save and reopen");
        }
    }
    document.reactions[0]=edited;optimizeMechanism(document,{edited.id},false);auto drawing=makeScene(document);
    bool black=false,red=false,blue=false,stateBlue=false;double x0=0,x1=0,x2=0;
    for(auto& item:drawing.items)if(item.owner==edited.id){
        if(item.textRole==3&&item.textIndex==0){if(item.text=="0.0"){black=item.color=="#000000";x0=item.points[0].x;}if(item.text=="8.7"){red=item.color=="#ff0000";x1=item.points[0].x;}if(item.text=="13.4"){blue=item.color=="#0000ff";x2=item.points[0].x;}
            check(item.points[0].y>reactionSlot(edited,0).y,"TS energy is below its reaction arrow");}
        if(item.textRole==9&&item.textIndex==0&&item.text=="4.4"){stateBlue=item.color=="#0000ff";check(item.points[0].y>reactionSlot(edited,0).y+edited.slotHeight/2,"intermediate energy is below its structure");}
    }
    check(black&&red&&blue&&x0<x1&&x1<x2&&stateBlue,"linear layout renders intermediate energies and ordered black/red/blue TS energies");
    double nameY=0,descriptionY=0,energyY=0;
    for(auto& item:drawing.items)if(item.owner==edited.id&&item.textIndex==0){
        if(item.textRole==11&&item.text=="TS1")nameY=item.points[0].y;
        if(item.textRole==2)descriptionY=item.points[0].y;
        if(item.textRole==3)energyY=item.points[0].y;
    }
    check(descriptionY<edited.p.y&&nameY>edited.p.y&&energyY>nameY+document.style.fontSize,"linear TS explanation is above the arrow; name is above its energy below the arrow");
    auto expected=mechanismRows(edited);
    for(auto restored:{fromNative(toNative(document)),fromCdxml(toCdxml(document))})check(mechanismRows(restored.reactions[0])==expected,"stage types, spins, and energies survive native and CDXML save/reopen");
    std::ofstream("mechanism-spin-linear.svg")<<toSvg(drawing);
    optimizeMechanism(document,{edited.id},true);auto cyclic=makeScene(document);bool cycleState=false;for(auto& item:cyclic.items)if(item.textRole==9&&item.text=="4.4")cycleState=item.color=="#0000ff";check(cycleState,"cyclic intermediate keeps its spin color");
    double cycleName=0,cycleDescription=0,cycleEnergy=0;
    for(auto& item:cyclic.items)if(item.owner==edited.id&&item.textIndex==0){if(item.textRole==11)cycleName=item.points[0].y;if(item.textRole==2)cycleDescription=item.points[0].y;if(item.textRole==3)cycleEnergy=item.points[0].y;}
    check(cycleDescription<cycleName&&cycleEnergy>cycleName+document.style.fontSize,"cyclic TS labels preserve the explanation/name/energy order and spacing");
    std::ofstream("mechanism-spin-cycle.svg")<<toSvg(cyclic);
    {
        Document large;Reaction r;r.id=large.nextId++;r.slots={{"A",{}},{"C",{}},{"E",{}},{"G",{}},{"H",{}}};
        r.above={"B / Ring Opening","D / C–C bond cleavage","F / C–OH bond formation","H / 1,2-hydrogen atom transfer"};
        r.below={"?.? / 12.5 / 21.3","?.? / 13.4 / 22.5","?.? / 17.7 / 22.3","?.? / 17.7 / ?.?"};large.reactions.push_back(r);
        auto molecule=demo();for(auto& atom:molecule.atoms)atom.p=atom.p*2.5;
        for(size_t i=0;i<5;i++)fillSlot(large,molecule,r.id,i);
        auto picture=makeScene(large);auto reaction=large.reactions[0];double font=mechanismFontSize(reaction,large.style);
        for(auto& item:picture.items)if(item.owner==r.id&&item.textRole==2){
            size_t chars=0;for(unsigned char c:item.text)if((c&0xc0)!=0x80)chars++;
            double x=item.points[0].x,w=chars*item.fontSize*.62;auto center=reactionSlot(reaction,size_t(item.textIndex));
            check(x>=center.x+reaction.slotWidth/2&&x+w<=center.x+reaction.spacing-reaction.slotWidth/2,"captions stay in the common arrow corridor between structures");
            check(item.fontSize>=large.style.fontSize*.9,"large-structure captions remain readable");
        }
        check(font>=14&&reaction.spacing-reaction.slotWidth>=120,"inserting structures automatically reserves a readable arrow corridor");
        auto checkArrows=[&](const Document& candidate){
            auto drawing=makeScene(candidate);auto& r=candidate.reactions[0];double font=mechanismFontSize(r,candidate.style);
            double expected=mechanismArrowLength(r,candidate.style)-4.8*std::max(1.25,font/10.);size_t count=0;
            for(auto& item:drawing.items)if(item.owner==r.id){
                if(item.type==Primitive::Line){check(std::abs((item.points.back()-item.points.front()).length()-expected)<1e-5,"every arrow has the same caption-driven length, including equilibrium arrows");count++;}
                if(item.type==Primitive::Text&&(item.textRole==1||item.textRole==2||item.textRole==3||item.textRole==9||item.textRole==11)&&!item.text.empty())check(std::abs(item.fontSize-font)<1e-7,"upper captions, lower names, and energies share the same automatic font size");
            }
            check(count==(r.slots.size()-1)*(r.arrowType==1?2:1),"all reaction connectors are checked");
        };
        checkArrows(large);auto equilibrium=large;equilibrium.reactions[0].arrowType=1;checkArrows(equilibrium);
        double oldLength=mechanismArrowLength(reaction,large.style);auto extended=large;
        extended.reactions[0].above[0]="B / Longest reaction description controls the shared arrow length";optimizeMechanism(extended,{r.id});
        check(mechanismArrowLength(extended.reactions[0],extended.style)>oldLength,"editing one long caption lengthens every arrow together");checkArrows(extended);
        for(auto restored:{fromNative(toNative(extended)),fromCdxml(toCdxml(extended))})checkArrows(restored);
        std::ofstream("mechanism-balanced-preview.svg")<<toSvg(picture);
    }
    {
        Document scripted;Object text;text.id=scripted.nextId++;text.kind=ObjectKind::Text;text.points={{50,50}};text.text="H_{2}O / Fe^{III}";scripted.objects.push_back(text);
        auto visual=makeScene(scripted);bool sub=false,super=false;
        for(auto& item:visual.items){check(item.text.find("_{")==std::string::npos&&item.text.find("^{")==std::string::npos,"ordinary text renders script runs rather than markup");if(item.text=="2")sub=item.fontSize<text.fontSize&&item.points[0].y>50;if(item.text=="III")super=item.fontSize<text.fontSize&&item.points[0].y<50;}
        check(sub&&super,"ordinary text supports raised and lowered script runs");
        Document atomLabel;int atom=atomLabel.addAtom({0,0},26);atomLabel.atom(atom)->label="Fe^{III}";
        auto xml=toCdxml(atomLabel);auto start=xml.rfind("<annotation");if(start!=std::string::npos)xml.erase(start,xml.find("/>",start)-start+2);
        auto imported=fromCdxml(xml);bool raised=false;for(auto& run:imported.atoms[0].labelRuns)if(run.text=="III")raised=(run.face&64)&&run.size<atomLabel.style.fontSize;
        check(raised,"atom label scripts export as real CDXML styled runs");
    }
    {
        Document folded;Reaction r;r.id=folded.nextId++;r.p={100,100};r.wrapColumns=3;r.rowGap=160;
        for(int i=0;i<7;i++){r.slots.push_back({"State "+std::to_string(i+1),{},"0.0"});if(i<6){r.above.push_back("TS"+std::to_string(i+1)+" / Step "+std::to_string(i+1));r.below.push_back("12.3");}}folded.reactions.push_back(r);
        for(size_t i=0;i<7;i++)fillSlot(folded,demo(),r.id,i);
        for(int kind:{0,1}){
            folded.reactions[0].arrowType=kind;auto& layout=folded.reactions[0];auto a=reactionSlot(layout,0),b=reactionSlot(layout,2),c=reactionSlot(layout,3),e=reactionSlot(layout,5),f=reactionSlot(layout,6);
            check(b.x>a.x&&c.x==b.x&&c.y>b.y&&e.x==a.x&&f.x==a.x&&f.y>e.y,"serpentine rows alternate directions and drop at the row ends");
            auto drawing=makeScene(folded);int names=0,energies=0;for(auto& item:drawing.items){if(item.textRole==11)names++;if(item.textRole==3)energies++;}
            check(names==6&&energies==6,"all transitions keep editable name and energy captions after wrapping");
            for(size_t i=0;i<layout.slots.size();i++){auto box=makeScene(folded.subset(layout.slots[i].members)).bounds;check((reactionSlot(layout,i)-Vec{box.x+box.w/2,box.y+box.h/2}).length()<.01,"inserted structures stay centered on their wrapped slots");}
            for(auto restored:{fromNative(toNative(folded)),fromCdxml(toCdxml(folded))}){auto& saved=restored.reactions[0];check(saved.wrapColumns==3&&saved.rowGap==160&&saved.arrowType==kind&&(reactionSlot(saved,6)-f).length()<.01,"serpentine layout and arrow style survive native and CDXML");}
        }
        std::ofstream("mechanism-serpentine.svg")<<toSvg(makeScene(folded));
        std::ofstream("mechanism-serpentine.ydraw")<<toNative(folded);
        auto old=folded.reactions[0];folded.reactions[0].wrapColumns=2;optimizeMechanism(folded,{r.id});check(reactionSlot(folded.reactions[0],2).y>reactionSlot(folded.reactions[0],1).y,"changing the wrap count recalculates rows");
        optimizeMechanism(folded,{r.id},true);check(folded.reactions[0].cyclic,"cycle layout still works after serpentine mode");
        folded.reactions[0].wrapColumns=0;optimizeMechanism(folded,{r.id},false);check(reactionSlot(folded.reactions[0],6).y==folded.reactions[0].p.y,"linear layout can be restored");
    }
    {
        Document manual=mechanismDemo();auto& r=manual.reactions[0];
        auto end=reactionSlot(r,2);appendMechanismStage(r,true);appendMechanismStage(r);appendMechanismStage(r);
        auto lower=reactionSlot(r,3),left=reactionSlot(r,5);
        check(lower.x==end.x&&lower.y>end.y&&left.x<lower.x,"bottom plus drops a row and continues left");
        appendMechanismStage(r,true);appendMechanismStage(r);
        auto bottom=reactionSlot(r,6),right=reactionSlot(r,7);
        check(bottom.x==left.x&&bottom.y>left.y&&right.x>bottom.x,"next bottom plus reverses to the right");
        appendMechanismStage(r,true);appendMechanismStage(r);
        check(reactionSlot(r,9).x<reactionSlot(r,8).x,"unequal row lengths still alternate direction");
        for(size_t i=3;i<r.slots.size();i++)fillSlot(manual,demo(),r.id,i);
        optimizeMechanism(manual,{r.id});
        for(auto restored:{fromNative(toNative(manual)),fromCdxml(toCdxml(manual))}){
            for(size_t i=0;i<r.slots.size();i++)check((reactionSlot(restored.reactions[0],i)-reactionSlot(r,i)).length()<.01,"manual turns survive save and reopen");
        }
        auto edited=mechanismGridData(r,mechanismRows(r));
        check(edited.slots[3].startsRow&&edited.slots[6].startsRow&&edited.slots[8].startsRow,"table editing preserves manual turns");
        for(int kind:{0,1}){r.arrowType=kind;auto picture=makeScene(manual);int names=0;for(auto& item:picture.items)if(item.textRole==2)names++;check(names==int(r.slots.size()-1),"manual row turns retain all connector descriptions");}
        r.wrapColumns=3;for(auto& slot:r.slots)slot.startsRow=false;
        auto old=reactionSlot(r,9);appendMechanismStage(r,true);
        check((reactionSlot(r,9)-old).length()<.01&&reactionSlot(r,10).x==old.x&&reactionSlot(r,10).y>old.y,"bottom plus preserves existing automatic rows");
    }
    {
        Reaction plain;plain.above={"Intermediate 123456"};Reaction scripted=plain;scripted.above={"Intermediate _{123456}"};
        check(mechanismArrowLength(scripted,Style{})<mechanismArrowLength(plain,Style{}),"script markup is measured by its visible smaller characters");
        Document vertical;Reaction r;r.id=vertical.nextId++;r.vertical=true;
        r.slots={{"I_{1}",{},"0.0 / 8.8 / 8.8"},{"I_{2}",{},"-7.6 / -11.4 / -9.7"},{"I_{3}",{},"-23.1 / -30.4 / -40.2"}};
        r.above={"TS^{1} / Relaxation","TS^{2} / C–H activation"};r.below={"8.1 / 7.5 / -4.1","-10.1 / -18.6 / -21.3"};vertical.reactions.push_back(r);
        for(size_t i=0;i<3;i++)fillSlot(vertical,demo(),r.id,i);
        auto& layout=vertical.reactions[0];auto first=reactionSlot(layout,0),last=reactionSlot(layout,2);
        check(first.x==last.x&&last.y>first.y,"vertical mechanisms align structures from top to bottom");
        auto picture=makeScene(vertical);size_t arrows=0,scripts=0;
        for(auto& item:picture.items)if(item.owner==r.id){
            if(item.type==Primitive::Line){check(item.points.back().y>item.points.front().y,"vertical arrows point down");arrows++;}
            if(item.type==Primitive::Text){check(item.text.find("_{")==std::string::npos&&item.text.find("^{")==std::string::npos,"mechanism names render script runs");if(item.textRole==1&&item.text=="1")scripts++;}
        }
        check(arrows==2&&scripts==1,"vertical mechanism retains connectors and subscript names");
        for(auto restored:{fromNative(toNative(vertical)),fromCdxml(toCdxml(vertical))})check(restored.reactions[0].vertical&&(reactionSlot(restored.reactions[0],2)-last).length()<1e-5,"vertical geometry and names survive save/reopen");
        std::ofstream("mechanism-vertical.svg")<<toSvg(picture);
    }
    std::cout<<"Mechanism regression tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

#include "model.h"
#include "scene.h"
#include "plot_data.h"
#ifdef TEST_CDXML
#include "cdxml.h"
#include "lab_templates_data.h"
#endif
#include <iostream>
#include <fstream>
#include <stdexcept>
using namespace yd;
int checks=0;static void check(bool v,const char* name){checks++;if(!v)throw std::runtime_error(name);}
int main(){try{
 {auto chair=chairTemplate();double shortest=1e9,longest=0,area=0;int concave=0;
  for(size_t i=0;i<6;i++){Vec a=chair.atoms[i].p,b=chair.atoms[(i+1)%6].p;area+=a.x*b.y-a.y*b.x;}
  for(size_t i=0;i<6;i++){Vec edge=chair.atoms[(i+1)%6].p-chair.atoms[i].p,next=chair.atoms[(i+2)%6].p-chair.atoms[(i+1)%6].p;shortest=std::min(shortest,edge.length());longest=std::max(longest,edge.length());if((edge.x*next.y-edge.y*next.x)*area<0)concave++;
   Vec opposite=chair.atoms[(i+4)%6].p-chair.atoms[(i+3)%6].p;check(std::abs(edge.x*opposite.y-edge.y*opposite.x)<1e-7,"chair opposite bonds remain parallel in projection");}
  check(concave==2&&longest/shortest<1.5,"chair has two folds and balanced projected bond lengths");
 }
 {auto drawing=demo();for(auto& a:drawing.atoms)a.p=a.p*1.35;cleanStructure(drawing);
  for(size_t i=0;i<6;i++){auto& b=drawing.bonds[i];check(std::abs((drawing.atom(b.a)->p-drawing.atom(b.b)->p).length()-22.5)<1e-6,"cleanup reduces benzene to seventy-five percent");}
  auto& connector=drawing.bonds[6];check(std::abs((drawing.atom(connector.a)->p-drawing.atom(connector.b)->p).length()-25.5)<1e-6,"cleanup shortens aryl-carbonyl connector even with an adjacent double bond");
  auto again=drawing;cleanStructure(drawing);for(size_t i=0;i<drawing.atoms.size();i++)check((drawing.atoms[i].p-again.atoms[i].p).length()<1e-5,"compact benzoyl cleanup is stable on repetition");
 }
 {Document layout;std::vector<int> roots;std::vector<Vec> internal;
  for(Vec p:std::vector<Vec>{{0,0},{100,60},{250,-40}}){int a=layout.addAtom(p),b=layout.addAtom(p+Vec{30,0});layout.addBond(a,b);roots.push_back(a);internal.push_back(layout.atom(b)->p-layout.atom(a)->p);}
  auto centerOf=[&](int id){auto box=makeScene(layout.subset(layout.component(id))).bounds;return Vec{box.x+box.w/2,box.y+box.h/2};};
  check(alignObjects(layout,roots,AlignMode::MiddleY),"align separate molecules by their vertical centers");
  for(size_t i=0;i<roots.size();i++){check(std::abs(centerOf(roots[i]).y-centerOf(roots[0]).y)<1e-8,"all middle alignment centers coincide");auto c=layout.component(roots[i]);check((layout.atom(c[1])->p-layout.atom(c[0])->p-internal[i]).length()<1e-8,"alignment never tears a partially selected molecule");}
  double first=centerOf(roots[0]).x,last=centerOf(roots[2]).x;
  check(alignObjects(layout,roots,AlignMode::DistributeHorizontal),"distribute three molecules");
  check(std::abs(centerOf(roots[1]).x-(first+last)/2)<1e-8&&centerOf(roots[0]).x==first&&centerOf(roots[2]).x==last,"distribution preserves outer objects and equalizes gaps");
  check(!alignObjects(layout,{roots[0]},AlignMode::MiddleY),"alignment needs independent objects");
 }
 {Document lengths;int c=lengths.addAtom({0,0});
  check(std::abs(defaultBondLength(lengths,c,BondKind::Single,6)-30)<1e-9,"C-C single reference length");
  check(std::abs(defaultBondLength(lengths,c,BondKind::Double,6)-26.8)<1e-9,"C=C length follows double-bond radii");
  check(std::abs(defaultBondLength(lengths,c,BondKind::Double,8)-24.8)<1e-9,"C=O length follows carbon/oxygen radii");
  check(std::abs(defaultBondLength(lengths,c,BondKind::Single,8)-27.6)<1e-9,"C-O remains longer than C=O");
  int o=lengths.addAtom({0,30});int edge=lengths.addBond(c,o);setAtomElement(lengths,o,8);setBondKind(lengths,edge,BondKind::Double);
  check(std::abs(lengths.atom(o)->p.length()-24.8)<1e-9,"element and order edits shorten terminal carbonyl");
  setBondKind(lengths,edge,BondKind::Single);check(std::abs(lengths.atom(o)->p.length()-27.6)<1e-9,"single/double conversion is reversible");
  setBondKind(lengths,edge,BondKind::Double);cleanStructure(lengths);check(std::abs((lengths.atom(o)->p-lengths.atom(c)->p).length()-24.8)<1e-6,"2D cleanup shares drawing lengths");
  auto before=lengths;cleanStructure(lengths);check((before.atom(o)->p-lengths.atom(o)->p).length()<1e-6,"carbonyl cleanup is idempotent");
  check(bondLengthScale(26,8,BondKind::Double)==1,"metal coordination geometry keeps its convention");
  Document old;int a=old.addAtom({0,0}),b=old.addAtom({30,0});old.addBond(a,b,BondKind::Triple);old.bondGeometryVersion=0;
  check(!upgradeBondGeometry(old)&&(old.atom(a)->p-old.atom(b)->p).length()==30,"legacy ring migration never treats an alkyne as benzene");
 }
 for(auto kind:{OrbitalKind::S,OrbitalKind::P,OrbitalKind::D,OrbitalKind::Hybrid})for(int phase:{0,1,2}){
  Document orbitals;auto members=addOrbital(orbitals,{50,60},{50,90},kind,phase);
  check(members.size()==(kind==OrbitalKind::S?1:kind==OrbitalKind::D?4:2),"orbital lobe count");
  check(orbitals.groups.size()==1&&orbitals.groups[0].members==members,"orbital lobes form one selectable group");
  for(auto& o:orbitals.objects){check(o.closed,"orbital curves are closed");if(o.kind==ObjectKind::Curve)check(o.points.size()==7&&(o.points.front()-o.points.back()).length()<1e-9,"orbital vector curves close at the nucleus");}
  auto svg=toSvg(makeScene(orbitals));check(svg.find(kind==OrbitalKind::S?"<ellipse":"<path")!=std::string::npos,"orbital SVG uses vector geometry");
  if(phase==2)check(svg.find("#2878b5")!=std::string::npos&&(kind==OrbitalKind::S||svg.find("#d6534d")!=std::string::npos),"orbital phase colors survive SVG export");
  auto original=orbitals.objects;orbitals.translate({members.front()},{7,-3});for(size_t i=0;i<original.size();i++)check((orbitals.objects[i].points[0]-original[i].points[0]-Vec{7,-3}).length()<1e-9,"moving one lobe moves the whole orbital");
  auto copy=orbitals.subset({members.front()});check(copy.objects.size()==members.size()&&copy.groups.size()==1,"orbital copy preserves grouped lobes");
  scaleStructures(orbitals,members,2);check(std::abs((orbitals.objects[0].points[1]-orbitals.objects[0].points[0]).length()-2*(original[0].points[1]-original[0].points[0]).length())<1e-8,"orbital resize scales the vector path");
#ifdef TEST_CDXML
  for(auto restored:{fromNative(toNative(orbitals)),fromCdxml(toCdxml(orbitals))}){
   check(restored.objects.size()==members.size()&&restored.groups.size()==1,"orbital native and CDXML roundtrip preserves groups");
   for(size_t i=0;i<members.size();i++)check(restored.objects[i].filled==orbitals.objects[i].filled&&restored.objects[i].color==orbitals.objects[i].color&&restored.objects[i].closed,"orbital roundtrip preserves phase fills and outlines");
  }
  auto xml=toCdxml(orbitals);auto annotation=xml.find("<annotation");if(annotation!=std::string::npos){auto end=xml.find("/>",annotation);xml.erase(annotation,end-annotation+2);}
  auto plain=fromCdxml(xml);check(plain.objects.size()==members.size(),"plain CDXML exposes all orbital lobes to other editors");
  for(auto& o:plain.objects)check(o.kind==ObjectKind::Ellipse||o.closed,"plain CDXML preserves closed orbital paths");
  check(std::count_if(plain.objects.begin(),plain.objects.end(),[](const Object& o){return o.filled;})==std::count_if(orbitals.objects.begin(),orbitals.objects.end(),[](const Object& o){return o.filled;}),"plain CDXML preserves phase fills without application annotations");
#endif
 }
 {Document old;old.ring({0,0},6,true);for(auto& a:old.atoms)a.p=a.p*(1/benzeneBondScale);int root=old.atoms[0].id;int c=old.addAtom(old.atom(root)->p+Vec{30,0});int next=old.addAtom(old.atom(c)->p+Vec{30,0});int link=old.addBond(root,c);old.addBond(c,next);old.bondGeometryVersion=0;
  check(upgradeBondGeometry(old),"older benzene geometry is upgraded");
  for(size_t i=0;i<6;i++){auto& b=old.bonds[i];check(std::abs((old.atom(b.a)->p-old.atom(b.b)->p).length()-22.5)<1e-7,"all six old benzene bonds adopt seventy-five percent default");}
  check(std::abs((old.atom(root)->p-old.atom(c)->p).length()-25.5)<1e-7,"old benzyl connector becomes shorter");check(std::abs((old.atom(next)->p-old.atom(c)->p).length()-30)<1e-7,"migration preserves ordinary chain lengths");
  check(!upgradeBondGeometry(old),"reopening migrated geometry does not shrink it again");
  cleanStructure(old,{},true);check(std::abs((old.atom(old.bonds[0].a)->p-old.atom(old.bonds[0].b)->p).length()-22.5)<1e-6,"projection-preserving cleanup also retains compact benzene");
#ifdef TEST_CDXML
  auto saved=fromNative(toNative(old));check(saved.bondGeometryVersion==1&&!upgradeBondGeometry(saved),"geometry convention persists after save and reload");
#endif

  check(std::abs(defaultBondLength(old,root)-25.5)<1e-7&&std::abs(defaultBondLength(old,root,BondKind::Single,8)-27.6)<1e-7,"drawing retains compact benzyl connector and uses C-O radii");
  cleanStructure(old);for(size_t i=0;i<6;i++){auto& b=old.bonds[i];check(std::abs((old.atom(b.a)->p-old.atom(b.b)->p).length()-22.5)<1e-6,"cleanup preserves seventy-five percent benzene");}
  check(std::abs((old.atom(old.bond(link)->a)->p-old.atom(old.bond(link)->b)->p).length()-25.5)<1e-6,"cleanup preserves shorter benzyl connector");
 }
 {Document chelate;chelate.style.bondLength=18;chelate.ring({60,60},5);int co=chelate.atoms[0].id;chelate.atom(co)->element=27;chelate.atom(co)->label="LCo^{III}";for(int i:{1,2,4})chelate.atoms[i].element=8;int carbon=chelate.atoms[3].id;chelate.addBond(carbon,chelate.addAtom(chelate.atom(carbon)->p+Vec{15,0}));chelate.addBond(carbon,chelate.addAtom(chelate.atom(carbon)->p+Vec{0,15},1));
  auto report=cleanStructure(chelate);check(report.regularRings==1,"simple metal chelate keeps regular five-membered ring");double edge=0;
  for(size_t i=0;i<5;i++){auto& b=chelate.bonds[i];double length=(chelate.atom(b.a)->p-chelate.atom(b.b)->p).length();if(!i)edge=length;check(std::abs(length-edge)<1e-5,"label-aware cleanup uniformly scales the pentagon");}
  auto scene=makeScene(chelate);for(size_t i=0;i<5;i++){bool visible=false;for(auto& p:scene.items)if(p.owner==chelate.bonds[i].id&&p.type==Primitive::Line&&p.points.size()==2&&(p.points[1]-p.points[0]).length()>=7)visible=true;check(visible,"every pentagon bond remains visible beside oxidation labels");}
  std::vector<Vec> branches;for(auto& b:chelate.bonds)if(b.a==carbon||b.b==carbon)branches.push_back(chelate.atom(b.a==carbon?b.b:b.a)->p-chelate.atom(carbon)->p);for(size_t i=0;i<branches.size();i++)for(size_t j=0;j<i;j++){auto a=branches[i],b=branches[j];double dot=(a.x*b.x+a.y*b.y)/(a.length()*b.length());check(std::abs(dot)>.08,"tetrahedral carbon avoids artificial right-angle branches");}
 }

 {Document d;std::vector<int> ring;
  for(Vec p:std::vector<Vec>{{53,163},{100,109},{179,143},{178,230},{89,249}})ring.push_back(d.addAtom(p));
  d.atom(ring[0])->element=27;d.atom(ring[0])->label="LCo^{III}";for(int i:{1,2,4})d.atom(ring[i])->element=8;
  for(int i=0;i<5;i++)d.addBond(ring[i],ring[(i+1)%5]);
  int c=d.addAtom({262,188}),h=d.addAtom({213,298},1),m=d.addAtom({304,261});d.addBond(ring[3],c);d.addBond(ring[3],h);d.addBond(c,m);
  std::vector<int> aryl;for(Vec p:std::vector<Vec>{{337,151},{317,91},{362,46},{423,63},{439,122},{395,168}})aryl.push_back(d.addAtom(p));
  for(int i=0;i<6;i++)d.addBond(aryl[i],aryl[(i+1)%6],i%2?BondKind::Single:BondKind::Double);d.addBond(c,aryl[0]);
  auto colored=d;colored.atom(ring[1])->color="#ff0000";colored.atom(ring[2])->color="#0000ff";colored.atom(ring[4])->color="#ff0000";
  colored.bonds[0].color="#ff0000";int oh=colored.addAtom(colored.atom(ring[4])->p+Vec{-12,24},1);colored.atom(oh)->color="#ff0000";colored.addBond(ring[4],oh);
  // Reproduce the reported straight C-C-Ph run, with a methyl below it.
  colored.atom(c)->p=colored.atom(ring[3])->p+Vec{30,-8};Vec arylShift=colored.atom(c)->p+Vec{25.5,-6.8}-colored.atom(aryl[0])->p;
  for(int id:aryl)colored.atom(id)->p=colored.atom(id)->p+arylShift;
  auto coloredReport=cleanStructure(colored,{colored.bonds[0].id});
  auto cu=colored.atom(ring[3])->p-colored.atom(c)->p,cv=colored.atom(aryl[0])->p-colored.atom(c)->p;
  check(coloredReport.regularRings==1&&std::abs((cu.x*cv.x+cu.y*cv.y)/(cu.length()*cv.length())+.5)<1e-6,"colored chelate with OH restores the C-C-Ph zigzag when selected by bond");
  check(colored.atom(oh)->color=="#ff0000"&&colored.atom(ring[2])->color=="#0000ff"&&colored.bonds[0].color=="#ff0000","zigzag cleanup preserves atom and bond annotation colors");
  auto coloredStable=colored;cleanStructure(colored);for(auto& a:coloredStable.atoms)check((colored.atom(a.id)->p-a.p).length()<1e-5,"colored OH chelate zigzag is stable on repetition");
  std::ofstream("publication-chelate-colored.svg")<<toSvg(makeScene(colored));std::ofstream("publication-chelate-colored.ydraw")<<toNative(colored);
  auto protectedDrawing=d;protectedDrawing.atom(ring[0])->protect=true;auto protectedBefore=toNative(protectedDrawing);cleanStructure(protectedDrawing);check(toNative(protectedDrawing)==protectedBefore,"explicitly protected aryl chelate stays unchanged");
  auto report=cleanStructure(d);check(report.regularRings==1&&report.protectedComponents==0,"aryl substituent does not block planar chelate cleanup");
  double edge=(d.atom(ring[0])->p-d.atom(ring[1])->p).length();for(int i=0;i<5;i++)check(std::abs((d.atom(ring[i])->p-d.atom(ring[(i+1)%5])->p).length()-edge)<1e-6,"distorted chelate becomes a regular pentagon");
  for(int i=0;i<6;i++)check(std::abs((d.atom(aryl[i])->p-d.atom(aryl[(i+1)%6])->p).length()-22.5)<1e-6,"label clearance keeps pendant benzene compact");
  auto u=d.atom(ring[3])->p-d.atom(c)->p,v=d.atom(aryl[0])->p-d.atom(c)->p;check(std::abs((u.x*v.x+u.y*v.y)/(u.length()*v.length())+.5)<1e-6,"aryl side chain restores a 120 degree bend");
  auto stable=d;cleanStructure(d);for(auto& a:stable.atoms)check((d.atom(a.id)->p-a.p).length()<1e-5,"publication chelate cleanup is stable on repetition");
  std::ofstream("publication-chelate.svg")<<toSvg(makeScene(d));std::ofstream("publication-chelate.ydraw")<<toNative(d);
 }
 {Document d;int center=d.addAtom({80,76}),leftH=d.addAtom({52,76},1),o=d.addAtom({96,51},8),side=d.addAtom({126,49}),methyl=d.addAtom({141,22});
  int redO=d.addAtom({67,103},8),oh=d.addAtom({97,104},8),h=d.addAtom({82,130},1),co=d.addAtom({23,103});d.atom(co)->label="LCo^{III}";
  d.atom(o)->color="#0000ff";for(int id:{redO,oh,h})d.atom(id)->color="#ff0000";
  for(auto edge:std::vector<std::pair<int,int>>{{center,leftH},{center,o},{o,side},{side,methyl},{center,redO},{redO,co},{center,oh},{oh,h}})d.addBond(edge.first,edge.second);
  std::vector<int> aryl;for(Vec p:std::vector<Vec>{{140,71},{177,71},{195,100},{176,130},{140,130},{122,100}})aryl.push_back(d.addAtom(p));
  for(int i=0;i<6;i++)d.addBond(aryl[i],aryl[(i+1)%6],i%2?BondKind::Single:BondKind::Double);d.addBond(side,aryl[0]);
  auto original=d;auto report=cleanStructure(d);auto scene=makeScene(d);auto a=scene.objectBounds.at(co),b=scene.objectBounds.at(h);
  check(report.protectedComponents==1,"reject the reported open-chain rearrangement that stacks LCo and OH");
  check(std::min(a.x+a.w,b.x+b.w)<=std::max(a.x,b.x)||std::min(a.y+a.h,b.y+b.h)<=std::max(a.y,b.y),"open colored chain never overlaps LCo oxidation label with OH hydrogen");
  if(report.protectedComponents){for(auto& atom:original.atoms)check((d.atom(atom.id)->p-atom.p).length()<1e-8,"collision rejection restores the complete original projection");}
  auto stable=d;cleanStructure(d);for(auto& atom:stable.atoms)check((d.atom(atom.id)->p-atom.p).length()<1e-5,"collision-aware open-chain cleanup is stable");
  auto preserved=original;cleanStructure(preserved,{},true);Vec before=original.atom(oh)->p-original.atom(h)->p,after=preserved.atom(oh)->p-preserved.atom(h)->p;
  check(std::abs(before.x*after.y-before.y*after.x)<1e-6,"projection-preserving default retains OH direction");
  std::ofstream("publication-open-chain.svg")<<toSvg(makeScene(d));std::ofstream("publication-open-chain.ydraw")<<toNative(d);
 }
 {Document d;Object image;image.id=d.nextId++;image.kind=ObjectKind::Image;image.points={{10,10},{50,50}};d.objects.push_back(image);EnergyPlot graph;graph.id=d.nextId++;graph.p={100,100};graph.states={{"R",0}};graph.states[0].above={image.id};d.plots.push_back(graph);
  d.imagesLocked=true;check(d.isLocked(image.id)&&!d.isLocked(graph.id),"image and graph layer locks are independent");
  d.translate({graph.id},{5,5});check((d.objects[0].points[0]-Vec{10,10}).length()<1e-8&&(d.plots[0].p-Vec{105,105}).length()<1e-8,"dragging graph preserves its locked image member");
  d.graphsLocked=true;auto before=d;d.translate({graph.id,image.id},{20,20});check((d.plots[0].p-before.plots[0].p).length()<1e-8&&(d.objects[0].points[0]-before.objects[0].points[0]).length()<1e-8,"locked layers cannot move");
  d.erase({graph.id,image.id});check(d.plots.size()==1&&d.objects.size()==1&&d.editableIds().empty(),"locked layers resist deletion and select all");
#ifdef TEST_CDXML
  auto restored=fromNative(toNative(d));check(restored.imagesLocked&&restored.graphsLocked,"layer locks survive native save");restored=fromCdxml(toCdxml(d));check(restored.imagesLocked&&restored.graphsLocked,"layer locks survive CDXML save");
#endif
  d.graphsLocked=false;d.erase({graph.id});check(d.plots.empty()&&d.objects.size()==1,"deleting unlocked parent retains locked child image");
 }

 check(chemicalScripts("LCoIII LCuII")=="LCo^{III} LCu^{II}","ligand-prefixed oxidation labels are recognized");
 check(chemicalScripts("CoIII FeII MnIV CuI")=="Co^{III} Fe^{II} Mn^{IV} Cu^{I}","element oxidation numbers become superscripts");
 check(chemicalScripts("III VIII CIVIL Co^{III} Co_{2} CO2")=="III VIII CIVIL Co^{III} Co_{2} CO2","ordinary words, Roman numerals and explicit scripts remain unchanged");
 {Document d;int a=d.addAtom({0,0},27),b=d.addAtom({35,-18});d.atom(a)->label="CoIII";d.addBond(a,b);d.atom(a)->charge=1;d.atom(a)->radical=2;
  auto sc=makeScene(d);bool super=false,annotation=false;double normalY=0;
  for(auto& p:sc.items)if(p.owner==a&&p.type==Primitive::Text&&p.text=="Co")normalY=p.points[0].y;
  for(auto& p:sc.items)if(p.owner==a&&p.type==Primitive::Text){if(p.text=="III")super=p.fontSize<d.style.fontSize&&p.points[0].y<normalY;if(p.textRole==13)annotation=p.text.find('+')!=std::string::npos;}
  check(super&&annotation,"custom atom labels show oxidation superscript and radical ion together");
  d.atom(a)->annotationManual=true;d.atom(a)->annotationOffset={-20,15};sc=makeScene(d);
  for(auto& p:sc.items)if(p.textRole==13)check((p.points[0]-Vec{-20,15}).length()<1e-8,"annotation honors manual placement");
#ifdef TEST_CDXML
  auto restored=fromNative(toNative(d));check(restored.atom(a)->annotationManual&&(restored.atom(a)->annotationOffset-Vec{-20,15}).length()<1e-8,"manual annotation location survives save and load");
  check(toCdxml(d).find("face=\"65\"")!=std::string::npos,"oxidation number exports as CDXML superscript");
#endif
 }

 // Depth strokes use the same full-width convention across every molecule.
 {Document ink;int a=ink.addAtom({0,0}),b=ink.addAtom({30,0});int edge=ink.addBond(a,b,BondKind::Wedge);
  auto scene=makeScene(ink);auto wedge=scene.items.front();check(std::abs((wedge.points[1]-wedge.points[2]).length()-ink.style.boldWidth)<1e-8,"wedge broad end matches bold bond full width");
  ink.style.boldWidth=4;scene=makeScene(ink);check(std::abs((scene.items.front().points[1]-scene.items.front().points[2]).length()-4)<1e-8,"existing document wedge no longer doubles stored width");
  ink.bond(edge)->kind=BondKind::Hash;scene=makeScene(ink);double last=0;for(auto& p:scene.items){check(p.width<=ink.style.lineWidth*.6&&p.flatStart&&p.flatEnd,"hash strokes stay light with flat caps in screen and SVG");double width=(p.points[1]-p.points[0]).length();check(width>=last&&width<=ink.style.boldWidth,"hash envelope grows to the same width as solid wedge");last=width;}
  check(toSvg(scene).find("stroke-linecap=\"butt\"")!=std::string::npos,"hash flat caps survive SVG export");
  ink.atom(b)->p={3,0};scene=makeScene(ink);check(scene.items.size()==3,"short coordination hash stays readable without overlapping bars");for(auto& p:scene.items)check(p.width<=.5,"short coordination hash scales its ink to available length");
  ink.bond(edge)->kind=BondKind::Wedge;scene=makeScene(ink);check((scene.items.front().points[1]-scene.items.front().points[2]).length()<=1.05+1e-8,"short wedge cannot become an oversized triangle");
  ink.atom(b)->p={30,0};ink.bond(edge)->kind=BondKind::Double;ink.bond(edge)->customDisplay="Bold";scene=makeScene(ink);check(scene.items[0].width==4&&scene.items[1].width==2,"foreground double has a bold skeleton and normal secondary stroke");
  ink.bond(edge)->doubleSide=1;scene=makeScene(ink);check(scene.items[0].width==4&&scene.items[1].width==2,"offset bold double keeps the secondary stroke light");
  ink.ring({100,100},6,true);auto& ringEdge=ink.bonds[1];ringEdge.customDisplay="Bold";ringEdge.kind=BondKind::Double;scene=makeScene(ink);std::vector<double> widths;for(auto& p:scene.items)if(p.owner==ringEdge.id)widths.push_back(p.width);check(widths==std::vector<double>{4,2},"ring double keeps its inner stroke thin");
 }
 {EnergyPlot p;p.states={{"R",-9.9,{},{}},{"TS",37.2,{},{}}};
  auto axis=energyAxis(p);check(axis.step==20&&axis.low==-40&&axis.high==60,"automatic axis uses round aligned ticks instead of equal range fractions");
  p.yTickStep=5;axis=energyAxis(p);check(axis.step==5&&axis.low==-25&&axis.high==50&&axis.intervals==15,"fixed y interval expands bounds to its multiples");
  p.yTickStep=2.5;axis=energyAxis(p);check(axis.step==2.5&&energyTickLabel(2.5,axis.step)=="2.5"&&energyTickLabel(0,axis.step)=="0.0","fractional tick interval keeps required precision");
  check(energyTickLabel(-1e-12,.25)=="0.00"&&energyTickLabel(.125,.125)=="0.125"&&energyTickLabel(10,5)=="10","tick labels avoid negative zero and unnecessary decimals");
  p.series={{"extra","#000000",{-80,100},0,2}};axis=energyAxis(p);check(axis.low<-80&&axis.high>100,"all pathways are included in axis bounds");
  p.yTickStep=.000001;axis=energyAxis(p);check(axis.intervals<=200&&axis.step>p.yTickStep,"later data expansion cannot create unbounded ticks");
  p.series.clear();p.states[0].energy=p.states[1].energy=0;p.yTickStep=0;axis=energyAxis(p);check(axis.high>axis.low&&axis.step>0,"constant energy plot has finite nonzero axis range");
  p.states[0].energy=-.002;p.states[1].energy=.003;axis=energyAxis(p);check(axis.step<.01&&axis.intervals>1,"automatic small energy range retains subunit ticks");
#ifdef TEST_CDXML
  Document plotDoc;p.yTickStep=2.5;p.id=plotDoc.nextId++;plotDoc.plots.push_back(p);auto saved=toNative(plotDoc);check(fromNative(saved).plots[0].yTickStep==2.5&&fromCdxml(toCdxml(plotDoc)).plots[0].yTickStep==2.5,"y tick interval persists in native and smart CDXML");
  auto attr=saved.find(" yTickStep=\"2.5\"");check(attr!=std::string::npos,"native writes tick attribute");saved.erase(attr,std::string(" yTickStep=\"2.5\"").size());check(fromNative(saved).plots[0].yTickStep==0,"older plots default to automatic ticks");
  plotDoc.plots[0].states={{"R",0,{},{}},{"P",20,{},{}}};plotDoc.plots[0].yTickStep=5;auto scene=makeScene(plotDoc);bool five=false,ten=false;for(auto& item:scene.items)if(item.type==Primitive::Text){five|=item.text=="5";ten|=item.text=="10";}check(five&&ten,"scene renders manually specified five-unit ticks");
#endif
 }
 {Document d;d.ring({0,0},6);for(auto& b:d.bonds)check(std::abs((d.atom(b.a)->p-d.atom(b.b)->p).length()-d.style.bondLength)<1e-8,"ordinary hexagon keeps standard bond length");
  int anchor=d.addAtom({200,0});Vec before=d.atom(anchor)->p;size_t first=d.bonds.size();d.attachRing(anchor,0,6,true);check((before-d.atom(anchor)->p).length()<1e-8,"smaller benzene keeps attachment atom fixed");
  for(size_t j=first;j<d.bonds.size();j++){auto& b=d.bonds[j];check(std::abs((d.atom(b.a)->p-d.atom(b.b)->p).length()-d.style.bondLength*benzeneBondScale)<1e-8,"attached benzene uses smaller default");}
  int edge=d.bonds.front().id;double shared=(d.atom(d.bond(edge)->a)->p-d.atom(d.bond(edge)->b)->p).length();first=d.bonds.size();d.attachRing(0,edge,6,true);for(size_t j=first;j<d.bonds.size();j++){auto& b=d.bonds[j];check(std::abs((d.atom(b.a)->p-d.atom(b.b)->p).length()-shared)<1e-8,"fused benzene preserves existing shared-edge geometry");}
 }
 {Document d;int a=d.addAtom({0,0}),b=d.addAtom({20,0}),c=d.addAtom({40,0}),e=d.addAtom({60,0});d.addBond(a,b);d.addBond(b,c);d.addBond(c,e);d.groups.push_back({d.nextId++,{a,b,c,e}});
  auto ids=scaleStructures(d,{b,c},.5);check((d.atom(a)->p-Vec{0,0}).length()<1e-8&&(d.atom(e)->p-Vec{60,0}).length()<1e-8,"partial selection does not expand to grouped unselected atoms");
  check(std::abs(d.atom(b)->p.x-25)<1e-8&&std::abs(d.atom(c)->p.x-35)<1e-8,"partial atom selection scales around its own center");
  check(ids.size()==3,"partial resize retains only selected atoms and their internal bond");
 }
 Document d;d.ring({40,40},6,true);check(d.atoms.size()==6&&d.bonds.size()==6,"benzene topology");for(auto& b:d.bonds)check(std::abs((d.atom(b.a)->p-d.atom(b.b)->p).length()-d.style.bondLength*benzeneBondScale)<1e-8,"benzene defaults to seventy-five percent bond length");
 int first=d.atoms[0].id;d.atom(first)->element=7;check(d.hydrogens(first)==0,"pyridine N valence");History h;h.checkpoint(d);d.erase({first});check(d.bonds.size()==4,"incident bonds deleted");check(h.undo(d)&&d.atoms.size()==6,"undo topology");check(h.redo(d)&&d.atoms.size()==5,"redo deletion");
 check(std::abs(snapped({0,0},{12,3},14.4).length()-14.4)<1e-8,"snap length");
 {auto molecule=demo();auto original=molecule;int seed=molecule.bonds.front().id;auto ids=transformStructures(molecule,{seed},90);check(ids.size()==molecule.atoms.size()+molecule.bonds.size(),"a selected bond rotates its whole molecule");
  for(size_t i=0;i<molecule.atoms.size();i++)for(size_t j=0;j<i;j++)check(std::abs((molecule.atoms[i].p-molecule.atoms[j].p).length()-(original.atoms[i].p-original.atoms[j].p).length())<1e-7,"rotation preserves every internal distance");
  transformStructures(molecule,{seed},-90);for(size_t i=0;i<molecule.atoms.size();i++)check((molecule.atoms[i].p-original.atoms[i].p).length()<1e-7,"clockwise and counterclockwise are inverses");
  transformStructures(molecule,{seed},37.25);transformStructures(molecule,{seed},-37.25);for(size_t i=0;i<molecule.atoms.size();i++)check((molecule.atoms[i].p-original.atoms[i].p).length()<1e-7,"arbitrary rotations are reversible");
  molecule.bonds[0].kind=BondKind::Wedge;molecule.bonds[1].kind=BondKind::Double;molecule.bonds[1].doubleSide=1;molecule.bonds[0].color="#a2a9b2";molecule.atoms[0].protect=true;original=molecule;
  transformStructures(molecule,{seed},0,true);check(molecule.bonds[0].kind==BondKind::Hash&&molecule.bonds[1].doubleSide==-1,"reflection compensates stereobonds and offset second strokes");check(molecule.bonds[0].color=="#a2a9b2"&&molecule.atoms[0].protect,"reflection preserves rear color and projection protection");transformStructures(molecule,{seed},0,true);for(size_t i=0;i<molecule.atoms.size();i++)check((molecule.atoms[i].p-original.atoms[i].p).length()<1e-7,"double reflection restores coordinates");check(molecule.bonds[0].kind==BondKind::Wedge,"double reflection restores stereobond");
  molecule.append(demo(),{400,0});auto other=molecule.atoms.back().p;transformStructures(molecule,{seed},0,false,true);check((molecule.atoms.back().p-other).length()<1e-7,"unselected molecule stays fixed");
  History history;history.checkpoint(molecule);auto before=molecule;transformStructures(molecule,{seed},90);check(history.undo(molecule)&&molecule.atoms.front().p.x==before.atoms.front().p.x,"rotation supports undo");
  Document annotation;Object text;text.id=annotation.nextId++;text.kind=ObjectKind::Text;text.points={{10,20}};text.text="Name";annotation.objects.push_back(text);Object arrow;arrow.id=annotation.nextId++;arrow.head=1;arrow.points={{0,0},{40,0}};annotation.objects.push_back(arrow);transformStructures(annotation,annotation.allIds(),90);check(annotation.objects[0].text=="Name"&&std::abs(annotation.objects[1].points[0].x-annotation.objects[1].points[1].x)<1e-7,"annotations follow rotation while text stays upright");
 }
 {auto molecule=demo();auto original=molecule;int seed=molecule.bonds.front().id;int a=molecule.bond(seed)->a,b=molecule.bond(seed)->b;
  auto affected=scaleStructures(molecule,{seed},1.5);check(affected.size()==3,"resizing a selected bond affects only its two endpoints and itself");
  check(std::abs((molecule.atom(a)->p-molecule.atom(b)->p).length()-1.5*(original.atom(a)->p-original.atom(b)->p).length())<1e-7,"selected bond scales by requested factor");
  for(auto& atom:original.atoms)if(atom.id!=a&&atom.id!=b)check((atom.p-molecule.atom(atom.id)->p).length()<1e-8,"unselected atoms in the same molecule stay fixed");
  scaleStructures(molecule,{seed},1/1.5);for(size_t i=0;i<molecule.atoms.size();i++)check((molecule.atoms[i].p-original.atoms[i].p).length()<1e-7,"inverse partial resize restores coordinates");
  molecule.append(demo(),{400,0});auto other=molecule.atoms.back().p;scaleStructures(molecule,{seed},.5);check((molecule.atoms.back().p-other).length()<1e-7,"unselected structures do not change size or position");
  auto before=molecule;bool failed=false;try{scaleStructures(molecule,{seed},0);}catch(...){failed=true;}check(failed&&(molecule.atoms.front().p-before.atoms.front().p).length()<1e-7,"invalid resize factor leaves coordinates unchanged");
  History history;history.checkpoint(molecule);scaleStructures(molecule,{seed},2);check(history.undo(molecule)&&(molecule.atoms.front().p-before.atoms.front().p).length()<1e-7,"resize supports undo");
  Document image;Object picture;picture.id=image.nextId++;picture.kind=ObjectKind::Image;picture.points={{10,20},{110,70}};picture.imageHex="1234";image.objects.push_back(picture);scaleStructures(image,{picture.id},2);
  check(image.objects[0].points[0].x==-40&&image.objects[0].points[1].x==160&&image.objects[0].points[0].y==-5&&image.objects[0].points[1].y==95&&image.objects[0].imageHex=="1234","image resize preserves center, aspect ratio and original bytes");
 }
 {Document centered;Object label;label.id=centered.nextId++;label.kind=ObjectKind::Text;label.points={{100,20}};label.fontSize=14;label.text="Long label\nx";centered.objects.push_back(label);auto visual=makeScene(centered);int rows=0;for(auto& item:visual.items)if(item.owner==label.id&&item.type==Primitive::Text){size_t chars=item.text.size();double width=chars*item.fontSize*.62;check(std::abs(item.points[0].x+width/2-100)<.001,"each caption row keeps the same center");rows++;}check(rows==2,"multiline centered caption has two aligned rows");
#ifdef TEST_CDXML
 auto saved=fromNative(toNative(centered));check(saved.objects[0].centerText,"centered text alignment persists");
#endif
 }
#ifdef TEST_CDXML
 {int ligands=0,complexes=0,total=0;for(const auto& entry:labTemplates){total++;auto drawing=fromNative(entry.data);check(!drawing.atoms.empty(),"bundled template has editable atoms");std::string category=entry.category;
     if(category=="리간드")ligands++;
     bool complex=category.find("착물")!=std::string::npos;
     if(complex){complexes++;auto before=toNative(drawing);auto report=cleanStructure(drawing);check(report.protectedComponents>0&&before==toNative(drawing),"cleanup preserves every projected coordination template exactly");
       Document subset;subset.append(drawing,{90,30});auto copy=subset;cleanStructure(subset,{subset.bonds.front().id});check(toNative(subset)==toNative(copy),"protection survives insertion and selection by bond");}
     auto restored=fromNative(toNative(drawing));check(restored.atoms.size()==drawing.atoms.size()&&restored.bonds.size()==drawing.bonds.size(),"all template topologies survive native save");
     auto external=toCdxml(drawing);auto begin=external.rfind("<annotation");if(begin!=std::string::npos)external.erase(begin,external.find("/>",begin)-begin+2);auto roundtrip=fromCdxml(external);check(roundtrip.atoms.size()==drawing.atoms.size()&&roundtrip.bonds.size()==drawing.bonds.size(),"ordinary CDXML preserves template topology");
     for(size_t i=0;i<drawing.bonds.size();i++)check(restored.bonds[i].color==drawing.bonds[i].color,"rear bond color survives native save");
     int gray=0,externalGray=0;for(auto& bond:drawing.bonds)if(bond.color!="#000000")gray++;for(auto& bond:roundtrip.bonds)if(bond.color!="#000000")externalGray++;check(gray==externalGray,"ordinary CDXML preserves gray rear bonds");
   }check(ligands==7&&complexes==127&&total==152,"complete categorized lab template library with 56 M-NOx variants");
   Document distorted;int metal=distorted.addAtom({0,0},27);int n1=distorted.addAtom({11,17},7),n2=distorted.addAtom({-28,5},7);distorted.addBond(metal,n1);distorted.addBond(metal,n2);auto before=toNative(distorted);cleanStructure(distorted);check(toNative(distorted)==before,"plain metal bonds trigger protection without wedge or explicit flags");
 }
#endif
 {Document allyl;int left=allyl.addAtom({-26,15}),center=allyl.addAtom({0,0}),right=allyl.addAtom({26,15});int first=allyl.addBond(left,center),second=allyl.addBond(right,center);check(delocalizeBonds(allyl,{left,center,right})==2,"three selected carbons convert both bonds");auto visual=makeScene(allyl);int solid=0,dashed=0;for(auto& stroke:visual.items)if(stroke.owner==first||stroke.owner==second){if(stroke.dash){dashed++;check(stroke.points[0].y>0&&stroke.points[1].y>0,"partial strokes face inside three-carbon fragment");}else solid++;}check(solid==2&&dashed==2,"each delocalized bond has one solid and one dashed stroke");auto before=visual;std::swap(allyl.bonds[0].a,allyl.bonds[0].b);auto reversed=makeScene(allyl);for(size_t i=0;i<before.items.size();i++)if(before.items[i].owner==first){auto& a=before.items[i];auto& b=reversed.items[i];check((a.points.front()-b.points.back()).length()<.001,"reversing endpoints preserves dashed side");}allyl.bonds[0].doubleSide=-1;check(makeScene(allyl).items.size()==visual.items.size(),"manual dashed side remains editable");
#ifdef TEST_CDXML
 for(auto restored:{fromNative(toNative(allyl)),fromCdxml(toCdxml(allyl))})check(restored.bonds[0].kind==BondKind::Aromatic&&restored.bonds[0].doubleSide==-1&&restored.bonds[1].kind==BondKind::Aromatic,"partial bonds and side survive save");check(toCdxml(allyl).find("Order=\"1.5\"")!=std::string::npos&&toSvg(visual).find("stroke-dasharray")!=std::string::npos,"partial bonds export as 1.5 bonds and dashed vector strokes");
#endif
 Document aromaticRing;aromaticRing.ring({0,0},6);delocalizeBonds(aromaticRing,aromaticRing.allIds());auto ringScene=makeScene(aromaticRing);int inner=0;for(auto& stroke:ringScene.items)if(stroke.dash){inner++;check(((stroke.points[0]+stroke.points[1])*.5).length()<26,"ring partial strokes lie inside ring");}check(inner==6,"all ring partial strokes rendered");}
 { // Exact geometry rather than a small visual nudge of distorted coordinates.
 auto bondLengths=[](const Document& molecule,bool compact=false){for(auto& edge:molecule.bonds)if(std::abs((molecule.atom(edge.a)->p-molecule.atom(edge.b)->p).length()-molecule.style.bondLength*(compact?benzeneBondScale:bondLengthScale(molecule.atom(edge.a)->element,molecule.atom(edge.b)->element,edge.kind)))>.01)return false;return true;};
 auto cosine=[](Vec a,Vec b){return (a.x*b.x+a.y*b.y)/(a.length()*b.length());};
 Document chain;std::vector<int> chainIds;for(int i=0;i<7;i++){chainIds.push_back(chain.addAtom({double(i*13),double(i%2*3)}));if(i)chain.addBond(chainIds[i-1],chainIds[i]);}cleanStructure(chain);check(bondLengths(chain),"chain cleanup gives exact uniform lengths");for(int i=1;i<6;i++)check(std::abs(cosine(chain.atom(chainIds[i-1])->p-chain.atom(chainIds[i])->p,chain.atom(chainIds[i+1])->p-chain.atom(chainIds[i])->p)+.5)<.0001,"chain cleanup makes 120 degree zigzags");auto chainBefore=chain;cleanStructure(chain);for(size_t i=0;i<chain.atoms.size();i++)check((chain.atoms[i].p-chainBefore.atoms[i].p).length()<.01,"cleanup is stable on repeated use");
 Document alkyne;int ca=alkyne.addAtom({0,0}),cb=alkyne.addAtom({13,4}),cc=alkyne.addAtom({22,17}),cd=alkyne.addAtom({39,18});alkyne.addBond(ca,cb);alkyne.addBond(cb,cc,BondKind::Triple);alkyne.addBond(cc,cd);cleanStructure(alkyne);check(bondLengths(alkyne),"alkyne lengths");check(cosine(alkyne.atom(ca)->p-alkyne.atom(cb)->p,alkyne.atom(cc)->p-alkyne.atom(cb)->p)<-.9999&&cosine(alkyne.atom(cb)->p-alkyne.atom(cc)->p,alkyne.atom(cd)->p-alkyne.atom(cc)->p)<-.9999,"triple bond centers are linear");
 Document branched;int branch=branched.addAtom({0,0});for(Vec p:std::vector<Vec>{{10,1},{8,5},{-5,-10}})branched.addBond(branch,branched.addAtom(p));cleanStructure(branched);check(bondLengths(branched),"branch bond lengths");for(size_t i=1;i<branched.atoms.size();i++)for(size_t j=i+1;j<branched.atoms.size();j++)check(std::abs(cosine(branched.atoms[i].p-branched.atom(branch)->p,branched.atoms[j].p-branched.atom(branch)->p)+.5)<.0001,"three branches use trigonal layout");
 Document fused;fused.ring({0,0},6,true);fused.attachRing(0,fused.bonds[0].id,6,true);for(size_t i=0;i<fused.atoms.size();i++)fused.atoms[i].p=fused.atoms[i].p+Vec{double(int(i%3)-1)*1.5,double(i%2)*2};cleanStructure(fused);check(bondLengths(fused,true),"fused rings share an exact edge after cleanup");
 auto selectedChain=chainBefore;for(auto& atom:selectedChain.atoms)atom.p=atom.p*1.7;cleanStructure(selectedChain,{selectedChain.bonds[0].id});check(bondLengths(selectedChain),"selecting a bond cleans its molecule");
 Document drawing;int da=drawing.addAtom({0,0}),db=drawing.addAtom({30,0});drawing.addBond(da,db);Vec newBond=bondEndpoint(drawing,db,{45,7});check(std::abs(cosine(drawing.atom(da)->p-drawing.atom(db)->p,newBond-drawing.atom(db)->p)+.5)<.0001,"drawing snaps to 120 degrees at existing bond");newBond=bondEndpoint(drawing,db,{45,7},BondKind::Triple);check(cosine(drawing.atom(da)->p-drawing.atom(db)->p,newBond-drawing.atom(db)->p)<-.9999,"drawing a triple bond extends linearly");
 Object resonance;resonance.id=91;resonance.head=3;resonance.points={{0,0},{60,0}};auto resonanceParts=arrowPrimitives(resonance);check(resonanceParts.size()==3&&resonanceParts[1].points[0].x==0&&resonanceParts[2].points[0].x==60,"resonance has full heads at both ends");check(resonanceParts[0].points[0].x>0&&resonanceParts[0].points[1].x<60,"resonance shaft stops at both arrowheads"); }
 d={};d.ring({40,40},6);int edge=d.bonds[0].id;d.attachRing(0,edge,6,true);check(d.atoms.size()==10&&d.bonds.size()==11,"fused hexagon topology");
 d={};d.ring({40,40},6,true);d.atoms[0].p.x+=3;d.atoms[2].p.y+=2;auto report=cleanStructure(d);check(report.regularRings==1,"detect planar ring");for(auto& b:d.bonds)check(std::abs((d.atom(b.a)->p-d.atom(b.b)->p).length()-d.style.bondLength*benzeneBondScale)<.01,"cleanup retains compact benzene");
 d=chairTemplate();auto a=d.atoms[0].p,b=d.atoms[1].p,c=d.atoms[2].p;double sign=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);report=cleanStructure(d);a=d.atoms[0].p;b=d.atoms[1].p;c=d.atoms[2].p;check(report.protectedComponents==1&&sign*((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))>0,"preserve perspective/chirality");
 d=demo();auto fragment=d.subset(d.component(d.atoms.front().id));size_t count=d.atoms.size();d.append(fragment,{100,0});check(d.atoms.size()==count*2,"template insertion topology");std::set<int> ids;for(int id:d.allIds())check(ids.insert(id).second,"fresh ids on insert");
 d=mechanismDemo();check(d.reactions.size()==1&&d.reactions[0].slots[0].members.size()>0,"reaction filled slots");int rid=d.reactions[0].id;int member=d.reactions[0].slots[0].members.front();Vec old=d.atom(member)->p;d.translate({rid},{20,10});check((d.atom(member)->p-old-Vec{20,10}).length()<1e-8,"block moves attached structures");
 auto sc=makeScene(d);check(sc.objectBounds.count(rid)>0,"reaction hit bounds");auto svg=toSvg(sc);check(svg.find("Nucleophilic")!=std::string::npos&&svg.find("attack")!=std::string::npos,"reaction caption exported");check(svg.find("Reactant")!=std::string::npos&&svg.find("Intermediate")!=std::string::npos&&svg.find("Product")!=std::string::npos&&svg.find("cleavage")!=std::string::npos,"publication mechanism uses English labels and captions");
 d=coordinateDemo();auto& p=d.plots.front();check(p.mode==3,"new energy plots default to flat levels");check(EnergySeries{}.color=="#000000"&&EnergySeries{}.dash==1,"new energy pathways default to black dashed connectors");check(p.series.front().color=="#000000"&&p.series.front().dash==1,"energy demo uses the default black dashed style");p.mode=0;p.series.push_back({"Path B","#aa1122",{0,20,10,22,-8},1,1.5});sc=makeScene(d);svg=toSvg(sc);check(svg.find("stroke-dasharray")!=std::string::npos&&svg.find("#aa1122")!=std::string::npos,"colored/dashed pathway SVG");check(svg.find(" C ")!=std::string::npos,"smooth curve SVG");p.mode=2;check(toSvg(makeScene(d)).find("stroke-dasharray")!=std::string::npos,"energy levels connectors");
 // Level plateaus stay solid even when either connector style is dashed/dotted.
 for(int mode:{2,3})for(int dash:{1,2}){auto levels=coordinateDemo();auto& graph=levels.plots[0];graph.mode=mode;graph.series[0].dash=dash;auto drawing=makeScene(levels);int plateaus=0;for(auto& item:drawing.items)if(item.owner==graph.id&&item.type==Primitive::Line&&item.points.size()==2&&item.textRole!=7&&item.color==graph.series[0].color&&item.points[0].y>=graph.p.y&&item.points[1].x>item.points[0].x&&std::abs(item.points[1].y-item.points[0].y)<1e-6){check(item.dash==0,"energy plateau must be solid");plateaus++;}check(plateaus==int(graph.states.size()),"each state has a solid plateau");}
 // The house preset and graph rendering share the same bold typography.
 check(Style{}.lineWidth==2&&Style{}.fontSize==14&&Style{}.labelFace==1,"Arial Bold / 2 pt preset");
 auto styled=demo();for(size_t i=0;i<styled.bonds.size();i++){auto& demoEdge=styled.bonds[i];check(std::abs((styled.atom(demoEdge.a)->p-styled.atom(demoEdge.b)->p).length()-30*(i<6?benzeneBondScale:i==6?.85:bondLengthScale(styled.atom(demoEdge.a)->element,styled.atom(demoEdge.b)->element,demoEdge.kind)))<.0001,"default demo uses compact benzene and standard substituent bonds");}
 auto boldScene=makeScene(styled);for(auto& item:boldScene.items)if(item.type==Primitive::Text)check((item.face&1)!=0&&item.font=="Arial","bold Arial chemical labels");
 Object caption;caption.id=styled.nextId++;caption.kind=ObjectKind::Text;caption.points={{0,0}};caption.text="First line\nSecond line";styled.objects.push_back(caption);
 auto captions=makeScene(styled);check(captions.objectBounds[caption.id].h>caption.fontSize*2,"multiline text hit bounds");
 auto rows=parseEnergyTable("State, Energy, Marker\nR, 0, circle\n\"TS, one\", 25.5, diamond\nP, -12, none\n");
 check(rows.size()==3&&rows[1].label=="TS, one"&&rows[1].marker==3,"CSV energy table with quoted names and point markers");
 EnergyPlot table;setEnergyData(table,rows);check(table.series[0].values[1]==25.5&&table.states[2].marker==0,"data drives geometry and symbols");
 check(parseEnergyTable(energyTable(table))[1].label=="TS, one","table text roundtrip");
 {auto edited=table;auto renamed=rows;renamed[1].label="Renamed TS";renamed[1].marker=-1;setEnergyData(edited,renamed,true);check(edited.states[1].marker==3,"grid rename preserves existing state marker");}

 auto badTable=[](std::string text){try{parseEnergyTable(text);return false;}catch(...){return true;}};
 check(badTable("R,0\nTS,nan"),"reject non-finite energy");check(badTable("R,0\nTS,20,unknown"),"reject unknown marker");check(badTable("R,0"),"reject incomplete table");
 table.states[1].above={123};table.series.push_back({"B","#000000",{0,22,-10},1,2});
 setEnergyData(table,parseEnergyTable("P,-15,triangle\nNew,5,square\n\"TS, one\",28,diamond\nR,1,circle"));
 check(table.states[2].above==std::vector<int>{123}&&table.states[1].above.empty(),"state insertion/reordering preserves the right structure attachment");
 check(table.series[1].values.size()==4&&table.series[1].values[2]==22,"other pathways follow reordered states");
 Document points;EnergyPlot pp=table;pp.id=points.nextId++;pp.p={0,0};pp.mode=3;points.plots.push_back(pp);
 auto rigid=makeScene(points);int symbols=0,curves=0;for(auto& shape:rigid.items){if(shape.filled&&shape.textRole!=7)symbols++;if(shape.type==Primitive::Bezier)curves++;}check(symbols==8&&curves==0,"rigid profile and per-state symbols");
 points.plots[0].states[2].above.clear();auto rigidSvg=toSvg(rigid);check(rigidSvg.find("font-weight=\"bold\"")!=std::string::npos,"graph typography exports bold");
#ifdef TEST_CDXML
 Document resonanceDoc;Object resonanceObject;resonanceObject.id=resonanceDoc.nextId++;resonanceObject.head=3;resonanceObject.points={{0,0},{90,0}};resonanceDoc.objects.push_back(resonanceObject);for(auto restored:{fromNative(toNative(resonanceDoc)),fromCdxml(toCdxml(resonanceDoc))})check(restored.objects[0].head==3,"resonance arrow survives native and CDXML save");check(toSvg(makeScene(resonanceDoc)).find("polygon")!=std::string::npos,"resonance arrow exports as vector geometry");
 auto pointCopy=fromNative(toNative(points));check(pointCopy.plots[0].mode==3&&pointCopy.plots[0].states[0].marker==4&&pointCopy.plots[0].series[0].marker==1,"marker/native roundtrip");
 auto native=toNative(d);auto loaded=fromNative(native);check(loaded.plots.size()==1&&loaded.plots[0].series.size()==2&&loaded.plots[0].states[0].above.size()>0,"native graph roundtrip");
 auto cdxml=toCdxml(d);loaded=fromCdxml(cdxml);check(loaded.plots.size()==1&&loaded.plots[0].series[1].color=="#aa1122","smart CDXML roundtrip");
 auto strip=[](std::string s){auto p=s.rfind("<annotation");if(p!=std::string::npos)s.erase(p,s.find("/>",p)-p+2);return s;};
 auto ordinaryMarkers=fromCdxml(strip(toCdxml(points)));bool hasFilled=false;for(auto& o:ordinaryMarkers.objects)hasFilled=hasFilled||o.filled;check(hasFilled,"point markers retain fill in ordinary CDXML");
 auto external=fromCdxml(strip(cdxml));check(external.plots.empty()&&!external.objects.empty(),"ordinary CDXML export independent of metadata");
 d=demo();int a2=d.addAtom({130,100}),b2=d.addAtom({144.4,100});d.addBond(a2,b2,BondKind::Wedge);auto xml=strip(toCdxml(d));loaded=fromCdxml(xml);check(loaded.atoms.size()==d.atoms.size()&&loaded.bonds.size()==d.bonds.size(),"standard CDXML atom/bond roundtrip");check(loaded.bonds.back().kind==BondKind::Wedge,"standard CDXML stereobond");
 std::string fixture="<CDXML BondLength='30' LabelSize='14'><page id='1'><group id='2'><fragment id='3'><n id='4' p='20 20' Element='7' Charge='1' Isotope='15' NumHydrogens='4'><t p='20 24'><s font='3' size='14'>NH4+</s></t></n></fragment><t id='5' p='50 50'><s font='3' size='12' color='0'>attack &amp; release</s></t><graphic id='6' GraphicType='UnknownFutureShape' BoundingBox='0 0 5 5' Future='retained'/></group><scheme id='7'><step id='8' ReactionStepReactants='3'/></scheme></page></CDXML>";
 loaded=fromCdxml(fixture);check(loaded.atoms[0].charge==1&&loaded.atoms[0].isotope==15&&loaded.atoms[0].explicitH==4,"import isotope/charge/H");check(loaded.objects.size()==1&&loaded.groups.size()==1,"import text/groups");check(loaded.style.bondLength==30,"retain source style");check(!loaded.importWarnings.empty(),"report unsupported visual");loaded.atom(4)->p={25,30};auto saved=toCdxml(loaded);check(saved.find("Future=\"retained\"")!=std::string::npos&&saved.find("ReactionStepReactants=\"3\"")!=std::string::npos,"retain unknown objects and reaction references");check(fromCdxml(strip(saved)).atom(4)->p.x==25,"patch imported coordinates");
 auto stale=toCdxml(d);auto at=stale.find("p=\"");stale.insert(at+3,"1");loaded=fromCdxml(stale);check(!loaded.importWarnings.empty(),"external modification invalidates stale smart data");
 auto rejects=[](const std::string& s){try{fromCdxml(s);return false;}catch(...){return true;}};
 check(!rejects("<!DOCTYPE CDXML SYSTEM 'https://static.chemistry.revvitycloud.com/cdxml/CDXML.dtd'><CDXML><page id='1'/></CDXML>"),"ChemDraw 26 external DTD declaration accepted without fetching");
 auto radical=fromCdxml("<CDXML><page id='1'><fragment id='2'><n id='3' p='20 20' Element='8' Radical='Doublet'/><n id='4' p='40 20'/><b id='5' B='3' E='4' Order='2.5'/></fragment></page></CDXML>");
 check(radical.atom(3)->radical==2&&toCdxml(radical).find("Order=\"2.5\"")!=std::string::npos,"retain named radicals and unusual bond orders");
 radical.addBond(4,radical.addAtom({54.4,20}));auto extension=strip(toCdxml(radical));check(fromCdxml(extension).atoms.size()==3,"extend an imported fragment");
 auto grouped=fromCdxml(fixture);grouped.groups.clear();check(strip(toCdxml(grouped)).find("<group id=\"2\"")==std::string::npos,"ungroup imported source objects");
 Document groupedNew=demo();groupedNew.groups.push_back({groupedNew.nextId++,groupedNew.allIds()});check(fromCdxml(strip(toCdxml(groupedNew))).groups.size()==1,"native grouping exported to ordinary CDXML");
 auto warningCopy=fromNative(toNative(radical));check(!warningCopy.importWarnings.empty(),"compatibility warnings survive native save");
 check(rejects("<CDXML><page id='1'><fragment id='2'><n id='3' p='0 0'/><b id='4' B='3' E='99'/></fragment></page></CDXML>"),"dangling references rejected");
 check(rejects("<CDXML><page id='1'><fragment id='2'><n id='3' p='0 0'/><n id='3' p='1 1'/></fragment></page></CDXML>"),"duplicate id rejected");check(rejects("<CDXML><page>"),"malformed XML rejected");check(rejects("<!DOCTYPE CDXML [<!ENTITY x SYSTEM 'file:///C:/Windows/win.ini'>]><CDXML><page id='1'>&x;</page></CDXML>"),"external entities prohibited");
#endif
 Document cycle=mechanismDemo();auto originalAtoms=cycle.atoms.size();int cycleId=cycle.reactions[0].id;optimizeMechanism(cycle,{cycleId},true);check(cycle.reactions[0].cyclic&&cycle.atoms.size()==originalAtoms,"cyclic layout retains structures");auto cycleScene=makeScene(cycle);int cycleArrows=0;for(auto& p:cycleScene.items)if(p.owner==cycleId&&p.type==Primitive::Polygon)cycleArrows++;check(cycleArrows==3,"catalytic cycle closes final arrow");
 check(cycle.reactions[0].above.size()==3&&cycle.reactions[0].below.size()==3&&cycle.reactions[0].above.back()=="Name"&&cycle.reactions[0].below.back()=="0.0","closing arrow has editable name and energy");
 check(cycle.reactions[0].slots[0].label=="Reactant"&&cycle.reactions[0].above[0]=="Nucleophilic attack"&&cycle.reactions[0].below[0]=="37.2","cycle conversion preserves existing captions");
 int energies=0,centers=0;for(auto& item:cycleScene.items)if(item.owner==cycleId){if(item.textRole==9){energies++;check(item.text=="0.0","structure energy placeholder");auto anchor=reactionSlot(cycle.reactions[0],size_t(item.textIndex));check(item.points[0].y>anchor.y+cycle.reactions[0].slotHeight/2+20,"energy sits below structure name");}if(item.textRole==10){centers++;check(item.text=="Cycle"&&item.textIndex==-1,"cycle center is editable text");}}
 check(energies==3&&centers==1,"all structures and cycle center have editable fields");
 cycle.reactions[0].slots[0].energy="-12.5";cycle.reactions[0].centerLabel="Catalytic cycle";optimizeMechanism(cycle,{cycleId},true);check(cycle.reactions[0].slots[0].energy=="-12.5"&&cycle.reactions[0].centerLabel=="Catalytic cycle","rearranging preserves edited energy and center");

 // Exercise ring spacing at different stage counts and explicit caption sizes.
 for(int count:{2,3,5,8,30})for(double font:{14.,28.}){
     Document ring;Reaction r;r.id=ring.nextId++;r.cyclic=true;r.centerLabel="Cycle";r.captionFontSize=font;
     r.slots.resize(count);r.above.resize(count,"Transition / Longer description");r.below.resize(count,"0.0");
     for(auto& slot:r.slots){slot.label="Intermediate\nName";slot.energy="-12.5";}
     ring.reactions.push_back(r);optimizeMechanism(ring,{r.id},true);auto picture=makeScene(ring);
     std::vector<Rect> captions;
     for(auto& item:picture.items)if(item.type==Primitive::Text&&!item.text.empty()){
         check(std::abs(item.fontSize-font)<.01,"all cycle captions including center use the same size");
         Rect box{item.points[0].x,item.points[0].y,item.text.size()*font*.62,font};
         for(auto& other:captions)check(box.x+box.w<=other.x||other.x+other.w<=box.x||box.y+box.h<=other.y||other.y+other.h<=box.y,"cycle captions never overlap");
         captions.push_back(box);
     }
     ring.reactions[0].hiddenArrows={0,count-1};auto opened=makeScene(ring);int heads=0;
     for(auto& item:opened.items)if(item.reactionArrow>=0){check(item.reactionArrow!=0&&item.reactionArrow!=count-1,"hidden connectors have no shaft or head");if(item.type==Primitive::Polygon)heads++;}
     check(heads==count-2,"individual cycle connectors can be omitted");
#ifdef TEST_CDXML
     for(auto restored:{fromNative(toNative(ring)),fromCdxml(toCdxml(ring))}){
         check(restored.reactions[0].hiddenArrows==ring.reactions[0].hiddenArrows&&restored.reactions[0].captionFontSize==font,"deleted arrows and uniform font survive saving");
     }
#endif
 }
 Document freeCycle=demo();freeCycle.append(demo(),{200,60});optimizeMechanism(freeCycle,{},true);for(auto& slot:freeCycle.reactions[0].slots)check(slot.label=="Name"&&slot.energy=="0.0","free structures get name and energy placeholders");
 Document free=demo();free.append(demo(),{200,60});Object name;name.id=free.nextId++;name.kind=ObjectKind::Text;name.points={{0,100}};name.text="촉매";free.objects.push_back(name);auto atomCount=free.atoms.size();optimizeMechanism(free);check(free.reactions.size()==1&&free.reactions[0].slots.size()==2&&free.atoms.size()==atomCount,"free drawing becomes an editable mechanism without losing structures");
 Document profile=coordinateDemo();auto& graph=profile.plots[0];graph.xLabel="H_{2} + x^{2}";graph.yLabel="ΔG^{‡}";graph.xColor="#aa1133";graph.series.push_back({"Path 2","#aa1133",{0,37.2,17.7,27.3,-9.9},0,2});auto profileScene=makeScene(profile);std::vector<Primitive> numbers;double legendY=0;int vertical=0;for(auto& item:profileScene.items){if(item.rotation==-90)vertical++;if(item.type==Primitive::Text&&(item.text=="Path 2"||item.text=="Path 1")){if(legendY)check(std::abs(legendY-item.points[0].y)<.01,"short legend entries share one horizontal row");legendY=item.points[0].y;}if(item.type==Primitive::Text&&(item.color=="#167d9a"||item.color=="#aa1133")&&item.text.find('.')!=std::string::npos)numbers.push_back(item);}
 check(vertical>0&&toSvg(profileScene).find("rotate(-90")!=std::string::npos,"vertical y caption exports to SVG");check(numbers.size()==10,"all pathways show energy values");for(size_t i=0;i<5;i++)check(std::abs(numbers[i].points[0].y-numbers[i+5].points[0].y)>=17,"coincident paths separate their value labels");
 Document imageDoc;Object image;image.id=imageDoc.nextId++;image.kind=ObjectKind::Image;image.imageHex="89504e470d0a1a0a";image.points={{0,0},{1000,500}};imageDoc.objects.push_back(image);Document target=coordinateDemo();fillSlot(target,imageDoc,target.plots[0].id,2,true);auto& inserted=target.objects.back();check(inserted.imageHex==image.imageHex&&(inserted.points[1]-inserted.points[0]).length()<130,"slot image scales presentation without changing source bytes");
#ifdef TEST_CDXML
 for(auto restored:{fromNative(toNative(cycle)),fromCdxml(toCdxml(cycle))})check(restored.reactions[0].centerLabel=="Catalytic cycle"&&restored.reactions[0].slots[0].energy=="-12.5","cycle text survives native and CDXML roundtrip");
 auto copyCycle=fromNative(toNative(cycle));check(copyCycle.reactions[0].cyclic&&copyCycle.reactions[0].unit=="kcal/mol","cycle settings persist");auto copyGraph=fromNative(toNative(profile));check(copyGraph.plots[0].xLabel=="H_{2} + x^{2}"&&copyGraph.plots[0].xColor=="#aa1133","axis formatting persists");check(toCdxml(profile).find("RotationAngle=\"-5898240\"")!=std::string::npos,"axis rotation exports to CDXML");
#endif
 Object straight;straight.id=500;straight.head=1;straight.points={{0,0},{60,0}};auto arrowParts=arrowPrimitives(straight);check(arrowParts.size()==2&&arrowParts[1].points.size()==3,"full arrowhead is a filled triangle");check(arrowParts[0].points.back().x<55&&arrowParts[1].points.front().x==60&&arrowParts[0].flatEnd,"shaft stops before sharp tip");
 straight.head=2;arrowParts=arrowPrimitives(straight);auto head=arrowParts[1].points;check(arrowParts[1].type==Primitive::Line&&head.size()==2&&arrowParts[1].width==straight.width,"fishhook is one barb with the same weight as its shaft");check((arrowParts[0].points.back()-head.back()).length()<1e-8&&head.back().x==60,"fishhook shaft and barb meet at the original tip");
 Object curly=straight;curly.kind=ObjectKind::Curve;curly.head=1;curly.points={{0,0},{10,-30},{50,-30},{60,0}};arrowParts=arrowPrimitives(curly);check(arrowParts[0].type==Primitive::Bezier&&arrowParts[0].points.back().x<60&&arrowParts[1].points[0].x==60,"cubic shaft trimmed while retaining original arrow tip");
 Object tiny=straight;tiny.points={{1,1},{1.5,1}};check(arrowPrimitives(tiny).size()==2,"very short arrow retains a head");
 curly.head=2;arrowParts=arrowPrimitives(curly);Vec hookWing=arrowParts[1].points[0]-curly.points.back(),endTangent=curly.points[3]-curly.points[2];check(endTangent.x*hookWing.y-endTangent.y*hookWing.x<0,"fishhook barb lies outside a clockwise screen curve");
 for(auto& point:curly.points)point.y=-point.y;arrowParts=arrowPrimitives(curly);hookWing=arrowParts[1].points[0]-curly.points.back();endTangent=curly.points[3]-curly.points[2];check(endTangent.x*hookWing.y-endTangent.y*hookWing.x>0,"mirrored curve keeps fishhook barb outside the bend");
 curly.points[2]=curly.points[3];arrowParts=arrowPrimitives(curly);check(arrowParts.size()==2&&std::isfinite(arrowParts[1].points[0].x),"fishhook handles a coincident endpoint control point");
 Document electrons;int donor=electrons.addAtom({0,0},8),acceptor=electrons.addAtom({100,0},7),obstacle=electrons.addAtom({50,24},9);Object flow;flow.id=electrons.nextId++;flow.kind=ObjectKind::Curve;flow.head=2;flow.autoRoute=true;flow.sourceAtom=donor;flow.targetAtom=acceptor;flow.points={{0,0},{28,32},{72,32},{100,0}};flow.color="#c43d42";electrons.objects.push_back(flow);
 std::map<int,Rect> atomBoxes={{donor,{-7,-7,14,14}},{acceptor,{93,-7,14,14}},{obstacle,{43,17,14,14}}};auto routed=routeArrow(electrons,flow,atomBoxes);check(routed.front().length()>12&&(routed.back()-Vec{100,0}).length()>12,"electron arrow leaves a visible gap at both atoms");
 auto bezier=[](const std::vector<Vec>& c,double t){double u=1-t;return c[0]*(u*u*u)+c[1]*(3*u*u*t)+c[2]*(3*u*t*t)+c[3]*(t*t*t);};check(bezier(routed,.5).y<0,"electron routing chooses the side away from a neighboring atom");bool avoids=true;for(int i=0;i<=100;i++){auto point=bezier(routed,i/100.);if(point.x>38&&point.x<62&&point.y>12&&point.y<36)avoids=false;}check(avoids,"routed cubic clears neighboring atom label and margin");
 Vec oldTip=routeArrow(electrons,flow).back();electrons.translate({acceptor},{20,-10});Vec newTip=routeArrow(electrons,electrons.objects[0]).back();check((newTip-oldTip).length()>10,"attached electron arrow follows the moved receiving atom");
 auto fragmentFlow=electrons.subset({flow.id});check(fragmentFlow.objects[0].sourceAtom==0&&fragmentFlow.objects[0].targetAtom==0&&!fragmentFlow.objects[0].autoRoute,"copying an arrow alone freezes its visible geometry");Document copiedFlow;copiedFlow.append(electrons,{200,10});check(copiedFlow.objects[0].sourceAtom==copiedFlow.atoms[0].id&&copiedFlow.objects[0].targetAtom==copiedFlow.atoms[1].id,"template insertion remaps both arrow attachments");
 Document movedArrow=electrons;auto beforeMove=routeArrow(movedArrow,movedArrow.objects[0]);movedArrow.translate({flow.id},{10,15});check(!movedArrow.objects[0].autoRoute&&(movedArrow.objects[0].points[0]-beforeMove[0]-Vec{10,15}).length()<.001,"dragging only an attached arrow detaches and translates its visible curve");
 Document removedFlow=electrons;removedFlow.erase({acceptor});check(!removedFlow.objects[0].autoRoute&&removedFlow.objects[0].sourceAtom==0,"deleting an attached atom preserves the visible arrow");
 auto electronScene=makeScene(electrons);int visibleHeads=0;for(auto& item:electronScene.items)if(item.owner==flow.id&&item.type==Primitive::Line&&item.points.size()==2)visibleHeads++;check(visibleHeads==1&&toSvg(electronScene).find("stroke-linecap=\"butt\"")!=std::string::npos,"screen and SVG use one stroked fishhook with a continuous shaft");
 Document compact;int ca=compact.addAtom({0,0}),cb=compact.addAtom({30,0}),cc=compact.addAtom({-15,26}),cd=compact.addAtom({45,26});int topBond=compact.addBond(ca,cb,BondKind::Double);compact.addBond(ca,cc);compact.addBond(cb,cd);int nitrogen=compact.addAtom({65,10},7);Object compactFlow;compactFlow.id=compact.nextId++;compactFlow.kind=ObjectKind::Curve;compactFlow.head=2;compactFlow.autoRoute=true;compactFlow.sourceAtom=ca;compactFlow.targetAtom=cb;compactFlow.points={{0,0},{0,-100},{30,-100},{30,0}};compact.objects.push_back(compactFlow);
 auto compactPath=makeScene(compact).arrowPaths.at(compactFlow.id);for(int i=0;i<=100;i++)check(std::abs(bezier(compactPath,i/100.).y)<=12.4,"adjacent atom arrow stays a compact arc even beside other atoms");
 Document ringFlow;ringFlow.ring({60,60},6,true);Object ringArrow=compactFlow;ringArrow.id=ringFlow.nextId++;ringArrow.sourceAtom=ringFlow.atoms[5].id;ringArrow.targetAtom=ringFlow.atoms[0].id;ringFlow.objects.push_back(ringArrow);auto ringPath=routeArrow(ringFlow,ringArrow);check(bezier(ringPath,.5).y<ringFlow.atoms[0].p.y,"benzene top-edge arrow prefers the open space outside the ring");
 Document cramped;int crampedA=cramped.addAtom({0,0},7),crampedB=cramped.addAtom({5,0},8);Object crampedArrow=compactFlow;crampedArrow.sourceAtom=crampedA;crampedArrow.targetAtom=crampedB;auto crampedPath=routeArrow(cramped,crampedArrow);check(std::abs(bezier(crampedPath,.5).y)<10,"overlapping labels cannot restore an old oversized curve as fallback");
 Object bondFlow=compactFlow;bondFlow.id=compact.nextId++;bondFlow.sourceAtom=0;bondFlow.sourceBond=topBond;bondFlow.sourceBondT=.5;bondFlow.targetAtom=nitrogen;compact.objects.push_back(bondFlow);auto bondPath=makeScene(compact).arrowPaths.at(bondFlow.id);check(std::abs(bondPath[0].y)>=8.5,"bond arrow starts with clearance from both lines of a double bond");for(int i=0;i<=100;i++)check(std::abs(bezier(bondPath,i/100.).y)<33,"bond arrow never grows a tall loop");
 auto frozenBond=compact.subset({bondFlow.id});check(!frozenBond.objects[0].autoRoute&&!frozenBond.objects[0].sourceBond,"copying a bond arrow alone detaches its bond");
 Document bondCopy;bondCopy.append(compact,{20,10});check(bondCopy.objects[1].sourceBond==bondCopy.bonds[0].id,"copying a molecule remaps the bond anchor");auto beforeBondMove=routeArrow(compact,compact.objects[1]);compact.translate({ca,cb},{0,-10});auto afterBondMove=routeArrow(compact,compact.objects[1]);check((beforeBondMove[0]-afterBondMove[0]).length()>5,"bond anchor follows moved bond atoms");
 auto deletedBond=compact;deletedBond.erase({topBond});check(!deletedBond.objects[1].autoRoute&&!deletedBond.objects[1].sourceBond,"deleting the anchor bond freezes the visible arrow");auto deletedBondAtom=compact;deletedBondAtom.erase({ca});check(!deletedBondAtom.objects[1].autoRoute&&!deletedBondAtom.objects[1].sourceBond,"deleting an anchor bond endpoint freezes the visible arrow");
 Document receiving=compact;receiving.objects[1].sourceBond=0;receiving.objects[1].sourceAtom=nitrogen;receiving.objects[1].targetAtom=0;receiving.objects[1].targetBond=topBond;auto receivingPath=routeArrow(receiving,receiving.objects[1]);Vec receivingTip=receivingPath.back();double receivingDx=std::max({-receivingTip.x,0.,receivingTip.x-30});check(std::hypot(receivingDx,receivingTip.y+10)>=8.5,"arrow ending at a double bond leaves clearance as well");
 Document twoBonds;int ba=twoBonds.addAtom({0,0}),bb=twoBonds.addAtom({30,0}),bc=twoBonds.addAtom({60,0}),bd=twoBonds.addAtom({90,0});int firstBond=twoBonds.addBond(ba,bb),lastBond=twoBonds.addBond(bc,bd);Object betweenBonds=compactFlow;betweenBonds.sourceAtom=betweenBonds.targetAtom=0;betweenBonds.sourceBond=firstBond;betweenBonds.targetBond=lastBond;auto twoBondPath=routeArrow(twoBonds,betweenBonds);check(std::abs(twoBondPath.front().y)>=5&&std::abs(twoBondPath.back().y)>=5,"bond-to-bond arrow leaves a small gap at both bonds");
#ifdef TEST_CDXML
 auto bondNative=fromNative(toNative(compact));auto bondCdxml=fromCdxml(toCdxml(compact));check(bondNative.objects[1].sourceBond==topBond&&bondNative.objects[1].sourceBondT==.5&&bondCdxml.objects[1].sourceBond==topBond,"bond anchors survive native and CDXML save and reload");
 auto nativeFlow=fromNative(toNative(electrons));check(nativeFlow.objects[0].sourceAtom==donor&&nativeFlow.objects[0].targetAtom==acceptor&&nativeFlow.objects[0].autoRoute,"electron attachments survive native save and reload");auto cdxmlFlow=fromCdxml(toCdxml(electrons));check(cdxmlFlow.objects[0].head==2&&cdxmlFlow.objects[0].autoRoute,"electron fishhook and routing survive CDXML metadata roundtrip");
#endif
 {
     Document imported;int m=imported.addAtom({10,20},27),a=imported.addAtom({23,29},7),b=imported.addAtom({-4,25},8);imported.addBond(m,a,BondKind::Wedge);imported.addBond(m,b);
     auto before=imported;auto untouched=imported.addAtom({300,300});auto result=cleanStructure(imported,{a},true);
     Vec v1=before.atom(a)->p-before.atom(m)->p,v2=imported.atom(a)->p-imported.atom(m)->p;
     double ratio=v2.length()/v1.length();
     check(result.components==1&&ratio>1&&std::abs(v1.x*v2.y-v1.y*v2.x)<1e-8,"projection-preserving cleanup normalizes a metal complex without rotating it");
     check(((imported.atom(b)->p-imported.atom(m)->p)-(before.atom(b)->p-before.atom(m)->p)*ratio).length()<1e-8&&imported.bonds[0].kind==BondKind::Wedge,"projection cleanup preserves all relative angles and wedge stereochemistry");
     check(imported.atom(untouched)->p.x==300,"projection cleanup leaves an unselected component unchanged");
     auto stable=imported;cleanStructure(imported,{a},true);for(auto& atom:stable.atoms)check((atom.p-imported.atom(atom.id)->p).length()<1e-8,"projection cleanup is stable on repeated use");
 }
 std::cout<<"PASS: "<<checks<<" checks (editing, cleanup, templates, blocks, graph, XML compatibility)\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

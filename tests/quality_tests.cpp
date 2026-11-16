#include "model.h"
#include "scene.h"
#include "cdxml.h"
#include "win_recovery.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
using namespace yd;
namespace fs=std::filesystem;
static int checks=0;
static void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
static std::string external(const Document& d){return std::regex_replace(toCdxml(d),std::regex("<annotation[^>]*Keyword=\"YoungDraw.State\"[^>]*/>"),"");}
int main(){try{
    Document d;int a=d.addAtom({0,0}),b=d.addAtom({30,0}),c=d.addAtom({60,0});int bond=d.addBond(a,b);d.addBond(b,c);
    d.translate({bond},{12,9});check(d.atom(a)->p.x==12&&d.atom(b)->p.x==42&&d.atom(c)->p.x==60,"bond-only drag moves its endpoints without dragging the whole molecule");
    Object arrow;arrow.id=d.nextId++;arrow.points={{12,9},{60,0}};arrow.sourceAtom=a;arrow.targetAtom=c;arrow.autoRoute=true;arrow.head=1;d.objects.push_back(arrow);
    auto visible=routeArrow(d,arrow);d.translate({arrow.id},{5,11});
    check(!d.objects[0].autoRoute&&(d.objects[0].points[0]-visible[0]-Vec{5,11}).length()<1e-8,"standalone straight arrow detaches from its anchors");
    auto state=toNative(d);History history;
    for(int i=0;i<80;++i){history.checkpoint(d);d.translate({bond},{1,-1});}
    for(int i=0;i<80;++i)check(history.undo(d),"long edit sequence undo");
    check(toNative(d)==state,"undo restores exact original geometry and anchors");
    for(int i=0;i<80;++i)check(history.redo(d),"long edit sequence redo");
    check(d.atom(a)->p.x==92&&d.atom(c)->p.x==60,"redo preserves partial selection boundary");
    Document curve;Object o;o.id=curve.nextId++;o.kind=ObjectKind::Curve;o.points={{0,0},{0,100},{100,100},{100,0}};curve.objects.push_back(o);
    auto scene=makeScene(curve);check(hitObject(scene,o.id,{50,75},2),"curve stroke is selectable");
    check(!hitObject(scene,o.id,{50,15},2),"empty curve interior is not selectable");
    curve.objects[0].filled=true;check(hitObject(makeScene(curve),o.id,{50,30},2),"filled curved shapes retain interior selection");
    o.kind=ObjectKind::Line;o.points={{0,0},{50,50},{100,0}};curve.objects[0]=o;scene=makeScene(curve);
    check(hitObject(scene,o.id,{75,25},2)&&!hitObject(scene,o.id,{50,0},2),"polyline selection tests every visible segment");
    Document imported=fromCdxml("<CDXML><page id='1'><fragment id='2'><n id='3' p='0 0' Element='7' NumHydrogens='1'/><n id='4' p='30 0'/><b id='5' B='3' E='4' Order='2' DoublePosition='Left'/></fragment></page></CDXML>");
    imported.atoms[0].explicitH=2;imported.atoms[0].color="#ab1234";imported.bonds[0].doubleSide=0;
    auto visibleXml=external(imported);auto reopened=fromCdxml(visibleXml);
    check(reopened.atoms[0].explicitH==2,"edited explicit hydrogens survive external CDXML roundtrip");
    check(reopened.atoms[0].color=="#ab1234","edited atom color survives external CDXML roundtrip");
    check(reopened.bonds[0].doubleSide==0,"automatic double-bond position clears imported override");
    imported.atoms[0].explicitH=-1;check(fromCdxml(external(imported)).atoms[0].explicitH==-1,"implicit hydrogens clear imported explicit count");
    for(auto geometry:{std::string("kind='2' points=''"),std::string("kind='1' points='0 0 1 1 2 2'"),std::string("kind='5' points='0 0'")}){
        bool rejected=false;try{fromNative("<YoungDraw Version='3'><object id='1' "+geometry+"/></YoungDraw>");}catch(const std::exception&){rejected=true;}
        check(rejected,"malformed geometry is rejected before rendering");
    }
    Scene vectorScene;vectorScene.bounds={0,0,100,100};Primitive ellipse;ellipse.type=Primitive::Ellipse;ellipse.points={{10,10},{90,80}};ellipse.dash=1;ellipse.opacity=128;vectorScene.items.push_back(ellipse);
    auto svg=toSvg(vectorScene);check(svg.find("stroke-dasharray=\"4 3\"")!=std::string::npos&&svg.find("opacity=\"0.5020\"")!=std::string::npos,"SVG preserves dashed shape outlines and opacity");
    Document large;int prev=large.addAtom({0,0});
    for(int i=1;i<3000;++i){int next=large.addAtom({double(i),double(i%2)});large.addBond(prev,next);prev=next;}
    check(large.component(large.atoms[0].id).size()==3000,"large connected molecule traversal");
    History largeHistory;auto largeOriginal=toNative(large);auto largeIds=large.allIds();
    for(int i=0;i<24;++i){largeHistory.checkpoint(large);large.translate(largeIds,{2,-1});}
    for(int i=0;i<24;++i)check(largeHistory.undo(large),"large document undo");
    check(toNative(large)==largeOriginal,"large document undo preserves all atoms and bonds");
    for(int i=0;i<24;++i)check(largeHistory.redo(large),"large document redo");
    check(large.atoms.front().p.x==48&&large.atoms.back().p.x==3047,"large document redo preserves rigid movement");
    auto root=fs::temp_directory_path()/(L"YoungDraw-recovery-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    fs::path snapshotFile;
    {
        recovery::Session session(root);session.save({{L"한글 문서.cdxml",imported},{L"",d}});
        check(recovery::abandoned(root).empty(),"running sessions cannot be recovered by another instance");
    }
    auto recovered=recovery::abandoned(root);check(recovered.size()==1&&recovered[0].documents.size()==2,"abandoned multi-document session recovers");
    check(recovered[0].documents[0].path==L"한글 문서.cdxml"&&toNative(recovered[0].documents[0].document)==toNative(imported),"recovery retains Unicode source path and unsupported source XML");
    check(toNative(recovered[0].documents[1].document)==toNative(d),"untitled edits and anchors recover exactly");
    snapshotFile=recovered[0].file;
    {recovery::Session clean(root);clean.save({{L"",d}});clean.clear();}
    check(recovery::abandoned(root).size()==1,"clean shutdown clears only its own recovery snapshot");
    {std::ofstream file(snapshotFile,std::ios::binary|std::ios::trunc);file<<"YoungDraw Recovery 1\n9999999999\nbroken";}
    check(recovery::abandoned(root).empty()&&fs::exists(snapshotFile),"corrupt recovery is retained without crashing or opening partial data");
    // Remove only exact files/directories created in this test; no recursive cleanup.
    for(auto& folder:fs::directory_iterator(root)){for(auto& file:fs::directory_iterator(folder.path()))fs::remove(file.path());fs::remove(folder.path());}fs::remove(root);
    std::cout<<"PASS: "<<checks<<" quality checks\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

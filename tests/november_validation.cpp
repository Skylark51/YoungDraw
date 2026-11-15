#include "cdxml.h"
#include "scene.h"
#include "mechanism_data.h"
#include "plot_data.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace yd;
static void check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
static void roundtrip(const Document& d){
    auto saved=fromCdxml(toCdxml(d));
    check(toNative(saved)==toNative(d),"CDXML editable metadata changed");
    check(toNative(fromNative(toNative(d)))==toNative(d),"project data changed");
    check(!toSvg(makeScene(saved)).empty(),"empty SVG");
}
int wmain(int argc,wchar_t** argv){try{
    for(int scenario=0;scenario<5;++scenario){
        Document d;EnergyPlot p;p.id=d.nextId++;p.width=760;p.height=450;
        for(int state=0;state<9;++state)p.states.push_back({"Intermediate "+std::to_string(state),double(state)});
        for(int path=0;path<3+scenario;++path){
            EnergySeries s;s.name="Catalytic pathway "+std::to_string(path);s.dash=path%3;
            s.color=std::vector<std::string>{"#167d9a","#b44758","#7a52a1","#27854a","#b87516","#3569a8","#b25289"}[path];
            for(int state=0;state<9;++state)s.values.push_back((state%2?24.:0.)+path*.6-state*1.5);
            if(scenario==2){s.present.assign(9,true);s.present[4]=false;}
            p.series.push_back(s);
        }
        if(scenario==1){p.yMin=-12;p.yMax=32;}
        if(scenario==3)p=bilateralPlot(p,0,"Alternative pathway");
        if(scenario==4)p=publicationPlot(p);
        d.plots={p};roundtrip(d);
        auto base="november-graph-"+std::to_string(scenario+1);
        std::ofstream(base+".svg")<<toSvg(makeScene(d));
        std::ofstream(base+".cdxml")<<toCdxml(d);
        std::ofstream(base+".ydraw")<<toNative(d);
    }
    int count=0,mechanisms=0,unconverted=0;
    if(argc>1)for(const auto& file:std::filesystem::recursive_directory_iterator(argv[1])){
        if(file.path().extension()!=L".cdxml")continue;
        std::ifstream input(file.path(),std::ios::binary);std::string xml((std::istreambuf_iterator<char>(input)),{});
        if(xml.find("ChemDraw")==std::string::npos)continue;
        try{
            auto d=fromCdxml(xml);roundtrip(d);
            d.style.lineWidth+=.1;roundtrip(d);
            auto before=d;
            int stages=0;
            try{stages=arrangeImportedMechanism(d,{},true);}
            catch(const std::runtime_error& e){++unconverted;std::cout<<"CONVERSION_UNAVAILABLE "<<e.what()<<'\n';}
            if(!stages&&!d.objects.empty()&&d.objects[0].kind==ObjectKind::Text)d.objects[0].text+=" (edited)";
            for(const auto& atom:before.atoms){auto* now=d.atom(atom.id);check(now&&(now->p-atom.p).length()<1e-6,"preserve-layout moved atom");}
            if(stages>0&&!d.reactions.empty()){
                ++mechanisms;auto& r=d.reactions.back();auto index=r.slots.size();
                if(index<40){addMechanismBranch(r,0);roundtrip(d);removeMechanismStage(d,r.id,index,-1,false);}
            }
            roundtrip(d);
            std::cout<<"PASS "<<file.path().u8string()<<" stages="<<stages<<" warnings="<<d.importWarnings.size()<<'\n';
            ++count;
        }catch(const std::exception& e){std::cerr<<"FAIL "<<file.path().u8string()<<": "<<e.what()<<'\n';return 1;}
        if(count>=20&&mechanisms>=20)break;
    }
    if(argc>1)check(count>=20,"fewer than 20 ChemDraw examples");
    std::cout<<"PASS graphs=5 ChemDraw="<<count<<" converted="<<mechanisms<<" unavailable="<<unconverted<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

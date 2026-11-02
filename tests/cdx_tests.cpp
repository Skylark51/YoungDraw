#include "cdxml.h"
#include "scene.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <cstdint>
using namespace yd;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static std::string le(uint32_t v,int width){std::string out;for(int i=0;i<width;i++)out+=char(v>>(8*i));return out;}
static std::string prop(unsigned tag,const std::string& value,bool extended=false){return le(tag,2)+(extended?le(65535,2)+le(unsigned(value.size()),4):le(unsigned(value.size()),2))+value;}
static std::string obj(unsigned tag,unsigned id,const std::string& content){return le(tag,2)+le(id,4)+content+le(0,2);}
static std::string position(int x,int y){return prop(0x200,le(uint32_t(y)*65536,4)+le(uint32_t(x)*65536,4));}
static std::string fixture(bool clipboard){
    std::string header="VjCD0100";header+=le(0x01020304,4);header+=std::string(clipboard?10:16,0);
    auto style=le(1,2)+le(0,2)+le(3,2)+le(1,2)+le(240,2)+le(0,2);
    auto label=obj(0x8006,0,position(20,0)+prop(0x700,style+"N"));
    auto a=obj(0x8004,3,position(-10,0));
    auto b=obj(0x8004,4,position(20,0)+prop(0x402,le(7,2))+prop(0x421,le(255,1))+prop(0x420,le(15,2))+label);
    auto bond=obj(0x8005,5,prop(0x604,le(3,4))+prop(0x605,le(4,4))+prop(0x601,le(7,2)));
    auto text=obj(0x8006,6,position(0,30)+prop(0x700,style+"TS ?.?"));
    return header+obj(0x8000,0,prop(0x805,le(30*65536,4))+prop(0x7000,"unknown\0property",true)+obj(0x8001,1,obj(0x8003,2,a+b+bond)+text))+le(0,2);
}
int main(int argc,char** argv){try{
    for(bool clipboard:{false,true}){
        auto bytes=fixture(clipboard);auto d=fromCdx(bytes);
        check(d.atoms.size()==2&&d.bonds.size()==1&&d.objects.size()==1,"binary CDX imports editable atoms, bonds, and text");
        check(d.atoms[0].p.x==-10&&d.atoms[1].element==7&&d.atoms[1].charge==-1&&d.atoms[1].isotope==15,"signed coordinates, element, isotope, and charge survive");
        check(d.atoms[1].label=="N"&&d.objects[0].text=="TS ?.?","styled labels and text retain exact contents");
        check(d.bonds[0].kind==BondKind::Wedge&&d.bonds[0].a==4&&d.bonds[0].b==3,"wedge end orientation is retained");
        for(auto restored:{fromNative(toNative(d)),fromCdxml(toCdxml(d))})check(restored.atoms.size()==2&&restored.bonds[0].a==4,"binary imports persist in native and CDXML");
        for(size_t n=0;n+2<bytes.size();n++){
            bool rejected=false;try{fromCdx(bytes.substr(0,n));}catch(...){rejected=true;}check(rejected,"truncated binary input is rejected");
        }
        auto bad=bytes;bad[0]='X';bool rejected=false;try{fromCdx(bad);}catch(...){rejected=true;}check(rejected,"non-CDX input is rejected");
        bad=bytes;bad+='X';rejected=false;try{fromCdx(bad);}catch(...){rejected=true;}check(rejected,"unexpected trailing binary data is rejected");
    }
    if(argc>1){
        std::ifstream in(argv[1],std::ios::binary);check(bool(in),"open real ChemDraw clipboard fixture");
        std::string bytes((std::istreambuf_iterator<char>(in)),{});auto d=fromCdx(bytes);
        check(d.atoms.size()==15&&d.bonds.size()==16,"real ChemDraw clipboard retains all 15 atoms and 16 bonds");
        bool label=false;for(auto& a:d.atoms)if(a.label=="LCoIII")label=true;check(label,"real ChemDraw custom metal label is retained");
        auto restored=fromNative(toNative(d));check(restored.atoms.size()==15&&restored.bonds.size()==16,"real clipboard import saves and reopens");
        std::ofstream("chemdraw-clipboard-import.svg")<<toSvg(makeScene(d));
        std::cout<<"Real ChemDraw clipboard: "<<d.atoms.size()<<" atoms, "<<d.bonds.size()<<" bonds\n";
    }
    std::cout<<"CDX import tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

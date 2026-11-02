#include "cdxml.h"
#include <windows.h>
#include <cstdint>
#include <map>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace yd {
namespace {
// ChemDraw CDX SDK: little-endian tagged objects, fixed-point coordinates,
// and length-delimited properties. Clipboard CDX uses a 22-byte header;
// standalone files also use the documented 28-byte header.
struct CdxNode {
    std::string name,text;
    std::map<std::string,std::string> attributes;
    std::vector<CdxNode> children;
};
std::string escapeXml(const std::string& text){
    std::string out;for(unsigned char c:text)switch(c){
        case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;case '"':out+="&quot;";break;
        case '\r':out+="&#13;";break;case '\n':out+="&#10;";break;case '\t':out+="&#9;";break;
        default:if(c<32)throw std::runtime_error("Invalid CDX text");out+=char(c);
    }return out;
}
std::string serialize(const CdxNode& n){
    std::string out="<"+n.name;for(auto& a:n.attributes)out+=" "+a.first+"=\""+escapeXml(a.second)+"\"";
    out+='>';out+=escapeXml(n.text);for(auto& c:n.children)out+=serialize(c);return out+"</"+n.name+">";
}
std::string decimal(double value){std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(12)<<value;return s.str();}
struct Reader {
    const std::string& data;size_t cursor=0,objects=0;
    std::map<unsigned,UINT> encodings;
    static uint32_t integer(const std::string& s,size_t at,size_t width){
        if(width>4||at>s.size()||width>s.size()-at)throw std::runtime_error("Truncated CDX data");
        uint32_t v=0;for(size_t i=0;i<width;i++)v|=uint32_t(static_cast<unsigned char>(s[at+i]))<<(8*i);return v;
    }
    uint32_t read(size_t width){auto v=integer(data,cursor,width);cursor+=width;return v;}
    static double coordinate(const std::string& s,size_t at){return int32_t(integer(s,at,4))/65536.;}
    static std::string decode(const std::string& text,UINT encoding=1252){
        if(text.empty())return {};int count=MultiByteToWideChar(encoding,0,text.data(),int(text.size()),nullptr,0);
        if(!count)throw std::runtime_error("Unsupported CDX text encoding");std::wstring wide(count,0);
        MultiByteToWideChar(encoding,0,text.data(),int(text.size()),wide.data(),count);
        count=WideCharToMultiByte(CP_UTF8,0,wide.data(),int(wide.size()),nullptr,0,nullptr,nullptr);std::string result(count,0);
        WideCharToMultiByte(CP_UTF8,0,wide.data(),int(wide.size()),result.data(),count,nullptr,nullptr);return result;
    }
    void styledText(CdxNode& n,const std::string& s){
        size_t count=integer(s,0,2),begin=2+10*count;if(begin>s.size())throw std::runtime_error("Invalid CDX style runs");
        auto text=s.substr(begin);
        if(!count){n.children.push_back({"s",decode(text),{}, {}});return;}
        for(size_t i=0;i<count;i++){
            size_t at=2+10*i,start=integer(s,at,2),end=i+1<count?integer(s,at+10,2):text.size();
            if((i==0&&start!=0)||start>end||end>text.size())throw std::runtime_error("Invalid CDX text range");
            auto font=integer(s,at+2,2);CdxNode run;run.name="s";
            run.attributes={{"font",decimal(font)},{"face",decimal(integer(s,at+4,2))},{"size",decimal(integer(s,at+6,2)/20.)},{"color",decimal(integer(s,at+8,2))}};
            run.text=decode(text.substr(start,end-start),encodings.count(font)?encodings[font]:1252);n.children.push_back(std::move(run));
        }
    }
    void property(CdxNode& n,unsigned tag,const std::string& s){
        auto set=[&](const char* key,double v){n.attributes[key]=decimal(v);};
        if(tag==0x0700){styledText(n,s);return;}
        if(tag==0x0100){
            CdxNode table;table.name="fonttable";size_t at=4,count=integer(s,2,2);
            for(size_t i=0;i<count;i++){
                auto id=integer(s,at,2),encoding=integer(s,at+2,2),size=integer(s,at+4,2);at+=6;
                if(size>s.size()-at)throw std::runtime_error("Truncated CDX font");encodings[id]=encoding==0?1252:encoding;
                table.children.push_back({"font","",{{"id",decimal(id)},{"name",decode(s.substr(at,size))}}, {}});at+=size;
            }n.children.push_back(std::move(table));return;
        }
        if(tag==0x0300){
            CdxNode table;table.name="colortable";size_t count=integer(s,0,2);
            for(size_t i=0;i<count;i++)table.children.push_back({"color","",{{"r",decimal(integer(s,2+6*i,2)/65535.)},{"g",decimal(integer(s,4+6*i,2)/65535.)},{"b",decimal(integer(s,6+6*i,2)/65535.)}}, {}});
            n.children.push_back(std::move(table));return;
        }
        if(tag==0x0200||tag==0x0204){
            n.attributes[tag==0x0200?"p":"BoundingBox"]=decimal(coordinate(s,4))+" "+decimal(coordinate(s,0));
            if(tag==0x0204)n.attributes["BoundingBox"]+=" "+decimal(coordinate(s,12))+" "+decimal(coordinate(s,8));return;
        }
        if(tag==0x0421){if(s.size()!=1&&s.size()!=2&&s.size()!=4)throw std::runtime_error("Invalid CDX charge");set("Charge",s.size()==1?int8_t(integer(s,0,1)):s.size()==2?int16_t(integer(s,0,2)):int32_t(integer(s,0,4)));return;}
        if(tag==0x0600){
            static const std::map<unsigned,std::string> orders={{1,"1"},{2,"2"},{4,"3"},{8,"4"},{16,"5"},{32,"6"},{64,"0.5"},{128,"1.5"},{256,"2.5"},{512,"3.5"},{4096,"dative"},{8192,"ionic"},{16384,"hydrogen"}};
            auto v=integer(s,0,2);if(orders.count(v))n.attributes["Order"]=orders.at(v);else if(v!=65535)throw std::runtime_error("Unsupported CDX query bond order");return;
        }
        if(tag==0x0601||tag==0x0602){
            static const char* displays[]={"Solid","Dash","Hash","WedgedHashBegin","WedgedHashEnd","Bold","WedgeBegin","WedgeEnd","Wavy","HollowWedgeBegin","HollowWedgeEnd","WavyWedgeBegin","WavyWedgeEnd","Dot","DashDot"};
            auto v=integer(s,0,2);if(v>=std::size(displays))throw std::runtime_error("Invalid CDX bond display");n.attributes[tag==0x0601?"Display":"Display2"]=displays[v];return;
        }
        if(tag==0x0603){auto v=integer(s,0,2)&0xff;n.attributes["DoublePosition"]=v==1?"Right":v==2?"Left":"Center";return;}
        if(tag==0x080a||tag==0x080b){const char* prefix=tag==0x080a?"Label":"Caption";set((std::string(prefix)+"Font").c_str(),integer(s,0,2));set((std::string(prefix)+"Face").c_str(),integer(s,2,2));set((std::string(prefix)+"Size").c_str(),integer(s,4,2)/20.);return;}
        static const std::map<unsigned,const char*> words={{0x0301,"color"},{0x0400,"NodeType"},{0x0402,"Element"},{0x0420,"Isotope"},{0x0422,"Radical"},{0x042b,"NumHydrogens"},{0x0604,"B"},{0x0605,"E"},{0x081a,"LabelFont"},{0x081b,"CaptionFont"},{0x081e,"LabelFace"},{0x081f,"CaptionFace"}};
        if(words.count(tag)){set(words.at(tag),integer(s,0,s.size()));return;}
        static const std::map<unsigned,const char*> coords={{0x0805,"BondLength"},{0x0806,"BoldWidth"},{0x0807,"LineWidth"},{0x0808,"MarginWidth"},{0x0809,"HashSpacing"}};
        if(coords.count(tag)){set(coords.at(tag),coordinate(s,0));return;}
        if(tag==0x0804){set("BondSpacing",integer(s,0,2)/10.);return;}
        if(tag==0x081c||tag==0x081d)set(tag==0x081c?"LabelSize":"CaptionSize",integer(s,0,2)/20.);
        // Unknown properties are length-delimited and must not be read as tags.
    }
    CdxNode object(unsigned tag,int depth){
        if(depth>64||++objects>200000)throw std::runtime_error("CDX object limit");
        static const std::map<unsigned,const char*> names={{0x8000,"CDXML"},{0x8001,"page"},{0x8002,"group"},{0x8003,"fragment"},{0x8004,"n"},{0x8005,"b"},{0x8006,"t"}};
        CdxNode n;n.name=names.count(tag)?names.at(tag):"cdxobject";auto id=read(4);
        if(id>INT_MAX)throw std::runtime_error("CDX object ID limit");if(id)n.attributes["id"]=decimal(id);
        for(;;){
            auto next=read(2);if(!next)break;
            if(next&0x8000)n.children.push_back(object(next,depth+1));
            else{size_t size=read(2);if(size==65535)size=read(4);if(size>data.size()-cursor)throw std::runtime_error("Truncated CDX property");auto bytes=data.substr(cursor,size);cursor+=size;property(n,next,bytes);}
        }return n;
    }
};
}
Document fromCdx(const std::string& bytes){
    if(bytes.size()>64*1024*1024||bytes.size()<28||bytes.compare(0,8,"VjCD0100")!=0)throw std::runtime_error("Invalid ChemDraw CDX header");
    Reader reader{bytes};
    if(bytes.size()>=34&&Reader::integer(bytes,28,2)==0x8000)reader.cursor=28;
    else if(Reader::integer(bytes,22,2)==0x8000)reader.cursor=22;
    else throw std::runtime_error("Missing CDX document");
    auto tag=reader.read(2);auto root=reader.object(tag,0);
    // Allocation padding and the optional EndFile marker consist only of zeros.
    for(size_t i=reader.cursor;i<bytes.size();i++)if(bytes[i])throw std::runtime_error("Unexpected CDX trailing data");
    return fromCdxml(serialize(root));
}
}

#include "mechanism_data.h"
#include <sstream>
#include <locale>
#include <stdexcept>
namespace yd {
std::vector<Vec> mechanismLinkPath(const Reaction& r,size_t index,const Style& style){
    auto l=r.links.at(index);Vec b=reactionSlot(r,size_t(l.to));
    double edge=r.slotWidth/2+mechanismArrowMargin(r,style),font=mechanismFontSize(r,style);
    if(l.from<0){
        double x=b.x;for(auto& link:r.links)if(link.from<0)x=std::min(x,reactionSlot(r,size_t(link.to)).x);
        x-=edge+mechanismArrowLength(r,style);b.x-=edge;return {{x,b.y},b};
    }
    Vec a=reactionSlot(r,size_t(l.from));double direction=b.x>=a.x?1.:-1.;a.x+=direction*edge;b.x-=direction*edge;
    if(std::abs(a.y-b.y)<.01)return {a,b};
    // Every outgoing path shares a fixed spine next to its source frame.
    double x=a.x+direction*font*.6;
    return {a,{x,a.y},{x,b.y},b};
}
Vec mechanismLinkCaption(const Reaction& r,size_t index,const Style& style){auto path=mechanismLinkPath(r,index,style);return (path[path.size()-2]+path.back())*.5;}

double mechanismFontSize(const Reaction& r,const Style& style){return r.captionFontSize>0?r.captionFontSize:r.cyclic?style.fontSize:std::max(style.fontSize*1.5,std::clamp(std::min(r.slotWidth,r.slotHeight)*.20,22.,48.));}
double mechanismArrowMargin(const Reaction& r,const Style& style){return std::max(10.,mechanismFontSize(r,style)*.5);}
double mechanismCaptionHeight(const Reaction& r,const Style& style){
    double height=0,font=mechanismFontSize(r,style);
    for(auto& slot:r.slots){double lines=1+std::count(slot.label.begin(),slot.label.end(),'\n');if(!slot.energy.empty())lines+=1+std::count(slot.energy.begin(),slot.energy.end(),'\n');height=std::max(height,font*(.6+1.5*lines));}
    return height;
}
double mechanismArrowLength(const Reaction& r,const Style& style){
    double font=mechanismFontSize(r,style),widest=0;
    auto measure=[&](const std::string& raw){
        auto text=chemicalScripts(raw);double line=0,scale=1;size_t close=std::string::npos;
        for(size_t i=0;i<text.size();i++){unsigned char c=text[i];
            if((c=='_'||c=='^')&&i+1<text.size()&&text[i+1]=='{'&&(close=text.find('}',i+2))!=std::string::npos){scale=.7;i++;continue;}
            if(i==close){scale=1;continue;}
            if(c=='\n'){widest=std::max(widest,line);line=0;continue;}
            if(c=='\r'||(c&0xc0)==0x80)continue;
            // Match the scene's conservative Arial advance instead of reserving
            // a wide glyph for every letter. Keep extra room for M/W and CJK.
            line+=font*scale*(c>=0x80?1.05:c=='W'||c=='M'?1.:.62);
        }widest=std::max(widest,line);
    };
    auto measureEnergy=[&](const std::string& raw){
        try{
            auto values=reactionEnergies(raw);std::string visible;
            while(!values.empty()&&values.back().value.empty())values.pop_back();
            for(size_t i=0;i<values.size();i++){auto& value=values[i];
                if(i)visible+=" / ";
                if(!value.value.empty()){
                    if(value.spin!=0&&value.spin!=1&&value.spin!=2){std::ostringstream label;label.imbue(std::locale::classic());label<<"S="<<value.spin<<": ";visible+=label.str();}
                    visible+=value.value;
                }
            }
            measure(visible);
        }catch(...){measure(raw);}
    };
    for(auto& raw:r.above){auto caption=transitionCaption(raw);measure(caption.description);measure(caption.name);}
    for(auto& raw:r.below)measureEnergy(raw);
    for(auto& slot:r.slots){measure(slot.label);measureEnergy(slot.energy);}
    return std::max(font*4.,widest+font);
}
namespace {
struct CycleBox {double x,y,w,h;};
double cycleTextWidth(const std::string& text,double font){
    double width=0,line=0;for(unsigned char c:text){if(c=='\n'){width=std::max(width,line);line=0;}else if((c&0xc0)!=0x80)line+=font*(c>=0x80||c=='W'||c=='M'?1.1:.74);}return std::max(width,line);
}
double cycleTextHeight(const std::string& text,double font){return text.empty()?0:font*(1+1.3*std::count(text.begin(),text.end(),'\n'));}
CycleBox cycleTransitionBox(const Reaction& r,size_t i,double font){
    auto c=transitionCaption(i<r.above.size()?r.above[i]:"");auto below=i<r.below.size()?r.below[i]:"";
    double w=std::max({cycleTextWidth(c.description,font),cycleTextWidth(c.name,font),cycleTextWidth(below,font)});
    double top=-font*(1.7+1.3*std::count(c.description.begin(),c.description.end(),'\n'));
    double bottom=font*.65+(c.name.empty()?0:font*1.5*(1+std::count(c.name.begin(),c.name.end(),'\n')))+cycleTextHeight(below,font);
    return {-w/2,top,w,std::max(font,bottom-top)};
}
}
double mechanismCycleNodeRadius(const Reaction& r,const Style& style){
    double font=mechanismFontSize(r,style),width=r.slotWidth;
    for(auto& slot:r.slots)width=std::max({width,cycleTextWidth(slot.label,font),cycleTextWidth(slot.energy,font)});
    return std::hypot(width/2,r.slotHeight/2+mechanismCaptionHeight(r,style));
}
Vec mechanismCycleTransitionPosition(const Reaction& r,size_t i,const Style& style,double radius){
    double font=mechanismFontSize(r,style),angle=-pi/2+2*pi*(i+.5)/std::max(size_t(1),r.slots.size());
    Vec radial{std::cos(angle),std::sin(angle)};auto box=cycleTransitionBox(r,i,font);
    double offset=font*2+std::abs(radial.x)*box.w/2+std::abs(radial.y)*box.h/2;
    return r.p+radial*(radius+offset)-Vec{0,box.y+box.h/2};
}
double mechanismCycleRadius(const Reaction& r,const Style& style){
    double font=mechanismFontSize(r,style),node=mechanismCycleNodeRadius(r,style);
    double radius=(node+font*3)/std::sin(pi/std::max(size_t(2),r.slots.size()));
    auto addText=[&](std::vector<CycleBox>& boxes,const std::string& text,Vec at){if(!text.empty()){double w=cycleTextWidth(text,font);boxes.push_back({at.x-w/2,at.y,w,cycleTextHeight(text,font)});}};
    // Expand only until the actual upright text blocks and structure boxes clear.
    for(int attempt=0;attempt<160;attempt++){
        std::vector<CycleBox> boxes;addText(boxes,r.centerLabel,r.p-Vec{0,font*.65});
        for(size_t i=0;i<r.slots.size();i++){
            double angle=-pi/2+2*pi*i/r.slots.size();Vec c=r.p+Vec{std::cos(angle),std::sin(angle)}*radius;
            boxes.push_back({c.x-r.slotWidth/2,c.y-r.slotHeight/2,r.slotWidth,r.slotHeight});
            double y=c.y+r.slotHeight/2+font*.6;addText(boxes,r.slots[i].label,{c.x,y});
            addText(boxes,r.slots[i].energy,{c.x,y+font*1.5*(1+std::count(r.slots[i].label.begin(),r.slots[i].label.end(),'\n'))});
            Vec mid=mechanismCycleTransitionPosition(r,i,style,radius);auto caption=transitionCaption(i<r.above.size()?r.above[i]:"");
            addText(boxes,caption.description,mid-Vec{0,font*(1.7+1.3*std::count(caption.description.begin(),caption.description.end(),'\n'))});
            addText(boxes,caption.name,mid+Vec{0,font*.65});
            if(i<r.below.size())addText(boxes,r.below[i],mid+Vec{0,font*.65+(caption.name.empty()?0:font*1.5*(1+std::count(caption.name.begin(),caption.name.end(),'\n')))});
        }
        bool overlap=false;double gap=font*.3;
        for(size_t i=0;i<boxes.size()&&!overlap;i++)for(size_t j=i+1;j<boxes.size();j++){auto a=boxes[i],b=boxes[j];if(a.x+a.w+gap>b.x&&b.x+b.w+gap>a.x&&a.y+a.h+gap>b.y&&b.y+b.h+gap>a.y){overlap=true;break;}}
        if(!overlap)return radius;
        radius=radius*1.06+font;
    }
    return radius;
}
static std::string trim(const std::string& s){auto a=s.find_first_not_of(" \t\r\n");return a==std::string::npos?"":s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);}
TransitionCaption transitionCaption(const std::string& value){
    auto separator=value.find('/');
    if(separator==std::string::npos)return {"",value,false};
    return {trim(value.substr(0,separator)),trim(value.substr(separator+1)),true};
}
std::string transitionCaptionText(const TransitionCaption& caption){
    return caption.separated||!caption.name.empty()?caption.name+" / "+caption.description:caption.description;
}
static std::vector<std::string> split(const std::string& s){
    std::vector<std::string> values;size_t begin=0;
    while(true){auto end=s.find('/',begin);values.push_back(trim(s.substr(begin,end==std::string::npos?end:end-begin)));if(end==std::string::npos)break;begin=end+1;}
    return values;
}
static std::string number(double v){std::ostringstream out;out.imbue(std::locale::classic());out<<v;return out.str();}
static double numeric(const std::string& s){
    std::istringstream in(s);in.imbue(std::locale::classic());double value;
    if(!(in>>value)||!std::isfinite(value)||std::abs(value)>1e6||(in>>std::ws,!in.eof()))throw std::runtime_error("올바른 숫자를 입력하세요.");
    return value;
}
static double spin(const std::string& s){auto text=trim(s);if(text.rfind("S",0)==0||text.rfind("s",0)==0){text=trim(text.substr(1));if(!text.empty()&&text[0]=='=')text=trim(text.substr(1));}
    double value=numeric(text);if(value<0||value>10||std::abs(value*2-std::round(value*2))>1e-8)throw std::runtime_error("S는 0, 0.5, 1, 1.5, 2처럼 0 이상의 정수·반정수로 입력하세요.");return value;}
std::vector<SpinEnergy> reactionEnergies(const std::string& value){
    std::vector<SpinEnergy> result;auto fields=split(value);
    for(size_t i=0;i<fields.size();i++)if(!fields[i].empty()){
        const auto& field=fields[i];auto colon=field.find(':');
        if(colon!=std::string::npos&&(field[0]=='S'||field[0]=='s'))result.push_back({spin(field.substr(0,colon)),trim(field.substr(colon+1))});
        else result.push_back({double(i),field});
    }
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.spin<b.spin;});return result;
}
std::string reactionEnergyList(std::vector<SpinEnergy> values){
    std::stable_sort(values.begin(),values.end(),[](const auto& a,const auto& b){return a.spin<b.spin;});
    // Retain the legacy representation for S=0,1,2 so old files remain compatible.
    bool standard=std::all_of(values.begin(),values.end(),[](const auto& v){return !trim(v.value).empty()&&(v.spin==0||v.spin==1||v.spin==2);});
    if(standard){std::vector<std::string> fields;for(auto& v:values)if(!trim(v.value).empty()){fields.resize(std::max(fields.size(),size_t(v.spin)+1));fields[size_t(v.spin)]=trim(v.value);}
        std::string result;for(size_t i=0;i<fields.size();i++){if(i)result+=" / ";result+=fields[i];}return result;}
    std::string result;for(auto& v:values){if(!result.empty())result+=" / ";result+="S="+number(v.spin)+":"+trim(v.value);}return result;
}
static std::vector<std::string> energyRow(std::string kind,std::string name,const std::string& value){
    auto energies=reactionEnergies(value);std::string spins,values;
    for(auto& e:energies){if(!spins.empty()){spins+=" / ";values+=" / ";}spins+=number(e.spin);values+=e.value;}
    return {std::move(kind),std::move(name),spins,values};
}
MechanismRows mechanismRows(const Reaction& reaction){
    MechanismRows rows;for(size_t i=0;i<reaction.slots.size();i++){
        rows.push_back(energyRow("중간체",reaction.slots[i].label,reaction.slots[i].energy));
        if(i+1<reaction.slots.size()||reaction.cyclic||i<reaction.above.size()||i<reaction.below.size())
            rows.push_back(energyRow("TS",i<reaction.above.size()?reaction.above[i]:"",i<reaction.below.size()?reaction.below[i]:""));
    }return rows;
}
Reaction mechanismGridData(const Reaction& original,const MechanismRows& rows){
    if(rows.empty()||rows.size()>60)throw std::runtime_error("중간체와 TS를 합해 1~60행 입력하세요. 빈칸은 그대로 둘 수 있습니다.");
    Reaction result=original;std::vector<size_t> matchedSlots;result.captionColors.clear();result.slots.clear();result.above.clear();result.below.clear();std::set<size_t> used;bool expectState=true;
    std::set<std::string> retainedNames;for(size_t i=0;i<rows.size();i+=2)if(rows[i].size()==4)retainedNames.insert(rows[i][1]);
    for(size_t i=0;i<rows.size();i++){
        if(rows[i].size()!=4)throw std::runtime_error("메커니즘 표는 구분·이름·S·에너지의 4열입니다.");
        const auto& row=rows[i];auto kind=trim(row[0]);bool state=kind.empty()?expectState:(kind=="중간체"||kind=="I"||kind=="INT"||kind=="R"||kind=="P");
        bool ts=kind=="TS"||kind=="ts"||kind=="전이상태";
        if(!kind.empty()&&!state&&!ts)throw std::runtime_error(std::to_string(i+1)+"행: 구분은 중간체 또는 TS입니다.");
        if(state!=expectState)throw std::runtime_error(std::to_string(i+1)+"행: 중간체 → TS → 중간체 순서로 배치하세요. 빈 구분은 자동으로 정합니다.");
        std::vector<SpinEnergy> energies;
        try{
            auto spins=split(row[2]),values=split(row[3]);bool hasSpins=!trim(row[2]).empty();
            if(hasSpins&&spins.size()!=values.size()&&!trim(row[3]).empty())throw std::runtime_error("S와 에너지의 개수를 맞추세요 (예: 0 / 2 와 0.0 / 13.4).");
            std::set<double> seen;
            for(size_t j=0,count=trim(row[2]).empty()&&trim(row[3]).empty()?0:std::max(spins.size(),values.size());j<count;j++){
                double s=hasSpins?spin(spins.at(j)):double(j);
                if(!seen.insert(s).second)throw std::runtime_error("같은 S를 중복해서 입력할 수 없습니다.");
                std::string v=j<values.size()?values[j]:"";
                if(!v.empty()){
                    // Existing nonnumeric annotations remain editable without migration loss.
                    std::string old=state?(result.slots.size()<original.slots.size()?original.slots[result.slots.size()].energy:""):(result.slots.size()-1<original.below.size()?original.below[result.slots.size()-1]:"");
                    bool legacy=false;for(auto& e:reactionEnergies(old))if(e.spin==s&&e.value==v)legacy=true;
                    if(!legacy&&v!="?"&&v!="?.?")numeric(v);
                }
                energies.push_back({s,v});
            }
        }catch(const std::exception& e){throw std::runtime_error(std::to_string(i+1)+"행: "+e.what());}
        auto energy=reactionEnergyList(energies);
        if(state){
            Slot slot;slot.label=row[1];slot.energy=energy;size_t n=result.slots.size(),match=original.slots.size();
            for(size_t j=0;j<original.slots.size();j++)if(!used.count(j)&&original.slots[j].label==slot.label){match=j;break;}
            if(match==original.slots.size()&&n<original.slots.size()&&!used.count(n)&&!retainedNames.count(original.slots[n].label))match=n;
            if(match<original.slots.size()){slot.members=original.slots[match].members;slot.startsRow=original.slots[match].startsRow;used.insert(match);}
            matchedSlots.push_back(match);result.slots.push_back(std::move(slot));
        }else{result.above.push_back(row[1]);result.below.push_back(energy);}
        expectState=!expectState;
    }
    for(auto& color:original.captionColors){
        if(color.role==10){result.captionColors.push_back(color);continue;}
        for(size_t n=0;n<matchedSlots.size();n++)if(matchedSlots[n]==size_t(color.index)){
            bool state=color.role==1||color.role==9;
            bool transition=(color.role==2||color.role==3||color.role==11)&&
                ((n+1<matchedSlots.size()&&matchedSlots[n+1]==matchedSlots[n]+1)||(original.cyclic&&n+1==matchedSlots.size()&&matchedSlots[n]+1==original.slots.size()));
            if(state||transition)result.captionColors.push_back({color.role,int(n),color.color});
        }
    }
    return result;
}
}

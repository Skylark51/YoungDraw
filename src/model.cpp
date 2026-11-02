#include "model.h"
#include <stdexcept>
#include <set>
#include <functional>

namespace yd {
static const std::vector<std::string> elements={"", "H","He","Li","Be","B","C","N","O","F","Ne","Na","Mg","Al","Si","P","S","Cl","Ar","K","Ca","Sc","Ti","V","Cr","Mn","Fe","Co","Ni","Cu","Zn","Ga","Ge","As","Se","Br","Kr","Rb","Sr","Y","Zr","Nb","Mo","Tc","Ru","Rh","Pd","Ag","Cd","In","Sn","Sb","Te","I","Xe","Cs","Ba","La","Ce","Pr","Nd","Pm","Sm","Eu","Gd","Tb","Dy","Ho","Er","Tm","Yb","Lu","Hf","Ta","W","Re","Os","Ir","Pt","Au","Hg","Tl","Pb","Bi","Po","At","Rn","Fr","Ra","Ac","Th","Pa","U","Np","Pu","Am","Cm","Bk","Cf","Es","Fm","Md","No","Lr","Rf","Db","Sg","Bh","Hs","Mt","Ds","Rg","Cn","Nh","Fl","Mc","Lv","Ts","Og"};
std::string symbol(int n) { return n>0 && n<int(elements.size()) ? elements[n] : "?"; }
int atomicNumber(const std::string& s) { auto i=std::find(elements.begin()+1,elements.end(),s); return i==elements.end()?0:int(i-elements.begin()); }
// Recognize element + canonical oxidation number, without changing ordinary Roman words.
std::string chemicalScripts(const std::string& text) {
    static const std::vector<std::string> roman={"I","II","III","IV","V","VI","VII","VIII","IX","X","XI","XII","XIII","XIV","XV","XVI","XVII","XVIII","XIX","XX"};
    auto letter=[](char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z');};
    std::string result;
    for(size_t i=0;i<text.size();){
        if((text[i]=='^'||text[i]=='_')&&i+1<text.size()&&text[i+1]=='{'){
            auto end=text.find('}',i+2);if(end!=std::string::npos){result+=text.substr(i,end-i+1);i=end+1;continue;}
        }
        if(!letter(text[i])){result+=text[i++];continue;}
        size_t end=i;while(end<text.size()&&letter(text[end]))end++;
        auto token=text.substr(i,end-i);bool converted=false;
        if(token.find_first_not_of("IVX")!=std::string::npos)for(size_t prefix:{size_t(0),size_t(1)}){
            if(prefix&&token[0]!='L')continue;
            for(size_t n:{size_t(2),size_t(1)}){
                if(token.size()<=prefix+n||!atomicNumber(token.substr(prefix,n)))continue;
                auto val=token.substr(prefix+n);if(std::find(roman.begin(),roman.end(),val)!=roman.end()){
                    result+=token.substr(0,prefix+n)+"^{"+val+"}";converted=true;break;
                }
            }if(converted)break;
        }
        result+=converted?"":token;i=end;
    }
    return result;
}
int order(BondKind k) { return k==BondKind::Double?2:k==BondKind::Triple?3:1; }
bool Document::isLocked(int id) const {
    if(graphsLocked)for(const auto& p:plots)if(p.id==id)return true;
    if(imagesLocked)for(const auto& o:objects)if(o.id==id&&o.kind==ObjectKind::Image)return true;
    return false;
}
std::vector<int> Document::editableIds() const {
    auto ids=allIds();ids.erase(std::remove_if(ids.begin(),ids.end(),[&](int id){return isLocked(id);}),ids.end());return ids;
}
int Document::addAtom(Vec p,int e) {
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||e<1||e>118) throw std::runtime_error("Invalid atom");
    int id=nextId++; atoms.push_back({id,p,e}); return id;
}
Atom* Document::atom(int id) { for(auto& a:atoms) if(a.id==id)return &a; return nullptr; }
const Atom* Document::atom(int id) const { for(auto& a:atoms) if(a.id==id)return &a; return nullptr; }
Bond* Document::bond(int id) { for(auto& b:bonds) if(b.id==id)return &b; return nullptr; }
const Bond* Document::bond(int id) const { for(auto& b:bonds) if(b.id==id)return &b; return nullptr; }
int Document::addBond(int a,int b,BondKind k) {
    if(a==b||!atom(a)||!atom(b)) throw std::runtime_error("Invalid bond endpoints");
    for(const auto& v:bonds)if((v.a==a&&v.b==b)||(v.a==b&&v.b==a))return v.id;
    int id=nextId++; bonds.push_back({id,a,b,k});return id;
}
int Document::atomAt(Vec p,double t) const { int id=0;for(const auto& a:atoms) { double d=(a.p-p).length();if(d<t){t=d;id=a.id;}}return id; }
int Document::bondAt(Vec p,double t) const {
    int id=0;for(const auto& b:bonds){ Vec a=atom(b.a)->p,v=atom(b.b)->p-a;double ll=v.x*v.x+v.y*v.y;
        if(ll<1e-12)continue; double u=std::clamp(((p-a).x*v.x+(p-a).y*v.y)/ll,0.,1.);double d=(p-a-v*u).length();if(d<t){t=d;id=b.id;}}
    return id;
}
void Document::eraseBond(int id){bonds.erase(std::remove_if(bonds.begin(),bonds.end(),[id](const Bond& b){return b.id==id;}),bonds.end());}
void Document::eraseAtom(int id){bonds.erase(std::remove_if(bonds.begin(),bonds.end(),[id](const Bond& b){return b.a==id||b.b==id;}),bonds.end());atoms.erase(std::remove_if(atoms.begin(),atoms.end(),[id](const Atom& a){return a.id==id;}),atoms.end());}
void Document::ring(Vec c,int n,bool alt){
    if(n<3||n>12)throw std::runtime_error("Invalid ring size");
    double r=style.bondLength*(n==6&&alt?benzeneBondScale:1.)/(2*std::sin(pi/n));std::vector<int> ids;
    for(int i=0;i<n;i++){double angle=-pi/2+pi/n+2*pi*i/n;ids.push_back(addAtom(c+Vec{std::cos(angle),std::sin(angle)}*r));}
    for(int i=0;i<n;i++)addBond(ids[i],ids[(i+1)%n],alt&&i%2==0?BondKind::Double:BondKind::Single);
}
// Dative bonds represent coordination and do not consume the donor's
// conventional covalent valence. Metal oxidation states are not inferred here.
static int targetValence(const Atom& a){
    switch(a.element){
    case 1:case 9:case 17:case 35:case 53:return 1;
    case 5:return 3;
    case 6:case 14:return 4;
    case 7:return a.charge>0?4:a.charge<0?2:3;
    case 8:return a.charge>0?3:a.charge<0?1:2;
    case 15:return 3;
    case 16:return 2;
    default:return 0; // Coordination metals and unspecified elements.
    }
}
static int valenceLimit(const Atom& a){
    if(a.element==15)return 5;
    if(a.element==16)return 6;
    return targetValence(a);
}
static int covalentValence(const Document& d,const Atom& a){
    int total=0,aromatic=0,other=0;
    for(const auto& b:d.bonds)if(b.a==a.id||b.b==a.id){
        if(b.kind==BondKind::Dative)continue;
        total+=order(b.kind);
        if(b.kind==BondKind::Aromatic)++aromatic;else ++other;
    }
    // Two aromatic strokes represent three bond-order units at pyridine-like N
    // and sp2 carbon. Pyrrole-like [nH] / substituted N instead contributes
    // its lone pair and retains two sigma bond orders.
    if(aromatic>=2&&(a.element==6||
       (a.element==7&&(a.charge>0||(other==0&&a.explicitH<=0)))))++total;
    return total;
}
int Document::hydrogens(int id) const {
    const Atom* a=atom(id);if(!a)return 0;
    if(a->explicitH>=0)return a->explicitH;
    const int target=targetValence(*a),used=covalentValence(*this,*a);
    if(!target)return 0;
    if((a->element==15&&used>3)||(a->element==16&&used>2))return 0;
    return std::max(0,target-used);
}
std::vector<std::string> Document::validate() const {
    std::vector<std::string> warnings;
    for(const auto& a:atoms){
        const int limit=valenceLimit(a);
        if(limit&&covalentValence(*this,a)+std::max(0,a.explicitH)>limit)
            warnings.push_back(symbol(a.element)+" #"+std::to_string(a.id)+": check valence");
    }
    return warnings;
}
Vec snapped(Vec a,Vec b,double len,bool free){Vec d=b-a;if(free)return b;double angle=d.length()<.1?-pi/6:std::atan2(d.y,d.x);angle=std::round(angle/(pi/6))*(pi/6);return a+Vec{std::cos(angle),std::sin(angle)}*len;}
double bondLengthScale(int first,int second,BondKind kind){
    // Pyykko/Atsumi additive radii (pm), normalized to a C-C single bond.
    // A 2D drawing convention, not a molecular geometry optimization.
    auto radius=[&](int e)->double{
        switch(e){
        case 6:return kind==BondKind::Double?67:kind==BondKind::Triple?60:75;
        case 7:return kind==BondKind::Double?60:kind==BondKind::Triple?54:71;
        case 8:return kind==BondKind::Double?57:kind==BondKind::Triple?53:63;
        default:return 0;
        }
    };
    if(kind==BondKind::Dative||kind==BondKind::Aromatic)return 1;
    double a=radius(first),b=radius(second);
    return a&&b?(a+b)/150.:1.; // Preserve metal/unsupported-element conventions.
}
static void adjustTerminalBond(Document& d,const Bond& b,double previousScale){
    auto* a=d.atom(b.a);auto* z=d.atom(b.b);if(!a||!z||a->protect||z->protect)return;
    int na=0,nz=0;for(auto& edge:d.bonds){na+=edge.a==a->id||edge.b==a->id;nz+=edge.a==z->id||edge.b==z->id;}
    if(na!=1&&nz!=1)return; // Ring and internal coordinates change only via cleanup.
    auto* terminal=nz==1?z:a;auto* anchor=terminal==z?a:z;
    double scale=bondLengthScale(a->element,z->element,b.kind)/previousScale;
    terminal->p=anchor->p+(terminal->p-anchor->p)*scale;
}
void setBondKind(Document& d,int id,BondKind kind){
    auto* b=d.bond(id);if(!b)return;auto* a=d.atom(b->a);auto* z=d.atom(b->b);if(!a||!z)return;
    double old=bondLengthScale(a->element,z->element,b->kind);b->kind=kind;
    adjustTerminalBond(d,*b,old);b->customOrder.clear();b->customDisplay.clear();
}
void setAtomElement(Document& d,int id,int element){
    auto* a=d.atom(id);if(!a)return;int old=a->element;if(old==element)return;
    std::vector<std::pair<int,double>> affected;
    for(auto& b:d.bonds)if(b.a==id||b.b==id){auto* other=d.atom(b.a==id?b.b:b.a);if(other)affected.push_back({b.id,bondLengthScale(old,other->element,b.kind)});}
    a->element=element;
    for(auto& edge:affected)adjustTerminalBond(d,*d.bond(edge.first),edge.second);
}
double defaultBondLength(const Document& d,int id,BondKind kind,int element){
    auto* atom=d.atom(id);double length=d.style.bondLength*bondLengthScale(atom?atom->element:6,element,kind);
    if(kind!=BondKind::Single||element!=6||!atom||atom->element!=6)return length;
    std::set<int> path{id};std::function<bool(int,int,int)> walk=[&](int at,int depth,int unsaturated){
        for(auto& b:d.bonds){int next=b.a==at?b.b:b.b==at?b.a:0;if(!next||d.atom(next)->element!=6)continue;int count=unsaturated+(b.kind==BondKind::Double||b.kind==BondKind::Aromatic);
            if(next==id){if(depth==6&&count>=3)return true;continue;}if(depth>=6||path.count(next))continue;path.insert(next);bool found=walk(next,depth+1,count);path.erase(next);if(found)return true;
        }return false;
    };
    return d.style.bondLength*(walk(id,1,0)?benzeneConnectorScale:1.);
}
std::vector<int> addOrbital(Document& d,Vec center,Vec tip,OrbitalKind kind,int phaseStyle){
    Vec axis=tip-center;double length=axis.length();
    if(!std::isfinite(length)||length<.01||!std::isfinite(center.x)||!std::isfinite(center.y))throw std::runtime_error("Invalid orbital size");
    if(int(kind)<0||int(kind)>3||phaseStyle<0||phaseStyle>2)throw std::runtime_error("Invalid orbital style");
    Vec u=axis*(1/length);std::vector<int> members;
    auto base=[&](bool positive){Object o;o.id=d.nextId++;o.width=d.style.lineWidth*.7;o.closed=true;o.text="YoungDraw.Orbital:"+std::to_string(int(kind))+":"+std::to_string(phaseStyle);
        o.filled=phaseStyle==2||(phaseStyle==0&&positive);o.color=phaseStyle==2?(positive?"#2878b5":"#d6534d"):"#000000";return o;};
    auto lobe=[&](Vec direction,double size,bool positive){Object o=base(positive);o.kind=ObjectKind::Curve;
        Vec v=direction*size,n{-v.y,v.x};
        o.points={center,center+v*.35+n*.6,center+v+n*.55,center+v,
                  center+v-n*.55,center+v*.35-n*.6,center};
        members.push_back(o.id);d.objects.push_back(std::move(o));};
    if(kind==OrbitalKind::S){Object o=base(true);o.kind=ObjectKind::Ellipse;o.points={center-Vec{length,length},center+Vec{length,length}};members.push_back(o.id);d.objects.push_back(std::move(o));}
    else if(kind==OrbitalKind::D){Vec n{-u.y,u.x};lobe(u,length,true);lobe(n,length,false);lobe(u*-1,length,true);lobe(n*-1,length,false);}
    else {lobe(u,length,true);lobe(u*-1,kind==OrbitalKind::Hybrid?length*.32:length,false);}
    Group group;group.id=d.nextId++;group.members=members;d.groups.push_back(std::move(group));return members;
}
Vec bondEndpoint(const Document& d,int id,Vec desired,BondKind kind,int element){
    double length=defaultBondLength(d,id,kind,element);
    const Atom* a=d.atom(id);if(!a)return desired;std::vector<Vec> adjacent;bool linear=kind==BondKind::Triple;int doubles=kind==BondKind::Double?1:0;
    for(auto& b:d.bonds)if(b.a==id||b.b==id){adjacent.push_back(d.atom(b.a==id?b.b:b.a)->p-a->p);linear=linear||b.kind==BondKind::Triple;if(b.kind==BondKind::Double)doubles++;}
    linear=linear||doubles==2;std::vector<double> angles;
    if(adjacent.empty())return snapped(a->p,desired,length);
    double base=std::atan2(adjacent[0].y,adjacent[0].x);
    if(adjacent.size()==1){if(linear)angles={base+pi};else angles={base+2*pi/3,base-2*pi/3};}
    else {Vec outward;for(Vec v:adjacent)outward=outward-v*(1/std::max(.001,v.length()));if(outward.length()>.01)angles.push_back(std::atan2(outward.y,outward.x));for(int i=0;i<12;i++)angles.push_back(i*pi/6);}
    double best=1e100;Vec result;Vec target=desired-a->p;
    for(double angle:angles){Vec u{std::cos(angle),std::sin(angle)},p=a->p+u*length;double score=target.length()>.01?(u-target*(1/target.length())).length():0;
        for(auto& other:d.atoms)if(other.id!=id){double gap=(p-other.p).length()/length;if(gap<.9)score+=1000*(.9-gap);else score+=.01/(gap*gap);}
        for(Vec v:adjacent){double dot=(u.x*v.x+u.y*v.y)/std::max(.001,v.length());if(dot>.2)score+=10*(dot-.2);if(a->element==6&&std::abs(dot)<.08)score+=20;}
        if(score<best){best=score;result=p;}}
    return result;
}
int delocalizeBonds(Document& d,const std::vector<int>& selection){
    std::set<int> ids(selection.begin(),selection.end());int count=0;
    for(auto& b:d.bonds)if(ids.count(b.id)||(ids.count(b.a)&&ids.count(b.b))){b.kind=BondKind::Aromatic;b.doubleSide=0;b.customOrder.clear();b.customDisplay.clear();count++;}return count;
}
void History::checkpoint(const Document& d){if(undo_.size()>=100)undo_.erase(undo_.begin());undo_.push_back(d);redo_.clear();}
bool History::undo(Document& d){if(undo_.empty())return false;redo_.push_back(d);d=undo_.back();undo_.pop_back();return true;}
bool History::redo(Document& d){if(redo_.empty())return false;undo_.push_back(d);d=redo_.back();redo_.pop_back();return true;}
Document demo(){
    Document d;double l=d.style.bondLength;d.ring({70,60},6,true);
    int a=d.atoms[1].id;Vec v{std::cos(pi/6)*l,l/2};int c=d.addAtom(d.atom(a)->p+v*benzeneConnectorScale);d.addBond(a,c);
    int o=d.addAtom(d.atom(c)->p+Vec{0,defaultBondLength(d,c,BondKind::Double,8)},8);d.addBond(c,o,BondKind::Double);
    int n=d.addAtom(d.atom(c)->p+Vec{v.x,-v.y}*bondLengthScale(6,7,BondKind::Single),7);d.addBond(c,n);
    int c2=d.addAtom(d.atom(n)->p+v*bondLengthScale(7,6,BondKind::Single));d.addBond(n,c2);return d;
}
}

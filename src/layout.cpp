#include "model.h"
#include "plot_data.h"
#include "scene.h"
#include "mechanism_data.h"
#include <map>
#include <queue>
#include <numeric>
#include <stdexcept>
namespace yd {
static std::vector<std::vector<int>> cycles(const Document& d,const std::vector<int>& component);
static std::map<int,double> bondTargets(const Document& d,const std::vector<std::vector<int>>& rings);
static void readableRings(Document& d,const std::vector<int>& comp,const std::vector<std::vector<int>>& rings);
static bool planarLayout(Document& d,const std::vector<int>& comp,const std::vector<std::vector<int>>& rings);
static bool has(const std::vector<int>& ids,int id){return std::find(ids.begin(),ids.end(),id)!=ids.end();}
std::vector<int> Document::component(int id) const {
    if(!atom(id))return {};std::vector<int> result{id};for(size_t i=0;i<result.size();++i)for(const auto& b:bonds){int n=b.a==result[i]?b.b:b.b==result[i]?b.a:0;if(n&&!has(result,n))result.push_back(n);}return result;
}
std::vector<int> Document::allIds() const {std::vector<int> ids;for(auto& a:atoms)ids.push_back(a.id);for(auto& b:bonds)ids.push_back(b.id);for(auto& o:objects)ids.push_back(o.id);for(auto& r:reactions)ids.push_back(r.id);for(auto& p:plots)ids.push_back(p.id);return ids;}
static void expand(const Document& d,std::set<int>& ids){bool more=true;while(more){size_t n=ids.size();for(auto& g:d.groups)if(ids.count(g.id)||std::any_of(g.members.begin(),g.members.end(),[&](int id){return ids.count(id)>0;})){ids.insert(g.id);ids.insert(g.members.begin(),g.members.end());}
    for(auto& r:d.reactions)if(ids.count(r.id)){for(auto& s:r.slots)ids.insert(s.members.begin(),s.members.end());for(auto& l:r.links)ids.insert(l.members.begin(),l.members.end());}
    for(auto& p:d.plots)if(ids.count(p.id))for(auto& s:p.states){ids.insert(s.above.begin(),s.above.end());ids.insert(s.below.begin(),s.below.end());}more=ids.size()!=n;}}
static std::set<int> editableExpansion(const Document& d,const std::vector<int>& selection){
    std::set<int> ids;for(int id:selection)if(!d.isLocked(id))ids.insert(id);
    expand(d,ids);for(auto i=ids.begin();i!=ids.end();)if(d.isLocked(*i))i=ids.erase(i);else ++i;
    return ids;
}
static bool missingAnchor(const Document& d,const Object& o,const std::set<int>& ids){
    auto missingBond=[&](int id){auto* b=d.bond(id);return id&&(!b||!ids.count(b->a)||!ids.count(b->b));};
    return (o.sourceAtom&&!ids.count(o.sourceAtom))||(o.targetAtom&&!ids.count(o.targetAtom))||missingBond(o.sourceBond)||missingBond(o.targetBond);
}
void Document::translate(const std::vector<int>& selection,Vec v){auto ids=editableExpansion(*this,selection);for(auto& o:objects)if(ids.count(o.id)&&o.autoRoute&&missingAnchor(*this,o,ids)){o.points=routeArrow(*this,o);o.sourceAtom=o.targetAtom=o.sourceBond=o.targetBond=0;o.autoRoute=false;}for(auto& a:atoms)if(ids.count(a.id))a.p=a.p+v;for(auto& o:objects)if(ids.count(o.id))for(auto& p:o.points)p=p+v;for(auto& r:reactions)if(ids.count(r.id))r.p=r.p+v;for(auto& p:plots)if(ids.count(p.id))p.p=p.p+v;}
std::vector<int> scaleStructures(Document& d,const std::vector<int>& selection,double factor,std::optional<Vec> center){
    if(selection.empty())throw std::runtime_error("크기를 조정할 구조·이미지를 먼저 선택하세요.");
    if(!std::isfinite(factor)||factor<.01||factor>10)throw std::runtime_error("배율은 1~1000%로 입력하세요.");
    // Scaling follows the explicit selection, even within a molecule or group.
    std::set<int> ids;for(int id:selection)if(!d.isLocked(id))ids.insert(id);
    bool expanded=true;while(expanded){size_t before=ids.size();for(auto& g:d.groups)if(ids.count(g.id))for(int id:g.members)if(!d.isLocked(id))ids.insert(id);expanded=before!=ids.size();}
    for(auto& r:d.reactions)if(ids.count(r.id))throw std::runtime_error("메커니즘의 구조 삽입 영역이나 원자·결합을 선택하세요.");
    for(auto& p:d.plots)if(ids.count(p.id))throw std::runtime_error("그래프의 구조 삽입 영역이나 원자·결합을 선택하세요.");
    for(auto& b:d.bonds)if(ids.count(b.id)){ids.insert(b.a);ids.insert(b.b);}
    for(auto& b:d.bonds)if(ids.count(b.a)&&ids.count(b.b))ids.insert(b.id);
    double l=1e20,t=1e20,r=-1e20,b=-1e20;auto bound=[&](Vec p){l=std::min(l,p.x);r=std::max(r,p.x);t=std::min(t,p.y);b=std::max(b,p.y);};
    for(auto& a:d.atoms)if(ids.count(a.id))bound(a.p);for(auto& o:d.objects)if(ids.count(o.id))for(Vec p:o.points)bound(p);
    if(r<l)throw std::runtime_error("크기를 조정할 구조·이미지를 먼저 선택하세요.");
    if(factor==1)return {ids.begin(),ids.end()};
    Vec anchor=center.value_or(Vec{(l+r)/2,(t+b)/2});
    for(auto& o:d.objects)if(ids.count(o.id)&&o.autoRoute&&missingAnchor(d,o,ids)){o.points=routeArrow(d,o);o.autoRoute=false;o.sourceAtom=o.targetAtom=o.sourceBond=o.targetBond=0;}
    for(auto& a:d.atoms)if(ids.count(a.id)){a.p=anchor+(a.p-anchor)*factor;a.labelOffset=a.labelOffset*factor;a.drawingScale*=factor;for(auto& run:a.labelRuns)run.size*=factor;if(a.annotationManual)a.annotationOffset=a.annotationOffset*factor;}
    for(auto& bond:d.bonds)if(ids.count(bond.id))bond.drawingScale*=factor;
    for(auto& o:d.objects)if(ids.count(o.id)){for(auto& p:o.points)p=anchor+(p-anchor)*factor;if(o.kind!=ObjectKind::Image){o.width*=factor;if(o.cross.enabled){o.cross.width*=factor;o.cross.size*=factor;}}if(o.kind==ObjectKind::Text){o.fontSize*=factor;for(auto& run:o.runs)run.size*=factor;}}
    return {ids.begin(),ids.end()};
}
std::vector<int> transformStructures(Document& d,const std::vector<int>& selection,double degrees,bool flipX,bool flipY){
    if(selection.empty())throw std::runtime_error("회전·반전할 구조를 먼저 선택하세요.");
    if(!std::isfinite(degrees)||std::abs(degrees)>36000)throw std::runtime_error("회전 각도는 -36000~36000도 범위로 입력하세요.");
    auto ids=editableExpansion(d,selection);
    for(auto& r:d.reactions)if(ids.count(r.id))throw std::runtime_error("반응 블록 안에서 회전할 원자·결합을 선택하세요.");
    for(auto& p:d.plots)if(ids.count(p.id))throw std::runtime_error("그래프 안에서 회전할 구조의 원자·결합을 선택하세요.");
    for(auto& b:d.bonds)if(ids.count(b.id)){ids.insert(b.a);ids.insert(b.b);}
    std::vector<int> seeds;for(auto& a:d.atoms)if(ids.count(a.id))seeds.push_back(a.id);
    for(int seed:seeds){auto comp=d.component(seed);ids.insert(comp.begin(),comp.end());}
    for(auto& b:d.bonds)if(ids.count(b.a)&&ids.count(b.b))ids.insert(b.id);
    for(auto& o:d.objects)if(ids.count(o.id)&&(o.kind==ObjectKind::Image||o.kind==ObjectKind::Ellipse||o.kind==ObjectKind::Rectangle)&&o.text.rfind("YoungDraw.Orbital:",0)!=0)throw std::runtime_error("이미지·타원·사각형을 제외하고 원자·결합을 선택하세요.");
    double l=1e20,t=1e20,r=-1e20,b=-1e20;Vec sum;size_t count=0;auto bound=[&](Vec p){sum=sum+p;count++;l=std::min(l,p.x);r=std::max(r,p.x);t=std::min(t,p.y);b=std::max(b,p.y);};
    for(auto& a:d.atoms)if(ids.count(a.id))bound(a.p);
    for(auto& o:d.objects)if(ids.count(o.id))for(Vec p:o.points)bound(p);
    if(r<l)throw std::runtime_error("회전할 원자·결합·텍스트·화살표를 선택하세요.");
    Vec center=sum*(1./count);double angle=std::remainder(degrees,360.)*pi/180,c=std::cos(angle),s=std::sin(angle);
    auto transform=[&](Vec p){Vec v=p-center;if(flipX)v.x=-v.x;if(flipY)v.y=-v.y;return center+Vec{c*v.x-s*v.y,s*v.x+c*v.y};};
    if(!flipX&&!flipY&&std::abs(std::remainder(degrees,360.))<1e-12)return {ids.begin(),ids.end()};
    // Freeze selected curves before moving their anchors, including arrows
    // attached to atoms outside the transformed selection.
    for(auto& o:d.objects)if(ids.count(o.id)&&o.autoRoute){o.points=routeArrow(d,o);o.autoRoute=false;o.sourceAtom=o.targetAtom=o.sourceBond=o.targetBond=0;}
    for(auto& a:d.atoms)if(ids.count(a.id))a.p=transform(a.p);
    for(auto& o:d.objects)if(ids.count(o.id)){
        if(o.kind==ObjectKind::Ellipse&&o.text.rfind("YoungDraw.Orbital:",0)==0&&o.points.size()==2){
            Vec c=transform((o.points[0]+o.points[1])*.5),radius=(o.points[1]-o.points[0])*.5;o.points={c-radius,c+radius};
        }else for(auto& p:o.points)p=transform(p); // Labels stay upright and readable.
    }
    if(flipX!=flipY)for(auto& bond:d.bonds)if(ids.count(bond.a)&&ids.count(bond.b)){
        bond.doubleSide=-bond.doubleSide;
        if(bond.kind==BondKind::Wedge)bond.kind=BondKind::Hash;else if(bond.kind==BondKind::Hash)bond.kind=BondKind::Wedge;
    }
    return {ids.begin(),ids.end()};
}
bool alignObjects(Document& d,const std::vector<int>& selection,AlignMode mode){
    // A connected molecule, integral group, reaction or graph is one rigid part.
    std::vector<std::set<int>> units;
    for(int selected:selection){
        auto ids=editableExpansion(d,{selected});size_t old=0;
        while(old!=ids.size()){old=ids.size();for(auto& a:d.atoms)if(ids.count(a.id)){auto comp=d.component(a.id);ids.insert(comp.begin(),comp.end());}expand(d,ids);}
        for(auto& b:d.bonds)if(ids.count(b.a)&&ids.count(b.b))ids.insert(b.id);
        for(auto it=ids.begin();it!=ids.end();)if(d.isLocked(*it))it=ids.erase(it);else ++it;
        if(ids.empty())continue;
        for(auto it=units.begin();it!=units.end();){bool overlap=std::any_of(ids.begin(),ids.end(),[&](int id){return it->count(id)>0;});
            if(overlap){ids.insert(it->begin(),it->end());it=units.erase(it);it=units.begin();}else ++it;}
        units.push_back(std::move(ids));
    }
    if(units.size()<2)return false;
    struct Part{std::vector<int> ids;Rect box;};std::vector<Part> parts;
    for(auto& ids:units){std::vector<int> list(ids.begin(),ids.end());parts.push_back({list,makeScene(d.subset(list)).bounds});}
    bool horizontal=mode==AlignMode::Left||mode==AlignMode::CenterX||mode==AlignMode::Right||mode==AlignMode::DistributeHorizontal;
    auto low=[&](const Part& p){return horizontal?p.box.x:p.box.y;};
    auto size=[&](const Part& p){return horizontal?p.box.w:p.box.h;};
    double first=1e20,last=-1e20,total=0;for(auto& p:parts){first=std::min(first,low(p));last=std::max(last,low(p)+size(p));total+=size(p);}
    bool distribute=mode==AlignMode::DistributeHorizontal||mode==AlignMode::DistributeVertical;
    if(distribute&&parts.size()<3)return false;
    if(distribute)std::stable_sort(parts.begin(),parts.end(),[&](const Part& a,const Part& b){return low(a)+size(a)/2<low(b)+size(b)/2;});
    double next=first,gap=distribute?(last-first-total)/(parts.size()-1):0;bool changed=false;
    for(auto& p:parts){double delta=0;
        if(distribute){delta=next-low(p);next+=size(p)+gap;}
        else if(mode==AlignMode::Left||mode==AlignMode::Top)delta=first-low(p);
        else if(mode==AlignMode::Right||mode==AlignMode::Bottom)delta=last-low(p)-size(p);
        else delta=(first+last)/2-low(p)-size(p)/2;
        if(std::abs(delta)>.000001){d.translate(p.ids,horizontal?Vec{delta,0}:Vec{0,delta});changed=true;}
    }return changed;
}
void Document::erase(const std::vector<int>& selection){auto ids=editableExpansion(*this,selection);
    for(auto& b:bonds)if(ids.count(b.a)||ids.count(b.b))ids.insert(b.id);
    for(auto& o:objects)if(o.autoRoute&&((o.sourceAtom&&ids.count(o.sourceAtom))||(o.targetAtom&&ids.count(o.targetAtom))||(o.sourceBond&&ids.count(o.sourceBond))||(o.targetBond&&ids.count(o.targetBond)))){o.points=routeArrow(*this,o);o.sourceAtom=o.targetAtom=o.sourceBond=o.targetBond=0;o.autoRoute=false;}
    bonds.erase(std::remove_if(bonds.begin(),bonds.end(),[&](const Bond& b){return ids.count(b.id)||ids.count(b.a)||ids.count(b.b);}),bonds.end());
    auto remove=[&](auto& entries){entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const auto& e){return ids.count(e.id)>0;}),entries.end());};
    remove(atoms);remove(objects);remove(reactions);remove(plots);remove(groups);
    auto prune=[&](std::vector<int>& members){members.erase(std::remove_if(members.begin(),members.end(),[&](int id){return ids.count(id)>0;}),members.end());};
    for(auto& r:reactions){for(auto& s:r.slots)prune(s.members);for(auto& l:r.links)prune(l.members);}for(auto& p:plots)for(auto& s:p.states){prune(s.above);prune(s.below);}for(auto& g:groups)prune(g.members);
}
Document Document::subset(const std::vector<int>& selection) const {
    Document out;out.style=style;auto ids=editableExpansion(*this,selection);
    for(auto& a:atoms)if(ids.count(a.id))out.atoms.push_back(a);
    for(auto& b:bonds)if(ids.count(b.a)&&ids.count(b.b))out.bonds.push_back(b);
    for(auto& o:objects)if(ids.count(o.id)){Object copy=o;if(missingAnchor(*this,copy,ids)){copy.points=routeArrow(*this,o);copy.sourceAtom=copy.targetAtom=copy.sourceBond=copy.targetBond=0;copy.autoRoute=false;}out.objects.push_back(copy);}
    for(auto& r:reactions)if(ids.count(r.id))out.reactions.push_back(r);
    for(auto& p:plots)if(ids.count(p.id))out.plots.push_back(p);
    for(auto& g:groups)if(ids.count(g.id))out.groups.push_back(g);
    for(int id:out.allIds())out.nextId=std::max(out.nextId,id+1);for(auto& g:out.groups)out.nextId=std::max(out.nextId,g.id+1);return out;
}
void Atom::preserveStyle(const Style& from,const Style& to){
    if(!labelSize&&from.fontSize!=to.fontSize)labelSize=from.fontSize;
    if(!labelMargin&&from.margin!=to.margin)labelMargin=from.margin;
    if(!labelFace&&from.labelFace!=to.labelFace)labelFace=from.labelFace;
}
void Bond::preserveStyle(const Style& from,const Style& to){
    if(!lineWidth&&from.lineWidth!=to.lineWidth)lineWidth=from.lineWidth;
    if(!boldWidth&&from.boldWidth!=to.boldWidth)boldWidth=from.boldWidth;
    if(!hashSpacing&&from.hashSpacing!=to.hashSpacing)hashSpacing=from.hashSpacing;
    if(!doubleGap&&from.bondLength*from.bondSpacing!=to.bondLength*to.bondSpacing)
        doubleGap=from.bondLength*from.bondSpacing;
}
void Document::rebaseStructureStyle(const Style& target){
    for(auto& a:atoms)a.preserveStyle(style,target);
    for(auto& b:bonds)b.preserveStyle(style,target);
    style=target;
}
void Document::append(const Document& other,Vec delta){
    std::map<int,int> mapping;for(int id:other.allIds())mapping[id]=nextId++;for(auto& g:other.groups)mapping[g.id]=nextId++;
    auto remap=[&](std::vector<int>& ids){for(int& id:ids)id=mapping.at(id);};
    for(auto a:other.atoms){a.id=mapping[a.id];a.p=a.p+delta;a.preserveStyle(other.style,style);atoms.push_back(a);}
    for(auto b:other.bonds){b.id=mapping[b.id];b.a=mapping[b.a];b.b=mapping[b.b];b.preserveStyle(other.style,style);bonds.push_back(b);}
    for(auto o:other.objects){o.sourceAtom=mapping.count(o.sourceAtom)?mapping[o.sourceAtom]:0;o.targetAtom=mapping.count(o.targetAtom)?mapping[o.targetAtom]:0;o.sourceBond=mapping.count(o.sourceBond)?mapping[o.sourceBond]:0;o.targetBond=mapping.count(o.targetBond)?mapping[o.targetBond]:0;o.id=mapping[o.id];for(auto& p:o.points)p=p+delta;objects.push_back(o);}
    for(auto r:other.reactions){r.id=mapping[r.id];r.p=r.p+delta;for(auto& s:r.slots)remap(s.members);for(auto& l:r.links)remap(l.members);reactions.push_back(r);}
    for(auto p:other.plots){p.id=mapping[p.id];p.p=p.p+delta;for(auto& s:p.states){remap(s.above);remap(s.below);}plots.push_back(p);}
    for(auto g:other.groups){g.id=mapping[g.id];remap(g.members);groups.push_back(g);}
}
void Document::attachRing(int atomId,int bondId,int n,bool alt){
    if(n<3||n>9)return;
    if(bondId){auto b=bond(bondId);if(!b)return;int first=b->a,second=b->b;Vec a=atom(first)->p,z=atom(second)->p,u=z-a;double len=u.length();if(len<1e-6)return;Vec normal{-u.y/len,u.x/len};
        double height=len/(2*std::tan(pi/n));Vec mid=(a+z)*.5;double score[2]={};
        for(int side=0;side<2;side++){Vec c=mid+normal*(height*(side?1:-1));for(auto& at:atoms)if(at.id!=first&&at.id!=second)score[side]+=1/(1+(at.p-c).length());}
        double sign=score[0]<score[1]?-1:1;Vec center=mid+normal*(height*sign);double angle=std::atan2(a.y-center.y,a.x-center.x),turn=sign*2*pi/n;std::vector<int> ids{first,second};
        // Choose the rotation that reaches the existing second endpoint.
        Vec next=center+Vec{std::cos(angle+turn),std::sin(angle+turn)}*(a-center).length();if((next-z).length()>.01)turn=-turn;
        for(int i=2;i<n;i++){Vec p=center+Vec{std::cos(angle+turn*i),std::sin(angle+turn*i)}*(a-center).length();int id=atomAt(p,len*.08);ids.push_back(id?id:addAtom(p));}
        for(int i=1;i<n;i++)addBond(ids[i],ids[(i+1)%n],alt&&i%2==0?BondKind::Double:BondKind::Single);return;
    }
    if(atomId){auto a=atom(atomId);if(!a)return;Vec p=a->p,dir;for(auto& b:bonds)if(b.a==atomId||b.b==atomId)dir=dir+p-atom(b.a==atomId?b.b:b.a)->p;if(dir.length()<.01)dir={1,0};dir=dir*(1/dir.length());double radius=style.bondLength*(n==6&&alt?benzeneBondScale:1.)/(2*std::sin(pi/n));Vec center=p+dir*radius;double start=std::atan2(p.y-center.y,p.x-center.x);std::vector<int> ids{atomId};for(int i=1;i<n;i++){double ang=start+i*2*pi/n;ids.push_back(addAtom(center+Vec{std::cos(ang),std::sin(ang)}*radius));}for(int i=0;i<n;i++)addBond(ids[i],ids[(i+1)%n],alt&&i%2==0?BondKind::Double:BondKind::Single);return;}
}
size_t reactionRow(const Reaction& r,size_t i){
    if(r.wrapColumns>0)return i/size_t(r.wrapColumns);
    size_t row=0;for(size_t j=1;j<=i&&j<r.slots.size();j++)if(r.slots[j].startsRow)row++;
    return row;
}
void appendMechanismStage(Reaction& r,bool below,size_t source){
    if(source==size_t(-1))source=r.slots.size()-1;
    if(source>=r.slots.size())throw std::runtime_error("Invalid stage source");
    if(!r.explicitConnections()&&source+1!=r.slots.size()){
        for(size_t i=0;i<r.slots.size();i++){auto delta=reactionSlot(r,i)-r.p;r.slotGrid.push_back({delta.x/r.spacing,delta.y/(r.slotHeight+std::max(r.rowGap,mechanismCaptionHeight(r,Style{})+80))});if(i)r.links.push_back({int(i)-1,int(i)});}
    }
    if(r.slots.size()>=40)throw std::runtime_error("메커니즘은 최대 40칸까지 추가할 수 있습니다.");
    if(below&&!r.cyclic&&!r.vertical){
        // Preserve existing automatic row breaks before switching to manual rows.
        if(r.wrapColumns>0){for(size_t i=1;i<r.slots.size();i++)r.slots[i].startsRow=i%size_t(r.wrapColumns)==0;r.wrapColumns=0;}
    }
    if(r.explicitConnections()){r.links.push_back({int(source),int(r.slots.size())});Vec next=r.slotGrid[source]+Vec{1,below?1.:0.};
        while(std::any_of(r.slotGrid.begin(),r.slotGrid.end(),[&](Vec p){return (p-next).length()<.1;}))next.y++;
        r.slotGrid.push_back(next);}
    r.slots.push_back({"Stage "+std::to_string(r.slots.size()+1),{},"",below&&!r.cyclic&&!r.vertical});
    r.above.push_back("Next step");r.below.push_back("");
}
Vec reactionSlot(const Reaction& r,size_t i){
    if(i<r.slotGrid.size())return r.p+Vec{r.slotGrid[i].x*r.spacing,r.slotGrid[i].y*(r.slotHeight+std::max(r.rowGap,mechanismCaptionHeight(r,Style{})+80))};
    if(r.cyclic){double radius=r.cycleRadius>0?r.cycleRadius:mechanismCycleRadius(r,Style{});double angle=-pi/2+2*pi*i/std::max(size_t(1),r.slots.size());return r.p+Vec{std::cos(angle),std::sin(angle)}*radius;}
    if(r.vertical)return r.p+Vec{0,double(i)*r.spacing};
    size_t row=reactionRow(r,i);double col=0;
    if(r.wrapColumns>0){size_t columns=size_t(r.wrapColumns),c=i%columns;col=double(row%2?columns-1-c:c);}
    else {size_t currentRow=0;for(size_t j=1;j<=i;j++){if(j<r.slots.size()&&r.slots[j].startsRow)currentRow++;else col+=currentRow%2?-1.:1.;}}
    double font=mechanismFontSize(r,Style{}),captionHeight=0;
    for(auto& slot:r.slots){double lines=1+std::count(slot.label.begin(),slot.label.end(),'\n');if(!slot.energy.empty())lines+=1+std::count(slot.energy.begin(),slot.energy.end(),'\n');captionHeight=std::max(captionHeight,font*(.6+1.5*lines));}
    return r.p+Vec{col*r.spacing,double(row)*(r.slotHeight+std::max(r.rowGap,captionHeight+80))};
}
Vec plotAnchor(const EnergyPlot& p,size_t i,bool above){double x=p.p.x+100+(p.width-154)*double(i)/std::max(size_t(1),p.states.size()-1);return {x,p.p.y+(above?-85:p.height+70)};}
static std::pair<Vec,std::vector<int>> slotContents(const Document& d,int container,size_t index,bool above){
    if(d.isLocked(container))throw std::runtime_error("잠긴 구조는 편집할 수 없습니다.");
    for(const auto& r:d.reactions)if(r.id==container&&index<r.slots.size())return {reactionSlot(r,index),r.slots[index].members};
    for(const auto& p:d.plots)if(p.id==container&&index<p.states.size())return {plotAnchor(p,index,above),above?p.states[index].above:p.states[index].below};
    throw std::runtime_error("편집할 단계의 구조 영역을 선택하세요.");
}
Document structureInSlot(const Document& d,int container,size_t index,bool above){
    auto content=slotContents(d,container,index,above);Document flat=d;flat.groups.clear();
    // Parent containers and cross-stage groups must never enter the isolated editor.
    flat.reactions.clear();flat.plots.clear();Document result=flat.subset(content.second);
    auto ids=result.allIds();for(const auto& group:d.groups)if(!group.members.empty()&&std::all_of(group.members.begin(),group.members.end(),[&](int id){return has(ids,id);}))result.groups.push_back(group);
    for(auto& group:result.groups)result.nextId=std::max(result.nextId,group.id+1);
    result.translate(result.allIds(),content.first*-1);result.bondGeometryVersion=d.bondGeometryVersion;return result;
}
void replaceSlotStructure(Document& d,const Document& fragment,int container,size_t index,bool above){
    auto content=slotContents(d,container,index,above);
    if(!fragment.reactions.empty()||!fragment.plots.empty())throw std::runtime_error("개별 구조 편집에는 구조·텍스트·이미지만 사용할 수 있습니다.");
    Document candidate=d;
    // Erase exactly this frame; a user group spanning several stages cannot expand deletion.
    for(auto& group:candidate.groups)group.members.erase(std::remove_if(group.members.begin(),group.members.end(),[&](int id){return has(content.second,id);}),group.members.end());
    candidate.groups.erase(std::remove_if(candidate.groups.begin(),candidate.groups.end(),[](const Group& g){return g.members.empty();}),candidate.groups.end());
    candidate.erase(content.second);
    Document edited=fragment;
    edited.rebaseStructureStyle(candidate.style);
    auto bounds=makeScene(edited).bounds;Vec delta=content.first-Vec{bounds.x+bounds.w/2,bounds.y+bounds.h/2};
    int first=candidate.nextId;candidate.append(edited,delta);std::vector<int> added;for(int id:candidate.allIds())if(id>=first)added.push_back(id);
    for(auto& r:candidate.reactions)if(r.id==container)r.slots[index].members=added;
    for(auto& p:candidate.plots)if(p.id==container)(above?p.states[index].above:p.states[index].below)=added;
    // Applying a local edit keeps other stages and their layout fixed.
    d=std::move(candidate);
}
void fillSlot(Document& d,const Document& source,int container,size_t index,bool above){
    Document fragment=source;upgradeBondGeometry(fragment);
    // Vector structures retain their editable coordinates. Images retain original bytes.
    std::vector<std::vector<int>> sourceRings;std::set<int> seen;for(auto& a:fragment.atoms)if(!seen.count(a.id)){auto ids=fragment.component(a.id);seen.insert(ids.begin(),ids.end());auto rings=cycles(fragment,ids);sourceRings.insert(sourceRings.end(),rings.begin(),rings.end());}
    auto sourceTargets=bondTargets(fragment,sourceRings);std::vector<double> lengths;for(auto& b:fragment.bonds)lengths.push_back((fragment.atom(b.a)->p-fragment.atom(b.b)->p).length()*fragment.style.bondLength/sourceTargets[b.id]);
    // Give mechanism structures more visual weight than their captions. Graph
    // inserts keep their separate, compact sizing policy.
    bool mechanism=std::any_of(d.reactions.begin(),d.reactions.end(),[&](const Reaction& r){return r.id==container;});
    double presentationScale=mechanism?1.4:1.;
    double scale=1;if(!lengths.empty()){std::sort(lengths.begin(),lengths.end());if(lengths[lengths.size()/2]>.001)scale=d.style.bondLength/lengths[lengths.size()/2];}
    else if(fragment.atoms.empty()&&fragment.reactions.empty()&&fragment.plots.empty()){auto b=makeScene(fragment).bounds;scale=std::min(1.,std::min(100./b.w,72./b.h));}
    for(auto& a:fragment.atoms){a.p=a.p*scale;a.labelOffset=a.labelOffset*scale;}for(auto& o:fragment.objects){for(auto& p:o.points)p=p*scale;if(o.kind!=ObjectKind::Image){o.fontSize*=scale;o.width*=scale;for(auto& run:o.runs)run.size*=scale;}}
    if(mechanism){
        for(auto& atom:fragment.atoms){atom.p=atom.p*presentationScale;atom.labelOffset=atom.labelOffset*presentationScale;atom.drawingScale*=presentationScale;if(atom.annotationManual)atom.annotationOffset=atom.annotationOffset*presentationScale;for(auto& run:atom.labelRuns)run.size*=presentationScale;}
        for(auto& bond:fragment.bonds)bond.drawingScale*=presentationScale;
        for(auto& object:fragment.objects){for(auto& point:object.points)point=point*presentationScale;if(object.kind!=ObjectKind::Image){object.fontSize*=presentationScale;object.width*=presentationScale;for(auto& run:object.runs)run.size*=presentationScale;}}
    }
    fragment.rebaseStructureStyle(d.style);
    auto fragmentBounds=makeScene(fragment).bounds;
    for(auto& r:d.reactions)if(r.id==container){Reaction old=r;r.slotWidth=std::max(r.slotWidth,fragmentBounds.w+6);r.slotHeight=std::max(r.slotHeight,fragmentBounds.h+6);r.spacing=std::max(r.spacing,r.slotWidth+90);for(size_t i=0;i<r.slots.size();i++)d.translate(r.slots[i].members,reactionSlot(r,i)-reactionSlot(old,i));}
    for(auto& p:d.plots)if(p.id==container){EnergyPlot old=p;p.width=std::max(p.width,(fragmentBounds.w+14)*std::max(size_t(1),p.states.size()-1)+114);for(size_t i=0;i<p.states.size();i++){d.translate(p.states[i].above,plotAnchor(p,i,true)-plotAnchor(old,i,true));d.translate(p.states[i].below,plotAnchor(p,i,false)-plotAnchor(old,i,false));}}
    Vec anchor;std::vector<int>* members=nullptr;for(auto& r:d.reactions)if(r.id==container&&index<r.slots.size()){anchor=reactionSlot(r,index);members=&r.slots[index].members;}
    for(auto& p:d.plots)if(p.id==container&&index<p.states.size()){anchor=plotAnchor(p,index,above);members=above?&p.states[index].above:&p.states[index].below;}
    if(!members)throw std::runtime_error("Choose a valid reaction/graph slot");
    // Save target by identity before append can reallocate its owning vector.
    auto bounds=makeScene(fragment).bounds;Vec delta=anchor-Vec{bounds.x+bounds.w/2,bounds.y+bounds.h/2};int first=d.nextId;d.append(fragment,delta);std::vector<int> added;for(int id:d.allIds())if(id>=first)added.push_back(id);
    for(auto& r:d.reactions)if(r.id==container)r.slots[index].members.insert(r.slots[index].members.end(),added.begin(),added.end());
    for(auto& p:d.plots)if(p.id==container){auto& v=above?p.states[index].above:p.states[index].below;v.insert(v.end(),added.begin(),added.end());}
    for(auto& r:d.reactions)if(r.id==container){optimizeMechanism(d,{container},r.cyclic);break;}
}
void optimizeMechanism(Document& d,const std::vector<int>& selection,bool cyclic){
    bool found=false;
    for(auto& r:d.reactions)if(selection.empty()||has(selection,r.id)){
        found=true;if(cyclic&&r.explicitConnections())throw std::runtime_error("분기 연결을 유지하려면 가로 메커니즘 배치를 사용하세요.");Reaction old=r;r.cyclic=cyclic;
        if(cyclic){
            r.above.resize(r.slots.size());r.below.resize(r.slots.size());
            if(!old.cyclic){
                if(r.centerLabel.empty())r.centerLabel="Cycle";
                for(auto& slot:r.slots){if(slot.label.empty())slot.label="Name";if(slot.energy.empty())slot.energy="0.0";}
                for(auto& name:r.above)if(name.empty())name="Name";
                for(auto& energy:r.below)if(energy.empty())energy="0.0";
            }
        }
        // Layout operates on explicit frame membership. A visual user group may
        // span several stages (or include the parent reaction); expanding it here
        // measures/moves the entire mechanism once for every individual frame.
        Document content=d;content.groups.clear();content.reactions.clear();content.plots.clear();
        struct FrameGroups {Document& document;std::vector<Group> saved;explicit FrameGroups(Document& d):document(d),saved(std::move(d.groups)){d.groups.clear();}~FrameGroups(){document.groups=std::move(saved);}} frameGroups(d);
        auto frameBounds=[&](const std::vector<int>& ids){return makeScene(content.subset(ids)).bounds;};
        double widest=64,tallest=65;
        for(auto& slot:r.slots)if(!slot.members.empty()){auto b=frameBounds(slot.members);widest=std::max(widest,b.w+12);tallest=std::max(tallest,b.h+12);}
        r.slotWidth=widest;r.slotHeight=tallest;
        if(r.explicitConnections())arrangeMechanismGraph(r);
        // Every connector uses the longest caption's width and identical margins.
        if(r.cyclic)r.cycleRadius=mechanismCycleRadius(r,d.style);
        r.spacing=widest+2*mechanismArrowMargin(r,d.style)+mechanismArrowLength(r,d.style)+(!r.explicitConnections()?0:mechanismFontSize(r,d.style)*1.5);
        if(r.vertical&&!r.cyclic){
            r.spacing=tallest+mechanismCaptionHeight(r,d.style)+2*mechanismArrowMargin(r,d.style)+mechanismArrowLength(r,d.style);
        }
        for(size_t i=0;i<r.slots.size();i++)if(!r.slots[i].members.empty()){auto b=frameBounds(r.slots[i].members);d.translate(r.slots[i].members,reactionSlot(r,i)-Vec{b.x+b.w/2,b.y+b.h/2});}
        for(size_t e=0;e<r.links.size();e++){auto& l=r.links[e];if(!l.members.empty()){auto b=frameBounds(l.members);Vec mid=mechanismLinkCaption(r,e,d.style);d.translate(l.members,mid-Vec{0,mechanismFontSize(r,d.style)*3+b.h/2}-Vec{b.x+b.w/2,b.y+b.h/2});}}
    }
    if(found)return;
    struct Part{std::vector<int> ids;Vec center;double w,h;};std::vector<Part> parts;std::set<int> seen;
    auto add=[&](std::vector<int> ids){auto rings=cycles(d,ids);auto targets=bondTargets(d,rings);std::vector<double> lengths;for(auto& bond:d.bonds)if(has(ids,bond.a)&&has(ids,bond.b))lengths.push_back((d.atom(bond.a)->p-d.atom(bond.b)->p).length()*d.style.bondLength/targets[bond.id]);
        if(!lengths.empty()){std::sort(lengths.begin(),lengths.end());double median=lengths[lengths.size()/2];if(median>.001){Vec center;for(int id:ids)center=center+d.atom(id)->p;center=center*(1./ids.size());for(int id:ids)d.atom(id)->p=center+(d.atom(id)->p-center)*(d.style.bondLength/median);}}
        else for(auto& image:d.objects)if(has(ids,image.id)&&image.kind==ObjectKind::Image&&image.points.size()>1){Vec center=(image.points[0]+image.points[1])*.5,v=image.points[1]-image.points[0];double scale=std::min(1.,std::min(110./std::max(1.,std::abs(v.x)),80./std::max(1.,std::abs(v.y))));image.points[0]=center-v*(scale/2);image.points[1]=center+v*(scale/2);}
        readableRings(d,ids,rings);auto b=makeScene(d.subset(ids)).bounds;parts.push_back({ids,{b.x+b.w/2,b.y+b.h/2},b.w,b.h});};
    for(auto& a:d.atoms)if(!seen.count(a.id)){auto ids=d.component(a.id);seen.insert(ids.begin(),ids.end());if(selection.empty()||std::any_of(ids.begin(),ids.end(),[&](int id){return has(selection,id);}))add(ids);}
    for(auto& o:d.objects)if(o.kind==ObjectKind::Image&&!d.isLocked(o.id)&&(selection.empty()||has(selection,o.id)))add({o.id});
    if(parts.size()<2)throw std::runtime_error("두 개 이상의 구조 또는 반응 블록을 선택하세요.");
    std::sort(parts.begin(),parts.end(),[](const Part& a,const Part& b){return a.center.x<b.center.x;});
    // Associate nearby annotations with their closest structure before repositioning.
    std::vector<int> arrows;
    for(auto& o:d.objects)if(selection.empty()||has(selection,o.id)){
        if(o.kind==ObjectKind::Text&&!o.points.empty()){size_t best=0;double distance=1e20;for(size_t i=0;i<parts.size();i++){double v=(parts[i].center-o.points[0]).length();if(v<distance){distance=v;best=i;}}parts[best].ids.push_back(o.id);}
        else if(o.head&&o.points.size()>=2)arrows.push_back(o.id);
    }
    Reaction r;r.id=d.nextId++;r.p=parts.front().center;r.cyclic=cyclic;
    if(cyclic){r.p={0,0};for(auto& part:parts)r.p=r.p+part.center;r.p=r.p*(1./parts.size());}
    for(auto& part:parts){auto b=makeScene(d.subset(part.ids)).bounds;r.slotWidth=std::max(r.slotWidth,b.w+12);r.slotHeight=std::max(r.slotHeight,b.h+12);r.slots.push_back({"",part.ids});}
    r.spacing=std::max(r.slotWidth+90,r.slotHeight+70);r.above.resize(cyclic?parts.size():parts.size()-1);r.below.resize(r.above.size());r.unit.clear();
    if(cyclic){r.centerLabel="Cycle";for(auto& slot:r.slots){slot.label="Name";slot.energy="0.0";}for(auto& name:r.above)name="Name";for(auto& energy:r.below)energy="0.0";}
    if(cyclic)r.cycleRadius=mechanismCycleRadius(r,d.style);
    for(size_t i=0;i<parts.size();i++){auto b=makeScene(d.subset(parts[i].ids)).bounds;d.translate(parts[i].ids,reactionSlot(r,i)-Vec{b.x+b.w/2,b.y+b.h/2});}
    // Existing free arrows become the connections of the editable block.
    d.erase(arrows);d.reactions.push_back(r);
}
static std::vector<std::vector<int>> cycles(const Document& d,const std::vector<int>& component){
    std::vector<std::vector<int>> result;std::set<std::set<int>> seen;
    for(auto& skip:d.bonds){if(!has(component,skip.a))continue;std::queue<int> q;q.push(skip.a);std::map<int,int> prev{{skip.a,0}};
        while(!q.empty()&&!prev.count(skip.b)){int a=q.front();q.pop();for(auto& b:d.bonds){if(b.id==skip.id)continue;int n=b.a==a?b.b:b.b==a?b.a:0;if(n&&!prev.count(n)){prev[n]=a;q.push(n);}}}
        if(!prev.count(skip.b))continue;std::vector<int> path;for(int i=skip.b;i;i=prev[i])path.push_back(i);if(path.size()<3||path.size()>8)continue;std::set<int> key(path.begin(),path.end());if(seen.insert(key).second)result.push_back(path);
    }return result;
}
static bool benzeneRing(const Document& d,const std::vector<int>& ring){
    if(ring.size()!=6)return false;
    std::vector<BondKind> orders;
    for(size_t i=0;i<ring.size();i++){
        const auto* atom=d.atom(ring[i]);
        if(atom->element!=6||atom->charge||atom->radical)return false;
        int next=ring[(i+1)%ring.size()];
        auto edge=std::find_if(d.bonds.begin(),d.bonds.end(),[&](const Bond& b){
            return (b.a==atom->id&&b.b==next)||(b.b==atom->id&&b.a==next);
        });
        if(edge==d.bonds.end()||!edge->customOrder.empty())return false;
        orders.push_back(edge->kind);
    }
    if(std::all_of(orders.begin(),orders.end(),[](BondKind k){return k==BondKind::Aromatic;}))return true;
    for(size_t i=0;i<orders.size();i++){
        if(orders[i]!=BondKind::Single&&orders[i]!=BondKind::Double)return false;
        if(orders[i]==orders[(i+1)%orders.size()])return false;
    }
    return true;
}
// Preserve a drafted multi-ring scaffold, including its shorter terminal bonds.
// A ring must have the shape of a regular hexagon at its existing size.
static bool draftedArylScaffold(const Document& d,const std::vector<int>& comp,
                               const std::vector<std::vector<int>>& rings){
    if(rings.size()<2)return false;
    std::set<int> ringAtoms;
    double reference=0;
    for(const auto& ring:rings){
        if(!benzeneRing(d,ring))return false;
        Vec center;
        for(int id:ring){
            if(!ringAtoms.insert(id).second)return false;
            center=center+d.atom(id)->p;
        }
        center=center*(1./ring.size());
        double radius=0;
        for(int id:ring)radius+=(d.atom(id)->p-center).length()/ring.size();
        if(radius<.001)return false;
        if(!reference)reference=radius;
        if(std::abs(radius/reference-1)>.12)return false;
        for(size_t i=0;i<ring.size();i++){
            Vec a=d.atom(ring[i])->p,b=d.atom(ring[(i+1)%ring.size()])->p;
            if(std::abs((a-center).length()/radius-1)>.08||
               std::abs((a-b).length()/radius-1)>.08)return false;
        }
    }
    for(const auto& b:d.bonds)if(has(comp,b.a)){
        Vec v=d.atom(b.b)->p-d.atom(b.a)->p;
        double ratio=v.length()/reference;
        if(ratio<.65||ratio>1.4)return false;
        // A short explicit H can deliberately deviate from the main zigzag.
        if(d.atom(b.a)->element==1||d.atom(b.b)->element==1)continue;
        if(std::abs(std::remainder(std::atan2(v.y,v.x),pi/6))>pi/15)return false;
    }
    return true;
}
static std::set<int> arylEdges(const Document& d,const std::vector<std::vector<int>>& rings){
    std::set<int> result;
    bool changed=true;
    while(changed){
        const size_t before=result.size();
        for(const auto& ring:rings){
            std::vector<const Bond*> edges;
            bool shared=false,conjugated=ring.size()==6;
            for(size_t i=0;i<ring.size();i++){
                int a=ring[i],b=ring[(i+1)%ring.size()];
                const auto* atom=d.atom(a);
                auto edge=std::find_if(d.bonds.begin(),d.bonds.end(),[&](const Bond& e){
                    return (e.a==a&&e.b==b)||(e.a==b&&e.b==a);
                });
                if(edge==d.bonds.end()){conjugated=false;break;}
                edges.push_back(&*edge);
                shared=shared||result.count(edge->id);
                conjugated=conjugated&&atom->element==6&&!atom->charge&&!atom->radical&&
                    edge->customOrder.empty()&&(edge->kind==BondKind::Single||
                    edge->kind==BondKind::Double||edge->kind==BondKind::Aromatic);
            }
            if(!benzeneRing(d,ring)){
                if(!shared||!conjugated)continue;
                // Each vertex must participate in the shared pi system.
                for(int id:ring)if(std::none_of(edges.begin(),edges.end(),[&](const Bond* e){
                    return (e->a==id||e->b==id)&&(result.count(e->id)||
                        e->kind==BondKind::Double||e->kind==BondKind::Aromatic);
                }))conjugated=false;
                if(!conjugated)continue;
            }
            for(auto* edge:edges)result.insert(edge->id);
        }
        changed=result.size()!=before;
    }
    return result;
}
static std::map<int,double> bondTargets(const Document& d,const std::vector<std::vector<int>>& rings){
    std::set<int> aromaticAtoms,ringEdges;
    auto aromaticEdges=arylEdges(d,rings);
    for(int id:aromaticEdges){auto* b=d.bond(id);aromaticAtoms.insert(b->a);aromaticAtoms.insert(b->b);}
    for(auto& ring:rings)for(size_t i=0;i<ring.size();i++)for(auto& b:d.bonds)
        if((b.a==ring[i]&&b.b==ring[(i+1)%ring.size()])||(b.b==ring[i]&&b.a==ring[(i+1)%ring.size()]))ringEdges.insert(b.id);
    std::map<int,double> result;
    for(auto& b:d.bonds){double scale=aromaticEdges.count(b.id)?benzeneBondScale:ringEdges.count(b.id)?1.:bondLengthScale(d.atom(b.a)->element,d.atom(b.b)->element,b.kind);
        if(b.kind==BondKind::Single&&(aromaticAtoms.count(b.a)!=aromaticAtoms.count(b.b))&&d.atom(b.a)->element==6&&d.atom(b.b)->element==6){
            scale=benzeneConnectorScale;
        }
        result[b.id]=d.style.bondLength*scale;
    }return result;
}
bool toggleAromaticDisplay(Document& d,const std::vector<int>& selection){
    auto selected=editableExpansion(d,selection);std::set<int> visited;bool changed=false;
    for(auto& atom:d.atoms){auto comp=d.component(atom.id);if(visited.count(atom.id))continue;visited.insert(comp.begin(),comp.end());
        if(!std::any_of(comp.begin(),comp.end(),[&](int id){return selected.count(id)>0;})&&!std::any_of(d.bonds.begin(),d.bonds.end(),[&](const Bond& b){return selected.count(b.id)&&has(comp,b.a);}))continue;
        auto edges=arylEdges(d,cycles(d,comp));std::set<int> atoms;
        for(int id:edges){auto* b=d.bond(id);atoms.insert(b->a);atoms.insert(b->b);}
        if(edges.empty())continue;
        bool aromatic=std::all_of(edges.begin(),edges.end(),[&](int id){return d.bond(id)->kind==BondKind::Aromatic;});std::set<int> doubles;
        if(aromatic){std::set<int> remaining=atoms;std::function<bool()> match=[&](){if(remaining.empty())return true;int a=*remaining.begin();remaining.erase(a);
            for(int id:edges){auto* b=d.bond(id);int next=b->a==a?b->b:b->b==a?b->a:0;if(!next||!remaining.erase(next))continue;doubles.insert(id);if(match())return true;doubles.erase(id);remaining.insert(next);}remaining.insert(a);return false;};
            if(!match())continue;}
        for(int id:edges){auto* b=d.bond(id);b->kind=aromatic?(doubles.count(id)?BondKind::Double:BondKind::Single):BondKind::Aromatic;b->customOrder.clear();b->doubleSide=0;}changed=true;
    }return changed;
}
// Move pendant fragments rigidly with their ring attachment. Never pull a
// second ring or a substituent apart to make room for a long atom label.
static bool moveRing(Document& d,const std::vector<int>& comp,const std::vector<int>& ring,const std::map<int,Vec>& points){
    std::set<int> seen(ring.begin(),ring.end());std::map<int,Vec> shifts;
    for(int seed:comp)if(!seen.count(seed)){
        std::vector<int> branch{seed};std::set<int> anchors;seen.insert(seed);
        for(size_t i=0;i<branch.size();i++)for(auto& b:d.bonds){int next=b.a==branch[i]?b.b:b.b==branch[i]?b.a:0;if(!next)continue;
            if(has(ring,next))anchors.insert(next);else if(seen.insert(next).second)branch.push_back(next);}
        if(anchors.size()!=1)return false;
        int anchor=*anchors.begin();Vec delta=points.at(anchor)-d.atom(anchor)->p;
        for(int id:branch)shifts[id]=delta;
    }
    for(auto& p:shifts)d.atom(p.first)->p=d.atom(p.first)->p+p.second;
    for(auto& p:points)d.atom(p.first)->p=p.second;
    return true;
}
static void readableRings(Document& d,const std::vector<int>& comp,const std::vector<std::vector<int>>& rings){
    for(const auto& ring:rings){
    std::set<int> edges;bool labelled=false;
    for(int id:ring)if(!d.atom(id)->label.empty()||!d.atom(id)->labelRuns.empty())labelled=true;for(auto& b:d.bonds)if(has(ring,b.a)&&has(ring,b.b))edges.insert(b.id);
    if(!labelled||edges.empty())continue;
    Vec center;for(int id:ring)center=center+d.atom(id)->p;center=center*(1./ring.size());
    double minimum=std::max(7.,d.style.fontSize*.55);
    for(int pass=0;pass<24;pass++){
        auto scene=makeScene(d);std::map<int,double> visible;
        for(auto& p:scene.items)if(edges.count(p.owner)&&p.type==Primitive::Line&&p.points.size()==2)visible[p.owner]=std::max(visible[p.owner],(p.points[1]-p.points[0]).length());
        bool clear=true;for(int id:edges)if(visible[id]<minimum)clear=false;if(clear)break;
        std::map<int,Vec> points;for(int id:ring)points[id]=center+(d.atom(id)->p-center)*1.08;
        if(!moveRing(d,comp,ring,points))break;
    }
    }
}
static bool publicationChelate(Document& d,const std::vector<int>& comp,const std::vector<std::vector<int>>& rings){
    const std::vector<int>* chelate=nullptr;
    for(const auto& ring:rings){
        int metals=0;for(int id:ring){int e=d.atom(id)->element;metals+=(e>=21&&e<=30)||(e>=39&&e<=48)||(e>=57&&e<=80)||e>=89;}
        if(metals==1&&ring.size()==5&&!chelate)chelate=&ring;
        else if(!benzeneRing(d,ring))return false;
    }
    if(!chelate)return false;
    // Annotation colors do not encode a spatial projection. Keep them while
    // cleaning the same planar topology as its black-and-white counterpart.
    for(auto& b:d.bonds)if(has(comp,b.a)&&b.kind==BondKind::Dative)return false;
    const auto& ring=*chelate;Vec center;double area=0;
    for(size_t i=0;i<ring.size();i++){Vec a=d.atom(ring[i])->p,b=d.atom(ring[(i+1)%ring.size()])->p;center=center+a;area+=a.x*b.y-a.y*b.x;}
    center=center*(1./ring.size());double turn=(area>=0?1:-1)*2*pi/ring.size(),dot=0,cross=0;
    for(size_t i=0;i<ring.size();i++){Vec q{std::cos(turn*i),std::sin(turn*i)},p=d.atom(ring[i])->p-center;dot+=q.x*p.x+q.y*p.y;cross+=q.x*p.y-q.y*p.x;}
    double angle=std::atan2(cross,dot),radius=d.style.bondLength/(2*std::sin(pi/ring.size()));std::map<int,Vec> points;
    for(size_t i=0;i<ring.size();i++)points[ring[i]]=center+Vec{std::cos(angle+turn*i),std::sin(angle+turn*i)}*radius;
    if(!moveRing(d,comp,ring,points))return false;
    // Lay out organic tails on their own, retaining their attachment direction.
    // This restores zigzags instead of a straight
    // aryl/alkyl chain or a right-angle methyl branch.
    for(int root:ring){
        std::vector<int> external;
        for(auto& b:d.bonds){int next=b.a==root?b.b:b.b==root?b.a:0;if(!next)continue;
            if(!has(ring,next))external.push_back(next);}
        for(int first:external){
            std::vector<int> branch{first};std::set<int> seen(ring.begin(),ring.end());seen.insert(first);
            for(size_t i=0;i<branch.size();i++)for(auto& b:d.bonds){int next=b.a==branch[i]?b.b:b.b==branch[i]?b.a:0;if(next&&seen.insert(next).second)branch.push_back(next);}
            // Retain the user's attachment direction, especially for O-H and
            // metal labels; only regularize the geometry inside this tail.
            Vec desired=d.atom(first)->p-d.atom(root)->p;double angle=std::atan2(desired.y,desired.x);
            auto ids=branch;ids.push_back(root);Document tail=d.subset(ids);
            if(!planarLayout(tail,ids,cycles(tail,ids)))continue;
            Vec origin=tail.atom(root)->p,v=tail.atom(first)->p-origin;double rotate=angle-std::atan2(v.y,v.x),c=std::cos(rotate),s=std::sin(rotate);
            for(int id:branch){Vec p=tail.atom(id)->p-origin;d.atom(id)->p=d.atom(root)->p+Vec{c*p.x-s*p.y,s*p.x+c*p.y};}
        }
    }
    return true;
}
// Upgrade older documents once, preserving branch projections and atom identities.
bool upgradeBondGeometry(Document& d){
    if(!d.sourceXml.empty())return false; // Imported chemistry keeps its original coordinates.
    if(d.bondGeometryVersion>=1)return false;d.bondGeometryVersion=1;bool changed=false;std::set<int> visited;
    for(auto& atom:d.atoms){if(visited.count(atom.id))continue;auto comp=d.component(atom.id);visited.insert(comp.begin(),comp.end());auto rings=cycles(d,comp);std::set<int> aromatic;
        auto migrationTargets=bondTargets(d,rings);for(auto& b:d.bonds)if(has(comp,b.a)&&std::abs(migrationTargets[b.id]-d.style.bondLength*benzeneBondScale)<1e-8&&std::any_of(rings.begin(),rings.end(),[&](const auto& ring){return has(ring,b.a)&&has(ring,b.b);})){aromatic.insert(b.a);aromatic.insert(b.b);}
        std::set<int> handled;
        for(int seed:aromatic){if(handled.count(seed))continue;std::vector<int> system{seed};handled.insert(seed);
            for(size_t i=0;i<system.size();i++)for(auto& ring:rings)if(ring.size()==6&&std::all_of(ring.begin(),ring.end(),[&](int id){return aromatic.count(id)>0;})&&has(ring,system[i]))for(int id:ring)if(handled.insert(id).second)system.push_back(id);
            std::vector<double> lengths;Vec center;for(int id:system)center=center+d.atom(id)->p;center=center*(1./system.size());
            for(auto& b:d.bonds)if(has(system,b.a)&&has(system,b.b))lengths.push_back((d.atom(b.a)->p-d.atom(b.b)->p).length());
            if(lengths.empty())continue;std::sort(lengths.begin(),lengths.end());double median=lengths[lengths.size()/2];if(median<.001)continue;
            double scale=d.style.bondLength*benzeneBondScale/median;std::map<int,Vec> old;for(int id:system){old[id]=d.atom(id)->p;d.atom(id)->p=center+(old[id]-center)*scale;}
            auto targets=bondTargets(d,rings);std::set<int> tails;
            for(auto& edge:d.bonds){int root=has(system,edge.a)?edge.a:has(system,edge.b)?edge.b:0,first=root?(edge.a==root?edge.b:edge.a):0;if(!root||has(system,first)||tails.count(first))continue;
                std::vector<int> branch{first};std::set<int> anchors;
                for(size_t i=0;i<branch.size();i++)for(auto& b:d.bonds){int next=b.a==branch[i]?b.b:b.b==branch[i]?b.a:0;if(!next)continue;if(has(system,next)){anchors.insert(next);continue;}if(!has(branch,next))branch.push_back(next);}
                tails.insert(branch.begin(),branch.end());if(anchors.size()!=1)continue;
                Vec delta=d.atom(root)->p-old[root],v=d.atom(first)->p-old[root];if(targets[edge.id]<d.style.bondLength&&v.length()>.001)delta=delta+v*(targets[edge.id]/v.length()-1.);
                for(int id:branch)d.atom(id)->p=d.atom(id)->p+delta;changed=changed||delta.length()>1e-8;
            }changed=changed||std::abs(scale-1.)>1e-8;
        }
    }return changed;
}
// Place simple planar ring systems and their branches using exact geometry.
// Reject bridged/overconstrained systems so their existing projection is retained.
static bool planarLayout(Document& d,const std::vector<int>& comp,const std::vector<std::vector<int>>& rings){
    std::map<int,Vec> original,placed;std::map<int,std::vector<int>> neighbors;
    for(int id:comp)original[id]=d.atom(id)->p;
    for(auto& b:d.bonds)if(original.count(b.a)){neighbors[b.a].push_back(b.b);neighbors[b.b].push_back(b.a);}
    double length=d.style.bondLength;auto targets=bondTargets(d,rings);
    auto edgeLength=[&](int a,int b){for(auto& edge:d.bonds)if((edge.a==a&&edge.b==b)||(edge.a==b&&edge.b==a))return targets[edge.id];return length;};
    auto unit=[](double angle){return Vec{std::cos(angle),std::sin(angle)};};
    std::set<size_t> done;
    while(done.size()<rings.size()){
        bool progress=false;
        for(size_t k=0;k<rings.size();k++)if(!done.count(k)){
            const auto& ring=rings[k];double ringLength=edgeLength(ring[0],ring[1]);size_t n=ring.size(),shared=0,start=0;
            for(size_t i=0;i<n;i++)if(placed.count(ring[i])){shared++;start=i;}
            if(!done.empty()&&!shared)continue;
            if(shared>2)return false;
            if(shared==2){bool adjacent=false;for(size_t i=0;i<n;i++)if(placed.count(ring[i])&&placed.count(ring[(i+1)%n])){start=i;adjacent=true;break;}if(!adjacent)return false;}
            double best=1e100;std::vector<Vec> bestPoints;
            for(double sign:{-1.,1.}){
                double turn=sign*2*pi/n;std::vector<Vec> points(n);double angle=0;Vec center;
                if(shared){
                    Vec a=placed[ring[start]];double edgeAngle;
                    if(shared==2){Vec v=placed[ring[(start+1)%n]]-a;ringLength=v.length();edgeAngle=std::atan2(v.y,v.x);}
                    else {Vec v=original[ring[(start+1)%n]]-original[ring[start]];edgeAngle=std::round(std::atan2(v.y,v.x)/(pi/6))*(pi/6);}
                    points[start]=a;for(size_t j=1;j<n;j++){size_t prev=(start+j-1)%n,index=(start+j)%n;points[index]=points[prev]+unit(edgeAngle+turn*(j-1))*ringLength;}
                }else{
                    double area=0;for(size_t i=0;i<n;i++){Vec a=original[ring[i]],b=original[ring[(i+1)%n]];center=center+a;area+=a.x*b.y-a.y*b.x;}if(area*sign<0)continue;center=center*(1./n);
                    double radius=ringLength/(2*std::sin(pi/n)),dot=0,cross=0;
                    for(size_t i=0;i<n;i++){Vec q=unit(turn*i),p=original[ring[i]]-center;dot+=q.x*p.x+q.y*p.y;cross+=q.x*p.y-q.y*p.x;}angle=std::atan2(cross,dot);
                    for(size_t i=0;i<n;i++)points[i]=center+unit(angle+turn*i)*radius;
                }
                double score=0;for(size_t i=0;i<n;i++){score+=(points[i]-original[ring[i]]).length()*.01;for(auto& p:placed)if(!has(ring,p.first)){double gap=(points[i]-p.second).length()/length;score+=gap<.8?1000*(.8-gap):0;}}
                if(score<best){best=score;bestPoints=points;}
            }
            if(bestPoints.empty())return false;
            for(size_t i=0;i<n;i++){if(placed.count(ring[i])&&(placed[ring[i]]-bestPoints[i]).length()>.01)return false;placed[ring[i]]=bestPoints[i];}
            done.insert(k);progress=true;
        }
        if(!progress)return false;
    }
    if(placed.empty()){int root=comp.front();for(int id:comp)if(neighbors[id].size()==1){root=id;break;}placed[root]=original[root];}
    std::queue<int> pending;for(auto& p:placed)pending.push(p.first);
    while(!pending.empty()){
        int id=pending.front();pending.pop();std::vector<int> waiting,fixed;
        for(int next:neighbors[id])(placed.count(next)?fixed:waiting).push_back(next);
        for(int next:waiting){
            bool linear=false;int doubles=0;for(auto& b:d.bonds)if(b.a==id||b.b==id){if(b.kind==BondKind::Triple)linear=true;if(b.kind==BondKind::Double)doubles++;}linear=linear||doubles==2;
            std::vector<double> candidates;
            if(fixed.empty()){Vec v=original[next]-original[id];candidates.push_back(std::round(std::atan2(v.y,v.x)/(pi/6))*(pi/6));}
            else if(fixed.size()==2&&neighbors[id].size()==3){Vec outward;for(int f:fixed){Vec v=placed[id]-placed[f];outward=outward+v*(1/std::max(.001,v.length()));}if(outward.length()>.01)candidates.push_back(std::atan2(outward.y,outward.x));}
            if(candidates.empty()){Vec v=placed[fixed.front()]-placed[id];double base=std::atan2(v.y,v.x);if(linear&&neighbors[id].size()==2)candidates={base+pi};else if(neighbors[id].size()>=4&&d.atom(id)->element==6)candidates={base+pi/3,base-pi/3,base+2*pi/3,base-2*pi/3,base+pi};else if(neighbors[id].size()>=4)candidates={base+pi/2,base+pi,base-pi/2};else candidates={base+2*pi/3,base-2*pi/3};}
            double best=1e100;Vec point;
            for(double angle:candidates){Vec p=placed[id]+unit(angle)*edgeLength(id,next);Vec desired=original[next]-original[id];double score=desired.length()>.001?(unit(angle)-desired*(1/desired.length())).length():0;
                if(d.atom(id)->element==6)for(int f:fixed){Vec v=placed[f]-placed[id];double dot=(unit(angle).x*v.x+unit(angle).y*v.y)/std::max(.001,v.length());if(std::abs(dot)<.08)score+=20;}
                for(auto& other:placed)if(other.first!=id){double gap=(p-other.second).length()/length;if(gap<.95)score+=10000*(.95-gap);else score+=.02/(gap*gap);}
                if(score<best){best=score;point=p;}}
            placed[next]=point;fixed.push_back(next);pending.push(next);
        }
    }
    if(placed.size()!=comp.size())return false;
    // Unplaced ring closures indicate a topology not covered by this layout.
    for(auto& b:d.bonds)if(original.count(b.a)&&std::abs((placed[b.a]-placed[b.b]).length()-targets[b.id])>.01)return false;
    Vec before,after;for(int id:comp){before=before+original[id];after=after+placed[id];}Vec shift=(before-after)*(1./comp.size());
    for(int id:comp)d.atom(id)->p=placed[id]+shift;return true;
}
static CleanupReport cleanStructureCandidate(Document& d,const std::vector<int>& selection,bool preserveProjection){
    CleanupReport report;std::set<int> visited;std::set<int> requested(selection.begin(),selection.end());expand(d,requested);for(auto& b:d.bonds)if(requested.count(b.id)){requested.insert(b.a);requested.insert(b.b);}
    for(auto& seed:d.atoms){if(visited.count(seed.id))continue;auto comp=d.component(seed.id);visited.insert(comp.begin(),comp.end());if(!requested.empty()&&!std::any_of(comp.begin(),comp.end(),[&](int id){return requested.count(id)>0;}))continue;if(comp.size()<2)continue;report.components++;
        Vec center;for(int id:comp)center=center+d.atom(id)->p;center=center*(1./comp.size());bool protect=false,explicitProtect=false,stereo=false;std::vector<double> lengths;auto rings=cycles(d,comp);auto targets=bondTargets(d,rings);
        for(auto& b:d.bonds)if(has(comp,b.a)){lengths.push_back((d.atom(b.a)->p-d.atom(b.b)->p).length()*d.style.bondLength/targets[b.id]);if(b.kind==BondKind::Wedge||b.kind==BondKind::Hash)protect=stereo=true;}
        for(int id:comp){auto* a=d.atom(id);if(a->protect)explicitProtect=true;if(a->protect||(a->element>=21&&a->element<=30)||(a->element>=39&&a->element<=48)||(a->element>=57&&a->element<=80)||a->element>=89)protect=true;}for(auto& b:d.bonds)if(has(comp,b.a)&&(b.kind==BondKind::Dative||b.color!="#000000"))protect=true;
        if(draftedArylScaffold(d,comp,rings)){report.protectedComponents++;continue;}
        if(preserveProjection){
            if(!lengths.empty()){std::sort(lengths.begin(),lengths.end());double median=lengths[lengths.size()/2];if(median>.001){double scale=d.style.bondLength/median;for(int id:comp){auto* atom=d.atom(id);atom->p=center+(atom->p-center)*scale;atom->labelOffset=atom->labelOffset*scale;}}}
            if(!explicitProtect)readableRings(d,comp,cycles(d,comp));
            continue;
        }
        for(auto& ring:rings){double turnSign=0;for(size_t i=0;i<ring.size();i++){Vec a=d.atom(ring[i])->p,b=d.atom(ring[(i+1)%ring.size()])->p,c=d.atom(ring[(i+2)%ring.size()])->p;Vec u=b-a,v=c-b;double cross=u.x*v.y-u.y*v.x;if(std::abs(cross)>.01){if(turnSign&&turnSign*cross<0)protect=true;turnSign=cross;}}}
        if(protect&&rings.size()>1&&!explicitProtect&&!stereo&&publicationChelate(d,comp,rings)){report.regularRings++;readableRings(d,comp,rings);continue;}
        bool simpleChelate=rings.size()==1&&rings[0].size()==5&&!explicitProtect&&!stereo;
        if(protect&&!simpleChelate){report.protectedComponents++;if(!explicitProtect)readableRings(d,comp,rings);continue;}
        if(planarLayout(d,comp,rings)){report.regularRings+=int(rings.size());readableRings(d,comp,rings);continue;}
        std::map<int,Vec> ringTargets;std::map<int,int> count;
        for(auto& ring:rings){Vec c;double area=0;for(size_t i=0;i<ring.size();i++){Vec a=d.atom(ring[i])->p,b=d.atom(ring[(i+1)%ring.size()])->p;c=c+a;area+=a.x*b.y-a.y*b.x;}c=c*(1./ring.size());double turn=(area>0?1:-1)*2*pi/ring.size(),radius=(benzeneRing(d,ring)?d.style.bondLength*benzeneBondScale:d.style.bondLength)/(2*std::sin(pi/ring.size()));double dot=0,cross=0;
            for(size_t i=0;i<ring.size();i++){Vec q{std::cos(turn*i),std::sin(turn*i)},p=d.atom(ring[i])->p-c;dot+=q.x*p.x+q.y*p.y;cross+=q.x*p.y-q.y*p.x;}double angle=std::atan2(cross,dot);
            for(size_t i=0;i<ring.size();i++){Vec p=c+Vec{std::cos(angle+turn*i),std::sin(angle+turn*i)}*radius;ringTargets[ring[i]]=ringTargets[ring[i]]+p;count[ring[i]]++;}report.regularRings++;
        }
        for(auto& entry:ringTargets)entry.second=entry.second*(1./count[entry.first]);
        // Keep the original directions between independent rings and their branches.
        // Snapping these bonds independently can reshape an already oriented scaffold.
        std::map<int,double> directions;
        for(const auto& b:d.bonds)if(has(comp,b.a)){
            Vec v=d.atom(b.b)->p-d.atom(b.a)->p;
            directions[b.id]=std::atan2(v.y,v.x);
        }
        for(int iteration=0;iteration<220;iteration++){std::map<int,Vec> force;for(auto& b:d.bonds)if(has(comp,b.a)){Vec a=d.atom(b.a)->p,z=d.atom(b.b)->p,v=z-a;double len=v.length();if(len<.001)v={1,0},len=1;double target=directions[b.id];Vec ideal=Vec{std::cos(target),std::sin(target)}*targets[b.id];Vec f=(v-v*(targets[b.id]/len))*.17;
                if(!count[b.a]||!count[b.b])f=f+(v-ideal)*.025;force[b.a]=force[b.a]+f;force[b.b]=force[b.b]-f;}
            for(size_t i=0;i<comp.size();i++)for(size_t j=i+1;j<comp.size();j++){Vec v=d.atom(comp[i])->p-d.atom(comp[j])->p;double len=v.length();if(len<d.style.bondLength*.7){if(len<.001)v={.1,.1},len=v.length();Vec f=v*((d.style.bondLength*.7-len)*.07/len);force[comp[i]]=force[comp[i]]+f;force[comp[j]]=force[comp[j]]-f;}}
            for(int id:comp){if(count[id])d.atom(id)->p=d.atom(id)->p+(ringTargets[id]-d.atom(id)->p)*.5;else d.atom(id)->p=d.atom(id)->p+force[id];}}
        Vec newCenter;for(int id:comp)newCenter=newCenter+d.atom(id)->p;newCenter=newCenter*(1./comp.size());for(int id:comp)d.atom(id)->p=d.atom(id)->p+center-newCenter;readableRings(d,comp,rings);
    }return report;
}
// Validate the rendered labels, including oxidation superscripts and manually
// offset text. Atom-centre repulsion alone cannot detect LCo/H overlaps.
static std::set<std::pair<int,int>> cleanupCollisions(const Document& d){
    auto scene=makeScene(d);std::set<std::pair<int,int>> result;
    auto overlaps=[](Rect a,Rect b){return std::min(a.x+a.w,b.x+b.w)-std::max(a.x,b.x)>.5&&std::min(a.y+a.h,b.y+b.h)-std::max(a.y,b.y)>.5;};
    for(size_t i=0;i<d.atoms.size();i++)for(size_t j=i+1;j<d.atoms.size();j++){
        int a=d.atoms[i].id,b=d.atoms[j].id;
        if(scene.objectBounds.count(a)&&scene.objectBounds.count(b)&&overlaps(scene.objectBounds.at(a),scene.objectBounds.at(b)))result.insert(std::minmax(a,b));
    }
    for(const auto& line:scene.items){
        auto* bond=d.bond(line.owner);if(!bond||line.type!=Primitive::Line||line.points.size()!=2)continue;
        Vec p=line.points[0],v=line.points[1]-p;
        for(const auto& atom:d.atoms){
            if(atom.id==bond->a||atom.id==bond->b||!scene.objectBounds.count(atom.id))continue;
            Rect r=scene.objectBounds.at(atom.id);double low=0,high=1;
            auto clip=[&](double origin,double delta,double lo,double hi){
                if(std::abs(delta)<1e-8)return origin>lo&&origin<hi;
                double a=(lo-origin)/delta,b=(hi-origin)/delta;if(a>b)std::swap(a,b);low=std::max(low,a);high=std::min(high,b);return low<high;
            };
            if(clip(p.x,v.x,r.x+.5,r.x+r.w-.5)&&clip(p.y,v.y,r.y+.5,r.y+r.h-.5))result.insert(std::minmax(atom.id,bond->id));
        }
    }
    auto cross=[](Vec a,Vec b){return a.x*b.y-a.y*b.x;};
    for(size_t i=0;i<d.bonds.size();i++)for(size_t j=i+1;j<d.bonds.size();j++){
        const auto& a=d.bonds[i];const auto& b=d.bonds[j];if(a.a==b.a||a.a==b.b||a.b==b.a||a.b==b.b)continue;
        Vec p=d.atom(a.a)->p,u=d.atom(a.b)->p-p,q=d.atom(b.a)->p,v=d.atom(b.b)->p-q;double det=cross(u,v);
        if(std::abs(det)<1e-8)continue;double t=cross(q-p,v)/det,s=cross(q-p,u)/det;
        if(t>.01&&t<.99&&s>.01&&s<.99)result.insert(std::minmax(a.id,b.id));
    }
    return result;
}
CleanupReport cleanStructure(Document& d,const std::vector<int>& selection,bool preserveProjection){
    Document original=d;auto before=cleanupCollisions(original);auto report=cleanStructureCandidate(d,selection,preserveProjection);
    auto after=cleanupCollisions(d);std::set<int> rejected;
    for(auto pair:after)if(!before.count(pair))for(int id:{pair.first,pair.second}){
        int seed=d.atom(id)?id:d.bond(id)?d.bond(id)->a:0;if(!seed)continue;
        auto comp=d.component(seed);rejected.insert(comp.begin(),comp.end());
    }
    // Keep the user's projection for a component whose candidate introduces
    // collisions; other independently selected structures may still improve.
    std::set<int> restored;
    for(int id:rejected){if(restored.count(id))continue;auto comp=d.component(id);bool moved=false;
        for(int atom:comp){moved=moved||(d.atom(atom)->p-original.atom(atom)->p).length()>.001;*d.atom(atom)=*original.atom(atom);restored.insert(atom);}
        if(moved){report.protectedComponents++;report.regularRings=std::max(0,report.regularRings-int(cycles(d,comp).size()));}
    }
    return report;
}
Document chairTemplate(){Document d;std::vector<Vec> points={{-45,-24},{-25,6},{20,6},{45,24},{25,-6},{-20,-6}};for(auto p:points){int id=d.addAtom(p);d.atom(id)->protect=true;}for(size_t i=0;i<points.size();i++)d.addBond(d.atoms[i].id,d.atoms[(i+1)%points.size()].id,i==1?BondKind::Bold:BondKind::Single);std::vector<double> lengths;Vec center;for(auto& a:d.atoms)center=center+a.p;center=center*(1./d.atoms.size());for(auto& b:d.bonds)lengths.push_back((d.atom(b.a)->p-d.atom(b.b)->p).length());std::sort(lengths.begin(),lengths.end());double scale=d.style.bondLength/lengths[lengths.size()/2];for(auto& a:d.atoms)a.p=center+(a.p-center)*scale;return d;}
Document mechanismDemo(){Document d;Reaction r;r.id=d.nextId++;r.p={65,80};r.slots={{"Reactant",{}},{"Intermediate",{}},{"Product",{}}};r.above={"Nucleophilic attack","C–O bond cleavage"};r.below={"37.2","17.7"};d.reactions.push_back(r);fillSlot(d,demo(),r.id,0);Document b=demo();b.atom(b.atoms.back().id)->element=8;fillSlot(d,b,r.id,1);fillSlot(d,demo(),r.id,2);optimizeMechanism(d,{r.id});return d;}
// Editable, intentionally generic hydrogenation cycle: no metal, ligand or
// energetic assignment is implied by these illustrative intermediates.
Document catalyticCycleDemo(){
    Document d;Reaction r;r.id=d.nextId++;r.p={0,0};r.cyclic=true;
    r.centerLabel="Catalytic cycle";r.captionFontSize=14;r.unit.clear();
    r.slots={{"Active catalyst",{}},{"Metal dihydride",{}},{"Metal ethyl hydride",{}}};
    r.above={"H_{2} / oxidative addition",
             "C_{2}H_{4} / migratory insertion",
             "C_{2}H_{6} / reductive elimination"};
    r.below.resize(r.slots.size());
    d.reactions.push_back(r);
    auto fragment=[](int stage){
        Document f;
        int m=f.addAtom({0,0},26);auto* metal=f.atom(m);
        metal->label="M";metal->explicitH=0;metal->protect=true;
        auto ligand=[&](Vec p,int element,const std::string& label){
            int id=f.addAtom(p,element);auto* atom=f.atom(id);
            atom->label=label;atom->explicitH=0;atom->protect=true;
            f.addBond(m,id);return id;
        };
        if(stage>=1)ligand({-30,0},1,"H");
        if(stage==1)ligand({30,0},1,"H");
        if(stage==2){
            int carbon=ligand({30,0},6,"CH_{2}");
            int terminal=f.addAtom({60,0},6);
            f.atom(terminal)->label="CH_{3}";
            f.atom(terminal)->explicitH=0;
            f.atom(terminal)->protect=true;
            f.addBond(carbon,terminal);
        }
        return f;
    };
    for(size_t i=0;i<r.slots.size();i++)fillSlot(d,fragment(int(i)),r.id,i);
    optimizeMechanism(d,{r.id},true);
    return d;
}
Document coordinateDemo(){Document d;EnergyPlot p;p.id=d.nextId++;p.p={25,90};p.states={{"R",0,{},{}},{"TS1",37.2,{},{}},{"I",17.7,{},{}},{"TS2",27.3,{},{}},{"P",-9.9,{},{}}};p.series={{"Path 1","#000000",{0,37.2,17.7,27.3,-9.9},1,3.5}};d.plots.push_back(p);fillSlot(d,demo(),p.id,0,true);fillSlot(d,chairTemplate(),p.id,4,false);d.plots.front().stages=defaultMechanismStages();return d;}
void applyHouseStyle(Document& d){
    Style target;double scale=target.bondLength/d.style.bondLength;
    for(auto& a:d.atoms){a.p=a.p*scale;a.labelOffset=a.labelOffset*scale;for(auto& r:a.labelRuns){r.font="Arial";r.face|=1;r.size*=target.fontSize/d.style.fontSize;}}
    for(auto& o:d.objects){if(d.isLocked(o.id))continue;for(auto& p:o.points)p=p*scale;o.font="Arial";o.face|=1;o.fontSize*=target.fontSize/d.style.fontSize;if(o.kind!=ObjectKind::Image)o.width=target.lineWidth;for(auto& r:o.runs){r.font="Arial";r.face|=1;r.size*=target.fontSize/d.style.fontSize;}}
    for(auto& r:d.reactions){r.p=r.p*scale;r.spacing*=scale;r.slotWidth*=scale;r.slotHeight*=scale;r.rowGap*=scale;r.cycleRadius*=scale;r.captionFontSize*=target.fontSize/d.style.fontSize;}
    for(auto& p:d.plots){if(d.isLocked(p.id))continue;p.p=p.p*scale;p.width*=scale;p.height*=scale;for(auto& s:p.series)s.width=target.lineWidth;}
    d.style=target;
}

}

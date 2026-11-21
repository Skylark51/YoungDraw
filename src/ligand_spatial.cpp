#include "ligand_spatial.h"
#include <array>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <stdexcept>
namespace yd {
namespace {
bool metal(int e){return (e>=21&&e<=30)||(e>=39&&e<=48)||(e>=72&&e<=80);}
struct P3 {double x,y,z;};
double determinant(P3 a,P3 b,P3 c){return a.x*(b.y*c.z-b.z*c.y)-a.y*(b.x*c.z-b.z*c.x)+a.z*(b.x*c.y-b.y*c.x);}
using Graph=std::map<int,std::map<int,const Bond*>>;
Graph graph(const Document& d){Graph g;for(const auto& a:d.atoms)g[a.id];for(const auto& b:d.bonds){g[b.a][b.b]=&b;g[b.b][b.a]=&b;}return g;}
bool bondCompatible(const Document&,const Bond& a,const Document& r,const Bond& b){
    if(metal(r.atom(b.a)->element)||metal(r.atom(b.b)->element))return true;
    auto kind=[](BondKind k){return k==BondKind::Wedge||k==BondKind::Hash||k==BondKind::Bold?BondKind::Single:k;};
    return kind(a.kind)==kind(b.kind)||a.kind==BondKind::Aromatic||b.kind==BondKind::Aromatic;
}
bool sameStereo(const Document& d,const Graph& actual,const Document& r,const Graph& expected,const std::map<int,int>& map){
    for(const auto& a:r.atoms){
        if(a.element!=6||!a.depth||expected.at(a.id).size()!=3)continue;
        const auto& center=*d.atom(map.at(a.id));std::array<P3,3> observed{},wanted{};bool specified=false;size_t i=0;
        for(const auto& [neighbor,bond]:expected.at(a.id)){
            const int id=map.at(neighbor);const auto& v=*d.atom(id);auto delta=v.p-center.p;
            auto* edge=actual.at(center.id).at(id);double z=0;
            if(center.depth&&v.depth){z=*v.depth*v.drawingScale-*center.depth*center.drawingScale;specified=true;}
            else if(edge->kind==BondKind::Wedge||edge->kind==BondKind::Hash){z=(edge->kind==BondKind::Wedge?-1.:1.)*(edge->a==center.id?1.:-1.)*std::max(1.,delta.length());specified=true;}
            observed[i]={delta.x,delta.y,z};const auto& n=*r.atom(neighbor);wanted[i]={n.p.x-a.p.x,n.p.y-a.p.y,n.depth.value_or(0)*n.drawingScale-*a.depth*a.drawingScale};++i;
        }
        if(specified&&determinant(observed[0],observed[1],observed[2])*determinant(wanted[0],wanted[1],wanted[2])< -1e-6)return false;
    }return true;
}
}
std::vector<int> spatialSelection(const Document& d,const std::vector<int>& selection){
    std::set<int> atoms;std::map<int,std::vector<int>> neighbors;std::map<int,const Bond*> bonds;
    for(const auto& a:d.atoms)neighbors[a.id];for(const auto& b:d.bonds){neighbors[b.a].push_back(b.b);neighbors[b.b].push_back(b.a);bonds[b.id]=&b;}
    std::vector<int> queue;for(int id:selection){if(d.isLocked(id))continue;if(neighbors.count(id)&&atoms.insert(id).second)queue.push_back(id);auto b=bonds.find(id);if(b!=bonds.end())for(int atom:{b->second->a,b->second->b})if(atoms.insert(atom).second)queue.push_back(atom);}
    for(size_t i=0;i<queue.size();++i)for(int next:neighbors[queue[i]])if(atoms.insert(next).second)queue.push_back(next);
    return {atoms.begin(),atoms.end()};
}
std::vector<SpatialCrossing> spatialCrossings(const Document& d){
    if(std::none_of(d.atoms.begin(),d.atoms.end(),[](const auto& a){return a.depth.has_value();})&&std::none_of(d.bonds.begin(),d.bonds.end(),[](const auto& b){return b.zOrder.has_value();}))return {};
    struct Edge {const Bond* bond;const Atom* a;const Atom* b;double left,right,top,bottom;};
    std::map<int,const Atom*> atoms;for(const auto& a:d.atoms)atoms[a.id]=&a;std::vector<Edge> edges;
    for(const auto& b:d.bonds){auto first=atoms.find(b.a),last=atoms.find(b.b);if(first==atoms.end()||last==atoms.end())continue;const auto* a=first->second;const auto* z=last->second;
        if((a->depth&&z->depth)||b.zOrder)edges.push_back({&b,a,z,std::min(a->p.x,z->p.x),std::max(a->p.x,z->p.x),std::min(a->p.y,z->p.y),std::max(a->p.y,z->p.y)});
    }
    std::stable_sort(edges.begin(),edges.end(),[](const auto& a,const auto& b){return a.left<b.left;});
    std::vector<SpatialCrossing> result;auto cross=[](Vec a,Vec b){return a.x*b.y-a.y*b.x;};
    for(size_t i=0;i<edges.size();i++)for(size_t j=i+1;j<edges.size()&&edges[j].left<=edges[i].right;j++){
        const auto& first=edges[i];const auto& second=edges[j];if(first.bottom<second.top||second.bottom<first.top)continue;
        const auto& a=*first.bond;const auto& b=*second.bond;if(a.a==b.a||a.a==b.b||a.b==b.a||a.b==b.b)continue;
        const auto& aa=*first.a;const auto& ab=*first.b;const auto& ba=*second.a;const auto& bb=*second.b;const Vec v=ab.p-aa.p,w=bb.p-ba.p;const double den=cross(v,w);if(std::abs(den)<1e-8)continue;
        const double t=cross(ba.p-aa.p,w)/den,u=cross(ba.p-aa.p,v)/den;if(t<=.01||t>=.99||u<=.01||u>=.99)continue;double za=0,zb=0;
        if(aa.depth&&ab.depth&&ba.depth&&bb.depth){za=*aa.depth*aa.drawingScale*(1-t)+*ab.depth*ab.drawingScale*t;zb=*ba.depth*ba.drawingScale*(1-u)+*bb.depth*bb.drawingScale*u;}
        else if(a.zOrder&&b.zOrder){za=-double(*a.zOrder);zb=-double(*b.zOrder);}else continue;if(std::abs(za-zb)<1e-5)continue;
        result.push_back(za<zb?SpatialCrossing{a.id,b.id,aa.p+v*t,t,u}:SpatialCrossing{b.id,a.id,aa.p+v*t,u,t});
    }return result;
}
std::vector<int> spatialBondOrder(const Document& d){
    std::map<int,int> pending;std::map<int,std::vector<int>> edges;for(const auto& b:d.bonds)pending[b.id]=0;
    for(const auto& c:spatialCrossings(d)){edges[c.back].push_back(c.front);pending[c.front]++;}
    std::vector<int> result;std::set<int> done;
    while(result.size()<d.bonds.size()){
        int next=0;double farthest=-std::numeric_limits<double>::infinity();
        for(const auto& b:d.bonds)if(!done.count(b.id)&&pending[b.id]==0){const auto& a=*d.atom(b.a);const auto& z=*d.atom(b.b);double depth=a.depth&&z.depth?(*a.depth*a.drawingScale+*z.depth*z.drawingScale)/2:-double(b.zOrder.value_or(0));if(!next||depth>farthest){next=b.id;farthest=depth;}}
        if(!next)for(const auto& b:d.bonds)if(!done.count(b.id)){next=b.id;break;}
        done.insert(next);result.push_back(next);for(int front:edges[next])--pending[front];
    }return result;
}
void shadeSpatial(Document& d,const std::vector<int>& selection,double gray){
    if(!std::isfinite(gray)||gray<0||gray>85)throw std::runtime_error("회색 강도는 0~85%로 입력하세요.");
    const auto ids=spatialSelection(d,selection);const std::set<int> scope(ids.begin(),ids.end());
    const int channel=int(std::lround(255*gray/100));const char* digits="0123456789abcdef";std::string color="#";for(int i=0;i<3;i++){color+=digits[channel/16];color+=digits[channel%16];}
    const auto neighbors=graph(d);
    for(auto& b:d.bonds)if((b.kind==BondKind::Wedge||b.kind==BondKind::Hash)&&scope.count(b.a)&&d.atom(b.a)->depth&&neighbors.at(b.a).size()==3){
        const auto& center=*d.atom(b.a);std::array<P3,3> actual{},paper{};size_t at=0;bool valid=true;
        for(const auto& [id,edge]:neighbors.at(b.a)){const auto& atom=*d.atom(id);if(!atom.depth){valid=false;break;}const auto delta=atom.p-center.p;actual[at]={delta.x,delta.y,*atom.depth*atom.drawingScale-*center.depth*center.drawingScale};paper[at]={delta.x,delta.y,id==b.b?-1.:0.};++at;}
        if(valid){double real=determinant(actual[0],actual[1],actual[2]),solid=determinant(paper[0],paper[1],paper[2]);if(std::abs(real*solid)>1e-8)b.kind=real*solid>0?BondKind::Wedge:BondKind::Hash;}
    }
    for(auto& b:d.bonds)if(scope.count(b.a)&&scope.count(b.b)&&d.atom(b.a)->depth&&d.atom(b.b)->depth){
        if(b.depthColor.empty())b.depthColor=b.color;
        b.color=b.depthColor;b.depthRearColor=gray>0?color:b.depthColor;
    }
}
void rotateSpatial(Document& d,const std::vector<int>& selection,double x,double y,double z,double gray){
    for(double value:{x,y,z,gray})if(!std::isfinite(value))throw std::runtime_error("입체 회전 값이 올바르지 않습니다.");
    if(gray<0||gray>85||std::abs(x)>36000||std::abs(y)>36000||std::abs(z)>36000)throw std::runtime_error("회전은 ±36000도, 회색 강도는 0~85%로 입력하세요.");
    const auto ids=spatialSelection(d,selection);if(ids.empty())throw std::runtime_error("입체로 바꿀 원자·결합이나 구조 영역을 선택하세요.");
    for(int id:ids)if(!d.atom(id)->depth)throw std::runtime_error("먼저 BQCN 또는 MeSiP₂ 입체 기준을 적용하세요.");
    std::set<int> done;x=std::remainder(x,360.)*pi/180;y=std::remainder(y,360.)*pi/180;z=std::remainder(z,360.)*pi/180;
    for(int seed:ids){if(done.count(seed))continue;auto comp=d.component(seed);Vec center;double cz=0;
        for(int id:comp){auto& a=*d.atom(id);center=center+a.p;cz+=*a.depth*a.drawingScale;done.insert(id);}center=center*(1./comp.size());cz/=comp.size();
        for(int id:comp){auto& a=*d.atom(id);double px=a.p.x-center.x,py=a.p.y-center.y,pz=*a.depth*a.drawingScale-cz;
            double ny=py*std::cos(x)-pz*std::sin(x),nz=py*std::sin(x)+pz*std::cos(x);py=ny;pz=nz;
            double nx=px*std::cos(y)+pz*std::sin(y);nz=-px*std::sin(y)+pz*std::cos(y);px=nx;pz=nz;
            nx=px*std::cos(z)-py*std::sin(z);ny=px*std::sin(z)+py*std::cos(z);a.p=center+Vec{nx,ny};a.depth=(pz+cz)/a.drawingScale;a.protect=true;
        }
    }shadeSpatial(d,ids,gray);
}
bool applyLigandSpatial(Document& d,const std::vector<int>& selection,const Document& r){
    const auto candidates=spatialSelection(d,selection);if(candidates.size()<r.atoms.size()||r.atoms.empty())return false;
    const auto actual=graph(d),expected=graph(r);std::set<int> allowed(candidates.begin(),candidates.end());
    std::vector<int> order;for(const auto& a:r.atoms)order.push_back(a.id);
    std::stable_sort(order.begin(),order.end(),[&](int a,int b){auto rank=[&](int id){return (metal(r.atom(id)->element)?100:0)+(r.atom(id)->element!=6?20:0)+int(expected.at(id).size());};return rank(a)>rank(b);});
    std::map<int,int> mapping,best;std::set<int> used;double bestError=std::numeric_limits<double>::infinity();size_t visits=0;
    std::function<void(size_t)> visit=[&](size_t at){
        if(++visits>200000)return;
        if(at==order.size()){
            if(!sameStereo(d,actual,r,expected,mapping))return;
            Vec pc,qc;for(const auto& [a,b]:mapping){pc=pc+r.atom(a)->p;qc=qc+d.atom(b)->p;}pc=pc*(1./mapping.size());qc=qc*(1./mapping.size());
            double dot=0,cross=0,norm=0;for(const auto& [a,b]:mapping){Vec p=r.atom(a)->p-pc,q=d.atom(b)->p-qc;dot+=p.x*q.x+p.y*q.y;cross+=p.x*q.y-p.y*q.x;norm+=p.x*p.x+p.y*p.y;}
            const double theta=std::atan2(cross,dot),scale=norm>1e-8?std::clamp(std::hypot(dot,cross)/norm,.05,20.):1.;double error=0;
            for(const auto& [a,b]:mapping){Vec p=(r.atom(a)->p-pc)*scale;Vec delta=qc+Vec{p.x*std::cos(theta)-p.y*std::sin(theta),p.x*std::sin(theta)+p.y*std::cos(theta)}-d.atom(b)->p;error+=delta.x*delta.x+delta.y*delta.y;}
            if(error<bestError){bestError=error;best=mapping;}return;
        }
        const int ref=order[at];const auto& a=*r.atom(ref);
        std::vector<int> options(candidates);for(const auto& [neighbor,edge]:expected.at(ref))if(mapping.count(neighbor)){options.clear();for(const auto& [id,b]:actual.at(mapping.at(neighbor)))if(allowed.count(id))options.push_back(id);break;}
        for(int id:options){const auto& b=*d.atom(id);if(used.count(id)||!(a.element==b.element||(metal(a.element)&&metal(b.element)))||!(a.label==b.label||(a.label=="Me"&&a.element==6&&a.explicitH==3&&b.label.empty()&&b.charge==0&&actual.at(id).size()==1)||(a.label.empty()&&(b.label==symbol(b.element)||(metal(b.element)&&(b.label=="M"||b.label.rfind(symbol(b.element)+"^{",0)==0)))))||actual.at(id).size()<expected.at(ref).size())continue;
            // Additional attachments are permitted at the metal only; this avoids matching a different substituted ligand.
            if(!metal(a.element)&&actual.at(id).size()!=expected.at(ref).size())continue;
            bool ok=true;for(const auto& [ra,da]:mapping){auto e=expected.at(ref).find(ra),v=actual.at(id).find(da);if((e==expected.at(ref).end())!=(v==actual.at(id).end())||(e!=expected.at(ref).end()&&!bondCompatible(d,*v->second,r,*e->second))){ok=false;break;}}
            if(!ok)continue;mapping[ref]=id;used.insert(id);visit(at+1);used.erase(id);mapping.erase(ref);
        }
    };visit(0);if(best.empty())return false;
    Document result=d;Vec pc,qc;double styleScale=0;for(const auto& [a,b]:best){pc=pc+r.atom(a)->p;qc=qc+d.atom(b)->p;styleScale+=d.atom(b)->drawingScale;}pc=pc*(1./best.size());qc=qc*(1./best.size());styleScale/=best.size();const double scale=d.style.bondLength/r.style.bondLength*styleScale;
    double dot=0,cross=0;for(const auto& [a,b]:best){Vec p=r.atom(a)->p-pc,q=d.atom(b)->p-qc;dot+=p.x*q.x+p.y*q.y;cross+=p.x*q.y-p.y*q.x;}const double theta=std::atan2(cross,dot),c=std::cos(theta),s=std::sin(theta);
    std::set<int> matched;std::map<int,Vec> delta;
    for(const auto& [a,b]:best){auto& dest=*result.atom(b);const auto& source=*r.atom(a);Vec p=(source.p-pc)*scale;Vec next=qc+Vec{c*p.x-s*p.y,s*p.x+c*p.y};delta[b]=next-dest.p;dest.p=next;dest.depth=source.depth?std::optional<double>(*source.depth*source.drawingScale*scale/dest.drawingScale):std::nullopt;dest.protect=true;if(source.label=="Me"&&dest.label.empty())dest.label="Me";matched.insert(b);}
    // Preserve the relative shape of each pendant co-ligand. Bridging attachments cannot be moved safely.
    std::set<int> visited;for(int seed:candidates)if(!matched.count(seed)&&!visited.count(seed)){
        std::vector<int> branch{seed};visited.insert(seed);std::set<int> anchors;
        for(size_t i=0;i<branch.size();i++)for(const auto& [id,edge]:actual.at(branch[i])){if(matched.count(id))anchors.insert(id);else if(visited.insert(id).second)branch.push_back(id);}
        if(anchors.size()!=1)return false;int anchor=*anchors.begin();for(int id:branch){auto& a=*result.atom(id);a.p=a.p+delta[anchor];a.depth=result.atom(anchor)->depth?std::optional<double>(*result.atom(anchor)->depth*result.atom(anchor)->drawingScale/a.drawingScale):std::nullopt;}
    }
    for(auto& b:result.bonds)if(matched.count(b.a)&&matched.count(b.b)){if(b.depthColor.empty())b.depthColor=b.color;}
    for(const auto& b:r.bonds)if(b.kind==BondKind::Wedge||b.kind==BondKind::Hash){auto* target=result.bond(actual.at(best.at(b.a)).at(best.at(b.b))->id);target->a=best.at(b.a);target->b=best.at(b.b);target->kind=b.kind;}
    d=std::move(result);return true;
}
}

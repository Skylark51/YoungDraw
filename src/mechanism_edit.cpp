#include "model.h"
#include "scene.h"
#include "mechanism_data.h"
#include <functional>
#include <map>
#include <regex>
#include <stdexcept>
namespace yd {
namespace {
bool has(const std::vector<int>& ids,int id){return std::find(ids.begin(),ids.end(),id)!=ids.end();}
void eraseExact(Document& d,const std::vector<int>& ids){
    d.bonds.erase(std::remove_if(d.bonds.begin(),d.bonds.end(),[&](const Bond& b){return has(ids,b.id)||has(ids,b.a)||has(ids,b.b);}),d.bonds.end());
    d.atoms.erase(std::remove_if(d.atoms.begin(),d.atoms.end(),[&](const Atom& a){return has(ids,a.id);}),d.atoms.end());
    d.objects.erase(std::remove_if(d.objects.begin(),d.objects.end(),[&](const Object& o){return has(ids,o.id);}),d.objects.end());
    auto prune=[&](std::vector<int>& v){v.erase(std::remove_if(v.begin(),v.end(),[&](int id){return has(ids,id);}),v.end());};
    for(auto& g:d.groups)prune(g.members);
    d.groups.erase(std::remove_if(d.groups.begin(),d.groups.end(),[](const Group& g){return g.members.empty();}),d.groups.end());
    for(auto& r:d.reactions){for(auto& s:r.slots)prune(s.members);for(auto& l:r.links)prune(l.members);}
    for(auto& p:d.plots)for(auto& s:p.states){prune(s.above);prune(s.below);}
}
Rect bounds(const Document& d,const std::vector<int>& ids){auto plain=d;plain.groups.clear();return makeScene(plain.subset(ids)).bounds;}
Vec center(Rect b){return {b.x+b.w/2,b.y+b.h/2};}
std::string caption(const Object& o){if(o.runs.empty())return o.text;std::string s;for(auto& r:o.runs)s+=(r.face&96)==64?"^{"+r.text+"}":(r.face&96)==32?"_{"+r.text+"}":r.text;return s;}
}
bool atomLabelHasCharge(const Atom& a){
    // A charge already present in an explicit label is authoritative ink.
    // Do not synthesize another floating charge from the same CDXML property.
    if(!a.charge)return false;for(auto& r:a.labelRuns)if((r.face&96)==64&&(r.text.find('+')!=std::string::npos||r.text.find('-')!=std::string::npos||r.text.find("−")!=std::string::npos))return true;
    auto s=chemicalScripts(a.label);return !s.empty()&&(s.back()=='+'||s.back()=='-'||s.find("+}")!=std::string::npos||s.find("-}")!=std::string::npos||s.find("−}")!=std::string::npos||s.find("⁺")!=std::string::npos||s.find("⁻")!=std::string::npos);
}
int collapsePhenyl(Document& d,const std::vector<int>& selection){
    std::vector<std::vector<int>> rings;std::set<std::set<int>> seen;
    for(auto& start:d.atoms){std::vector<int> path{start.id};std::function<void(int)> walk=[&](int id){
        for(auto& b:d.bonds){int n=b.a==id?b.b:b.b==id?b.a:0;if(!n)continue;
            if(n==start.id&&path.size()==6){std::set<int> key(path.begin(),path.end());if(seen.insert(key).second)rings.push_back(path);}
            else if(path.size()<6&&!has(path,n)&&d.atom(n)->element==6){path.push_back(n);walk(n);path.pop_back();}
        }};if(start.element==6)walk(start.id);
    }
    int count=0;
    for(auto& ring:rings){bool valid=true;int attachment=0,external=0,doubles=0,aromatics=0;std::vector<int> edges;
        for(int id:ring){auto* a=d.atom(id);if(!a||a->charge||a->isotope||a->radical||!a->label.empty()||!a->labelRuns.empty()){valid=false;break;}}
        if(!valid)continue;
        for(auto& b:d.bonds){bool a=has(ring,b.a),z=has(ring,b.b);if(a&&z){edges.push_back(b.id);doubles+=b.kind==BondKind::Double;aromatics+=b.kind==BondKind::Aromatic;if(b.kind!=BondKind::Single&&b.kind!=BondKind::Double&&b.kind!=BondKind::Aromatic)valid=false;}
            else if(a||z){external++;attachment=a?b.a:b.b;if(b.kind!=BondKind::Single)valid=false;}}
        bool selected=std::any_of(ring.begin(),ring.end(),[&](int id){return has(selection,id);})||std::any_of(edges.begin(),edges.end(),[&](int id){return has(selection,id);});
        if(!valid||!selected||external!=1||edges.size()!=6||!((doubles==3&&aromatics==0)||aromatics==6))continue;
        // Alternation is required; cyclohexadienes and fused/substituted rings are not Ph.
        if(!aromatics)for(int id:ring){int n=0;for(auto& b:d.bonds)if(has(edges,b.id)&&(b.a==id||b.b==id)&&b.kind==BondKind::Double)n++;if(n!=1)valid=false;}
        if(!valid)continue;
        std::vector<int> removed=edges;for(int id:ring)if(id!=attachment)removed.push_back(id);
        // Detach electron arrows from atoms that disappear, retaining their visible path.
        for(auto& o:d.objects)if(has(removed,o.sourceAtom)||has(removed,o.targetAtom)||has(removed,o.sourceBond)||has(removed,o.targetBond)){o.points=routeArrow(d,o);o.autoRoute=false;o.sourceAtom=o.targetAtom=o.sourceBond=o.targetBond=0;}
        eraseExact(d,removed);auto* a=d.atom(attachment);a->label="Ph";a->labelRuns.clear();a->explicitH=0;count++;
    }return count;
}
void addMechanismBranch(Reaction& r,size_t source){
    if(r.cyclic||r.vertical)throw std::runtime_error("분기는 가로 메커니즘에서 추가하세요.");
    if(source>=r.slots.size()||r.slots.size()>=30)throw std::runtime_error("분기 시작 단계와 최대 30칸을 확인하세요.");
    if(r.links.empty()){
        for(size_t i=0;i<r.slots.size();i++){Vec p=reactionSlot(r,i)-r.p;r.slotGrid.push_back({p.x/r.spacing,p.y/(r.slotHeight+std::max(r.rowGap,mechanismCaptionHeight(r,Style{})+80))});if(i)r.links.push_back({int(i)-1,int(i),{}});}
    }
    Vec p=r.slotGrid.at(source)+Vec{1,1};bool occupied=true;while(occupied){occupied=false;for(auto q:r.slotGrid)if((p-q).length()<.5){p.y++;occupied=true;break;}}
    r.links.push_back({int(source),int(r.slots.size()),{}});r.slotGrid.push_back(p);r.slots.push_back({"",{},""});r.above.push_back("");r.below.push_back("");
}
static int arrangeImportedMechanismImpl(Document& d,const std::vector<int>& selection){
    struct Part{std::vector<int> ids;Rect box;Vec at;};std::vector<Part> parts,notations;std::set<int> seen,owned;
    for(auto& r:d.reactions){for(auto& s:r.slots)owned.insert(s.members.begin(),s.members.end());for(auto& l:r.links)owned.insert(l.members.begin(),l.members.end());}for(auto& p:d.plots)for(auto& s:p.states){owned.insert(s.above.begin(),s.above.end());owned.insert(s.below.begin(),s.below.end());}
    auto selected=[&](const std::vector<int>& ids){return selection.empty()||std::any_of(ids.begin(),ids.end(),[&](int id){return has(selection,id);})||std::any_of(d.bonds.begin(),d.bonds.end(),[&](const Bond& b){return has(selection,b.id)&&(has(ids,b.a)||has(ids,b.b));});};
    for(auto& a:d.atoms)if(!seen.count(a.id)&&!owned.count(a.id)){auto ids=d.component(a.id);seen.insert(ids.begin(),ids.end());if(!selected(ids))continue;auto b=bounds(d,ids);Part p{ids,b,center(b)};if(ids.size()==1)notations.push_back(p);else parts.push_back(p);}
    for(auto& o:d.objects)if(o.kind==ObjectKind::Image&&!owned.count(o.id)&&!d.isLocked(o.id)&&selected({o.id})){auto b=bounds(d,{o.id});parts.push_back({{o.id},b,center(b)});}
    if(parts.size()<2||parts.size()>30)throw std::runtime_error("정리할 구조 2~30개와 반응 화살표·텍스트를 선택하세요.");
    // Row clustering retains the drawn rows; no chemical coordinate is rescaled.
    std::sort(parts.begin(),parts.end(),[](const Part& a,const Part& b){return a.at.y<b.at.y;});
    std::vector<std::vector<Part>> rows;for(auto p:parts){if(rows.empty()||std::abs(p.at.y-rows.back().front().at.y)>std::max(40.,p.box.h*.6))rows.push_back({});rows.back().push_back(p);}parts.clear();
    Reaction r;r.id=d.nextId++;r.unit.clear();
    for(size_t y=0;y<rows.size();y++){auto& row=rows[y];std::sort(row.begin(),row.end(),[](const Part& a,const Part& b){return a.at.x<b.at.x;});for(auto p:row){parts.push_back(p);}}
    r.p=parts.front().at;
    // Columns are clustered independently of rows so branches share a column.
    std::vector<double> columns;for(auto& p:parts)columns.push_back(p.at.x);std::sort(columns.begin(),columns.end());std::vector<double> xs;for(double x:columns)if(xs.empty()||x-xs.back()>std::max(40.,d.style.bondLength*2))xs.push_back(x);
    size_t k=0;for(size_t y=0;y<rows.size();y++)for(size_t j=0;j<rows[y].size();j++,k++){size_t col=0;double best=1e30;for(size_t c=0;c<xs.size();c++)if(std::abs(parts[k].at.x-xs[c])<best){best=std::abs(parts[k].at.x-xs[c]);col=c;}r.slotGrid.push_back({double(col),double(y)});r.slots.push_back({"",parts[k].ids,""});}
    std::vector<int> consumed;std::vector<Vec> mids;std::vector<const Object*> arrows;
    for(auto& o:d.objects)if(o.kind==ObjectKind::Line&&o.head&&o.points.size()>=2&&!owned.count(o.id)&&selected({o.id}))arrows.push_back(&o);
    auto nearest=[&](Vec point){size_t best=0;double dist=1e30;for(size_t i=0;i<parts.size();i++){auto b=parts[i].box;double dx=std::max({b.x-point.x,0.,point.x-b.x-b.w}),dy=std::max({b.y-point.y,0.,point.y-b.y-b.h});double v=std::hypot(dx,dy);if(v<dist){dist=v;best=i;}}return best;};
    for(auto* arrow:arrows){size_t a=nearest(arrow->points.front()),b=nearest(arrow->points.back());if(a==b)continue;r.links.push_back({int(a),int(b),{}});mids.push_back((arrow->points.front()+arrow->points.back())*.5);consumed.push_back(arrow->id);auto mark=arrow->cross;mark.index=int(r.links.size())-1;if(mark.enabled)r.crosses.push_back(mark);}
    if(r.links.empty())throw std::runtime_error("구조 사이 반응 화살표를 찾지 못했습니다. 구조와 화살표를 함께 선택하세요.");
    r.above.resize(r.links.size());r.below.resize(r.links.size());
    static const std::regex energy(R"(^\s*(?:[-+]?\d+(?:\.\d+)?\*?|\?(?:\.\?)?)(?:\s*/\s*(?:[-+]?\d+(?:\.\d+)?\*?|\?(?:\.\?)?))*\s*$)");
    for(auto& o:d.objects)if(!o.points.empty()&&!owned.count(o.id)&&selected({o.id})&&!has(consumed,o.id)){
        if(o.kind==ObjectKind::Text){Vec p=center(bounds(d,{o.id}));size_t state=nearest(p),edge=0;double ds=(p-parts[state].at).length(),de=1e30;for(size_t e=0;e<mids.size();e++){double v=(p-mids[e]).length();if(v<de){de=v;edge=e;}}if(std::min(ds,de)>std::max(180.,d.style.bondLength*6))continue;
            bool isEnergy=std::regex_match(o.text,energy);if(!isEnergy&&!o.text.empty()&&(o.text.front()=='+'||o.text.front()=='-')&&de<ds){r.links[edge].members.push_back(o.id);continue;}std::string& dest=de<ds?(isEnergy?r.below[edge]:r.above[edge]):(isEnergy?r.slots[state].energy:r.slots[state].label);
            // Ambiguous multiple energy rows remain visible instead of being discarded.
            if(isEnergy&&!dest.empty()){(de<ds?r.links[edge].members:r.slots[state].members).push_back(o.id);continue;}
            if(!dest.empty())dest+='\n';dest+=isEnergy?o.text:caption(o);consumed.push_back(o.id);
        }else if(o.kind==ObjectKind::Curve){size_t i=nearest(o.points.front());r.slots[i].members.push_back(o.id);}
    }
    for(auto& p:notations){size_t edge=0;double best=1e30;for(size_t i=0;i<mids.size();i++){double v=(p.at-mids[i]).length();if(v<best){best=v;edge=i;}}if(best<180)r.links[edge].members.insert(r.links[edge].members.end(),p.ids.begin(),p.ids.end());}
    // Dissolve only grouping that crosses frame boundaries; per-molecule groups survive.
    std::map<int,int> frame;for(size_t i=0;i<r.slots.size();i++)for(int id:r.slots[i].members)frame[id]=int(i);for(size_t i=0;i<r.links.size();i++)for(int id:r.links[i].members)frame[id]=int(r.slots.size()+i);
    d.groups.erase(std::remove_if(d.groups.begin(),d.groups.end(),[&](const Group& g){std::set<int> frames;for(int id:g.members)if(frame.count(id))frames.insert(frame[id]);return frames.size()>1;}),d.groups.end());
    eraseExact(d,consumed);d.reactions.push_back(r);optimizeMechanism(d,{r.id},false);return r.id;
}
int arrangeImportedMechanism(Document& d,const std::vector<int>& selection){Document candidate=d;int id=arrangeImportedMechanismImpl(candidate,selection);d=std::move(candidate);return id;}

}

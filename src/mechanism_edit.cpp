#include "model.h"
#include "structure_templates.h"
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
}
std::string importedEnergy(std::string text){
    for(size_t at=0;(at=text.find("−",at))!=std::string::npos;++at)text.replace(at,std::string("−").size(),"-");
    static const std::string value=R"((?:[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?\*?|\?(?:\.\?)?))";
    static const std::string prefix=R"((?:[sS]\s*=\s*(?:[0-9](?:\.[05])?|10(?:\.0)?)\s*[:=]\s*|[mM]\s*=\s*(?:[1-9]|1\d|2[01])\s*[:=]\s*)?)";
    static const std::string unit=R"((?:\s*(?:kcal|kJ)\s*/\s*mol|\s*(?:eV|hartree))?)";
    static const std::regex pattern("^\\s*"+prefix+value+unit+"(?:\\s*/\\s*"+prefix+value+unit+")*\\s*$",std::regex::icase);
    if(!std::regex_match(text,pattern))return {};
    // Canonicalize units so their slash is distinguishable from an energy-list separator.
    static const std::regex molar(R"((kcal|kJ)\s*/\s*mol)",std::regex::icase);
    text=std::regex_replace(text,molar,"$1·mol⁻¹");
    static const std::regex spinLabel(R"([sS]\s*=\s*([\d.]+)\s*[:=]\s*)");
    text=std::regex_replace(text,spinLabel,"S=$1:");
    static const std::regex multiplicity(R"([mM]\s*=\s*(\d+)\s*[:=]\s*)");
    std::string normalized;
    size_t cursor=0;
    for(std::sregex_iterator it(text.begin(),text.end(),multiplicity),end;it!=end;++it){
        normalized.append(text,cursor,size_t(it->position())-cursor);
        int spin=std::stoi((*it)[1])-1;
        normalized+="S="+std::to_string(spin/2)+(spin%2?".5:":":");
        cursor=size_t(it->position()+it->length());
    }
    return normalized+text.substr(cursor);
}
namespace {
Vec center(Rect b){return {b.x+b.w/2,b.y+b.h/2};}
std::string caption(const Object& o){if(o.runs.empty())return o.text;std::string s;for(auto& r:o.runs)s+=(r.face&96)==64?"^{"+r.text+"}":(r.face&96)==32?"_{"+r.text+"}":r.text;return s;}
}
bool attachMechanismEnergy(Document& doc,int source,int role,int index,int owner,size_t slot){
    auto target=std::find_if(doc.reactions.begin(),doc.reactions.end(),[&](const Reaction& r){return r.id==owner;});
    if(target==doc.reactions.end()||slot>=target->slots.size()||target->slots[slot].members.empty()||
       !target->slots[slot].energy.empty()||doc.isLocked(owner)||doc.isLocked(source))return false;
    if(role==0){
        auto object=std::find_if(doc.objects.begin(),doc.objects.end(),[&](const Object& o){return o.id==source&&o.kind==ObjectKind::Text;});
        if(object==doc.objects.end())return false;
        auto energy=importedEnergy(object->text);
        if(energy.empty())return false;
        target->slots[slot].energy=std::move(energy);
        eraseExact(doc,{source});
        return true;
    }
    if(source!=owner||index<0)return false;
    size_t from=size_t(index);
    std::string* energy=nullptr;
    if(role==3&&from<target->below.size())energy=&target->below[from];
    if(role==9&&from<target->slots.size())energy=&target->slots[from].energy;
    if(!energy||energy->empty())return false;
    target->slots[slot].energy.swap(*energy);
    for(auto& color:target->captionColors)if(color.role==role&&color.index==index){
        color.role=9;
        color.index=int(slot);
    }
    return true;
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
        for(int id:ring){auto* a=d.atom(id);if(!a||a->charge||a->isotope||a->radical||a->protect||a->mullikenSpin||!a->label.empty()||!a->labelRuns.empty()){valid=false;break;}}
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
int collapseFunctionalGroups(Document& d,const std::vector<int>& selection,const std::string& abbreviation){
    if(selection.empty())return 0;
    std::vector<const StructureTemplate*> patterns;
    for(const auto& entry:functionalGroups())
        if(abbreviation.empty()||abbreviation==entry.abbreviation)patterns.push_back(&entry);
    std::stable_sort(patterns.begin(),patterns.end(),[](const auto* a,const auto* b){
        return a->drawing.atoms.size()>b->drawing.atoms.size();
    });
    int count=0;
    for(const auto* pattern:patterns){
        std::vector<int> atoms;
        for(const auto& a:d.atoms)atoms.push_back(a.id);
        std::vector<std::vector<int>> matches;
        matchTemplate(d,atoms,pattern->drawing,true,[&](const std::vector<int>& ids){
            if(std::any_of(ids.begin(),ids.end(),[&](int id){return has(selection,id);})||
               std::any_of(d.bonds.begin(),d.bonds.end(),[&](const Bond& b){
                   return has(selection,b.id)&&has(ids,b.a)&&has(ids,b.b);
               }))matches.push_back(ids);
        });
        std::set<int> consumed, retainedNeighbors;
        for(const auto& ids:matches){
            if(std::any_of(ids.begin(),ids.end(),[&](int id){
                return consumed.count(id)||retainedNeighbors.count(id)||!d.atom(id);
            }))continue;
            std::vector<int> removed(ids.begin()+1,ids.end());
            const int root=ids.front();
            int neighbor=0;
            for(const auto& b:d.bonds){
                const bool a=has(ids,b.a), z=has(ids,b.b);
                if(a&&z)removed.push_back(b.id);
                if(a!=z&&(a?b.a:b.b)==root&&b.kind==BondKind::Single&&b.customOrder.empty()&&b.customDisplay.empty())
                    neighbor=a?b.b:b.a;
            }
            if(!neighbor||consumed.count(neighbor))continue;
            auto split=[&](const std::vector<int>& members){
                const bool containsRoot=has(members,root);
                return std::any_of(ids.begin()+1,ids.end(),[&](int id){return has(members,id)!=containsRoot;});
            };
            bool spansSlots=false;
            for(const auto& r:d.reactions)for(const auto& slot:r.slots)spansSlots|=split(slot.members);
            for(const auto& p:d.plots)for(const auto& state:p.states)spansSlots|=split(state.above)||split(state.below);
            if(spansSlots)continue;
            for(auto& o:d.objects)if(has(removed,o.sourceAtom)||has(removed,o.targetAtom)||has(removed,o.sourceBond)||has(removed,o.targetBond)){
                o.points=routeArrow(d,o);
                o.autoRoute=false;
                o.sourceAtom=o.targetAtom=o.sourceBond=o.targetBond=0;
            }
            eraseExact(d,removed);
            auto& a=*d.atom(root);
            a.label=pattern->abbreviation;
            const auto digit=a.label.find('2');
            if(digit!=std::string::npos)a.label.replace(digit,1,"_{2}");
            if(a.label=="CF3")a.label="CF_{3}";
            a.charge=0;
            a.explicitH=0;
            consumed.insert(ids.begin(),ids.end());
            retainedNeighbors.insert(neighbor);
            ++count;
        }
    }
    return count;
}

void normalizeChemicalLabels(Document& d,const std::vector<int>& selection){
    auto normalize=[](std::string& text,std::vector<TextRun>& runs){
        if(runs.empty()){text=chemicalScripts(text);return;}
        std::string raw;for(auto& run:runs)raw+=run.text;
        auto formatted=chemicalScripts(raw);if(formatted==raw)return;
        // Map generated superscripts back onto the original colored/font runs, even
        // when a metal symbol and its Roman numeral were split across CDXML runs.
        std::vector<bool> super;std::string visible;bool script=false;
        for(size_t i=0;i<formatted.size();){
            if(formatted.compare(i,2,"^{")==0){script=true;i+=2;continue;}
            if(script&&formatted[i]=='}'){script=false;i++;continue;}
            visible+=formatted[i++];super.push_back(script);
        }
        if(visible!=raw)return;
        double baseline=0;for(auto& run:runs)if((run.face&96)!=64)baseline=std::max(baseline,run.size);if(baseline<=0)for(auto& run:runs)baseline=std::max(baseline,run.size);
        std::vector<TextRun> result;size_t offset=0;
        for(auto run:runs){size_t start=0;while(start<run.text.size()){
            bool elevated=super[offset+start];size_t end=start+1;
            while(end<run.text.size()&&super[offset+end]==elevated)end++;
            auto part=run;part.text=run.text.substr(start,end-start);
            if(elevated){part.face=(part.face&~96)|64;part.size=std::min(part.size,baseline*chemicalScriptScale(part.text));}
            result.push_back(part);start=end;
        }offset+=run.text.size();}
        runs=std::move(result);text=formatted;
    };
    auto ids=selection.empty()?d.allIds():d.subset(selection).allIds();
    for(auto& a:d.atoms)if(has(ids,a.id))normalize(a.label,a.labelRuns);
    for(auto& o:d.objects)if(o.kind==ObjectKind::Text&&has(ids,o.id))normalize(o.text,o.runs);
}
std::vector<int> addBranchArrows(Document& d,Vec top,double height,double length,size_t count,bool left,int stemId){
    if(count<2||count>20||!std::isfinite(height)||!std::isfinite(length)||height<10||height>10000||length<10||length>10000||!std::isfinite(top.x)||!std::isfinite(top.y))throw std::runtime_error("분기는 2~20개, 세로선 높이와 화살표 길이는 10~10000 pt로 지정하세요.");
    Object* existing=nullptr;
    if(stemId){for(auto& o:d.objects)if(o.id==stemId)existing=&o;
        if(!existing||d.isLocked(stemId)||existing->kind!=ObjectKind::Line||existing->points.size()!=2||std::abs(existing->points[0].x-existing->points[1].x)>.01)throw std::runtime_error("분기 기준으로 사용할 세로 직선을 선택하세요.");
    }
    std::vector<int> ids;double width=d.style.lineWidth;std::string color="#000000";
    if(existing){existing->head=0;existing->points={top,top+Vec{0,height}};width=existing->width;color=existing->color;ids.push_back(existing->id);}
    else{Object stem;stem.id=d.nextId++;stem.points={top,top+Vec{0,height}};stem.width=width;d.objects.push_back(stem);ids.push_back(stem.id);}
    for(size_t i=0;i<count;i++){Object arrow;arrow.id=d.nextId++;Vec start=top+Vec{0,height*i/(count-1)};arrow.points={start,start+Vec{left?-length:length,0}};arrow.head=1;arrow.width=width;arrow.color=color;d.objects.push_back(arrow);ids.push_back(arrow.id);}
    // Normal document grouping keeps the entire fork movable and exportable.
    Group group;group.id=d.nextId++;group.members=ids;d.groups.push_back(group);return ids;
}
void addMechanismBranch(Reaction& r,size_t source){
    if(r.cyclic||r.vertical)throw std::runtime_error("분기는 가로 메커니즘에서 추가하세요.");
    if(source>=r.slots.size()||r.slots.size()>=40)throw std::runtime_error("분기 시작 단계와 최대 40칸을 확인하세요.");
    if(!r.explicitConnections()){
        for(size_t i=0;i<r.slots.size();i++){Vec p=reactionSlot(r,i)-r.p;r.slotGrid.push_back({p.x/r.spacing,p.y/(r.slotHeight+std::max(r.rowGap,mechanismCaptionHeight(r,Style{})+80))});if(i)r.links.push_back({int(i)-1,int(i),{}});}
    }
    Vec p=r.slotGrid.at(source)+Vec{1,1};bool occupied=true;while(occupied){occupied=false;for(auto q:r.slotGrid)if((p-q).length()<.5){p.y++;occupied=true;break;}}
    r.links.push_back({int(source),int(r.slots.size()),{}});r.slotGrid.push_back(p);r.slots.push_back({"",{},""});r.above.push_back("");r.below.push_back("");
}
static int arrangeImportedMechanismImpl(Document& d,const std::vector<int>& selection,bool preserveLayout){
    const Document original=d;
    normalizeChemicalLabels(d,selection);
    struct Part{std::vector<int> ids;Rect box;Vec at;};std::vector<Part> parts,notations;std::set<int> seen,owned;
    for(auto& r:d.reactions){for(auto& s:r.slots)owned.insert(s.members.begin(),s.members.end());for(auto& l:r.links)owned.insert(l.members.begin(),l.members.end());}for(auto& p:d.plots)for(auto& s:p.states){owned.insert(s.above.begin(),s.above.end());owned.insert(s.below.begin(),s.below.end());}
    auto selected=[&](const std::vector<int>& ids){return selection.empty()||std::any_of(ids.begin(),ids.end(),[&](int id){return has(selection,id);})||std::any_of(d.bonds.begin(),d.bonds.end(),[&](const Bond& b){return has(selection,b.id)&&(has(ids,b.a)||has(ids,b.b));});};
    for(auto& a:d.atoms)if(!seen.count(a.id)&&!owned.count(a.id)){auto ids=d.component(a.id);seen.insert(ids.begin(),ids.end());if(!selected(ids))continue;auto b=bounds(d,ids);Part p{ids,b,center(b)};if(ids.size()==1&&a.element==1)notations.push_back(p);else parts.push_back(p);}
    for(auto& o:d.objects)if(o.kind==ObjectKind::Image&&!owned.count(o.id)&&!d.isLocked(o.id)&&selected({o.id})){auto b=bounds(d,{o.id});parts.push_back({{o.id},b,center(b)});}
    std::vector<const Object*> arrows,stems;
    for(auto& o:d.objects)if((o.kind==ObjectKind::Line||o.kind==ObjectKind::Curve)&&o.points.size()>=2&&!owned.count(o.id)&&selected({o.id})){
        if(o.head)arrows.push_back(&o);
        else if(o.kind==ObjectKind::Line&&!o.dash&&std::abs(o.points.front().x-o.points.back().x)<.1)stems.push_back(&o);
    }
    // A state may contain disconnected molecules/ions. Merge nearby components
    // only inside the same arrow-delimited region; never across a reaction arrow.
    auto separated=[&](const Part& a,const Part& b){
        for(auto* arrow:arrows){Vec start=arrow->points.front(),end=arrow->points.back();
            double low=std::min(a.at.x,b.at.x),high=std::max(a.at.x,b.at.x);
            double mid=(start.x+end.x)/2;
            if(mid>low&&mid<high&&std::abs(start.x-end.x)>std::abs(start.y-end.y)&&
                std::abs((start.y+end.y)/2-(a.at.y+b.at.y)/2)<std::max({25.,a.box.h,b.box.h}))return true;
            double midY=(start.y+end.y)/2;
            if(midY>std::min(a.at.y,b.at.y)&&midY<std::max(a.at.y,b.at.y)&&std::abs(start.y-end.y)>std::abs(start.x-end.x)&&
                std::abs((start.x+end.x)/2-(a.at.x+b.at.x)/2)<std::max({25.,a.box.w,b.box.w}))return true;
        }return false;
    };
    bool merged=true;while(merged){merged=false;
        for(size_t i=0;i<parts.size()&&!merged;i++)for(size_t j=i+1;j<parts.size();j++){
            auto& a=parts[i];auto& b=parts[j];
            double gap=std::max({a.box.x-b.box.x-b.box.w,b.box.x-a.box.x-a.box.w,0.});
            bool grouped=false;for(auto& group:d.groups){
                bool first=std::any_of(a.ids.begin(),a.ids.end(),[&](int id){return has(group.members,id);});
                bool second=std::any_of(b.ids.begin(),b.ids.end(),[&](int id){return has(group.members,id);});
                bool hasArrow=std::any_of(arrows.begin(),arrows.end(),[&](const Object* o){return has(group.members,o->id);});
                if(first&&second&&!hasArrow){grouped=true;break;}
            }
            bool overlapping=gap==0&&a.box.y<b.box.y+b.box.h&&b.box.y<a.box.y+a.box.h;
            if((!grouped&&!overlapping&&(gap>std::max(70.,d.style.bondLength*3)||std::abs(a.at.y-b.at.y)>std::max(25.,std::min(a.box.h,b.box.h)*.7)))||separated(a,b))continue;
            a.ids.insert(a.ids.end(),b.ids.begin(),b.ids.end());
            double right=std::max(a.box.x+a.box.w,b.box.x+b.box.w),bottom=std::max(a.box.y+a.box.h,b.box.y+b.box.h);
            a.box.x=std::min(a.box.x,b.box.x);a.box.y=std::min(a.box.y,b.box.y);a.box.w=right-a.box.x;a.box.h=bottom-a.box.y;a.at=center(a.box);
            parts.erase(parts.begin()+j);merged=true;break;
        }
    }
    if(parts.size()<2||parts.size()>40)throw std::runtime_error("정리할 구조 2~40개와 반응 화살표·텍스트를 선택하세요.");
    // Row clustering retains the drawn rows; no chemical coordinate is rescaled.
    std::sort(parts.begin(),parts.end(),[](const Part& a,const Part& b){return a.at.y<b.at.y;});
    std::vector<std::vector<Part>> rows;for(auto p:parts){if(rows.empty()||std::abs(p.at.y-rows.back().front().at.y)>std::max(40.,p.box.h*.6))rows.push_back({});rows.back().push_back(p);}parts.clear();
    Reaction r;r.id=d.nextId++;r.unit.clear();
    std::vector<double> fonts;for(auto& o:d.objects)if(o.kind==ObjectKind::Text&&selected({o.id})&&!owned.count(o.id)&&o.fontSize>0)fonts.push_back(o.fontSize);
    if(!fonts.empty()){std::sort(fonts.begin(),fonts.end());r.captionFontSize=std::clamp(fonts[fonts.size()/2],6.,48.);r.rowGap=std::max(30.,r.captionFontSize*4);}

    for(size_t y=0;y<rows.size();y++){auto& row=rows[y];std::sort(row.begin(),row.end(),[](const Part& a,const Part& b){return a.at.x<b.at.x;});for(auto p:row){parts.push_back(p);}}
    r.p=parts.front().at;
    // Columns are clustered independently of rows so branches share a column.
    std::vector<double> columns;for(auto& p:parts)columns.push_back(p.at.x);std::sort(columns.begin(),columns.end());std::vector<double> xs;for(double x:columns)if(xs.empty()||x-xs.back()>std::max(40.,d.style.bondLength*2))xs.push_back(x);
    size_t k=0;for(size_t y=0;y<rows.size();y++)for(size_t j=0;j<rows[y].size();j++,k++){size_t col=0;double best=1e30;for(size_t c=0;c<xs.size();c++)if(std::abs(parts[k].at.x-xs[c])<best){best=std::abs(parts[k].at.x-xs[c]);col=c;}r.slotGrid.push_back({double(col),double(y)});r.slots.push_back({"",parts[k].ids,""});}
    std::vector<int> consumed;std::vector<Vec> mids;
    auto nearest=[&](Vec point){size_t best=0;double dist=1e30;for(size_t i=0;i<parts.size();i++){auto b=parts[i].box;double dx=std::max({b.x-point.x,0.,point.x-b.x-b.w}),dy=std::max({b.y-point.y,0.,point.y-b.y-b.h});double v=std::hypot(dx,dy);if(v<dist){dist=v;best=i;}}return best;};
    auto onStem=[](Vec p,const Object& stem){auto a=stem.points.front(),b=stem.points.back();return std::abs(p.x-a.x)<3&&p.y>=std::min(a.y,b.y)-3&&p.y<=std::max(a.y,b.y)+3;};
    for(auto* arrow:arrows){int a=int(nearest(arrow->points.front())),b=int(nearest(arrow->points.back()));
        for(auto* stem:stems)if(onStem(arrow->points.front(),*stem)){
            int nearestSource=int(nearest(arrow->points.front()));a=nearestSource!=b?nearestSource:-1;
            // A horizontal parent arrow crossing this spine supplies the source state.
            for(auto* parent:arrows)if(parent!=arrow){
                if(onStem(parent->points.front(),*stem)){
                    int source=int(nearest(parent->points.front())),target=int(nearest(parent->points.back()));
                    if(source!=target){a=source;break;}continue;
                }
                auto start=parent->points.front(),end=parent->points.back();double x=stem->points.front().x;
                if(std::abs(start.y-end.y)<3&&x>=std::min(start.x,end.x)&&x<=std::max(start.x,end.x)&&
                    start.y>=std::min(stem->points.front().y,stem->points.back().y)-3&&start.y<=std::max(stem->points.front().y,stem->points.back().y)+3){a=int(nearest(start));break;}
            }
            if(!has(consumed,stem->id))consumed.push_back(stem->id);break;
        }
        if(a==b)continue;r.links.push_back({a,b,{}});mids.push_back((arrow->points[arrow->points.size()-2]+arrow->points.back())*.5);consumed.push_back(arrow->id);auto mark=arrow->cross;mark.index=int(r.links.size())-1;if(mark.enabled)r.crosses.push_back(mark);
    }
    if(!r.explicitConnections())throw std::runtime_error("구조 사이 반응 화살표를 찾지 못했습니다. 구조와 화살표를 함께 선택하세요.");
    r.above.resize(r.links.size());r.below.resize(r.links.size());
    for(auto& o:d.objects)if(!o.points.empty()&&!owned.count(o.id)&&selected({o.id})&&!has(consumed,o.id)){
        if(o.kind==ObjectKind::Text&&o.text=="+"){r.slots[nearest(o.points[0])].members.push_back(o.id);continue;}
        if(o.kind==ObjectKind::Text){Vec p=center(bounds(d,{o.id}));size_t state=nearest(p),edge=0;double ds=(p-parts[state].at).length(),de=1e30;for(size_t e=0;e<mids.size();e++){double v=(p-mids[e]).length();if(v<de){de=v;edge=e;}}if(std::min(ds,de)>std::max(180.,d.style.bondLength*6))continue;
            auto numericCaption=importedEnergy(o.text);bool isEnergy=!numericCaption.empty();
            bool onEdge=de<ds;
            if(isEnergy){
                // A caption directly below a structure can be closer to the
                // midpoint of its outgoing vertical arrow than to its center.
                double closest=std::max(d.style.bondLength,r.captionFontSize*3);
                for(size_t i=0;i<parts.size();i++){
                    auto b=parts[i].box;double gap=p.y-(b.y+b.h);
                    if(gap<0||gap>=closest||p.x<b.x-d.style.bondLength*.3||p.x>b.x+b.w+d.style.bondLength*.3)continue;
                    closest=gap;state=i;onEdge=false;
                }
            }
            if(!isEnergy&&!o.text.empty()&&(o.text.front()=='+'||o.text.front()=='-')&&onEdge){r.links[edge].members.push_back(o.id);continue;}std::string& dest=onEdge?(isEnergy?r.below[edge]:r.above[edge]):(isEnergy?r.slots[state].energy:r.slots[state].label);
            // Ambiguous multiple energy rows remain visible instead of being discarded.
            if(isEnergy&&!dest.empty()){
                auto entries=reactionEnergies(dest),extra=reactionEnergies(numericCaption);
                const auto first=numericCaption.find_first_not_of(" \t\r\n");
                if(numericCaption.compare(first,2,"S=")!=0||std::any_of(extra.begin(),extra.end(),[&](const SpinEnergy& item){
                    return std::any_of(entries.begin(),entries.end(),[&](const SpinEnergy& saved){return item.spin==saved.spin;});
                })){
                    (onEdge?r.links[edge].members:r.slots[state].members).push_back(o.id);continue;
                }
                dest+=" / "+numericCaption;
            }else{
                if(!dest.empty())dest+='\n';
                dest+=isEnergy?numericCaption:caption(o);
            }
            consumed.push_back(o.id);
        }else if(o.kind==ObjectKind::Curve){size_t i=nearest(o.points.front());r.slots[i].members.push_back(o.id);}
        else if(o.kind==ObjectKind::Ellipse&&!d.isLocked(o.id)){
            r.slots[nearest(center(bounds(d,{o.id})))].members.push_back(o.id);
        }
    }
    for(auto& p:notations){size_t edge=0;double best=1e30;for(size_t i=0;i<mids.size();i++){double v=(p.at-mids[i]).length();if(v<best){best=v;edge=i;}}if(best<180)r.links[edge].members.insert(r.links[edge].members.end(),p.ids.begin(),p.ids.end());}
    // Dissolve only grouping that crosses frame boundaries; per-molecule groups survive.
    std::map<int,int> frame;for(size_t i=0;i<r.slots.size();i++)for(int id:r.slots[i].members)frame[id]=int(i);for(size_t i=0;i<r.links.size();i++)for(int id:r.links[i].members)frame[id]=int(r.slots.size()+i);
    d.groups.erase(std::remove_if(d.groups.begin(),d.groups.end(),[&](const Group& g){std::set<int> frames;for(int id:g.members)if(frame.count(id))frames.insert(frame[id]);return frames.size()>1;}),d.groups.end());
    eraseExact(d,consumed);
    for(auto& slot:r.slots){std::set<int> covered;size_t components=0;
        for(int id:slot.members)if(d.atom(id)&&!covered.count(id)){auto ids=d.component(id);covered.insert(ids.begin(),ids.end());components++;}
        if(components>1){Group group;group.id=d.nextId++;group.members=slot.members;d.groups.push_back(group);}
    }
    d.reactions.push_back(r);optimizeMechanism(d,{r.id},false);
    if(preserveLayout){
        auto& saved=d.reactions.back();double pitch=saved.slotHeight+std::max(saved.rowGap,mechanismCaptionHeight(saved,Style{})+80);
        for(size_t i=0;i<parts.size();i++)saved.slotGrid[i]={(parts[i].at.x-saved.p.x)/saved.spacing,(parts[i].at.y-saved.p.y)/pitch};
        for(auto& a:d.atoms)if(auto* old=original.atom(a.id))a.p=old->p;
        for(auto& o:d.objects)for(auto& old:original.objects)if(o.id==old.id){o.points=old.points;break;}
    }
    return r.id;
}
int arrangeImportedMechanism(Document& d,const std::vector<int>& selection,bool preserveLayout){Document candidate=d;int id=arrangeImportedMechanismImpl(candidate,selection,preserveLayout);d=std::move(candidate);return id;}

}

#include "mechanism_data.h"
#include <stdexcept>
#include <queue>
#include <map>
#include <numeric>

namespace yd {
namespace {
struct CircularMergePaths {
    std::vector<int> feed,left,right,outlet;
};

std::optional<CircularMergePaths> circularMergePaths(const Reaction& r){
    if(!r.circularMerge||r.slots.empty())return {};
    std::vector<std::vector<int>> next(r.slots.size()),prev(r.slots.size());
    for(const auto& link:r.links){
        if(link.from<0||link.to<0||size_t(link.from)>=r.slots.size()||size_t(link.to)>=r.slots.size())return {};
        next[link.from].push_back(link.to);
        prev[link.to].push_back(link.from);
    }
    int fork=-1,merge=-1;
    for(size_t i=0;i<r.slots.size();i++){
        if(next[i].size()>1){
            if(fork>=0||next[i].size()!=2)return {};
            fork=int(i);
        }
        if(prev[i].size()>1){
            if(merge>=0||prev[i].size()!=2)return {};
            merge=int(i);
        }
    }
    if(fork<0||merge<0||fork==merge)return {};
    CircularMergePaths paths;
    std::set<int> visited{fork,merge};
    auto chain=[&](std::vector<int>& path,int node,const auto& neighbors,int stop){
        while(node!=stop){
            if(node<0||!visited.insert(node).second)return false;
            path.push_back(node);
            node=neighbors[node].empty()?-1:neighbors[node].front();
        }
        return true;
    };
    if(!chain(paths.feed,prev[fork].empty()?-1:prev[fork].front(),prev,-1)||
       !chain(paths.left,next[fork][0],next,merge)||!chain(paths.right,next[fork][1],next,merge)||
       !chain(paths.outlet,next[merge].empty()?-1:next[merge].front(),next,-1)||
       visited.size()!=r.slots.size())return {};
    std::reverse(paths.feed.begin(),paths.feed.end());
    paths.feed.push_back(fork);
    for(auto* lane:{&paths.left,&paths.right}){
        lane->insert(lane->begin(),fork);
        lane->push_back(merge);
    }
    paths.outlet.insert(paths.outlet.begin(),merge);
    return paths;
}
}

Reaction mechanismTemplate(MechanismTemplate layout,int horizontal,int left,int right){
    Reaction r;
    if(layout==MechanismTemplate::Linear){
        r.slots={{"Reactant",{}},{"Intermediate",{}},{"Product",{}}};
        r.above={"Step 1","Step 2"};
        r.below.resize(2);
        return r;
    }
    if(layout==MechanismTemplate::CatalyticCycle){
        r.cyclic=true;
        r.centerLabel="Catalytic cycle";
        r.slots={{"Catalyst",{}},{"Intermediate 1",{}},{"Intermediate 2",{}},{"Intermediate 3",{}}};
        r.above={"TS1","TS2","TS3","TS4"};
        r.below.resize(4);
        return r;
    }
    r.fixedFrames=true;
    if(layout==MechanismTemplate::FeedIntoCycle){
        r.slots={{"Reactant",{}},{"Intermediate",{}},{"Cycle entry",{}},{"Intermediate 2",{}},{"Intermediate 3",{}},{"Intermediate 4",{}}};
        r.slotGrid={{-1,0},{0,0},{0,1},{1.35,2},{0,3},{-1.35,2}};
        r.links={{0,1,{}},{1,2,{}},{2,3,{},-.38},{3,4,{},-.38},{4,5,{},-.38},{5,2,{},-.38}};
    }else{
        if(horizontal<1||horizontal>37||left<0||left>37||right<0||right>37||
           left+right==0||horizontal+left+right+3>40)
            throw std::runtime_error("가로 화살표는 1개 이상, 좌우 중간체는 합계 1개 이상, 전체 구조는 최대 40개입니다.");
        r.circularMerge=true;
        for(int i=0;i<=horizontal;i++){
            r.slots.push_back({i==0?"Reactant":"Intermediate "+std::to_string(i),{}});
            r.slotGrid.push_back({double(i-horizontal),0});
            r.links.push_back({i,i+1,{}});
        }
        const int fork=int(r.slots.size()),merge=fork+left+right+1;
        r.slots.push_back({"Branch point",{}});
        r.slotGrid.push_back({0,1});
        for(auto [count,name]:{std::pair{left,"Left"},std::pair{right,"Right"}}){
            int previous=fork;
            for(int i=0;i<count;i++){
                int stage=int(r.slots.size());
                r.links.push_back({previous,stage,{}});
                r.slots.push_back({std::string(name)+" intermediate "+std::to_string(i+1),{}});
                r.slotGrid.push_back({});
                previous=stage;
            }
            r.links.push_back({previous,merge,{}});
        }
        r.slots.push_back({"Product",{}});
        r.slotGrid.push_back({});
        arrangeCircularMerge(r,Style{});
    }
    r.above.resize(r.links.size());
    r.below.resize(r.links.size());
    for(size_t i=0;i<r.links.size();i++)r.above[i]="TS"+std::to_string(i+1);
    return r;
}

bool arrangeCircularMerge(Reaction& r,const Style& style){
    const auto paths=circularMergePaths(r);
    if(!paths)return false;
    const double pitch=r.slotHeight+std::max(r.rowGap,mechanismCaptionHeight(r,Style{})+80);
    const double font=mechanismFontSize(r,style),margin=mechanismArrowMargin(r,style);
    double stageGap=std::max(r.spacing,pitch+std::max(0.,mechanismCaptionHeight(r,style)-mechanismCaptionHeight(r,Style{})));
    for(const auto* lane:{&paths->left,&paths->right})
        for(int stage:*lane)stageGap=std::max(stageGap,mechanismStageCaptionWidth(r.slots[stage],font)+2*margin);
    const size_t segments=std::max(paths->left.size(),paths->right.size())-1;
    const double radius=stageGap/(2*std::sin(pi/(2*segments)));
    auto& grid=r.slotGrid;
    size_t elbow=0;
    const auto& feed=paths->feed;
    while(elbow+1<feed.size()&&std::abs(grid[feed[elbow+1]].y-grid[feed.front()].y)<1e-6)elbow++;
    for(size_t i=0;i<feed.size();i++)
        grid[feed[i]]={std::min(0.,double(i)-double(elbow)),std::max(0.,double(i)-double(elbow))};
    const double top=grid[feed.back()].y;
    const Vec center{0,top+radius/pitch};
    auto place=[&](const std::vector<int>& lane,double direction){
        for(size_t i=1;i+1<lane.size();i++){
            const double angle=pi*i/(lane.size()-1);
            grid[lane[i]]=center+Vec{direction*radius*std::sin(angle)/r.spacing,-radius*std::cos(angle)/pitch};
        }
    };
    place(paths->left,-1);
    place(paths->right,1);
    const auto& outlet=paths->outlet;
    Vec previousPosition=grid[outlet.front()];
    grid[outlet.front()]={0,top+2*radius/pitch};
    for(size_t i=1;i<outlet.size();i++){
        Vec position=grid[outlet[i]],delta=position-previousPosition;
        grid[outlet[i]]=grid[outlet[i-1]]+(std::abs(delta.y)>.1?Vec{0,1}:Vec{1,0});
        previousPosition=position;
    }
    return true;
}

std::optional<MechanismArc> mechanismLinkArc(const Reaction& r,size_t index,const Style& style){
    const auto paths=circularMergePaths(r);
    if(!paths)return {};
    const auto& link=r.links.at(index);
    const Vec top=reactionSlot(r,size_t(paths->feed.back())),bottom=reactionSlot(r,size_t(paths->outlet.front()));
    const double radius=(bottom.y-top.y)/2;
    if(radius<=0)return {};
    const Vec center=(top+bottom)*.5;
    for(const auto* lane:{&paths->left,&paths->right}){
        auto from=std::find(lane->begin(),lane->end(),link.from);
        if(from==lane->end()||from+1==lane->end()||from[1]!=link.to)continue;
        const double sweep=(lane==&paths->left?-pi:pi)/(lane->size()-1);
        const double start=-pi/2+double(from-lane->begin())*sweep,end=start+sweep;
        const double margin=mechanismArrowMargin(r,style);
        auto trim=[&](int stage,double angle,double direction){
            const Vec anchor=reactionSlot(r,size_t(stage));
            const auto& slot=r.slots[stage];
            const double caption=slot.label.empty()&&slot.energy.empty()?0:mechanismCaptionHeight(r,style);
            double inside=0,outside=std::abs(sweep)*.5;
            const bool below=center.y+std::sin(angle+direction*outside)*radius>anchor.y;
            const double width=below?std::max(r.slotWidth,mechanismStageCaptionWidth(slot,mechanismFontSize(r,style))):r.slotWidth;
            for(int i=0;i<24;i++){
                const double offset=(inside+outside)*.5,at=angle+direction*offset;
                Vec delta=center+Vec{std::cos(at),std::sin(at)}*radius-anchor;
                if(std::abs(delta.x)<width/2+margin&&delta.y>-r.slotHeight/2-margin&&
                   delta.y<r.slotHeight/2+margin+caption)inside=offset;
                else outside=offset;
            }
            return direction*std::min(outside,std::abs(sweep)*.45);
        };
        const double direction=std::copysign(1.,sweep);
        return MechanismArc{center,radius,start+trim(link.from,start,direction),end+trim(link.to,end,-direction)};
    }
    return {};
}

size_t splitMechanismLink(Reaction& r,size_t edge){
    if(r.slots.size()>=40||r.links.size()>=100)throw std::runtime_error("구조는 최대 40개, 화살표는 최대 100개입니다.");
    Reaction result=r;
    if(!result.explicitConnections()){
        const auto count=result.slots.empty()?0:result.slots.size()-(result.cyclic?0:1);
        if(edge>=count)throw std::out_of_range("mechanism connection");
        const auto stage=edge+1;
        result.slots.insert(result.slots.begin()+stage,Slot{"Intermediate "+std::to_string(stage+1),{}});
        result.above.resize(count);result.below.resize(count);
        result.above.insert(result.above.begin()+stage,"TS"+std::to_string(count+1));
        result.below.insert(result.below.begin()+stage,"");
        for(auto& c:result.captionColors)if((c.role==1||c.role==9)?c.index>=int(stage):c.index>int(edge))++c.index;
        for(auto& c:result.crosses)if(c.index>int(edge))++c.index;
        for(auto& hidden:result.hiddenArrows)if(hidden>int(edge))++hidden;
        r=std::move(result);return stage;
    }
    const auto link=result.links.at(edge);
    if(link.from<0)throw std::runtime_error("시작 구조가 있는 화살표를 선택하세요.");
    const size_t stage=result.slots.size();
    result.slots.push_back({"Intermediate "+std::to_string(stage+1),{}});
    result.slotGrid.push_back((result.slotGrid.at(link.from)+result.slotGrid.at(link.to))*.5);
    result.links[edge].to=int(stage);
    result.links.push_back({int(stage),link.to,{},link.bend});
    result.above.resize(result.links.size());result.below.resize(result.links.size());
    result.above.back()="TS"+std::to_string(result.links.size());
    r=std::move(result);return stage;
}

void makeMechanismGraph(Reaction& r){
    if(r.explicitConnections()){r.manualConnections=true;return;}
    std::vector<Vec> frames;double pitch=r.slotHeight+std::max(r.rowGap,mechanismCaptionHeight(r,Style{})+80);
    for(size_t i=0;i<r.slots.size();i++){auto p=reactionSlot(r,i)-r.p;frames.push_back({p.x/r.spacing,p.y/pitch});}
    for(size_t i=0;i<r.slots.size();i++)if(i+1<r.slots.size()||r.cyclic)r.links.push_back({int(i),int((i+1)%r.slots.size()),{}});
    r.slotGrid=std::move(frames);r.manualConnections=true;r.cyclic=false;r.vertical=false;r.wrapColumns=0;
}
static void remapEdges(Reaction& r,const std::vector<int>& keep){
    auto before=r;r.links.clear();r.above.clear();r.below.clear();r.hiddenArrows.clear();r.crosses.clear();
    r.captionColors.erase(std::remove_if(r.captionColors.begin(),r.captionColors.end(),[](const CaptionColor& c){return c.role==2||c.role==3||c.role==11;}),r.captionColors.end());
    for(size_t n=0;n<keep.size();n++){size_t old=size_t(keep[n]);r.links.push_back(before.links.at(old));r.above.push_back(old<before.above.size()?before.above[old]:"");r.below.push_back(old<before.below.size()?before.below[old]:"");
        if(std::find(before.hiddenArrows.begin(),before.hiddenArrows.end(),int(old))!=before.hiddenArrows.end())r.hiddenArrows.push_back(int(n));
        for(auto mark:before.crosses)if(mark.index==int(old)){mark.index=int(n);r.crosses.push_back(mark);}
        for(auto color:before.captionColors)if((color.role==2||color.role==3||color.role==11)&&color.index==int(old)){color.index=int(n);r.captionColors.push_back(color);}
    }
}
bool mechanismLinkSelected(const MechanismLink& link,const std::set<size_t>& stages){
    return stages.count(size_t(link.to))&&(link.from<0||stages.count(size_t(link.from)));
}
Document copyMechanismStages(const Document& document,int owner,const std::set<size_t>& stages){
    Document source=document;
    auto found=std::find_if(source.reactions.begin(),source.reactions.end(),[=](const Reaction& r){return r.id==owner;});
    if(found==source.reactions.end()||!found->explicitConnections()||stages.empty()||*stages.rbegin()>=found->slots.size())
        throw std::runtime_error("복사할 메커니즘 단계를 선택하세요.");
    const Reaction original=*found;
    auto& r=*found;
    r.manualConnections=true;
    r.slots.clear();
    r.slotGrid.clear();
    std::vector<int> stageMap(original.slots.size(),-1);
    for(size_t i:stages){
        stageMap[i]=int(r.slots.size());
        r.slots.push_back(original.slots[i]);
    }
    // Keep the copied molecules aligned with the frames even if caption height changes.
    const double pitch=r.slotHeight+std::max(r.rowGap,mechanismCaptionHeight(r,Style{})+80);
    for(size_t i:stages){
        Vec delta=reactionSlot(original,i)-r.p;
        r.slotGrid.push_back({delta.x/r.spacing,delta.y/pitch});
    }
    std::vector<int> edges;
    for(size_t i=0;i<original.links.size();++i)
        if(mechanismLinkSelected(original.links[i],stages))edges.push_back(int(i));
    remapEdges(r,edges);
    for(auto& link:r.links){
        if(link.from>=0)link.from=stageMap.at(size_t(link.from));
        link.to=stageMap.at(size_t(link.to));
    }
    auto stageColor=[](const CaptionColor& c){return c.role==1||c.role==9;};
    r.captionColors.erase(std::remove_if(r.captionColors.begin(),r.captionColors.end(),[&](const CaptionColor& c){
        return stageColor(c)&&!stages.count(size_t(c.index));
    }),r.captionColors.end());
    for(auto& c:r.captionColors)if(stageColor(c))c.index=stageMap.at(size_t(c.index));

    // A group spanning several branches must not expand the selection.
    std::set<int> members{owner};
    for(auto& s:r.slots)members.insert(s.members.begin(),s.members.end());
    for(auto& l:r.links)members.insert(l.members.begin(),l.members.end());
    for(auto& b:source.bonds)if(members.count(b.a)&&members.count(b.b))members.insert(b.id);
    for(size_t previous=0;previous!=members.size();){
        previous=members.size();
        for(auto& g:source.groups)if(!g.members.empty()&&std::all_of(g.members.begin(),g.members.end(),[&](int id){
            return members.count(id)&&!source.isLocked(id);
        }))members.insert(g.id);
    }
    source.groups.erase(std::remove_if(source.groups.begin(),source.groups.end(),[&](const Group& g){
        return !members.count(g.id);
    }),source.groups.end());
    Document fragment=source.subset({owner});
    auto copied=fragment.allIds();
    std::set<int> copiedIds(copied.begin(),copied.end());
    auto prune=[&](std::vector<int>& ids){
        ids.erase(std::remove_if(ids.begin(),ids.end(),[&](int id){return !copiedIds.count(id);}),ids.end());
    };
    for(auto& s:fragment.reactions.front().slots)prune(s.members);
    for(auto& l:fragment.reactions.front().links)prune(l.members);
    return fragment;
}
size_t connectMechanismStages(Reaction& r,int from,int to,int replace){
    if(from<-1||to<0||from>=int(r.slots.size())||to>=int(r.slots.size())||from==to)throw std::runtime_error("서로 다른 시작·도착 단계를 지정하세요.");
    size_t count=r.explicitConnections()?r.links.size():r.slots.empty()?0:r.slots.size()-(r.cyclic?0:1);
    if(replace>=0&&size_t(replace)>=count)throw std::runtime_error("수정할 연결을 찾을 수 없습니다.");
    makeMechanismGraph(r);
    for(size_t i=0;i<r.links.size();i++)if(int(i)!=replace&&r.links[i].from==from&&r.links[i].to==to){if(replace>=0)throw std::runtime_error("이미 있는 연결입니다. 기존 연결을 제거한 뒤 변경하세요.");r.hiddenArrows.erase(std::remove(r.hiddenArrows.begin(),r.hiddenArrows.end(),int(i)),r.hiddenArrows.end());return i;}
    if(replace>=0){if(size_t(replace)>=r.links.size())throw std::runtime_error("수정할 연결을 찾을 수 없습니다.");r.links[size_t(replace)].from=from;r.links[size_t(replace)].to=to;r.hiddenArrows.erase(std::remove(r.hiddenArrows.begin(),r.hiddenArrows.end(),replace),r.hiddenArrows.end());return size_t(replace);}
    if(r.links.size()>=100)throw std::runtime_error("화살표 연결은 최대 100개입니다.");
    r.links.push_back({from,to,{}});r.above.resize(r.links.size());r.below.resize(r.links.size());return r.links.size()-1;
}
void removeMechanismConnection(Reaction& r,size_t edge){makeMechanismGraph(r);if(edge>=r.links.size())throw std::runtime_error("삭제할 연결을 찾을 수 없습니다.");std::vector<int> keep;for(size_t i=0;i<r.links.size();i++)if(i!=edge)keep.push_back(int(i));remapEdges(r,keep);}
void insertMechanismStage(Reaction& r,size_t before){
    if(before>r.slots.size()||r.slots.size()>=40)throw std::runtime_error("단계는 최대 40개까지 추가할 수 있습니다.");makeMechanismGraph(r);
    Vec p=before<r.slotGrid.size()?r.slotGrid[before]:r.slotGrid.empty()?Vec{}:r.slotGrid.back()+Vec{1,0};
    for(size_t i=before;i<r.slotGrid.size();i++)r.slotGrid[i].x++;
    for(auto& link:r.links){if(link.from>=int(before))link.from++;if(link.to>=int(before))link.to++;}
    for(auto& c:r.captionColors)if((c.role==1||c.role==9)&&c.index>=int(before))c.index++;
    r.slots.insert(r.slots.begin()+before,Slot{});r.slotGrid.insert(r.slotGrid.begin()+before,p);
    if(before+1<r.slots.size()){for(auto& link:r.links)if(link.to==int(before)+1)link.to=int(before);connectMechanismStages(r,int(before),int(before)+1);}
    else if(before>0)connectMechanismStages(r,int(before)-1,int(before));
}
void removeMechanismStage(Document& d,int owner,size_t stage,int mergeInto,bool deleteContents){
    auto r=std::find_if(d.reactions.begin(),d.reactions.end(),[&](const Reaction& r){return r.id==owner;});
    if(r==d.reactions.end()||stage>=r->slots.size()||d.isLocked(owner))throw std::runtime_error("제거할 단계를 선택하세요.");
    if(mergeInto>=int(r->slots.size())||mergeInto==int(stage))throw std::runtime_error("합칠 다른 단계를 지정하세요.");
    const Reaction original=*r;const bool keepCycle=r->cyclic&&!r->explicitConnections()&&mergeInto<0;
    makeMechanismGraph(*r);auto members=r->slots[stage].members;
    if(mergeInto<0&&circularMergePaths(*r)){
        auto incoming=std::find_if(r->links.begin(),r->links.end(),[=](const MechanismLink& l){return l.to==int(stage);});
        auto outgoing=std::find_if(r->links.begin(),r->links.end(),[=](const MechanismLink& l){return l.from==int(stage);});
        if(incoming!=r->links.end()&&outgoing!=r->links.end()&&
           std::count_if(r->links.begin(),r->links.end(),[=](const MechanismLink& l){return l.to==int(stage)||l.from==int(stage);})==2){
            if(std::any_of(r->links.begin(),r->links.end(),[&](const MechanismLink& l){return l.from==incoming->from&&l.to==outgoing->to;}))
                throw std::runtime_error("좌우 경로 중 한 곳에 중간체를 한 개 이상 유지하세요.");
            incoming->to=outgoing->to;
        }
    }
    if(mergeInto>=0){auto& dest=r->slots[size_t(mergeInto)];dest.members.insert(dest.members.end(),members.begin(),members.end());if(dest.label.empty())dest.label=r->slots[stage].label;if(dest.energy.empty())dest.energy=r->slots[stage].energy;}
    std::vector<int> keep;std::set<std::pair<int,int>> pairs;
    for(size_t i=0;i<r->links.size();i++){auto& link=r->links[i];if(link.from==int(stage)||link.to==int(stage)){if(mergeInto<0)continue;if(link.from==int(stage))link.from=mergeInto;if(link.to==int(stage))link.to=mergeInto;}
        if(link.from==link.to||!pairs.insert({link.from,link.to}).second)continue;keep.push_back(int(i));}
    remapEdges(*r,keep);
    for(auto& link:r->links){if(link.from>int(stage))link.from--;if(link.to>int(stage))link.to--;}
    r->slots.erase(r->slots.begin()+stage);r->slotGrid.erase(r->slotGrid.begin()+stage);
    r->captionColors.erase(std::remove_if(r->captionColors.begin(),r->captionColors.end(),[&](const CaptionColor& c){return (c.role==1||c.role==9)&&c.index==int(stage);}),r->captionColors.end());
    for(auto& c:r->captionColors)if((c.role==1||c.role==9)&&c.index>int(stage))c.index--;
    if(keepCycle&&!r->slots.empty()){
        r->cyclic=true;r->manualConnections=false;r->fixedFrames=false;r->links.clear();r->slotGrid.clear();
        r->above=original.above;r->below=original.below;
        r->above.resize(original.slots.size());r->below.resize(original.slots.size());
        r->above.erase(r->above.begin()+stage);r->below.erase(r->below.begin()+stage);
        r->captionColors=original.captionColors;
        r->captionColors.erase(std::remove_if(r->captionColors.begin(),r->captionColors.end(),[&](auto& c){return c.index==int(stage);}),r->captionColors.end());
        for(auto& c:r->captionColors)if(c.index>int(stage))--c.index;
        r->crosses=original.crosses;r->crosses.erase(std::remove_if(r->crosses.begin(),r->crosses.end(),[&](auto& c){return c.index==int(stage);}),r->crosses.end());
        for(auto& c:r->crosses)if(c.index>int(stage))--c.index;
        r->hiddenArrows.clear();for(int i:original.hiddenArrows)if(i!=int(stage))r->hiddenArrows.push_back(i>int(stage)?i-1:i);
    }
    if(deleteContents){for(auto& g:d.groups)g.members.erase(std::remove_if(g.members.begin(),g.members.end(),[&](int id){return std::find(members.begin(),members.end(),id)!=members.end();}),g.members.end());d.erase(members);}
    if(r->slots.empty()){d.reactions.erase(std::remove_if(d.reactions.begin(),d.reactions.end(),[&](const Reaction& value){return value.id==owner;}),d.reactions.end());for(auto& g:d.groups)g.members.erase(std::remove(g.members.begin(),g.members.end(),owner),g.members.end());}
}
void arrangeMechanismGraph(Reaction& r){
    if(!r.explicitConnections()||r.slots.empty()||r.fixedFrames)return;
    const size_t n=r.slots.size();auto old=r.slotGrid;old.resize(n);
    // Keep the drawing's rows globally. Centering each depth independently folds
    // unequal-length parallel pathways into one row as soon as a short path ends.
    auto cluster=[&](bool horizontal){
        std::vector<size_t> order(n);std::iota(order.begin(),order.end(),0);
        auto value=[&](size_t i){return horizontal?old[i].x:old[i].y;};
        std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return value(a)<value(b);});
        std::vector<int> group(n);int rank=-1;double first=0;
        for(size_t i:order){if(rank<0||value(i)-first>.25){rank++;first=value(i);}group[i]=rank;}return group;
    };
    auto oldColumn=cluster(true),oldRow=cluster(false);
    std::vector<std::vector<int>> next(n),previous(n);
    for(auto l:r.links)if(l.from>=0){next.at(size_t(l.from)).push_back(l.to);previous.at(size_t(l.to)).push_back(l.from);}
    // Collapse cycles before assigning columns. Previously every unresolved node
    // (including everything downstream of a cycle) received another new column.
    std::vector<int> discovery(n,-1),low(n),component(n,-1),stack;std::vector<bool> active(n);int clock=0,count=0;
    std::function<void(int)> visit=[&](int a){
        discovery[size_t(a)]=low[size_t(a)]=clock++;stack.push_back(a);active[size_t(a)]=true;
        for(int b:next[size_t(a)]){if(discovery[size_t(b)]<0){visit(b);low[size_t(a)]=std::min(low[size_t(a)],low[size_t(b)]);}else if(active[size_t(b)])low[size_t(a)]=std::min(low[size_t(a)],discovery[size_t(b)]);}
        if(low[size_t(a)]==discovery[size_t(a)]){for(;;){int b=stack.back();stack.pop_back();active[size_t(b)]=false;component[size_t(b)]=count;if(a==b)break;}count++;}
    };
    for(size_t i=0;i<n;i++)if(discovery[i]<0)visit(int(i));
    std::vector<int> first(static_cast<size_t>(count),static_cast<int>(n)),last(static_cast<size_t>(count),-1),degree(static_cast<size_t>(count),0),start(static_cast<size_t>(count),0);std::vector<std::set<int>> successors{size_t(count)};
    for(size_t i=0;i<n;i++){size_t c=size_t(component[i]);first[c]=std::min(first[c],oldColumn[i]);last[c]=std::max(last[c],oldColumn[i]);}
    for(size_t a=0;a<n;a++)for(int b:next[a])if(component[a]!=component[size_t(b)]&&successors[size_t(component[a])].insert(component[size_t(b)]).second)degree[size_t(component[size_t(b)])]++;
    std::queue<int> queue;for(int c=0;c<count;c++){if(!degree[size_t(c)])queue.push(c);}
    // Unconnected standalone frames keep their existing columns. Connected DAG
    // branches use depth, so siblings created by dragging still align together.
    for(size_t i=0;i<n;i++)if(next[i].empty()&&previous[i].empty())start[size_t(component[i])]=oldColumn[i];
    while(!queue.empty()){int a=queue.front();queue.pop();int end=start[size_t(a)]+last[size_t(a)]-first[size_t(a)]+1;
        for(int b:successors[size_t(a)]){start[size_t(b)]=std::max(start[size_t(b)],end);if(--degree[size_t(b)]==0)queue.push(b);}}
    std::map<int,std::vector<size_t>> columns;
    for(size_t i=0;i<n;i++){size_t c=size_t(component[i]);int col=start[c]+oldColumn[i]-first[c];columns[col].push_back(i);}
    const int existingRows=*std::max_element(oldRow.begin(),oldRow.end())+1;std::vector<int> placedRow(n,-1);r.slotGrid.resize(n);
    int compactColumn=0;
    for(auto& column:columns){auto& nodes=column.second;std::stable_sort(nodes.begin(),nodes.end(),[&](size_t a,size_t b){return oldRow[a]<oldRow[b];});std::set<int> occupied;
        for(size_t i:nodes){int row=oldRow[i],distance=int(n)+1;
            // A continuation follows its predecessor's lane, including a lane
            // allocated to a newly dragged branch. Explicit row changes survive.
            for(int parent:previous[i]){size_t a=size_t(parent);if(placedRow[a]>=0&&oldRow[a]==oldRow[i]){int delta=std::abs(placedRow[a]-oldRow[i]);if(delta<distance){distance=delta;row=placedRow[a];}}}
            if(occupied.count(row)){row=existingRows;while(occupied.count(row))row++;}
            placedRow[i]=row;occupied.insert(row);r.slotGrid[i]={double(compactColumn),double(row)};
        }
        compactColumn++;
    }
}
Reaction mechanismGraphData(const Reaction& original,const MechanismRows& rows){
    Reaction result=original;makeMechanismGraph(result);auto base=result;result.slots.clear();result.slotGrid.clear();result.links.clear();result.above.clear();result.below.clear();result.hiddenArrows.clear();result.crosses.clear();result.captionColors.clear();
    std::map<int,int> states,edgeMap;std::set<int> usedEdges;
    auto integer=[](const std::string& s,int min,int max){size_t n=0;int v=std::stoi(s,&n);if(n!=s.size()||v<min||v>max)throw std::runtime_error("올바른 단계·연결 번호를 입력하세요.");return v;};
    auto decimal=[](const std::string& s,double limit){
        size_t n=0;double v=std::stod(s,&n);
        if(n!=s.size()||!std::isfinite(v)||std::abs(v)>limit)throw std::runtime_error("배치 좌표 또는 곡률이 범위를 벗어났습니다.");
        return v;
    };
    for(auto& row:rows){if(row.size()!=6&&row.size()!=7)throw std::runtime_error("구조·화살표 표는 6열 또는 7열입니다.");if(row[0].empty()&&row[4].empty())continue;if(row[0]!="구조")continue;
        int id=row[1].empty()?int(base.slots.size()+1):integer(row[1],1,1000);if(row[1].empty())while(states.count(id))id++;if(states.count(id))throw std::runtime_error("구조 번호를 중복할 수 없습니다. 새 구조의 번호는 비워 두세요.");
        states[id]=int(result.slots.size());Slot slot=id<=int(base.slots.size())?base.slots[size_t(id-1)]:Slot{};slot.label=row[4];slot.energy=row[5];result.slots.push_back(slot);result.slotGrid.push_back(id<=int(base.slotGrid.size())?base.slotGrid[size_t(id-1)]:Vec{double(result.slots.size()-1),0});
        if(result.fixedFrames){
            auto& p=result.slotGrid.back();
            if(!row[2].empty())p.x=decimal(row[2],100);
            if(!row[3].empty())p.y=decimal(row[3],100);
        }
    }
    if(result.slots.empty()||result.slots.size()>40)throw std::runtime_error("구조는 1~40개여야 합니다.");
    for(auto& row:rows){if(row[0].empty()||row[0]=="구조")continue;if(row[0]!="화살표")throw std::runtime_error("구분은 구조 또는 화살표로 입력하세요.");int from=integer(row[2],0,1000),to=integer(row[3],1,1000);if((from&&!states.count(from))||!states.count(to))continue;
        int a=from?states[from]:-1,b=states[to];if(a==b)throw std::runtime_error("같은 구조로 연결할 수 없습니다.");
        if(std::any_of(result.links.begin(),result.links.end(),[&](const MechanismLink& l){return l.from==a&&l.to==b;}))throw std::runtime_error("중복 연결을 제거하세요.");
        MechanismLink l{a,b,{}};int old=row[1].empty()?-1:integer(row[1],1,int(base.links.size()))-1;
        if(old>=0){if(!usedEdges.insert(old).second)throw std::runtime_error("복제한 새 화살표의 번호는 비워 두세요.");l.members=base.links[size_t(old)].members;l.bend=base.links[size_t(old)].bend;}
        if(row.size()==7)l.bend=row[6].empty()?0:decimal(row[6],2);
        result.links.push_back(l);result.above.push_back(row[4]);result.below.push_back(row[5]);if(result.links.size()>100)throw std::runtime_error("화살표 연결은 최대 100개입니다.");if(old>=0)edgeMap[old]=int(result.links.size()-1);
        if(old>=0)for(auto mark:base.crosses)if(mark.index==old){mark.index=int(result.links.size()-1);result.crosses.push_back(mark);}
    }
    for(auto c:base.captionColors){if((c.role==1||c.role==9)&&states.count(c.index+1)){c.index=states[c.index+1];result.captionColors.push_back(c);}else if((c.role==2||c.role==3||c.role==11)&&edgeMap.count(c.index)){c.index=edgeMap[c.index];result.captionColors.push_back(c);}else if(c.role==10)result.captionColors.push_back(c);}
    return result;
}
}

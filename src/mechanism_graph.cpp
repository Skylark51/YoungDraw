#include "mechanism_data.h"
#include <stdexcept>
#include <queue>
#include <map>
#include <numeric>

namespace yd {
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
    makeMechanismGraph(*r);auto members=r->slots[stage].members;
    if(mergeInto>=0){auto& dest=r->slots[size_t(mergeInto)];dest.members.insert(dest.members.end(),members.begin(),members.end());if(dest.label.empty())dest.label=r->slots[stage].label;if(dest.energy.empty())dest.energy=r->slots[stage].energy;}
    std::vector<int> keep;std::set<std::pair<int,int>> pairs;
    for(size_t i=0;i<r->links.size();i++){auto& link=r->links[i];if(link.from==int(stage)||link.to==int(stage)){if(mergeInto<0)continue;if(link.from==int(stage))link.from=mergeInto;if(link.to==int(stage))link.to=mergeInto;}
        if(link.from==link.to||!pairs.insert({link.from,link.to}).second)continue;keep.push_back(int(i));}
    remapEdges(*r,keep);
    for(auto& link:r->links){if(link.from>int(stage))link.from--;if(link.to>int(stage))link.to--;}
    r->slots.erase(r->slots.begin()+stage);r->slotGrid.erase(r->slotGrid.begin()+stage);
    r->captionColors.erase(std::remove_if(r->captionColors.begin(),r->captionColors.end(),[&](const CaptionColor& c){return (c.role==1||c.role==9)&&c.index==int(stage);}),r->captionColors.end());
    for(auto& c:r->captionColors)if((c.role==1||c.role==9)&&c.index>int(stage))c.index--;
    if(deleteContents){for(auto& g:d.groups)g.members.erase(std::remove_if(g.members.begin(),g.members.end(),[&](int id){return std::find(members.begin(),members.end(),id)!=members.end();}),g.members.end());d.erase(members);}
    if(r->slots.empty()){d.reactions.erase(std::remove_if(d.reactions.begin(),d.reactions.end(),[&](const Reaction& value){return value.id==owner;}),d.reactions.end());for(auto& g:d.groups)g.members.erase(std::remove(g.members.begin(),g.members.end(),owner),g.members.end());}
}
void arrangeMechanismGraph(Reaction& r){
    if(!r.explicitConnections()||r.slots.empty())return;
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
    for(auto& row:rows){if(row.size()!=6)throw std::runtime_error("구조·화살표 표는 6열입니다.");if(row[0].empty()&&row[4].empty())continue;if(row[0]!="구조")continue;
        int id=row[1].empty()?int(base.slots.size()+1):integer(row[1],1,1000);if(row[1].empty())while(states.count(id))id++;if(states.count(id))throw std::runtime_error("구조 번호를 중복할 수 없습니다. 새 구조의 번호는 비워 두세요.");
        states[id]=int(result.slots.size());Slot slot=id<=int(base.slots.size())?base.slots[size_t(id-1)]:Slot{};slot.label=row[4];slot.energy=row[5];result.slots.push_back(slot);result.slotGrid.push_back(id<=int(base.slotGrid.size())?base.slotGrid[size_t(id-1)]:Vec{double(result.slots.size()-1),0});
    }
    if(result.slots.empty()||result.slots.size()>40)throw std::runtime_error("구조는 1~40개여야 합니다.");
    for(auto& row:rows){if(row[0].empty()||row[0]=="구조")continue;if(row[0]!="화살표")throw std::runtime_error("구분은 구조 또는 화살표로 입력하세요.");int from=integer(row[2],0,1000),to=integer(row[3],1,1000);if((from&&!states.count(from))||!states.count(to))continue;
        int a=from?states[from]:-1,b=states[to];if(a==b)throw std::runtime_error("같은 구조로 연결할 수 없습니다.");
        if(std::any_of(result.links.begin(),result.links.end(),[&](const MechanismLink& l){return l.from==a&&l.to==b;}))throw std::runtime_error("중복 연결을 제거하세요.");
        MechanismLink l{a,b,{}};int old=row[1].empty()?-1:integer(row[1],1,int(base.links.size()))-1;
        if(old>=0){if(!usedEdges.insert(old).second)throw std::runtime_error("복제한 새 화살표의 번호는 비워 두세요.");l.members=base.links[size_t(old)].members;}
        result.links.push_back(l);result.above.push_back(row[4]);result.below.push_back(row[5]);if(result.links.size()>100)throw std::runtime_error("화살표 연결은 최대 100개입니다.");if(old>=0)edgeMap[old]=int(result.links.size()-1);
        if(old>=0)for(auto mark:base.crosses)if(mark.index==old){mark.index=int(result.links.size()-1);result.crosses.push_back(mark);}
    }
    for(auto c:base.captionColors){if((c.role==1||c.role==9)&&states.count(c.index+1)){c.index=states[c.index+1];result.captionColors.push_back(c);}else if((c.role==2||c.role==3||c.role==11)&&edgeMap.count(c.index)){c.index=edgeMap[c.index];result.captionColors.push_back(c);}else if(c.role==10)result.captionColors.push_back(c);}
    return result;
}
}

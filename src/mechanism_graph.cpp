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
    const size_t n=r.slots.size();std::vector<int> degree(n),rank(n),done(n);std::vector<std::vector<int>> next(n);
    for(auto l:r.links)if(l.from>=0){next.at(size_t(l.from)).push_back(l.to);degree.at(size_t(l.to))++;}
    std::queue<int> queue;for(size_t i=0;i<n;i++)if(!degree[i])queue.push(int(i));
    while(!queue.empty()){int a=queue.front();queue.pop();done[size_t(a)]=1;for(int b:next[size_t(a)]){rank[size_t(b)]=std::max(rank[size_t(b)],rank[size_t(a)]+1);if(--degree[size_t(b)]==0)queue.push(b);}}
    int last=*std::max_element(rank.begin(),rank.end());for(size_t i=0;i<n;i++)if(!done[i])rank[i]=++last;
    if(r.links.empty())for(size_t i=0;i<n;i++)rank[i]=int(i);
    std::map<int,std::vector<size_t>> columns;for(size_t i=0;i<n;i++)columns[rank[i]].push_back(i);
    auto old=r.slotGrid;r.slotGrid.resize(n);
    for(auto& column:columns){auto& list=column.second;std::stable_sort(list.begin(),list.end(),[&](size_t a,size_t b){return a<old.size()&&b<old.size()?old[a].y<old[b].y:a<b;});for(size_t j=0;j<list.size();j++)r.slotGrid[list[j]]={double(column.first),double(j)-(list.size()-1)*.5};}
    for(auto column=columns.rbegin();column!=columns.rend();++column)if(column->second.size()==1){size_t a=column->second[0];double total=0;size_t count=0;for(int b:next[a])if(rank[size_t(b)]>rank[a]){total+=r.slotGrid[size_t(b)].y;count++;}if(count)r.slotGrid[a].y=total/count;}
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

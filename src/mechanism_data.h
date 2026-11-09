#pragma once
#include "model.h"
#include <functional>
namespace yd {
struct MechanismArc {
    Vec center;
    double radius,start,end;
    Vec point(double angle) const {return center+Vec{std::cos(angle),std::sin(angle)}*radius;}
};
std::optional<MechanismArc> mechanismLinkArc(const Reaction&,size_t,const Style&);
std::vector<Vec> mechanismLinkPath(const Reaction&,size_t,const Style&);
Vec mechanismLinkCaption(const Reaction&,size_t,const Style&);
struct SpinEnergy {double spin=0;std::string value;};
double mechanismCycleNodeRadius(const Reaction&,const Style&);
Vec mechanismCycleTransitionPosition(const Reaction&,size_t,const Style&,double radius);
double mechanismCycleRadius(const Reaction&,const Style&);
double mechanismFontSize(const Reaction& reaction,const Style& style);
double mechanismArrowLength(const Reaction& reaction,const Style& style);
using MechanismMeasure=std::function<double(const std::string&,double,const std::string&,int)>;
void setMechanismTextMeasure(MechanismMeasure measure);
double mechanismArrowWeight(const Reaction&,const Style&);
double mechanismArrowHead(const Reaction&,const Style&);
double mechanismArrowMargin(const Reaction& reaction,const Style& style);
double mechanismCaptionHeight(const Reaction& reaction,const Style& style);
double mechanismStageCaptionWidth(const Slot& slot,double font);
struct TransitionCaption {std::string name,description;bool separated=false;};
TransitionCaption transitionCaption(const std::string& value);
std::string transitionCaptionText(const TransitionCaption& caption);
std::vector<SpinEnergy> reactionEnergies(const std::string& value);
std::string reactionEnergyList(std::vector<SpinEnergy> values);
std::string importedEnergy(std::string text);
bool attachMechanismEnergy(Document& doc,int source,int role,int index,int owner,size_t slot);
using MechanismRows=std::vector<std::vector<std::string>>;
// Each row is one intermediate or transition state: kind, name, S values, energies.
MechanismRows mechanismRows(const Reaction& reaction);
Reaction mechanismGridData(const Reaction& original,const MechanismRows& rows);
void makeMechanismGraph(Reaction& reaction);
enum class MechanismTemplate { Linear, FeedIntoCycle, SplitAndMerge, CatalyticCycle };
Reaction mechanismTemplate(MechanismTemplate layout,int horizontal=1,int left=2,int right=1);
bool arrangeCircularMerge(Reaction& reaction,const Style& style);
size_t splitMechanismLink(Reaction& reaction,size_t edge);
bool mechanismLinkSelected(const MechanismLink& link,const std::set<size_t>& stages);
Document copyMechanismStages(const Document& document,int owner,const std::set<size_t>& stages);
void arrangeMechanismGraph(Reaction& reaction);
size_t connectMechanismStages(Reaction& reaction,int from,int to,int replace=-1);
void removeMechanismConnection(Reaction& reaction,size_t edge);
void removeMechanismStage(Document& document,int owner,size_t stage,int mergeInto=-1,bool deleteContents=false);
void insertMechanismStage(Reaction& reaction,size_t before);
Reaction mechanismGraphData(const Reaction& original,const MechanismRows& rows);
}

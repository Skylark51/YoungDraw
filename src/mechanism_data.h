#pragma once
#include "model.h"
namespace yd {
struct SpinEnergy {double spin=0;std::string value;};
double mechanismCycleNodeRadius(const Reaction&,const Style&);
Vec mechanismCycleTransitionPosition(const Reaction&,size_t,const Style&,double radius);
double mechanismCycleRadius(const Reaction&,const Style&);
double mechanismFontSize(const Reaction& reaction,const Style& style);
double mechanismArrowLength(const Reaction& reaction,const Style& style);
double mechanismArrowMargin(const Reaction& reaction,const Style& style);
double mechanismCaptionHeight(const Reaction& reaction,const Style& style);
struct TransitionCaption {std::string name,description;bool separated=false;};
TransitionCaption transitionCaption(const std::string& value);
std::string transitionCaptionText(const TransitionCaption& caption);
std::vector<SpinEnergy> reactionEnergies(const std::string& value);
std::string reactionEnergyList(std::vector<SpinEnergy> values);
using MechanismRows=std::vector<std::vector<std::string>>;
// Each row is one intermediate or transition state: kind, name, S values, energies.
MechanismRows mechanismRows(const Reaction& reaction);
Reaction mechanismGridData(const Reaction& original,const MechanismRows& rows);
}

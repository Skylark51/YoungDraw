#pragma once

#include "model.h"

namespace yd::editor {

struct AnimationStage {
    Document structure;
    std::string structureName, stageName, energies, transitionEnergies;
};
std::vector<AnimationStage> animationMechanismStages(const Document& source,const Reaction& reaction,
                                                    const std::vector<size_t>& route={});
enum class AnimationBackground { Theme, Transparent, White };
std::string bondAnimationGif(std::vector<AnimationStage> stages,bool repeat=true,
                             AnimationBackground background=AnimationBackground::White);
void createBondAnimation();

} // namespace yd::editor

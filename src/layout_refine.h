#pragma once
#include "model.h"
#include <utility>

namespace yd::detail {
using LayoutCollisions = std::set<std::pair<int,int>>;
LayoutCollisions layoutCollisions(const Document& d);
// Adapted RDKit bond-flip search, evaluated with YoungDraw's rendered labels.
void refineLayoutCollisions(Document& d,const std::vector<int>& component,
                            const LayoutCollisions& originalCollisions);
}

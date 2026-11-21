#pragma once
#include "model.h"
namespace yd {
struct LigandTemplate {
    const char* slug; const char* name; const char* category; const char* family;
    bool spatial,bound,compact;
    Document drawing;
};
const std::vector<LigandTemplate>& ligandTemplates();
// Selected atoms/bonds expand to their connected molecules. Containers are resolved by the editor.
std::vector<int> spatialSelection(const Document& d,const std::vector<int>& selection);
void rotateSpatial(Document& d,const std::vector<int>& selection,double x,double y,double z,double gray=60);
// Exact element/connection matching; extra co-ligands retain attachment offsets.
// No atoms or bonds are added, removed, or assigned oxidation states.
bool applyLigandSpatial(Document& d,const std::vector<int>& selection,const Document& reference);
}

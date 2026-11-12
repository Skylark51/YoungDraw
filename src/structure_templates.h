#pragma once

#include "model.h"
#include <functional>

namespace yd {
struct StructureTemplate {
    const char* name;
    const char* abbreviation;
    Document drawing;
};
const std::vector<StructureTemplate>& functionalGroups();
const std::vector<StructureTemplate>& layoutReferences();

// Match in template atom order, excluding the final R marker for substituents.
void matchTemplate(const Document& d,const std::vector<int>& candidates,const Document& pattern,
                   bool substituent,const std::function<void(const std::vector<int>&)>& match);
bool applyLayoutReference(Document& d,const std::vector<int>& component);
int collapseFunctionalGroups(Document& d,const std::vector<int>& selection,const std::string& abbreviation={});
}

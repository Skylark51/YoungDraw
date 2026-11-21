#include "ligand_spatial.h"
#include "ligand_templates_data.h"
#include "cdxml.h"
namespace yd {
const std::vector<LigandTemplate>& ligandTemplates(){
    static const std::vector<LigandTemplate> entries=[](){
        std::vector<LigandTemplate> result;
        for(const auto& e:ligandTemplateData)result.push_back({e.slug,e.name,e.category,e.family,e.spatial,e.bound,e.compact,fromNative(e.data)});
        return result;
    }();
    return entries;
}
}

#pragma once
#include <string>
namespace yd::storage {
// Write beside the destination and atomically replace it only after a complete
// flush. Stale temporary files from a crash never block the next save.
void writeAtomic(const std::wstring& path,const std::string& data,bool replace=true);
}

#pragma once
#include "model.h"
namespace yd {
// Imports the neutral atom / ordinary bond subset. Throws before replacing a document.
Document fromCdxml(const std::string& xml);
// Reads ChemDraw's binary clipboard representation without a ChemDraw dependency.
Document fromCdx(const std::string& bytes);
std::string toCdxml(const Document& doc);
std::string toNative(const Document& doc);
Document fromNative(const std::string& xml);
}

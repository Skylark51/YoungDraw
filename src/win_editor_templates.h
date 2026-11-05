#pragma once

#include "win_editor.h"
#include "win_gallery.h"

namespace yd::editor {

std::string templateKey(const ui::TemplateCard& card);
std::set<std::string> loadFavorites();
void saveFavorites(const std::set<std::string>& keys);
bool validTemplatePart(const std::wstring& part);
fs::path templateLocation(const std::wstring& name,const std::wstring& group,const std::wstring& ext=L".cdxml");
std::vector<ui::TemplateCard>& templateCards(size_t& unreadable);
void saveCurrentTemplate();
void library(const std::wstring& initialCategory=L"리간드");

} // namespace yd::editor

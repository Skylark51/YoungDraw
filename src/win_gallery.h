#pragma once
#include <windows.h>
#include <gdiplus.h>
#include "scene.h"
#include <optional>
#include <functional>
namespace ui {
struct TemplateCard {std::wstring name,category;yd::Document document;yd::Scene preview;std::wstring path,group;bool favorite=false;};
enum class TemplateAction { Insert, Edit, Rename, Move, Remove, Favorite };
struct TemplateChoice {size_t index=0;int metal=0;TemplateAction action=TemplateAction::Insert;};
using TemplatePainter=std::function<void(Gdiplus::Graphics&,const yd::Scene&,Gdiplus::RectF)>;
std::optional<TemplateChoice> templateGallery(HWND parent,const std::vector<TemplateCard>& cards,const std::wstring& initialCategory,const TemplatePainter& painter);
}

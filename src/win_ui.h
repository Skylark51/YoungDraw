#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>

namespace ui {
// Use the canonical family name so Korean UI text does not depend on font linking.
inline const wchar_t* interfaceFont() { return L"Malgun Gothic"; }

inline void drawLabel(Gdiplus::Graphics& graphics, const std::wstring& text,
    Gdiplus::RectF box, float size, Gdiplus::Color color, bool bold = false,
    bool centered = false, bool singleLine = true) {
    using namespace Gdiplus;
    FontFamily korean(interfaceFont());
    const FontFamily* family = korean.GetLastStatus() == Ok ? &korean : FontFamily::GenericSansSerif();
    Font font(family, size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(centered ? StringAlignmentCenter : StringAlignmentNear);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    if (singleLine) format.SetFormatFlags(StringFormatFlagsNoWrap);
    auto saved = graphics.Save();
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    graphics.DrawString(text.c_str(), -1, &font, box, &format, &brush);
    graphics.Restore(saved);
}

inline void presentPixels(Gdiplus::Graphics& graphics, Gdiplus::Bitmap& bitmap) {
    using namespace Gdiplus;
    graphics.SetPageUnit(UnitPixel);
    const int width = int(bitmap.GetWidth()), height = int(bitmap.GetHeight());
    graphics.DrawImage(&bitmap, Rect(0, 0, width, height), 0, 0, width, height, UnitPixel);
}
}

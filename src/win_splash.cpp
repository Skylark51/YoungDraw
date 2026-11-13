#include "win_splash.h"
#include "win_ui.h"
#include <gdiplus.h>
#include <shlwapi.h>
#include <algorithm>
#include <memory>

namespace {
constexpr int width = 560, height = 370;

void roundedFrame(Gdiplus::GraphicsPath& path, float inset, float radius) {
    const float diameter = radius * 2;
    const float right = width - inset - diameter;
    const float bottom = height - inset - diameter;
    path.AddArc(inset, inset, diameter, diameter, 180, 90);
    path.AddArc(right, inset, diameter, diameter, 270, 90);
    path.AddArc(right, bottom, diameter, diameter, 0, 90);
    path.AddArc(inset, bottom, diameter, diameter, 90, 90);
    path.CloseFigure();
}

void drawFrame(Gdiplus::Graphics& graphics) {
    using namespace Gdiplus;
    GraphicsPath outer, inner;
    roundedFrame(outer, 2.0f, 16.0f);
    roundedFrame(inner, 11.0f, 10.0f);
    LinearGradientBrush metal(Point(0, 0), Point(width, height),
        Color(255, 136, 185, 208), Color(255, 36, 65, 89));
    Pen rim(&metal, 1.5f);
    Pen inset(Color(125, 94, 139, 169), 1.0f);
    graphics.DrawPath(&rim, &outer);
    graphics.DrawPath(&inset, &inner);

    // Short architectural corner strokes give the inset border a framed finish.
    Pen detail(Color(220, 142, 207, 230), 2.0f);
    for (int side = 0; side < 2; ++side) {
        const float x = side ? width - 22.0f : 22.0f;
        const float direction = side ? -1.0f : 1.0f;
        for (int edge = 0; edge < 2; ++edge) {
            const float y = edge ? height - 22.0f : 22.0f;
            const float vertical = edge ? -1.0f : 1.0f;
            graphics.DrawLine(&detail, x, y, x + direction * 25.0f, y);
            graphics.DrawLine(&detail, x, y, x, y + vertical * 15.0f);
        }
    }
}

void drawWordmark(Gdiplus::Graphics& graphics) {
    using namespace Gdiplus;
    FontFamily georgia(L"Georgia");
    const FontFamily* family = georgia.GetLastStatus() == Ok ? &georgia : FontFamily::GenericSerif();
    GraphicsPath lettering;
    lettering.AddString(L"YoungDraw", -1, family, FontStyleBold | FontStyleItalic,
        40.0f, PointF(0, 0), StringFormat::GenericTypographic());
    RectF bounds;
    lettering.GetBounds(&bounds);
    Matrix placement;
    placement.Translate((width - bounds.Width) / 2.0f - bounds.X, 221.0f - bounds.Y);
    lettering.Transform(&placement);

    LinearGradientBrush ink(Point(160, 220), Point(400, 260),
        Color(255, 245, 248, 255), Color(255, 126, 210, 243));
    graphics.FillPath(&ink, &lettering);

    // A tapered pen flourish echoes the motion of drawing beneath the signature.
    GraphicsPath flourish;
    flourish.AddBezier(145.0f, 255.0f, 203.0f, 246.0f, 287.0f, 261.0f, 355.0f, 250.0f);
    flourish.AddBezier(355.0f, 250.0f, 302.0f, 267.0f, 206.0f, 251.0f, 145.0f, 255.0f);
    flourish.CloseFigure();
    Matrix flourishPlacement;
    flourishPlacement.Translate(30.0f, 10.0f);
    flourish.Transform(&flourishPlacement);
    SolidBrush flourishInk(Color(180, 104, 198, 240));
    graphics.FillPath(&flourishInk, &flourish);
}

LRESULT CALLBACK splashProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_CLOSE) return 0;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_PAINT) {
        using namespace Gdiplus;
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        Bitmap buffer(width, height, PixelFormat32bppARGB);
        Graphics graphics(&buffer);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        LinearGradientBrush background(Point(0, 0), Point(width, height), Color(255, 13, 29, 54), Color(255, 5, 13, 29));
        graphics.FillRectangle(&background, 0, 0, width, height);
        drawFrame(graphics);

        auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
        HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(204), RT_RCDATA);
        IStream* stream = nullptr;
        if (resource) {
            HGLOBAL data = LoadResource(instance, resource);
            stream = SHCreateMemStream(static_cast<const BYTE*>(LockResource(data)), SizeofResource(instance, resource));
        }
        if (stream) {
            // Keep the stream alive until the decoder has been destroyed.
            {
                std::unique_ptr<Bitmap> icon(Bitmap::FromStream(stream));
                if (icon && icon->GetLastStatus() == Ok) graphics.DrawImage(icon.get(), (width - 172) / 2, 38, 172, 172);
            }
            stream->Release();
        } else {
            Bitmap icon(LoadIconW(instance, MAKEINTRESOURCEW(101)));
            graphics.DrawImage(&icon, (width - 128) / 2, 50, 128, 128);
        }
        FontFamily family(L"Segoe UI");
        Font subtitle(&family, 13, FontStyleRegular, UnitPixel);
        SolidBrush muted(Color(255, 153, 177, 203));
        StringFormat centered;
        centered.SetAlignment(StringAlignmentCenter);
        drawWordmark(graphics);
        FontFamily georgia(L"Georgia");
        const FontFamily* taglineFamily = georgia.GetLastStatus() == Ok ? &georgia : FontFamily::GenericSerif();
        Font tagline(taglineFamily, 16, FontStyleItalic, UnitPixel);
        SolidBrush taglineInk(Color(255, 184, 206, 224));
        centered.SetFormatFlags(StringFormatFlagsNoWrap);
        graphics.DrawString(L"For Making Structures, Mechanism, and Graphs", -1, &tagline,
            RectF(28, 282, width - 56, 26), &centered, &taglineInk);
        graphics.DrawString(L"시작하는 중…", -1, &subtitle, RectF(0, 326, width, 24), &centered, &muted);
        Graphics output(dc);
        output.DrawImage(&buffer, 0, 0, width, height);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

bool pumpUntil(ULONGLONG deadline) {
    while (GetTickCount64() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { PostQuitMessage(int(message.wParam)); return false; }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        const auto now = GetTickCount64();
        if (now < deadline) MsgWaitForMultipleObjectsEx(0, nullptr, DWORD(std::min<ULONGLONG>(16, deadline - now)), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    return true;
}
}

StartupSplash::StartupSplash(HINSTANCE instance, bool enabled) {
    if (!enabled) return;
    WNDCLASSW type{};
    type.lpfnWndProc = splashProc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.lpszClassName = L"YoungDrawStartupSplash";
    RegisterClassW(&type);
    POINT cursor{};
    GetCursorPos(&cursor);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const auto& area = monitor.rcWork;
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE, type.lpszClassName,
        L"YoungDraw 시작", WS_POPUP, area.left + (area.right - area.left - width) / 2,
        area.top + (area.bottom - area.top - height) / 2, width, height, nullptr, nullptr, instance, nullptr);
    if (!window_) return;
    SetWindowRgn(window_, CreateRoundRectRgn(0, 0, width + 1, height + 1, 36, 36), FALSE);
    SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    UpdateWindow(window_);
    started_ = GetTickCount64();
}

StartupSplash::~StartupSplash() {
    if (window_) DestroyWindow(window_);
}

void StartupSplash::finish(HWND mainWindow, int show) {
    if (window_ && !pumpUntil(started_ + 500)) return;
    if (window_) SetWindowLongPtrW(window_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(mainWindow));
    ShowWindow(mainWindow, show);
    UpdateWindow(mainWindow);
    BOOL animate = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0);
    if (window_ && animate && IsWindowVisible(mainWindow) && !IsIconic(mainWindow)) {
        RECT bounds{};
        GetWindowRect(window_, &bounds);
        const auto start = GetTickCount64();
        for (;;) {
            const double fraction = std::min(1.0, double(GetTickCount64() - start) / 180.0);
            SetLayeredWindowAttributes(window_, 0, BYTE(255 * (1.0 - fraction)), LWA_ALPHA);
            SetWindowPos(window_, nullptr, bounds.left, bounds.top + int(14 * fraction), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            if (fraction >= 1.0 || !pumpUntil(GetTickCount64() + 16)) break;
        }
    }
    if (window_) { DestroyWindow(window_); window_ = nullptr; }
}

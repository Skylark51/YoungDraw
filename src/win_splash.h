#pragma once
#include <windows.h>

// GDI+ must remain initialized for the lifetime of this window.
class StartupSplash {
public:
    explicit StartupSplash(HINSTANCE instance, bool enabled);
    ~StartupSplash();
    StartupSplash(const StartupSplash&) = delete;
    StartupSplash& operator=(const StartupSplash&) = delete;
    void finish(HWND mainWindow, int show);
private:
    HWND window_ = nullptr;
    ULONGLONG started_ = 0;
};

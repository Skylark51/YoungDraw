#pragma once
#include <windows.h>
#include <ole2.h>
#include <functional>
#include "model.h"

namespace yd::office {
struct Editor {
    HWND window;
    std::function<void(const Document&)> open;
    std::function<Document()> current;
    std::function<HENHMETAFILE(const Document&)> picture;
};
HRESULT start(Editor editor, bool embedding, bool registerClass=true);
void stop();
HRESULT copy(const Document& document);
bool editing();
HRESULT update();
void close();
HRESULT selfTest();
}

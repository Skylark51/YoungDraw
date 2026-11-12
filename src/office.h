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
HRESULT exportEmbedded(const Document& document,const wchar_t* storagePath,const wchar_t* imagePath);
HRESULT copy(const Document& document);
Document fromChemDrawEmbedding(const std::string& bytes);
bool editing();
HRESULT update();
void close();
HRESULT selfTest();
}

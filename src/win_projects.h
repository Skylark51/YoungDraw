#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>
#include <functional>
#include <optional>

namespace yd::projects {
struct Item { std::wstring file, name, note; };
using PngWriter = std::function<void(const std::filesystem::path& drawing,const std::filesystem::path& png)>;
struct Project {
    std::filesystem::path folder;
    std::wstring name, note;
    std::vector<Item> items;
    void save() const;
    static Project load(const std::filesystem::path& folder);
    static Project create(const std::filesystem::path& root, const std::wstring& name);
    static Project importFrom(const std::filesystem::path& manifest, const std::filesystem::path& root);
    Project copyTo(const std::filesystem::path& root) const;
    Project exportTo(const std::filesystem::path& root,const PngWriter& writePng) const;
    size_t add(const std::filesystem::path& source);
    size_t addDrawing(const std::wstring& name, const std::string& bytes);
    void move(size_t from, size_t to);
};
std::filesystem::path root();
std::filesystem::path imageDrawing(const std::filesystem::path& source);
bool image(const std::filesystem::path& path);
bool supported(const std::filesystem::path& path);
std::wstring displayName(const std::wstring& path);
struct Callbacks {
    std::string blank;
    std::function<void(const std::wstring&)> open;
    std::function<std::optional<std::pair<std::wstring,std::string>>()> current;
    std::function<bool(const std::vector<std::wstring>&)> close;
    std::function<void(HDC,const RECT&,const std::wstring&)> preview;
    std::function<bool(const std::filesystem::path&)> saveForExport;
    PngWriter writePng;
};
void manage(HWND owner, const Callbacks& callbacks,const std::filesystem::path& initial={});
}

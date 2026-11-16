#pragma once
#include "model.h"
#include <windows.h>
#include <filesystem>

namespace yd::recovery {
struct Snapshot { std::wstring path; Document document; };
struct Abandoned { std::filesystem::path file; std::vector<Snapshot> documents; };
// A live process holds its lock without sharing; other instances leave it alone.
class Session {
    std::filesystem::path folder_;
    HANDLE lock_=INVALID_HANDLE_VALUE;
public:
    explicit Session(const std::filesystem::path& root);
    ~Session();
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    void save(const std::vector<Snapshot>& documents);
    void clear();
};
std::vector<Abandoned> abandoned(const std::filesystem::path& root);
}

#pragma once

#include "win_editor.h"
#include <wrl/client.h>
#include <map>
#include <memory>

namespace yd::editor {

struct ImageCache {
    // The stream must outlive the GDI+ decoder, including all frame changes.
    Microsoft::WRL::ComPtr<IStream> stream;std::unique_ptr<Bitmap> bitmap;
    std::vector<ULONGLONG> ends;ULONGLONG started=0;UINT frame=0,cycles=1;bool infinite=false;
    UINT frameAt(ULONGLONG elapsed) const {
        if(ends.empty())return 0;
        if(!infinite&&elapsed/ends.back()>=cycles)return UINT(ends.size()-1);
        return UINT(std::upper_bound(ends.begin(),ends.end(),elapsed%ends.back())-ends.begin());
    }
    bool playing(ULONGLONG elapsed) const {return ends.size()>1&&(infinite||elapsed/ends.back()<cycles);}
};

extern std::map<std::string, ImageCache> imageCache;
extern bool animationVisible;
extern UINT animationDelay;
Bitmap* decode(const std::string& hex,bool animate=false);
std::string imageMime(const std::string& bytes);
Document imageDocument(const std::string& bytes);

} // namespace yd::editor

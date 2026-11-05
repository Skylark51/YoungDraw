#include "win_editor_images.h"
#include "win_editor_state.h"
#include <shlwapi.h>

namespace yd::editor {

std::map<std::string,ImageCache> imageCache;bool animationVisible=false;UINT animationDelay=1000;
Bitmap* decode(const std::string& hex,bool animate){
    auto it=imageCache.find(hex);
    if(it==imageCache.end()){
        std::string bytes;for(size_t i=0;i+1<hex.size();){if(isspace((unsigned char)hex[i])){i++;continue;}bytes+=char(std::stoi(hex.substr(i,2),nullptr,16));i+=2;}
        ImageCache c;c.stream.Attach(SHCreateMemStream((const BYTE*)bytes.data(),UINT(bytes.size())));
        if(!c.stream)return nullptr;c.bitmap.reset(Bitmap::FromStream(c.stream.Get()));
        if(!c.bitmap||c.bitmap->GetLastStatus()!=Ok)return nullptr;
        UINT count=c.bitmap->GetFrameDimensionsCount();std::vector<GUID> dimensions(count);
        if(count&&c.bitmap->GetFrameDimensionsList(dimensions.data(),count)==Ok&&std::any_of(dimensions.begin(),dimensions.end(),[](const GUID& id){return IsEqualGUID(id,FrameDimensionTime);})){
            UINT frames=c.bitmap->GetFrameCount(&FrameDimensionTime);
            auto property=[&](PROPID id){UINT size=c.bitmap->GetPropertyItemSize(id);std::vector<BYTE> data(size);if(size&&c.bitmap->GetPropertyItem(id,size,reinterpret_cast<PropertyItem*>(data.data()))!=Ok)data.clear();return data;};
            if(frames>1){
                auto delays=property(PropertyTagFrameDelay);auto* delay=delays.empty()?nullptr:reinterpret_cast<PropertyItem*>(delays.data());ULONGLONG total=0;
                for(UINT i=0;i<frames;i++){ULONG ticks=10;if(delay&&delay->type==PropertyTagTypeLong&&delay->length/sizeof(ULONG)>i)memcpy(&ticks,static_cast<BYTE*>(delay->value)+i*sizeof(ULONG),sizeof(ticks));total+=std::max(1UL,ticks)*10ULL;c.ends.push_back(total);}
                auto loops=property(PropertyTagLoopCount);auto* loop=loops.empty()?nullptr:reinterpret_cast<PropertyItem*>(loops.data());
                if(loop&&loop->type==PropertyTagTypeShort&&loop->length>=sizeof(USHORT)){USHORT repetitions;memcpy(&repetitions,loop->value,sizeof(repetitions));c.infinite=repetitions==0;c.cycles=UINT(repetitions)+1;}
            }
        }
        if(imageCache.size()>100)imageCache.clear();it=imageCache.emplace(hex,std::move(c)).first;
    }
    auto& c=it->second;if(animate&&!c.started)c.started=GetTickCount64();ULONGLONG elapsed=c.started?GetTickCount64()-c.started:0;UINT frame=animate?c.frameAt(elapsed):0;
    if(frame!=c.frame&&c.bitmap->SelectActiveFrame(&FrameDimensionTime,frame)==Ok)c.frame=frame;
    if(animate&&c.playing(elapsed)){animationVisible=true;animationDelay=UINT(std::min<ULONGLONG>(animationDelay,c.ends[frame]-elapsed%c.ends.back()));}
    return c.bitmap.get();
}
std::string imageMime(const std::string& bytes){
    if(bytes.compare(0,6,"GIF87a")==0||bytes.compare(0,6,"GIF89a")==0)return "image/gif";
    if(bytes.compare(0,8,std::string("\x89PNG\r\n\x1a\n",8))==0)return "image/png";
    if(bytes.size()>=3&&BYTE(bytes[0])==0xff&&BYTE(bytes[1])==0xd8&&BYTE(bytes[2])==0xff)return "image/jpeg";
    return {};
}
Document imageDocument(const std::string& bytes){
    if(bytes.size()>10*1024*1024)throw std::runtime_error("이미지는 10 MB까지 지원합니다.");
    Object o;o.id=1;o.kind=ObjectKind::Image;o.mime=imageMime(bytes);
    if(o.mime.empty())throw std::runtime_error("GIF, PNG, JPEG 이미지를 선택하세요.");
    static const char hex[]="0123456789ABCDEF";o.imageHex.reserve(bytes.size()*2);
    for(unsigned char c:bytes){o.imageHex+=hex[c>>4];o.imageHex+=hex[c&15];}
    auto* img=decode(o.imageHex);if(!img||!img->GetWidth()||!img->GetHeight())throw std::runtime_error("이미지를 읽을 수 없습니다.");
    double width=80,height=width*img->GetHeight()/img->GetWidth();o.points={{0,0},{width,height}};
    Document result;result.nextId=2;result.objects.push_back(std::move(o));return result;
}

} // namespace yd::editor

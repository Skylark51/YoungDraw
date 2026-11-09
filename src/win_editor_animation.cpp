#include "win_editor_animation.h"
#include "cdxml.h"
#include "mechanism_data.h"
#include "win_dialogs.h"
#include "win_dialog_style.h"
#include "win_editor_clipboard.h"
#include "win_editor_files.h"
#include "win_editor_render.h"
#include "win_editor_state.h"
#include "win_editor_structure.h"
#include "win_ui.h"
#include <commdlg.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <array>
#include <deque>
#include <tuple>
#include <sstream>
#include <windowsx.h>

namespace yd::editor {
namespace {

constexpr int transitionSteps=36;
constexpr int arrowSteps=transitionSteps/2;
constexpr int maxStages=40;
constexpr int frameDelay=4, stageDelay=38;
constexpr int maxAtoms=300;

void keepStructure(Document& structure){
    if(structure.atoms.empty()||structure.atoms.size()>maxAtoms||structure.bonds.size()>maxAtoms*4)
        throw std::runtime_error("원자가 1~300개인 구조를 선택하세요.");
    structure.objects.erase(std::remove_if(structure.objects.begin(),structure.objects.end(),
        [](const Object& object){
            return object.kind!=ObjectKind::Ellipse&&object.kind!=ObjectKind::Curve&&
                !(object.kind==ObjectKind::Line&&(object.head||object.dash==2||object.autoRoute));
        }),
        structure.objects.end());
    structure.reactions.clear();
    structure.plots.clear();
    structure.groups.clear();
    structure.sourceXml.clear();
    structure.sourceOwned.clear();
    structure.sourceGroups.clear();
    structure.importWarnings.clear();
    std::set<int> ids;
    Vec center;
    for(const auto& atom:structure.atoms){
        if(!std::isfinite(atom.p.x)||!std::isfinite(atom.p.y)||!ids.insert(atom.id).second)
            throw std::runtime_error("구조에 잘못된 원자 좌표 또는 번호가 있습니다.");
        center=center+atom.p;
    }
    std::set<int> bondIds;
    for(const auto& bond:structure.bonds)
        if(bond.a==bond.b||!ids.count(bond.a)||!ids.count(bond.b)||ids.count(bond.id)||!bondIds.insert(bond.id).second)
            throw std::runtime_error("구조에 잘못된 결합이 있습니다.");
    center=center*(1./structure.atoms.size());
    for(auto& atom:structure.atoms)atom.p=atom.p-center;
    for(auto& object:structure.objects)
        for(auto& point:object.points)point=point-center;
}

using AtomKey=std::tuple<int,int,std::string>;
AtomKey atomKey(const Atom& atom){return {atom.element,atom.isotope,atom.label};}

std::map<int,int> matchAtoms(const Document& start,const Document& end){
    std::map<AtomKey,std::deque<int>> remaining;
    for(const auto& atom:start.atoms)remaining[atomKey(atom)].push_back(atom.id);
    std::map<int,int> matched;
    for(const auto& atom:end.atoms){
        auto& candidates=remaining[atomKey(atom)];
        if(candidates.empty())continue;
        matched.emplace(atom.id,candidates.front());
        candidates.pop_front();
    }
    return matched;
}

using BondKey=std::tuple<int,int,BondKind,int,std::string,std::string>;
BondKey bondKey(const Bond& bond,int a,int b){
    int side=bond.doubleSide;
    const bool directional=bond.kind==BondKind::Wedge||bond.kind==BondKind::Hash||bond.kind==BondKind::Dative||
                           !bond.customDisplay.empty();
    if(!directional&&a>b){std::swap(a,b);side=-side;}
    return {a,b,bond.kind,side,bond.customOrder,bond.customDisplay};
}

// A primitive can be shared only if its rendered appearance and geometry agree.
bool sameDrawing(const Primitive& a,const Primitive& b){
    return std::tie(a.type,a.width,a.fontSize,a.rotation,a.text,a.color,a.font,a.dash,a.face,a.closed,a.filled,a.flatEnd,a.flatStart)==
           std::tie(b.type,b.width,b.fontSize,b.rotation,b.text,b.color,b.font,b.dash,b.face,b.closed,b.filled,b.flatEnd,b.flatStart)&&
           a.points.size()==b.points.size()&&std::equal(a.points.begin(),a.points.end(),b.points.begin(),
               [](Vec x,Vec y){return (x-y).length()<1e-7;});
}

// Keep completed segments and split the segment containing the growing tip.
std::vector<Vec> arrowPrefix(const std::vector<Vec>& points,bool cubic,double progress){
    const size_t stride=cubic?3:1;
    const double position=progress*((points.size()-1)/stride);
    const size_t first=size_t(position)*stride;
    const double t=position-size_t(position);
    std::vector<Vec> result(points.begin(),points.begin()+first+1);
    if(t==0)return result;
    auto mix=[t](Vec a,Vec b){return a+(b-a)*t;};
    const Vec a=mix(points[first],points[first+1]);
    if(!cubic){result.push_back(a);return result;}
    const Vec b=mix(points[first+1],points[first+2]),c=mix(points[first+2],points[first+3]);
    const Vec d=mix(a,b),e=mix(b,c);
    result.insert(result.end(),{a,d,mix(d,e)});
    return result;
}

class Transition {
    Document start_,end_;
    std::vector<Vec> origins_,destinations_;
    std::map<int,int> owners_;
    std::map<int,size_t> matchedPositions_;
    bool growsArrows_;
public:
    Transition(const Document& start,const Document& end):start_(start),end_(end),
        growsArrows_(std::any_of(start.objects.begin(),start.objects.end(),[](const Object& o){
            return o.head&&(o.kind==ObjectKind::Line||o.kind==ObjectKind::Curve)&&o.points.size()>=2;
        })){
        owners_=matchAtoms(start_,end_);
        if(owners_.empty())throw std::runtime_error("인접한 두 단계에 대응할 원자가 하나 이상 있어야 합니다.");
        std::map<int,size_t> indices;
        for(const auto& atom:start_.atoms){
            indices.emplace(atom.id,origins_.size());
            origins_.push_back(atom.p);
        }
        destinations_=origins_;
        for(const auto& atom:end_.atoms){
            auto found=owners_.find(atom.id);
            if(found==owners_.end())continue;
            auto index=indices.at(found->second);
            destinations_[index]=atom.p;
            matchedPositions_.emplace(atom.id,index);
        }
        std::map<BondKey,int> bonds;
        for(const auto& b:start_.bonds)bonds.emplace(bondKey(b,b.a,b.b),b.id);
        for(const auto& b:end_.bonds){
            auto a=owners_.find(b.a), z=owners_.find(b.b);
            if(a==owners_.end()||z==owners_.end())continue;
            auto found=bonds.find(bondKey(b,a->second,z->second));
            if(found!=bonds.end())owners_.emplace(b.id,found->second);
        }
        std::map<std::pair<int,int>,std::deque<int>> contacts;
        for(const auto& o:start_.objects)
            if(o.kind==ObjectKind::Line&&!o.head&&o.autoRoute&&o.sourceAtom&&o.targetAtom)
                contacts[{o.sourceAtom,o.targetAtom}].push_back(o.id);
        for(const auto& o:end_.objects){
            if(o.kind!=ObjectKind::Line||o.head||!o.autoRoute)continue;
            auto a=owners_.find(o.sourceAtom),b=owners_.find(o.targetAtom);
            if(a==owners_.end()||b==owners_.end())continue;
            auto& matches=contacts[{a->second,b->second}];
            if(matches.empty())continue;
            owners_.emplace(o.id,matches.front());
            matches.pop_front();
        }
    }

    double structureProgress(double progress) const {
        return growsArrows_?std::max(0.,progress*2-1):progress;
    }
    bool hasGrowingArrows() const {return growsArrows_;}

    Scene sceneAt(double progress){
        const double t=structureProgress(progress);
        for(size_t i=0;i<start_.atoms.size();++i)start_.atoms[i].p=origins_[i]*(1-t)+destinations_[i]*t;
        for(auto& atom:end_.atoms){
            auto it=matchedPositions_.find(atom.id);
            if(it!=matchedPositions_.end())atom.p=start_.atoms[it->second].p;
        }
        auto before=makeScene(start_,measure);
        auto removeArrows=[](Scene& scene){
            scene.items.erase(std::remove_if(scene.items.begin(),scene.items.end(),
                [&](const Primitive& item){return scene.arrowPaths.count(item.owner)!=0;}),scene.items.end());
        };
        if(growsArrows_&&progress==.5)return before;
        removeArrows(before);
        if(growsArrows_&&progress>0&&progress<.5){
            for(auto object:start_.objects){
                auto path=before.arrowPaths.find(object.id);
                if(path==before.arrowPaths.end()||path->second.size()<2)continue;
                const bool cubic=object.kind==ObjectKind::Curve&&path->second.size()>=4&&(path->second.size()-1)%3==0;
                object.points=arrowPrefix(path->second,cubic,progress*2);
                auto pieces=arrowPrimitives(object);
                before.items.insert(before.items.end(),pieces.begin(),pieces.end());
            }
        }
        if(t==0)return before;
        auto after=makeScene(end_,measure);
        removeArrows(after);
        if(t==1)return after;

        std::map<int,std::vector<size_t>> byOwner;
        for(size_t i=0;i<before.items.size();++i)byOwner[before.items[i].owner].push_back(i);
        std::vector<bool> shared(before.items.size(),false);
        std::vector<bool> duplicate(after.items.size(),false);
        for(size_t i=0;i<after.items.size();++i){
            const auto& p=after.items[i];
            auto match=owners_.find(p.owner);
            if(match==owners_.end())continue;
            for(size_t j:byOwner[match->second]){
                if(shared[j]||!sameDrawing(before.items[j],p))continue;
                shared[j]=duplicate[i]=true;
                break;
            }
        }

        Scene frame;
        auto append=[&](Scene& scene,const std::vector<bool>& common,bool first,bool labels,double opacity){
            for(size_t i=0;i<scene.items.size();++i){
                auto& p=scene.items[i];
                if((p.type==Primitive::Text)!=labels||(!first&&common[i]))continue;
                if(!common[i])p.opacity=static_cast<unsigned char>(std::lround(opacity*255));
                frame.items.push_back(std::move(p));
            }
        };
        for(bool labels:{false,true}){
            append(before,shared,true,labels,1-t);
            append(after,duplicate,false,labels,t);
        }
        const auto& a=before.bounds;
        const auto& b=after.bounds;
        const double x=std::min(a.x,b.x),y=std::min(a.y,b.y);
        frame.bounds={x,y,std::max(a.x+a.w,b.x+b.w)-x,std::max(a.y+a.h,b.y+b.h)-y};
        return frame;
    }
};

void word(std::string& bytes,int value){
    bytes+=char(value&255);
    bytes+=char((value>>8)&255);
}

// GIF LZW with a bounded dictionary keeps all codes at nine bits.
std::string compressedPixels(const BitmapData& pixels,int width,int height,bool transparent){
    constexpr unsigned clear=256,end=257,first=258,limit=510;
    std::array<unsigned short,limit*256> dictionary{};
    std::string packed;
    unsigned bits=0, used=0, next=first;
    auto emit=[&](unsigned code){
        bits|=code<<used;
        used+=9;
        while(used>=8){packed+=char(bits&255);bits>>=8;used-=8;}
    };
    emit(clear);
    int prefix=-1;
    for(int y=0;y<height;++y){
        const auto* row=static_cast<const BYTE*>(pixels.Scan0)+ptrdiff_t(y)*pixels.Stride;
        for(int x=0;x<width;++x){
            const auto* pixel=row+4*x;
            // 3 red bits, 3 green bits, 2 blue bits.
            unsigned index=(pixel[2]&224)|((pixel[1]&224)>>3)|(pixel[0]>>6);
            if(transparent){
                // Ordered alpha dithering preserves gradual bond fades in GIF's one-bit alpha.
                static constexpr unsigned bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
                if(pixel[3]<=bayer[y%4][x%4]*16+8)index=0;
                else if(index==0)index=254;
                else if(index==254)index=255;
            }
            if(prefix<0){prefix=int(index);continue;}
            auto& entry=dictionary[unsigned(prefix)*256+index];
            if(entry){prefix=entry;continue;}
            emit(unsigned(prefix));
            if(next==limit){emit(clear);dictionary.fill(0);next=first;}
            else entry=static_cast<unsigned short>(next++);
            prefix=int(index);
        }
    }
    emit(unsigned(prefix));
    emit(end);
    if(used)packed+=char(bits&255);
    return packed;
}

void gifFrame(std::string& bytes,Bitmap& bitmap,int width,int height,int delay,bool transparent){
    bytes.append("\x21\xf9\x04",3);
    bytes+=char(transparent?9:4);
    word(bytes,delay);
    bytes+=char(0);
    bytes.append("\0\x2c\0\0\0\0",6);
    word(bytes,width);word(bytes,height);
    bytes.append("\0\x08",2);
    Gdiplus::Rect area(0,0,width,height);
    BitmapData pixels{};
    if(bitmap.LockBits(&area,ImageLockModeRead,PixelFormat32bppARGB,&pixels)!=Ok)
        throw std::runtime_error("GIF 프레임을 읽을 수 없습니다.");
    std::string packed;
    try{packed=compressedPixels(pixels,width,height,transparent);}
    catch(...){bitmap.UnlockBits(&pixels);throw;}
    bitmap.UnlockBits(&pixels);
    for(size_t i=0;i<packed.size();i+=255){
        size_t count=std::min<size_t>(255,packed.size()-i);
        bytes+=char(count);
        bytes.append(packed,i,count);
    }
    bytes+='\0';
}

constexpr auto animationPreferences=L"Software\\YoungDraw\\Animation";
constexpr auto gifFolderPreference=L"LastGifFolder";

void rememberGifFolder(const std::wstring& path){
    const auto folder=fs::path(path).parent_path().wstring();
    RegSetKeyValueW(HKEY_CURRENT_USER,animationPreferences,gifFolderPreference,REG_SZ,
        folder.c_str(),DWORD((folder.size()+1)*sizeof(wchar_t)));
}

std::optional<std::wstring> gifSaveFile(HWND owner){
    using Microsoft::WRL::ComPtr;
    ComPtr<IFileSaveDialog> picker;
    if(FAILED(CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&picker))))
        throw std::runtime_error("GIF 저장 창을 열 수 없습니다.");
    const COMDLG_FILTERSPEC filter={L"Animated GIF",L"*.gif"};
    picker->SetFileTypes(1,&filter);
    picker->SetDefaultExtension(L"gif");
    picker->SetFileName(L"bond-animation.gif");
    picker->SetTitle(L"결합 애니메이션 GIF 저장");
    picker->SetOptions(FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR|FOS_PATHMUSTEXIST|FOS_OVERWRITEPROMPT);

    wchar_t folder[32768]{};
    DWORD bytes=sizeof(folder);
    std::error_code error;
    if(RegGetValueW(HKEY_CURRENT_USER,animationPreferences,gifFolderPreference,RRF_RT_REG_SZ,
        nullptr,folder,&bytes)==ERROR_SUCCESS&&fs::is_directory(folder,error)){
        ComPtr<IShellItem> item;
        if(SUCCEEDED(SHCreateItemFromParsingName(folder,nullptr,IID_PPV_ARGS(&item))))
            picker->SetFolder(item.Get());
    }

    const auto result=picker->Show(owner);
    if(result==HRESULT_FROM_WIN32(ERROR_CANCELLED))return {};
    ComPtr<IShellItem> file;
    PWSTR path=nullptr;
    if(FAILED(result)||FAILED(picker->GetResult(&file))||FAILED(file->GetDisplayName(SIGDN_FILESYSPATH,&path)))
        throw std::runtime_error("GIF 저장 위치를 가져올 수 없습니다.");
    const std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> selected(path,CoTaskMemFree);
    return std::wstring(selected.get());
}

std::optional<std::wstring> animationFile(const wchar_t* title,HWND owner=nullptr){
    wchar_t path[32768]={};
    OPENFILENAMEW picker{};
    picker.lStructSize=sizeof(picker);
    picker.hwndOwner=owner?owner:win;
    picker.lpstrTitle=title;
    picker.lpstrFile=path;
    picker.nMaxFile=DWORD(std::size(path));
    picker.lpstrFilter=L"ChemDraw / YoungDraw\0*.cdx;*.cdxml;*.ydraw\0\0";
    picker.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
    if(GetOpenFileNameW(&picker))return std::wstring(path);
    return {};
}

Document readStructure(const std::wstring& path){
    auto bytes=readFile(path);
    return _wcsicmp(fs::path(path).extension().c_str(),L".cdx")==0?fromCdx(bytes):fromCdxml(bytes);
}

std::string multiplicityEnergies(const std::string& energy,const std::string& unit){
    std::string text;
    for(const auto& entry:reactionEnergies(energy)){
        if(entry.value.empty())continue;
        if(!text.empty())text+=" / ";
        text+="M="+std::to_string(std::lround(2*entry.spin+1))+": "+entry.value;
    }
    if(!text.empty()&&!unit.empty())text+=" "+unit;
    return text;
}

std::vector<size_t> defaultRoute(const Reaction& mechanism){
    if(!mechanism.explicitConnections()){
        std::vector<size_t> route(mechanism.slots.size());
        for(size_t i=0;i<route.size();++i)route[i]=i;
        return route;
    }
    auto valid=[&](const MechanismLink& link){
        return link.from>=0&&link.to>=0&&size_t(link.from)<mechanism.slots.size()&&size_t(link.to)<mechanism.slots.size();
    };
    auto first=std::find_if(mechanism.links.begin(),mechanism.links.end(),[&](const MechanismLink& link){
        return valid(link)&&std::none_of(mechanism.links.begin(),mechanism.links.end(),
            [&](const MechanismLink& other){return valid(other)&&other.to==link.from;});
    });
    if(first==mechanism.links.end())first=std::find_if(mechanism.links.begin(),mechanism.links.end(),valid);
    if(first==mechanism.links.end())return {};
    std::vector<size_t> route={size_t(first->from),size_t(first->to)};
    for(;;){
        auto next=std::find_if(mechanism.links.begin(),mechanism.links.end(),[&](const MechanismLink& link){
            return valid(link)&&size_t(link.from)==route.back()&&
                   std::find(route.begin(),route.end(),size_t(link.to))==route.end();
        });
        if(next==mechanism.links.end())return route;
        route.push_back(size_t(next->to));
    }
}

void drawCaption(Graphics& g,const std::string& first,const std::string& second,double t,
                 float width,float y,float height,float size,bool bold,Color color,float x=0,float padding=12){
    if(first.empty()&&second.empty())return;
    const bool same=first==second;
    const auto& value=same||t<.5?first:second;
    double opacity=same?1.:std::abs(2*t-1);
    if(value.empty()||opacity<.01)return;
    std::string caption=value;
    for(size_t i=0;i+2<caption.size();++i)
        if(caption[i]=='\\'&&(caption[i+1]=='_'||caption[i+1]=='^')&&caption[i+2]=='{')caption.erase(i,1);
    for(auto& ch:caption)if(ch=='\r'||ch=='\n')ch=' ';
    const auto runs=scriptRuns(caption);
    FontFamily family(ui::interfaceFont());
    const auto style=bold?FontStyleBold:FontStyleRegular;
    Font font(&family,size,style,UnitPixel);
    SolidBrush brush(Color(BYTE(std::lround(opacity*255)),color.GetR(),color.GetG(),color.GetB()));
    if(std::any_of(runs.begin(),runs.end(),[](const ScriptRun& run){return run.script!=0;})){
        StringFormat format(StringFormat::GenericTypographic());
        auto runSize=[size](const ScriptRun& run){
            return size*float(run.script>0?chemicalScriptScale(run.text):run.script<0?.7:1.);
        };
        std::vector<float> widths;
        float total=0;
        for(const auto& run:runs){
            auto text=ui::wide(run.text);
            Font scriptFont(&family,runSize(run),style,UnitPixel);
            RectF bounds;
            g.MeasureString(text.c_str(),int(text.size()),&scriptFont,PointF{},&format,&bounds);
            widths.push_back(bounds.Width);
            total+=bounds.Width;
        }
        const float fit=std::min(1.f,(width-2*padding)/std::max(1.f,total));
        const auto saved=g.Save();
        g.TranslateTransform(x+(width-total*fit)/2,y+(height-size*fit)/2);
        g.ScaleTransform(fit,fit);
        float cursor=0;
        for(size_t i=0;i<runs.size();++i){
            const auto& run=runs[i];
            auto text=ui::wide(run.text);
            Font scriptFont(&family,runSize(run),style,UnitPixel);
            const float offset=run.script>0?-size*.23f:run.script<0?size*.46f:0.f;
            g.DrawString(text.c_str(),int(text.size()),&scriptFont,PointF(cursor,offset),&format,&brush);
            cursor+=widths[i];
        }
        g.Restore(saved);
        return;
    }
    auto text=ui::wide(caption);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(text.c_str(),-1,&font,RectF(x+padding,y,width-2*padding,height),&format,&brush);
}

} // namespace

std::vector<AnimationStage> animationMechanismStages(const Document& source,const Reaction& mechanism,
                                                    const std::vector<size_t>& requestedRoute){
    auto route=requestedRoute.empty()?defaultRoute(mechanism):requestedRoute;
    std::vector<AnimationStage> stages;
    for(size_t i=0;i<route.size();++i){
        auto slotIndex=route[i];
        const auto& slot=mechanism.slots.at(slotIndex);
        size_t arrow=i?route[i-1]:size_t(-1);
        if(i&&mechanism.explicitConnections()){
            auto link=std::find_if(mechanism.links.begin(),mechanism.links.end(),[&](const MechanismLink& e){
                return e.from==int(route[i-1])&&e.to==int(slotIndex);
            });
            if(link==mechanism.links.end())throw std::runtime_error("화살표로 직접 연결된 순서대로 단계를 입력하세요.");
            arrow=size_t(link-mechanism.links.begin());
        }
        auto structure=structureInSlot(source,mechanism.id,slotIndex);
        if(structure.atoms.empty())continue;
        std::string name="단계 "+std::to_string(slotIndex+1);
        if(arrow<mechanism.above.size()){
            auto caption=transitionCaption(mechanism.above[arrow]);
            if(!caption.name.empty())name=caption.name;
            else if(!caption.description.empty())name=caption.description;
        }
        auto incoming=arrow<mechanism.below.size()?multiplicityEnergies(mechanism.below[arrow],mechanism.unit):"";
        stages.push_back({std::move(structure),slot.label,std::move(name),
                          multiplicityEnergies(slot.energy,mechanism.unit),std::move(incoming)});
    }
    if(stages.size()<2)throw std::runtime_error("선택한 메커니즘 경로에 편집 가능한 원자 구조가 두 개 이상 필요합니다.");
    return stages;
}

namespace {
struct Playback {
    double speed=1.;
    int hold=stageDelay; // Centiseconds, like GIF frame delays.
    enum Loop {Once,Forward,PingPong};
    Loop loop=PingPong;
    double scale=1.;
    int resolution=0;
    double multiplier=1.;
    int stride=1;
    AnimationBackground background=AnimationBackground::Theme;
};
class AnimationTimeline {
    static constexpr int padding=20;
    std::vector<Transition> transitions;
    bool titles=false,names=false,energies=false,transitionEnergies=false;
    double xMin=0,yMin=0,scale=1;
    int header=0,structureWidth=0,structureHeight=0;
public:
    std::vector<AnimationStage> stages;
    int width=0,height=0;
    explicit AnimationTimeline(std::vector<AnimationStage> input):stages(std::move(input)){
        if(stages.size()<2||stages.size()>maxStages)throw std::runtime_error("GIF에 넣을 구조는 2~40개로 지정하세요.");
        for(auto& stage:stages)keepStructure(stage.structure);


        double xMax=0,yMax=0;
        for(size_t i=1;i<stages.size();++i){
            transitions.emplace_back(stages[i-1].structure,stages[i].structure);
            // Sample the moving label and bond extents for a single viewport across all stages.
            for(double t:{0.,.25,.5,.75,1.}){
                const auto b=transitions.back().sceneAt(t).bounds;
                if(!std::isfinite(b.x+b.w)||!std::isfinite(b.y+b.h))throw std::runtime_error("구조의 그림 범위가 너무 큽니다.");
                xMin=std::min(xMin,b.x);yMin=std::min(yMin,b.y);
                xMax=std::max(xMax,b.x+b.w);yMax=std::max(yMax,b.y+b.h);
            }
        }

        titles=std::any_of(stages.begin(),stages.end(),[](const auto& s){return !s.structureName.empty();});
        names=std::any_of(stages.begin(),stages.end(),[](const auto& s){return !s.stageName.empty();});
        energies=std::any_of(stages.begin(),stages.end(),[](const auto& s){return !s.energies.empty();});
        transitionEnergies=std::any_of(stages.begin(),stages.end(),[](const auto& s){return !s.transitionEnergies.empty();});
        header=titles?37:0;
        const int footer=(names?27:0)+(energies?25:0)+(transitionEnergies?25:0);
        scale=std::min(4.,580./std::max({1.,xMax-xMin,yMax-yMin}));
        structureWidth=int(std::ceil((xMax-xMin)*scale))+2*padding;
        structureHeight=int(std::ceil((yMax-yMin)*scale))+2*padding;
        width=std::max(structureWidth,(energies||transitionEnergies)?480:(titles||names)?320:0);
        height=header+structureHeight+footer;
    }
    int lastFrame() const {return int(transitions.size())*transitionSteps;}
    int frameCount() const {return 2*lastFrame();}
    bool hasArrows(size_t segment) const {return transitions.at(segment).hasGrowingArrows();}
    int morphDuration(size_t segment) const {
        return stages.at(segment).morphTime.value_or((hasArrows(segment)?arrowSteps:transitionSteps)*frameDelay);
    }
    int delay(int frame,const Playback& playback) const {
        auto scaled=[&](double time){return int(std::lround(time/playback.speed));};
        if(frame==lastFrame())return std::max(2,scaled(playback.hold));
        const size_t segment=size_t(frame/transitionSteps);
        const int local=frame%transitionSteps;
        const bool arrows=hasArrows(segment);
        const bool growing=arrows&&local<arrowSteps;
        const int steps=arrows?arrowSteps:transitionSteps;
        const int index=arrows&&!growing?local-arrowSteps:local;
        const int duration=growing?stages[segment].arrowGrowth:morphDuration(segment);
        const int motion=scaled(double(duration)*(index+1)/steps)-scaled(double(duration)*index/steps);
        const int hold=local==0?playback.hold:arrows&&local==arrowSteps?stages[segment].arrowHold:0;
        return std::max(2,motion)+scaled(hold);
    }
    int forwardFrame(int frame) const {return std::min(frame,frameCount()-frame);}
    void draw(Graphics& g,int frame,bool themed){
        const size_t segment=std::min(size_t(frame/transitionSteps),transitions.size()-1);
        double t=double(frame-int(segment)*transitionSteps)/transitionSteps;
        t=t*t*(3-2*t);
        auto scene=transitions[segment].sceneAt(t);
        t=transitions[segment].structureProgress(t);
        const auto& from=stages[segment];
        const auto& to=stages[segment+1];
        const auto textColor=themed?appearance::palette().canvasInk:Color(27,39,51);
        auto saved=g.Save();
        g.TranslateTransform(float((width-structureWidth)*.5+padding-xMin*scale),float(header+padding-yMin*scale));
        g.ScaleTransform(float(scale),float(scale));
        render(g,scene,false,themed);
        g.Restore(saved);
        if(titles)drawCaption(g,from.structureName,to.structureName,t,width,4,header-4,23,true,textColor);
        const int baseline=header+structureHeight;
        if(names)drawCaption(g,from.stageName,to.stageName,std::min(1.,t*2),width,baseline,27,18,true,textColor);
        if(energies)drawCaption(g,from.energies,to.energies,t,width,baseline+(names?27:0),23,15,false,textColor);
        if(transitionEnergies&&t>0&&t<1&&!to.transitionEnergies.empty())
            drawCaption(g,"TS  "+to.transitionEnergies,"",std::abs(t-.5),width,height-25,23,15,false,textColor);
    }
};

int outputPixels(int base,const Playback& playback){return std::max(1,int(std::lround(base*playback.scale)));}

using GifProgress=std::function<bool(int,int,size_t)>;
std::string encodeAnimation(AnimationTimeline& timeline,const Playback& playback,const GifProgress& progress={}){
    const int width=outputPixels(timeline.width,playback),height=outputPixels(timeline.height,playback);
    const int forwardCount=timeline.lastFrame()/playback.stride+1;
    const int total=playback.loop==Playback::PingPong?2*forwardCount-2:forwardCount;
    std::string bytes="GIF89a";
    word(bytes,width);word(bytes,height);
    const bool transparent=playback.background==AnimationBackground::Transparent;
    const bool themed=playback.background==AnimationBackground::Theme;
    bytes+='\xf7';
    bytes+=char(transparent?0:255);
    bytes+='\0';
    for(int i=0;i<256;++i){
        if(transparent&&i==254){bytes.append(3,char(0));continue;}
        bytes+=char(((i>>5)&7)*255/7);
        bytes+=char(((i>>2)&7)*255/7);
        bytes+=char((i&3)*255/3);
    }
    if(playback.loop!=Playback::Once)bytes.append("\x21\xff\x0bNETSCAPE2.0\x03\x01\0\0\0",19);
    Bitmap bitmap(width,height,PixelFormat32bppARGB);
    std::vector<size_t> frameOffsets;
    for(int frame=0;frame<=timeline.lastFrame();frame+=playback.stride){
        if(progress&&!progress(int(frameOffsets.size()),total,bytes.size()))return {};
        {
            Graphics g(&bitmap);
            g.Clear(transparent?Color(0,0,0,0):Color(255,255,255));
            if(themed)appearance::background(g,RectF(0,0,float(width),float(height)));
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            g.ScaleTransform(float(width)/timeline.width,float(height)/timeline.height);
            timeline.draw(g,frame,themed);
        }
        frameOffsets.push_back(bytes.size());
        int duration=0;
        for(int offset=0;offset<playback.stride&&frame+offset<=timeline.lastFrame();++offset)
            duration+=timeline.delay(frame+offset,playback);
        gifFrame(bytes,bitmap,width,height,duration,transparent);
    }
    frameOffsets.push_back(bytes.size());
    // Replay the same frames in reverse, so the loop also has a continuous transition.
    if(playback.loop==Playback::PingPong)for(size_t i=frameOffsets.size()-3;i>0;--i){
        if(progress&&!progress(total-int(i),total,bytes.size()))return {};
        bytes.append(bytes,frameOffsets[i],frameOffsets[i+1]-frameOffsets[i]);
    }
    if(progress&&!progress(total,total,bytes.size()+1))return {};
    bytes+=';';
    return bytes;
}

bool editAnimationCaptions(HWND owner,std::vector<AnimationStage>& stages){
    ui::Table rows;
    for(const auto& stage:stages)rows.push_back({stage.structureName,stage.stageName,stage.energies,stage.transitionEnergies});
    auto labels=ui::table(owner,L"결합 애니메이션 · 단계별 자막",
        {L"상단 구조 이름",L"하단 단계 이름",L"하단 구조 에너지",L"이 단계로 들어오는 TS 에너지"},rows,
        [&](const ui::Table& edited)->std::wstring{
            if(edited.size()!=stages.size())return L"구조와 자막 행의 개수를 같게 유지하세요.";
            for(const auto& row:edited)for(const auto& cell:row)
                if(cell.size()>480)return L"각 자막을 더 짧게 입력하세요.";
            return {};
        },false,L"자막은 비워 둘 수 있습니다. 에너지 예시  M=1: 0.0 / M=3: 12.5 kcal/mol");
    if(!labels)return false;
    for(size_t i=0;i<stages.size();++i){
        stages[i].structureName=std::move((*labels)[i][0]);
        stages[i].stageName=std::move((*labels)[i][1]);
        stages[i].energies=std::move((*labels)[i][2]);
        stages[i].transitionEnergies=std::move((*labels)[i][3]);
    }
    return true;
}
enum PreviewControl {
    PreviewPlay=101,PreviewRestart,PreviewPrevious,PreviewNext,PreviewTiming,
    PreviewCaptions,PreviewReplace,PreviewSave,PreviewClose,PreviewReadout,PreviewRepeat,PreviewBackground,PreviewAdd,PreviewRemove,
    PreviewSegmentTiming
};
constexpr int previewSidebar=252;
const std::vector<std::wstring> loopModes={L"1회 재생",L"정방향 반복",L"왕복 반복"};

struct AnimationPreview {
    ui::DialogColors colors;
    AnimationTimeline timeline;
    Document selectedStructure;
    Playback playback;
    std::vector<ULONGLONG> frameEnds;
    ULONGLONG started=0, position=0;
    int frame=0;
    bool playing=true, done=false, seeking=false;
    int thumbFrom=-1,thumbOffset=0;
    std::vector<Scene> thumbs;
    HFONT font=nullptr;
    std::unique_ptr<Bitmap> buffer;

    AnimationPreview(std::vector<AnimationStage> stages,Document selected):
        timeline(std::move(stages)),selectedStructure(std::move(selected)){
        rebuildThumbs();updateTiming(0);
    }
    void rebuildThumbs(){
        thumbs.clear();
        for(const auto& stage:timeline.stages)thumbs.push_back(makeScene(stage.structure,measure));
    }
    void replace(std::vector<AnimationStage> stages,int index){
        timeline=AnimationTimeline(std::move(stages));
        rebuildThumbs();
        thumbOffset=std::min(thumbOffset,int(thumbs.size())-1);
        if(playback.resolution)playback.scale=playback.resolution*playback.multiplier/std::max(timeline.width,timeline.height);
        updateTiming(index*transitionSteps);
    }
    ~AnimationPreview(){if(font)DeleteObject(font);}
    void updateTiming(int selected){
        frameEnds.clear();
        ULONGLONG total=0;
        for(int i=0;i<(playback.loop==Playback::PingPong?timeline.frameCount():timeline.lastFrame()+1);++i){
            total+=10*timeline.delay(timeline.forwardFrame(i),playback);
            frameEnds.push_back(total);
        }
        seek(selected);
    }
    void seek(int selected){
        frame=std::clamp(selected,0,int(frameEnds.size())-1);
        position=frame?frameEnds[size_t(frame-1)]:0;
        started=GetTickCount64();
    }
    void tick(){
        if(!playing)return;
        auto elapsed=position+GetTickCount64()-started;
        if(playback.loop==Playback::Once&&elapsed>=frameEnds.back()){
            frame=timeline.lastFrame();
            playing=false;
            return;
        }
        elapsed%=frameEnds.back();
        frame=int(std::upper_bound(frameEnds.begin(),frameEnds.end(),elapsed)-frameEnds.begin());
    }
    int stage() const {
        return (timeline.forwardFrame(frame)+transitionSteps/2)/transitionSteps;
    }
};

RECT seekBounds(HWND hwnd){
    RECT rc{};
    GetClientRect(hwnd,&rc);
    return {rc.right-previewSidebar+18,74,rc.right-18,100};
}

void refreshPreview(HWND hwnd,const AnimationPreview& preview){
    auto readout=L"단계 "+std::to_wstring(preview.stage()+1)+L" / "+std::to_wstring(preview.timeline.stages.size())+
        L"    ·    프레임 "+std::to_wstring(preview.frame+1);
    SetDlgItemTextW(hwnd,PreviewReadout,readout.c_str());
    const auto output=L"출력 · "+std::to_wstring(outputPixels(preview.timeline.width,preview.playback))+L" × "+
        std::to_wstring(outputPixels(preview.timeline.height,preview.playback))+L" px…";
    SetDlgItemTextW(hwnd,PreviewBackground,output.c_str());
    InvalidateRect(hwnd,nullptr,FALSE);
}

void previewButtons(HWND hwnd){
    RECT rc{};
    GetClientRect(hwnd,&rc);
    const int x=rc.right-previewSidebar+16;
    const int w=previewSidebar-32;
    MoveWindow(GetDlgItem(hwnd,PreviewReadout),x+2,47,w-4,24,TRUE);
    MoveWindow(GetDlgItem(hwnd,PreviewPlay),x,110,w,34,TRUE);
    MoveWindow(GetDlgItem(hwnd,PreviewRestart),x,151,(w-8)/2,32,TRUE);
    MoveWindow(GetDlgItem(hwnd,PreviewSegmentTiming),x+(w+8)/2,151,(w-8)/2,32,TRUE);
    MoveWindow(GetDlgItem(hwnd,PreviewPrevious),x,190,(w-8)/2,32,TRUE);
    MoveWindow(GetDlgItem(hwnd,PreviewNext),x+(w+8)/2,190,(w-8)/2,32,TRUE);
    MoveWindow(GetDlgItem(hwnd,PreviewTiming),x,229,(w-8)/2,32,TRUE);
    MoveWindow(GetDlgItem(hwnd,PreviewRepeat),x+(w+8)/2,232,(w-8)/2,145,TRUE);
    MoveWindow(GetDlgItem(hwnd,PreviewBackground),x,268,w,32,TRUE);
    MoveWindow(GetDlgItem(hwnd,PreviewCaptions),x,340,w,30,TRUE);
    MoveWindow(GetDlgItem(hwnd,PreviewReplace),x,376,w,30,TRUE);
    MoveWindow(GetDlgItem(hwnd,PreviewAdd),x,412,(w-8)/2,28,TRUE);
    MoveWindow(GetDlgItem(hwnd,PreviewRemove),x+(w+8)/2,412,(w-8)/2,28,TRUE);
    MoveWindow(GetDlgItem(hwnd,PreviewSave),x,rc.bottom-83,w,35,TRUE);
    MoveWindow(GetDlgItem(hwnd,PreviewClose),x,rc.bottom-41,w,29,TRUE);
}

void paintPreview(HWND hwnd,HDC dc,AnimationPreview& preview){
    RECT rc{};
    GetClientRect(hwnd,&rc);
    const int w=std::max(1L,rc.right),h=std::max(1L,rc.bottom);
    if(!preview.buffer||preview.buffer->GetWidth()!=UINT(w)||preview.buffer->GetHeight()!=UINT(h))
        preview.buffer=std::make_unique<Bitmap>(w,h,PixelFormat32bppARGB);
    const auto& palette=preview.colors.palette;
    {
        Graphics g(preview.buffer.get());
        g.Clear(palette.panel);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        const float sidebar=float(w-previewSidebar);
        const RectF canvas(0,0,sidebar,float(h-83));
        SolidBrush backdrop(palette.canvas);
        g.FillRectangle(&backdrop,canvas);
        auto saved=g.Save();
        g.SetClip(canvas);
        const auto& timeline=preview.timeline;
        const float scale=std::max(.1f,std::min((sidebar-48)/timeline.width,(h-125.f)/timeline.height));
        g.TranslateTransform((sidebar-timeline.width*scale)*.5f,(h-83-timeline.height*scale)*.5f);
        g.ScaleTransform(scale,scale);
        const bool themed=preview.playback.background==AnimationBackground::Theme;
        if(themed){
            appearance::background(g,RectF(0,0,float(timeline.width),float(timeline.height)));
        }else{
            SolidBrush whitePaper(Color(255,255,255));
            g.FillRectangle(&whitePaper,0,0,timeline.width,timeline.height);
            if(preview.playback.background==AnimationBackground::Transparent){
                SolidBrush tile(Color(224,224,224));
                for(int y=0;y<timeline.height;y+=12)for(int x=0;x<timeline.width;x+=12)
                    if((x/12+y/12)%2)g.FillRectangle(&tile,x,y,std::min(12,timeline.width-x),std::min(12,timeline.height-y));
            }
        }
        preview.timeline.draw(g,preview.timeline.forwardFrame(preview.frame),themed);
        g.Restore(saved);

        saved=g.Save();g.SetClip(RectF(0,float(h-83),sidebar,83));
        for(size_t i=size_t(preview.thumbOffset);i<preview.thumbs.size();++i){
            const float x=8+float(i-preview.thumbOffset)*92;
            if(x>=sidebar)break;
            RectF box(x,float(h-78),86,70);
            appearance::rounded(g,box,6,int(i)==preview.stage()||int(i)==preview.thumbFrom?palette.hover:palette.raised);
            const auto& scene=preview.thumbs[i];
            const auto& b=scene.bounds;
            const float fit=float(std::min(74./std::max(1.,b.w),46./std::max(1.,b.h)));
            auto thumbState=g.Save();
            g.TranslateTransform(x+43-float(b.x+b.w/2)*fit,float(h-53)-float(b.y+b.h/2)*fit);
            g.ScaleTransform(fit,fit);render(g,scene,false,true);g.Restore(thumbState);
            const auto title=std::to_string(i+1)+" "+timeline.stages[i].structureName;
            drawCaption(g,title,title,0,80,float(h-26),17,11,false,palette.text,x+3,3);
        }
        g.Restore(saved);
        Pen divider(palette.edge,1);
        g.DrawLine(&divider,sidebar,0.f,sidebar,float(h));
        ui::drawLabel(g,L"재생",RectF(sidebar+16,13,220,26),18,palette.text,true);
        ui::drawLabel(g,L"편집",RectF(sidebar+16,308,220,26),18,palette.text,true);
        const auto seek=seekBounds(hwnd);
        const float length=float(seek.right-seek.left);
        const float filled=length*preview.frame/(preview.frameEnds.size()-1);
        appearance::rounded(g,RectF(float(seek.left),84,length,6),3,palette.edge);
        if(filled>0)appearance::rounded(g,RectF(float(seek.left),84,filled,6),3,palette.accent);
        SolidBrush thumb(palette.accent);
        g.FillEllipse(&thumb,seek.left+filled-5,82.f,10.f,10.f);
    }
    Graphics output(dc);
    output.SetPageUnit(UnitPixel);
    output.DrawImage(preview.buffer.get(),Gdiplus::Rect(0,0,w,h),0,0,w,h,UnitPixel);
}

void setPlaying(HWND hwnd,AnimationPreview& preview,bool playing){
    preview.seek(preview.frame);
    preview.playing=playing;
    SetDlgItemTextW(hwnd,PreviewPlay,playing?L"일시정지":L"재생");
}

const std::vector<std::wstring> backgroundNames={L"현재 테마",L"투명",L"흰색"};

struct ExportProgress {
    HWND owner,window=nullptr,readout=nullptr,bar=nullptr,cancel=nullptr;
    ULONGLONG start=GetTickCount64();
    bool stopped=false;
    explicit ExportProgress(HWND parent):owner(parent){
        const RECT r=ui::ownedWindowRect(owner,420,176);
        window=CreateWindowExW(WS_EX_DLGMODALFRAME,L"STATIC",L"GIF 생성 중",WS_POPUP|WS_CAPTION,
            r.left,r.top,420,176,owner,nullptr,nullptr,nullptr);
        if(!window)throw std::runtime_error("진행 창을 열 수 없습니다.");
        readout=CreateWindowW(L"STATIC",L"첫 프레임 생성 중…",WS_CHILD|WS_VISIBLE,16,14,378,28,window,nullptr,nullptr,nullptr);
        bar=CreateWindowW(PROGRESS_CLASSW,L"",WS_CHILD|WS_VISIBLE,16,48,378,22,window,nullptr,nullptr,nullptr);
        cancel=CreateWindowW(L"BUTTON",L"취소",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,290,83,104,28,window,nullptr,nullptr,nullptr);
        for(auto child:{readout,cancel})SendMessageW(child,WM_SETFONT,WPARAM(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SetWindowSubclass(window,proc,1,reinterpret_cast<DWORD_PTR>(this));
        EnableWindow(owner,FALSE);ShowWindow(window,SW_SHOW);SetFocus(cancel);
    }
    ~ExportProgress(){EnableWindow(owner,TRUE);DestroyWindow(window);SetActiveWindow(owner);}
    static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
        auto& self=*reinterpret_cast<ExportProgress*>(data);
        if(msg==WM_CLOSE||(msg==WM_COMMAND&&HWND(lp)==self.cancel)){self.stopped=true;return 0;}
        return DefSubclassProc(hwnd,msg,wp,lp);
    }
    bool update(int done,int total,size_t bytes){
        MSG msg{};
        while(PeekMessageW(&msg,window,0,0,PM_REMOVE)){
            if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE)stopped=true;
            TranslateMessage(&msg);DispatchMessageW(&msg);
        }
        if(stopped)return false;
        if(done){
            const auto eta=(GetTickCount64()-start)*(total-done)/done/1000;
            const auto estimate=double(bytes)*total/done/1048576.;
            wchar_t label[160]{};
            swprintf_s(label,L"%d%%  ·  예상 %.2f MB  ·  남은 시간 %llu초",100*done/total,estimate,eta);
            SetWindowTextW(readout,label);
        }
        SendMessageW(bar,PBM_SETPOS,100*done/total,0);
        UpdateWindow(window);UpdateWindow(readout);UpdateWindow(bar);
        return true;
    }
};

void previewAction(HWND hwnd,AnimationPreview& preview,int id){
    if(id==PreviewPlay){
        if(!preview.playing&&preview.playback.loop==Playback::Once&&preview.frame==preview.timeline.lastFrame())preview.seek(0);
        setPlaying(hwnd,preview,!preview.playing);
    }else if(id==PreviewRestart){
        preview.seek(0);
        setPlaying(hwnd,preview,true);
    }else if(id==PreviewPrevious||id==PreviewNext){
        const int current=preview.timeline.forwardFrame(preview.frame);
        int stage=id==PreviewPrevious?(std::max(0,current-1)/transitionSteps):(current/transitionSteps+1);
        preview.seek(std::min(stage*transitionSteps,preview.timeline.lastFrame()));
        setPlaying(hwnd,preview,false);
    }else if(id==PreviewSegmentTiming){
        auto& timeline=preview.timeline;
        ui::Table rows;
        for(size_t i=0;i+1<timeline.stages.size();++i){
            const auto& stage=timeline.stages[i];
            const bool arrows=timeline.hasArrows(i);
            rows.push_back({arrows?number(stage.arrowGrowth/100.):"-",
                            arrows?number(stage.arrowHold/100.):"-",number(timeline.morphDuration(i)/100.)});
        }
        auto parseTimes=[&](const ui::Table& values){
            if(values.size()!=rows.size())throw std::runtime_error("구간 수를 유지하세요.");
            std::vector<std::array<int,3>> times;
            auto read=[](const std::string& value,double minimum){return int(std::lround(numeric(value,minimum,30)*100));};
            for(size_t i=0;i<values.size();++i){
                const auto& row=values[i];
                const bool arrows=timeline.hasArrows(i);
                times.push_back({arrows?read(row[0],.36):timeline.stages[i].arrowGrowth,
                                 arrows?read(row[1],0):timeline.stages[i].arrowHold,
                                 read(row[2],arrows?.36:.72)});
            }
            return times;
        };
        auto result=ui::table(hwnd,L"결합 애니메이션 · 구간별 시간",
            {L"화살표 그리기 (초)",L"완성된 화살표 유지 (초)",L"다음 구조로 전환 (초)"},rows,
            [&](const ui::Table& values)->std::wstring{
                try{parseTimes(values);return {};}
                catch(const std::exception&){return L"시간은 30초 이하로 입력하세요. 그리기는 0.36초 이상, 구조 전환은 화살표가 있으면 0.36초, 없으면 0.72초 이상입니다.";}
            },false,L"1행은 1 → 2단계입니다. 시간은 1× 기준이며, 화살표가 없는 구간은 구조 전환 시간만 적용됩니다.");
        if(result){
            auto times=parseTimes(*result);
            for(size_t i=0;i<times.size();++i){
                auto& stage=timeline.stages[i];
                stage.arrowGrowth=times[i][0];
                stage.arrowHold=times[i][1];
                stage.morphTime=times[i][2];
            }
            preview.updateTiming(preview.frame);
        }
    }else if(id==PreviewTiming){
        static const std::array<double,5> speeds={.5,.75,1.,1.5,2.};
        const auto selected=std::find(speeds.begin(),speeds.end(),preview.playback.speed)-speeds.begin();
        auto result=ui::form(hwnd,L"애니메이션 재생 설정",
            {{L"재생 속도",std::to_string(selected),false,{L"0.5×",L"0.75×",L"1×",L"1.5×",L"2×"}},
             {L"단계에서 정지 (초)",std::to_string(preview.playback.hold/100.)}});
        if(result){
            auto speed=speeds.at(size_t(std::stoi((*result)[0])));
            auto hold=int(std::lround(numeric((*result)[1],0,10)*100));
            preview.playback.speed=speed;
            preview.playback.hold=hold;
            preview.updateTiming(preview.frame);
        }
    }else if(id==PreviewRepeat){
        const int frame=preview.timeline.forwardFrame(preview.frame);
        const auto mode=SendDlgItemMessageW(hwnd,PreviewRepeat,CB_GETCURSEL,0,0);
        if(mode!=CB_ERR){
            preview.playback.loop=Playback::Loop(mode);
            preview.updateTiming(frame);
        }
    }else if(id==PreviewBackground){
        const int longest=std::max(preview.timeline.width,preview.timeline.height);
        auto scaleFor=[&](const std::vector<std::string>& values){
            const double pixels=std::lround(numeric(values[1],32,2400))*numeric(values[2],.25,4);
            if(pixels<32||pixels>2400)throw std::runtime_error("최종 해상도의 긴 변은 32~2400 px로 설정하세요.");
            return pixels/longest;
        };
        auto result=ui::form(hwnd,L"GIF 출력 설정",{
            {L"배경",std::to_string(int(preview.playback.background)),false,backgroundNames},
            {L"기준 해상도 · 긴 변 (px)",std::to_string(preview.playback.resolution?preview.playback.resolution:longest)},
            {L"출력 배율 (예: 1, 2)",std::to_string(preview.playback.multiplier)},
            {L"모션 화질",std::to_string(preview.playback.stride-1),false,
                {L"높음 · 모든 프레임",L"보통 · 2프레임 간격",L"낮음 · 3프레임 간격"}}
        },[&](const auto& values)->std::wstring{scaleFor(values);return {};},{},
        [&](HDC dc,const RECT& box,const auto& values){
            auto playback=preview.playback;playback.scale=scaleFor(values);
            const auto label=L"최종 GIF · "+std::to_wstring(outputPixels(preview.timeline.width,playback))+L" × "+
                std::to_wstring(outputPixels(preview.timeline.height,playback))+L" px";
            Graphics g(dc);
            ui::drawLabel(g,label,RectF(float(box.left),float(box.top),600,40),18,appearance::palette().text,true);
        });
        if(result){
            preview.playback.scale=scaleFor(*result);
            preview.playback.resolution=int(std::lround(numeric((*result)[1],32,2400)));
            preview.playback.multiplier=numeric((*result)[2],.25,4);
            preview.playback.background=AnimationBackground(std::stoi(result->front()));
            preview.playback.stride=1+std::stoi((*result)[3]);
        }
    }else if(id==PreviewCaptions||id==PreviewReplace||id==PreviewAdd||id==PreviewRemove){
        auto stages=preview.timeline.stages;
        int index=preview.stage();
        if(id==PreviewCaptions){
            if(!editAnimationCaptions(hwnd,stages))return;
        }else if(id==PreviewRemove){
            if(stages.size()<=2)throw std::runtime_error("GIF에는 단계가 두 개 이상 필요합니다.");
            stages.erase(stages.begin()+index);
            index=std::min(index,int(stages.size())-1);
        }else{
            Document structure;
            bool useSelection=false;
            if(!preview.selectedStructure.atoms.empty()&&preview.selectedStructure.reactions.empty()&&
               preview.selectedStructure.plots.empty()){
                auto choice=ui::form(hwnd,L"단계에 넣을 구조",{
                    {L"구조 가져오기","0",false,{L"문서에서 선택한 구조",L"파일에서 열기"}}
                });
                if(!choice)return;
                useSelection=choice->front()=="0";
            }
            if(useSelection)structure=preview.selectedStructure;
            else{
                auto path=animationFile(L"단계에 넣을 구조 열기",hwnd);
                if(!path)return;
                structure=readStructure(*path);
            }
            if(id==PreviewAdd){
                ++index;
                stages.insert(stages.begin()+index,{std::move(structure),"","단계 "+std::to_string(index+1),"",""});
            }else stages[size_t(index)].structure=std::move(structure);
        }
        preview.replace(std::move(stages),index);
    }else if(id==PreviewSave){
        auto path=gifSaveFile(hwnd);
        if(!path)return;
        std::string bytes;
        {
            ExportProgress progress(hwnd);
            bytes=encodeAnimation(preview.timeline,preview.playback,[&](int done,int total,size_t size){
                return progress.update(done,total,size);
            });
        }
        if(bytes.empty())return;
        writeFile(*path,bytes);
        rememberGifFolder(*path);
        status=L"결합 애니메이션 GIF를 저장했습니다.";
        redraw();
        MessageBoxW(hwnd,L"GIF를 저장했습니다.",L"결합 애니메이션",MB_OK|MB_ICONINFORMATION);
    }
}

int thumbnailAt(HWND hwnd,const AnimationPreview& preview,POINT p){
    RECT rc{};GetClientRect(hwnd,&rc);
    if(p.y<rc.bottom-83||p.y>=rc.bottom||p.x<8||p.x>=rc.right-previewSidebar)return -1;
    const int i=preview.thumbOffset+(p.x-8)/92;
    return size_t(i)<preview.thumbs.size()?i:-1;
}

LRESULT CALLBACK animationPreviewProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    auto* preview=reinterpret_cast<AnimationPreview*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_CREATE){
        preview=static_cast<AnimationPreview*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(preview));
        preview->font=CreateFontW(-15,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,ui::interfaceFont());
        const wchar_t* labels[]={L"일시정지",L"처음부터 재생",L"이전 단계",L"다음 단계",L"속도 · 정지 시간…",
            L"구조명 · 단계명 · 에너지…",L"현재 단계 구조 바꾸기…",L"GIF 저장…",L"닫기"};
        for(int id=PreviewPlay;id<=PreviewClose;++id){
            HWND control=CreateWindowW(L"BUTTON",labels[id-PreviewPlay],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
                0,0,0,0,hwnd,reinterpret_cast<HMENU>(INT_PTR(id)),nullptr,nullptr);
            SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(preview->font),FALSE);
        }
        HWND repeat=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,
            0,0,0,0,hwnd,HMENU(INT_PTR(PreviewRepeat)),nullptr,nullptr);
        SendMessageW(repeat,WM_SETFONT,WPARAM(preview->font),FALSE);
        for(const auto& mode:loopModes)SendMessageW(repeat,CB_ADDSTRING,0,LPARAM(mode.c_str()));
        SendMessageW(repeat,CB_SETCURSEL,Playback::PingPong,0);
        for(auto id:{PreviewBackground,PreviewAdd,PreviewRemove,PreviewSegmentTiming}){
            auto control=CreateWindowW(L"BUTTON",id==PreviewAdd?L"단계 추가…":id==PreviewRemove?L"단계 삭제":
                id==PreviewSegmentTiming?L"구간별 시간…":L"출력 설정…",
                WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd,reinterpret_cast<HMENU>(INT_PTR(id)),nullptr,nullptr);
            SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(preview->font),FALSE);
        }
        HWND readout=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,0,0,hwnd,
            reinterpret_cast<HMENU>(INT_PTR(PreviewReadout)),nullptr,nullptr);
        SendMessageW(readout,WM_SETFONT,reinterpret_cast<WPARAM>(preview->font),FALSE);
        previewButtons(hwnd);
        refreshPreview(hwnd,*preview);
        SetTimer(hwnd,1,15,nullptr);
        return 0;
    }
    if(!preview)return DefWindowProcW(hwnd,msg,wp,lp);
    switch(msg){
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={680,580};return 0;
    case WM_SIZE:
        previewButtons(hwnd);InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_CTLCOLORSTATIC:return preview->colors.control(msg,reinterpret_cast<HDC>(wp));
    case WM_DRAWITEM:{
        const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        preview->colors.draw(item,item.CtlID==PreviewSave);return TRUE;
    }
    case WM_PAINT:{
        PAINTSTRUCT ps{};
        auto dc=BeginPaint(hwnd,&ps);
        try{paintPreview(hwnd,dc,*preview);}
        catch(...){FillRect(dc,&ps.rcPaint,preview->colors.panel);}
        EndPaint(hwnd,&ps);return 0;
    }
    case WM_TIMER:
        if(IsWindowEnabled(hwnd)&&!IsIconic(hwnd)){
            int before=preview->frame;
            const bool wasPlaying=preview->playing;
            preview->tick();
            if(preview->playing!=wasPlaying)SetDlgItemTextW(hwnd,PreviewPlay,L"재생");
            if(preview->frame!=before||preview->playing!=wasPlaying)refreshPreview(hwnd,*preview);
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE:{
        POINT point{short(LOWORD(lp)),short(HIWORD(lp))};
        const auto seek=seekBounds(hwnd);
        if(msg==WM_LBUTTONDOWN){
            const int thumb=thumbnailAt(hwnd,*preview,point);
            if(thumb>=0){
                setPlaying(hwnd,*preview,false);
                preview->thumbFrom=thumb;preview->seek(thumb*transitionSteps);
                SetCapture(hwnd);refreshPreview(hwnd,*preview);return 0;
            }
        }
        if(msg==WM_LBUTTONDOWN&&PtInRect(&seek,point)){
            preview->seeking=true;
            setPlaying(hwnd,*preview,false);
            SetCapture(hwnd);
        }
        if(preview->seeking){
            double fraction=double(point.x-seek.left)/std::max(1L,seek.right-seek.left);
            preview->seek(int(std::lround(std::clamp(fraction,0.,1.)*(preview->frameEnds.size()-1))));
            refreshPreview(hwnd,*preview);
        }
        return 0;
    }
    case WM_LBUTTONUP:{
        const int from=preview->thumbFrom;
        const int to=thumbnailAt(hwnd,*preview,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});
        if(preview->seeking||from>=0)ReleaseCapture();
        if(from>=0&&to>=0&&to!=from){
            auto stages=preview->timeline.stages;
            auto stage=std::move(stages[from]);
            stages.erase(stages.begin()+from);
            stages.insert(stages.begin()+to,std::move(stage));
            try{preview->replace(std::move(stages),to);}
            catch(const std::exception& e){MessageBoxW(hwnd,ui::wide(e.what()).c_str(),L"단계 순서",MB_OK|MB_ICONWARNING);}
        }
        refreshPreview(hwnd,*preview);return 0;
    }
    case WM_MOUSEWHEEL:{
        RECT rc{};GetClientRect(hwnd,&rc);
        const int visible=std::max(1,int(rc.right-previewSidebar-8)/92);
        preview->thumbOffset=std::clamp(preview->thumbOffset-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA,
            0,std::max(0,int(preview->thumbs.size())-visible));
        InvalidateRect(hwnd,nullptr,FALSE);return 0;
    }
    case WM_CAPTURECHANGED:
        preview->seeking=false;preview->thumbFrom=-1;return 0;
    case WM_COMMAND:{
        const int id=LOWORD(wp);
        if(id==PreviewClose||id==IDCANCEL){SendMessageW(hwnd,WM_CLOSE,0,0);return 0;}
        if(id==PreviewRepeat&&HIWORD(wp)!=CBN_SELCHANGE)return 0;
        if((id>=PreviewPlay&&id<=PreviewSave)||id==PreviewRepeat||id==PreviewBackground||id==PreviewAdd||id==PreviewRemove||id==PreviewSegmentTiming){
            // Pause while editing or choosing an export destination.
            if(id>=PreviewTiming)setPlaying(hwnd,*preview,false);
            try{previewAction(hwnd,*preview,id);}
            catch(const std::exception& e){MessageBoxW(hwnd,ui::wide(e.what()).c_str(),L"결합 애니메이션",MB_OK|MB_ICONWARNING);}
            refreshPreview(hwnd,*preview);
        }
        return 0;
    }
    case WM_CLOSE:
        KillTimer(hwnd,1);preview->done=true;PostMessageW(hwnd,WM_NULL,0,0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

struct RoutePicker {
    const Reaction& reaction;
    Scene scene;
    std::vector<bool> available;
    std::vector<size_t> route;
    HWND info=nullptr;
    double zoom=1.;
    Vec origin;
    bool done=false,accepted=false;

    void update(HWND hwnd){
        std::wstring text=L"구조를 화살표 순서대로 클릭하세요. 선택:";
        for(auto i:route)text+=L" "+std::to_wstring(i+1);
        SetWindowTextW(info,text.c_str());
        EnableWindow(GetDlgItem(hwnd,IDOK),route.size()>=2);
        InvalidateRect(hwnd,nullptr,TRUE);
    }
    static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
        if(msg==WM_CREATE){
            auto p=static_cast<RoutePicker*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));
            RECT r{};GetClientRect(hwnd,&r);
            p->info=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_PATHELLIPSIS,14,9,r.right-28,22,hwnd,nullptr,nullptr,nullptr);
            for(auto [id,label,x]:std::vector<std::tuple<int,const wchar_t*,int>>{
                {3,L"마지막 선택 취소",14},{IDOK,L"경로 적용",int(r.right)-228},{IDCANCEL,L"닫기",int(r.right)-116}}){
                HWND button=CreateWindowW(L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP,x,r.bottom-40,108,28,hwnd,HMENU(INT_PTR(id)),nullptr,nullptr);
                SendMessageW(button,WM_SETFONT,WPARAM(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
            }
            p->update(hwnd);return 0;
        }
        auto* p=reinterpret_cast<RoutePicker*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(!p)return DefWindowProcW(hwnd,msg,wp,lp);
        if(msg==WM_PAINT){
            PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);
            RECT r{};GetClientRect(hwnd,&r);
            Graphics g(dc);g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetClip(RectF(0,36,float(r.right),float(r.bottom-84)));
            const auto& b=p->scene.bounds;
            p->zoom=std::min((r.right-32)/std::max(1.,b.w),(r.bottom-98)/std::max(1.,b.h));
            p->origin={16-b.x*p->zoom,44-b.y*p->zoom};
            g.TranslateTransform(float(p->origin.x),float(p->origin.y));
            g.ScaleTransform(float(p->zoom),float(p->zoom));render(g,p->scene);
            g.ResetTransform();
            for(size_t i=0;i<p->reaction.slots.size();++i){
                if(!p->available[i])continue;
                const Vec c=p->origin+reactionSlot(p->reaction,i)*p->zoom;
                const auto found=std::find(p->route.begin(),p->route.end(),i);
                const bool selected=found!=p->route.end();
                Pen pen(selected?Color(0,135,120):Color(155,160,164),selected?2.f:1.f);
                const float w=float(p->reaction.slotWidth*p->zoom),h=float(p->reaction.slotHeight*p->zoom);
                g.DrawRectangle(&pen,float(c.x)-w/2,float(c.y)-h/2,w,h);
                if(selected)ui::drawLabel(g,std::to_wstring(found-p->route.begin()+1),
                    RectF(float(c.x)-w/2+2,float(c.y)-h/2+2,25,18),14,Color(0,100,90),true);
            }
            EndPaint(hwnd,&ps);return 0;
        }
        if(msg==WM_LBUTTONDOWN&&p->zoom>0){
            const Vec pt=(Vec{double(GET_X_LPARAM(lp)),double(GET_Y_LPARAM(lp))}-p->origin)*(1/p->zoom);
            size_t next=p->available.size();double distance=1e30;
            for(size_t i=0;i<p->available.size();++i){
                const Vec d=pt-reactionSlot(p->reaction,i);
                if(p->available[i]&&std::abs(d.x)<=p->reaction.slotWidth/2&&std::abs(d.y)<=p->reaction.slotHeight/2&&d.length()<distance){
                    next=i;distance=d.length();
                }
            }
            if(next<p->available.size()&&p->route.size()<maxStages){
                if(p->route.empty()||std::any_of(p->reaction.links.begin(),p->reaction.links.end(),
                    [&](const MechanismLink& link){return link.from==int(p->route.back())&&link.to==int(next);})){
                    p->route.push_back(next);p->update(hwnd);
                }else MessageBeep(MB_ICONWARNING);
            }
            return 0;
        }
        if(msg==WM_CLOSE||msg==WM_COMMAND){
            const auto id=msg==WM_CLOSE?IDCANCEL:LOWORD(wp);
            if(id==3&&!p->route.empty()){p->route.pop_back();p->update(hwnd);}
            if(id==IDCANCEL||(id==IDOK&&p->route.size()>=2)){
                p->accepted=id==IDOK;p->done=true;
            }
            return 0;
        }
        return DefWindowProcW(hwnd,msg,wp,lp);
    }
};

std::optional<std::vector<size_t>> pickRoute(const Document& source,const Reaction& mechanism){
    RoutePicker picker{mechanism};
    Document mechanismDrawing=source;
    mechanismDrawing.reactions.erase(std::remove_if(mechanismDrawing.reactions.begin(),mechanismDrawing.reactions.end(),
        [&](const Reaction& r){return r.id!=mechanism.id;}),mechanismDrawing.reactions.end());
    mechanismDrawing.plots.clear();
    std::vector<int> keep{mechanism.id};
    for(size_t i=0;i<mechanism.slots.size();++i){
        auto structure=structureInSlot(source,mechanism.id,i);
        picker.available.push_back(!structure.atoms.empty());
        keep.insert(keep.end(),mechanism.slots[i].members.begin(),mechanism.slots[i].members.end());
    }
    mechanismDrawing=mechanismDrawing.subset(keep);
    picker.scene=makeScene(mechanismDrawing,measure);
    static const auto registered=[]{
        WNDCLASSW wc{};wc.lpfnWndProc=RoutePicker::proc;wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpszClassName=L"YoungDrawRoutePicker";wc.hCursor=LoadCursorW(nullptr,IDC_HAND);
        wc.hbrBackground=HBRUSH(COLOR_WINDOW+1);return RegisterClassW(&wc);
    }();
    if(!registered)return {};
    const RECT r=ui::ownedWindowRect(win,1040,670);
    HWND hwnd=CreateWindowExW(WS_EX_DLGMODALFRAME,L"YoungDrawRoutePicker",L"분기 메커니즘 경로",
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,r.left,r.top,r.right-r.left,r.bottom-r.top,win,nullptr,nullptr,&picker);
    if(!hwnd)return {};
    EnableWindow(win,FALSE);ShowWindow(hwnd,SW_SHOW);
    MSG msg{};BOOL got=1;
    while(!picker.done&&(got=GetMessageW(&msg,nullptr,0,0))>0){
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE)picker.done=true;
        else if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    EnableWindow(win,TRUE);DestroyWindow(hwnd);SetActiveWindow(win);
    if(!got)PostQuitMessage(int(msg.wParam));
    if(picker.accepted)return picker.route;
    return {};
}

void previewAnimation(std::vector<AnimationStage> stages,Document selected){
    static constexpr wchar_t windowClass[]=L"YoungDrawAnimationPreview";
    static const ATOM registered=[](){
        WNDCLASSW wc{};
        wc.lpfnWndProc=animationPreviewProc;
        wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpszClassName=windowClass;
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        return RegisterClassW(&wc);
    }();
    if(!registered)throw std::runtime_error("애니메이션 미리보기 창을 등록할 수 없습니다.");
    AnimationPreview preview(std::move(stages),std::move(selected));
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(win,MONITOR_DEFAULTTONEAREST),&monitor);
    const auto rect=ui::ownedWindowRect(win,std::min(1050,int(monitor.rcWork.right-monitor.rcWork.left)),
        std::min(680,int(monitor.rcWork.bottom-monitor.rcWork.top)));
    auto hwnd=CreateWindowExW(WS_EX_DLGMODALFRAME,windowClass,L"결합 애니메이션 미리보기",
        WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MAXIMIZEBOX|WS_POPUP|WS_CLIPCHILDREN,
        rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,win,nullptr,GetModuleHandleW(nullptr),&preview);
    if(!hwnd)throw std::runtime_error("애니메이션 미리보기 창을 열 수 없습니다.");
    appearance::titlebar(hwnd);
    EnableWindow(win,FALSE);
    ShowWindow(hwnd,SW_SHOW);
    SetFocus(GetDlgItem(hwnd,PreviewPlay));
    preview.seek(0);
    MSG msg{};
    BOOL read=1;
    while(!preview.done&&(read=GetMessageW(&msg,nullptr,0,0))>0){
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE&&IsWindowEnabled(hwnd))
            SendMessageW(hwnd,WM_CLOSE,0,0);
        else if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    EnableWindow(win,TRUE);
    DestroyWindow(hwnd);
    SetActiveWindow(win);
    if(read==0)PostQuitMessage(int(msg.wParam));
}

} // namespace

std::string bondAnimationGif(std::vector<AnimationStage> stages,bool repeat,AnimationBackground background){
    AnimationTimeline timeline(std::move(stages));
    Playback playback;
    playback.loop=repeat?Playback::PingPong:Playback::Once;
    playback.background=background;
    return encodeAnimation(timeline,playback);
}

void createBondAnimation(){
    auto selected=selectedDocument();
    std::vector<std::wstring> inputs;
    std::vector<int> mechanisms;
    int preferred=targetContainer;
    for(const auto& mechanism:doc.reactions){
        if(mechanism.slots.size()<2)continue;
        mechanisms.push_back(mechanism.id);
        std::wstring name=L"메커니즘 "+std::to_wstring(mechanisms.size())+L" · "+
            ui::wide(mechanism.slots.front().label)+L" → "+ui::wide(mechanism.slots.back().label);
        inputs.push_back(std::move(name));
        if(contains(selection,mechanism.id)||(!preferred&&std::any_of(mechanism.slots.begin(),mechanism.slots.end(),
            [&](const Slot& slot){return std::any_of(slot.members.begin(),slot.members.end(),
                [&](int id){return contains(selection,id);});})))preferred=mechanism.id;
    }
    auto found=std::find(mechanisms.begin(),mechanisms.end(),preferred);
    const auto initial=found==mechanisms.end()?0:size_t(found-mechanisms.begin());
    auto ids=groupedSelection(selection);
    auto isArrow=[](const Object& object){return object.kind==ObjectKind::Line&&object.head&&object.points.size()>=2;};
    const bool canDetect=!doc.atoms.empty()&&std::any_of(doc.objects.begin(),doc.objects.end(),isArrow);
    const bool detectSelection=canDetect&&!selected.atoms.empty()&&std::any_of(doc.objects.begin(),doc.objects.end(),
        [&](const Object& object){return isArrow(object)&&contains(ids,object.id);});
    if(canDetect)inputs.push_back(L"현재 문서에서 구조·화살표·자막 자동 인식");
    inputs.push_back(L"시작·끝 구조를 파일에서 열기");
    if(!selected.atoms.empty()){
        inputs.push_back(L"선택한 구조 → 파일에서 열기");
        inputs.push_back(L"선택한 구조 → 클립보드 구조");
    }
    auto options=ui::form(win,L"결합 애니메이션 GIF",{
        {L"구조 가져오기\n같은 원소·라벨은 저장 순서로 대응",std::to_string(detectSelection&&found==mechanisms.end()?mechanisms.size():initial),false,inputs}
    });
    if(!options)return;
    const auto mode=size_t(std::stoi(options->front()));
    std::vector<AnimationStage> stages;
    const bool autoDetect=canDetect&&mode==mechanisms.size();
    if(mode<mechanisms.size()||autoDetect){
        std::optional<Document> recognized;
        const Document* source=&doc;
        const Reaction* chosen=nullptr;
        if(autoDetect){
            recognized=doc;
            arrangeImportedMechanism(*recognized,detectSelection?ids:std::vector<int>{},true);
            source=&*recognized;
            chosen=&source->reactions.back();
        }else chosen=reaction(mechanisms[mode]);
        const auto& mechanism=*chosen;
        auto route=defaultRoute(mechanism);
        const bool chooseRoute=mechanism.explicitConnections()&&
            (route.size()!=mechanism.slots.size()||std::any_of(mechanism.links.begin(),mechanism.links.end(),
                [&](const MechanismLink& link){
                    if(link.from<0)return false;
                    auto at=std::find(route.begin(),route.end(),size_t(link.from));
                    if(at==route.end())return true;
                    const auto next=std::next(at)==route.end()?route.front():*std::next(at);
                    return size_t(link.to)!=next;
                }));
        if(chooseRoute){
            auto choice=pickRoute(*source,mechanism);
            if(!choice)return;
            route=std::move(*choice);
        }
        stages=animationMechanismStages(*source,mechanism,route);
    }else{
        Document start,end;
        const size_t external=mode-mechanisms.size()-size_t(canDetect);
        if(external==0){
            auto path=animationFile(L"시작 구조 열기");
            if(!path)return;
            start=readStructure(*path);
        }else start=selected;
        if(external==2){
            auto copied=clipboardDocument();
            if(!copied||copied->atoms.empty())throw std::runtime_error("ChemDraw 또는 YoungDraw의 끝 구조를 먼저 복사하세요.");
            end=std::move(*copied);
        }else{
            auto path=animationFile(L"끝 구조 열기");
            if(!path)return;
            end=readStructure(*path);
        }
        stages={{std::move(start),"","단계 1","",""},{std::move(end),"","단계 2","",""}};
    }

    previewAnimation(std::move(stages),std::move(selected));

}

} // namespace yd::editor

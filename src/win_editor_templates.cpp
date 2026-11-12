#include "win_editor_clipboard.h"
#include "win_editor_files.h"
#include "win_editor_render.h"
#include "win_editor_shortcuts.h"
#include "win_editor_state.h"
#include "win_editor_tabs.h"
#include "win_editor_templates.h"
#include "cdxml.h"
#include "lab_templates_data.h"
#include "structure_templates.h"
#include "win_dialogs.h"
#include "win_ui.h"
#include <cwctype>
#include <fstream>
#include <sstream>

namespace yd::editor {

fs::path templateDir(){wchar_t path[32768];DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);if(!n||n>=32768)throw std::runtime_error("LOCALAPPDATA unavailable");fs::path p=fs::path(path)/L"YoungDraw"/L"Templates";fs::create_directories(p);return p;}

fs::path favoritesPath(){return templateDir().parent_path()/L"template-favorites.txt";}
std::string templateKey(const ui::TemplateCard& card){
    return card.path.empty()?std::string("builtin:")+ui::utf8(card.category)+":"+ui::utf8(card.name)
                            :std::string("user:")+ui::utf8(card.path);
}
std::set<std::string> loadFavorites(){
    std::set<std::string> keys;std::ifstream in(favoritesPath(),std::ios::binary);std::string line;
    while(std::getline(in,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(!line.empty())keys.insert(line);}
    return keys;
}
void saveFavorites(const std::set<std::string>& keys){
    std::string data;for(const auto& key:keys)data+=key+"\n";
    writeFile(favoritesPath().wstring(),data);
}
bool validTemplatePart(const std::wstring& part){
    if(part.empty()||part==L"."||part==L".."||part.size()>100||part.back()==L'.'||part.back()==L' ')return false;
    if(part.find_first_of(L"<>:\"/\\|?*")!=std::wstring::npos)return false;
    for(wchar_t c:part)if(c<32)return false;
    std::wstring upper=part;std::transform(upper.begin(),upper.end(),upper.begin(),[](wchar_t c){return wchar_t(std::towupper(c));});
    auto stem=upper.substr(0,upper.find(L'.'));
    if(stem==L"CON"||stem==L"PRN"||stem==L"AUX"||stem==L"NUL")return false;
    if(stem.size()==4&&(stem.substr(0,3)==L"COM"||stem.substr(0,3)==L"LPT")&&stem[3]>=L'1'&&stem[3]<=L'9')return false;
    return true;
}
fs::path templateLocation(const std::wstring& name,const std::wstring& group,const std::wstring& ext){
    if(!validTemplatePart(name)||!validTemplatePart(group))throw std::runtime_error("템플릿 이름·분류에 사용할 수 없는 문자가 있습니다.");
    return templateDir()/group/(name+ext);
}
std::wstring groupOf(const fs::path& path){
    auto relative=path.parent_path().lexically_relative(templateDir());
    return relative.empty()||relative==fs::path(L".")?L"일반":relative.wstring();
}
void retargetTemplateTabs(const fs::path& from,const std::optional<fs::path>& to){
    if(filePath==from.wstring())filePath=to?to->wstring():L"";
    for(auto& tab:documents)if(tab.path==from.wstring())tab.path=to?to->wstring():L"";
    title();
}
std::vector<ui::TemplateCard>& templateCards(size_t& unreadable){
    struct Cache {std::string signature;std::vector<ui::TemplateCard> cards;size_t unreadable=0;};
    static Cache cache;
    std::vector<fs::path> paths;
    for(auto& entry:fs::recursive_directory_iterator(templateDir(),fs::directory_options::skip_permission_denied)){
        if(!entry.is_regular_file())continue;
        auto ext=entry.path().extension().wstring();
        if(_wcsicmp(ext.c_str(),L".ydraw")==0||_wcsicmp(ext.c_str(),L".cdxml")==0)paths.push_back(entry.path());
    }
    std::sort(paths.begin(),paths.end());
    std::string fingerprint;
    for(const auto& path:paths){
        try{
            fingerprint+=ui::utf8(path.wstring())+"|"+std::to_string(fs::file_size(path))+"|"+
                         std::to_string(fs::last_write_time(path).time_since_epoch().count())+"\n";
        }catch(const fs::filesystem_error&){fingerprint+=ui::utf8(path.wstring())+"|unreadable\n";}
    }
    if(cache.signature!=fingerprint||cache.cards.empty()){
        Cache next;next.signature=fingerprint;
        auto add=[&](std::wstring category,std::wstring name,Document fragment,std::wstring path=L"",std::wstring group=L""){
            auto preview=makeScene(fragment,measure);
            next.cards.push_back({std::move(name),std::move(category),std::move(fragment),std::move(preview),std::move(path),std::move(group),false});
        };
        Document basic;basic.ring({0,0},6,true);add(L"기본",L"벤젠",basic);
        basic={};basic.ring({0,0},5);add(L"기본",L"사이클로펜테인",basic);
        add(L"기본",L"의자형 · 형태 보호",chairTemplate());
        add(L"기본",L"반응 메커니즘",mechanismDemo());
        add(L"기본",L"촉매 순환 · 수소화 예시",catalyticCycleDemo());
        add(L"기본",L"에너지 그래프",coordinateDemo());
        for(const auto& entry:layoutReferences())add(L"2D 배치 기준",ui::wide(entry.name),entry.drawing);
        for(const auto& entry:functionalGroups())
            add(L"작용기",ui::wide(entry.abbreviation)+L" · "+ui::wide(entry.name),entry.drawing);
        for(auto& entry:labTemplates)add(ui::wide(entry.category),ui::wide(entry.name),fromNative(entry.data));
        for(const auto& path:paths)try{
            add(L"사용자",path.stem().wstring(),fromCdxml(readFile(path.wstring())),path.wstring(),groupOf(path));
        }catch(...){next.unreadable++;}
        cache=std::move(next);
    }
    auto favorites=loadFavorites();
    for(auto& card:cache.cards)card.favorite=favorites.count(templateKey(card))!=0;
    unreadable=cache.unreadable;
    return cache.cards;
}
void saveCurrentTemplate(){
    Document fragment=selection.empty()?doc:selectedDocument();
    if(fragment.allIds().empty())throw std::runtime_error("템플릿으로 저장할 객체가 없습니다.");
    auto result=ui::form(win,L"템플릿 저장",{{L"템플릿 이름","새 템플릿"},{L"분류(폴더)","일반"}});
    if(!result)return;
    auto path=templateLocation(ui::wide((*result)[0]),ui::wide((*result)[1]));
    if(fs::exists(path)&&MessageBoxW(win,L"같은 이름의 템플릿을 덮어쓰시겠습니까?",L"템플릿 저장",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
    fs::create_directories(path.parent_path());
    writeFile(path.wstring(),toCdxml(fragment));
    status=L"사용자 템플릿을 저장했습니다. · 삽입 > 템플릿에서 관리할 수 있습니다.";
    redraw();
}

void library(const std::wstring& initialCategory){
    std::wstring category=initialCategory;
    for(;;){
        size_t unreadable=0;
        auto& cards=templateCards(unreadable);
        auto result=ui::templateGallery(win,cards,category,[](Graphics& g,const Scene& scene,RectF box){
            auto b=scene.bounds;double scale=std::min(double(box.Width)/std::max(1.,b.w),double(box.Height)/std::max(1.,b.h));auto saved=g.Save();g.SetClip(box,CombineModeIntersect);
            g.TranslateTransform(float(box.X+box.Width/2-(b.x+b.w/2)*scale),float(box.Y+box.Height/2-(b.y+b.h/2)*scale));g.ScaleTransform(float(scale),float(scale));render(g,scene,false,true);g.Restore(saved);
        });
        if(!result){if(unreadable)status=L"읽을 수 없는 사용자 템플릿 "+std::to_wstring(unreadable)+L"개를 제외했습니다.";return;}
        auto card=cards.at(result->index);
        if(result->action==ui::TemplateAction::Favorite){
            auto favorites=loadFavorites();auto key=templateKey(card);
            if(!favorites.erase(key))favorites.insert(key);
            saveFavorites(favorites);category=L"전체";continue;
        }
        if(result->action==ui::TemplateAction::Edit){
            auto fragment=card.document;
            if(result->metal){const int elements[]={26,26,25,27,28,29,30};for(auto& a:fragment.atoms)if(a.label=="M"){a.element=elements[result->metal];a.label.clear();}}
            newDocumentTab(std::move(fragment),card.path);
            status=card.path.empty()?L"기본 템플릿의 복사본입니다. 파일 > 템플릿으로 저장으로 보관하세요.":L"템플릿 편집 탭입니다. Ctrl+S로 원본 템플릿을 덮어씁니다.";
            redraw();return;
        }
        if(result->action==ui::TemplateAction::Rename||result->action==ui::TemplateAction::Move){
            if(card.path.empty())continue;
            auto isMove=result->action==ui::TemplateAction::Move;
            auto answer=ui::form(win,isMove?L"템플릿 분류 이동":L"템플릿 이름 변경",
                {{L"템플릿 이름",ui::utf8(card.name)},{L"분류(폴더)",ui::utf8(card.group)}});
            if(!answer)continue;
            auto oldPath=fs::path(card.path),nextPath=templateLocation(ui::wide((*answer)[0]),ui::wide((*answer)[1]),oldPath.extension().wstring());
            if(nextPath==oldPath)continue;
            if(fs::exists(nextPath))throw std::runtime_error("같은 이름의 템플릿이 이미 있습니다.");
            fs::create_directories(nextPath.parent_path());
            fs::rename(oldPath,nextPath);
            auto favorites=loadFavorites();
            if(favorites.erase(templateKey(card))){card.path=nextPath.wstring();favorites.insert(templateKey(card));saveFavorites(favorites);}
            retargetTemplateTabs(oldPath,nextPath);
            category=L"사용자";continue;
        }
        if(result->action==ui::TemplateAction::Remove){
            if(card.path.empty())continue;
            if(MessageBoxW(win,(L"다음 사용자 템플릿을 삭제하시겠습니까?\n"+card.name).c_str(),L"템플릿 삭제",MB_YESNO|MB_ICONWARNING)!=IDYES)continue;
            auto path=fs::path(card.path);fs::remove(path);
            auto favorites=loadFavorites();if(favorites.erase(templateKey(card)))saveFavorites(favorites);
            retargetTemplateTabs(path,std::nullopt);
            category=L"사용자";continue;
        }
        auto fragment=card.document;
        if(result->metal){const int elements[]={26,26,25,27,28,29,30};for(auto& a:fragment.atoms)if(a.label=="M"){a.element=elements[result->metal];a.label.clear();}}
        Vec at=center();if(!doc.atoms.empty()||!doc.objects.empty()||!doc.reactions.empty()||!doc.plots.empty()){
            auto old=scene().bounds,incoming=makeScene(fragment).bounds;at={old.x+old.w+incoming.w/2+30,old.y+old.h/2};
        }
        insert(fragment,at);tool=Select;status=L"미리보기에서 선택한 템플릿을 삽입했습니다.";fit();return;
    }
}

} // namespace yd::editor

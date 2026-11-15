#include "office.h"
#include "cdxml.h"
#include "mechanism_data.h"
#include "plot_data.h"
#include "win_editor_render.h"
#include <wrl/client.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace yd;
using Microsoft::WRL::ComPtr;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static Document readStorage(IStorage* storage){
    CLSID identity{};check(SUCCEEDED(ReadClassStg(storage,&identity)),"missing Office object identity");
    ComPtr<IOleCache> cache;check(SUCCEEDED(CreateDataCache(nullptr,identity,IID_PPV_ARGS(&cache))),"cannot create Office presentation cache");
    ComPtr<IPersistStorage> persisted;check(SUCCEEDED(cache.As(&persisted))&&SUCCEEDED(persisted->Load(storage)),"cannot load saved Office presentation");
    ComPtr<IDataObject> data;check(SUCCEEDED(cache.As(&data)),"missing Office presentation data");
    FORMATETC format{CF_ENHMETAFILE,nullptr,DVASPECT_CONTENT,-1,TYMED_ENHMF};STGMEDIUM medium{};
    check(SUCCEEDED(data->GetData(&format,&medium)),"Office cannot retrieve cached vector presentation");
    ENHMETAHEADER header{};auto headerSize=GetEnhMetaFileHeader(medium.hEnhMetaFile,sizeof(header),&header);ReleaseStgMedium(&medium);
    check(headerSize&&header.nRecords>5,"saved Office presentation is empty");
    ComPtr<IStream> stream;check(SUCCEEDED(storage->OpenStream(L"YoungDrawDocument",nullptr,STGM_READ|STGM_SHARE_EXCLUSIVE,0,&stream)),"missing Office editing source");
    STATSTG info{};check(SUCCEEDED(stream->Stat(&info,STATFLAG_NONAME))&&info.cbSize.QuadPart<128*1024*1024,"invalid Office source size");
    std::string bytes(size_t(info.cbSize.QuadPart),'\0');ULONG read=0;
    check(SUCCEEDED(stream->Read(bytes.data(),ULONG(bytes.size()),&read))&&read==bytes.size(),"truncated Office source");
    return fromNative(bytes);
}
static Document readObject(const std::wstring& path){
    ComPtr<IStorage> storage;check(SUCCEEDED(StgOpenStorage(path.c_str(),nullptr,STGM_READ|STGM_SHARE_EXCLUSIVE,nullptr,0,&storage)),"cannot reopen Office storage");
    return readStorage(storage.Get());
}
class TestSite final:public IOleClientSite {
    LONG refs=1;
public:
    IPersistStorage* object=nullptr;ComPtr<IStorage> storage;int saves=0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown&&iid!=IID_IOleClientSite)return E_NOINTERFACE;*out=static_cast<IOleClientSite*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ULONG(InterlockedIncrement(&refs));}
    ULONG STDMETHODCALLTYPE Release()override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return ULONG(n);}
    HRESULT STDMETHODCALLTYPE SaveObject()override{
        ComPtr<ILockBytes> bytes;auto hr=CreateILockBytesOnHGlobal(nullptr,TRUE,&bytes);if(FAILED(hr))return hr;
        ComPtr<IStorage> next;hr=StgCreateDocfileOnILockBytes(bytes.Get(),STGM_CREATE|STGM_READWRITE|STGM_SHARE_EXCLUSIVE,0,&next);if(FAILED(hr))return hr;
        hr=object->Save(next.Get(),FALSE);if(SUCCEEDED(hr))hr=object->SaveCompleted(next.Get());if(SUCCEEDED(hr))hr=next->Commit(STGC_DEFAULT);
        if(SUCCEEDED(hr)){storage=next;++saves;}return hr;
    }
    HRESULT STDMETHODCALLTYPE GetMoniker(DWORD,DWORD,IMoniker** out)override{if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetContainer(IOleContainer** out)override{if(out)*out=nullptr;return E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE ShowObject()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnShowWindow(BOOL)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE RequestNewObjectLayout()override{return E_NOTIMPL;}
};
static void editLifecycle(const Document& original,const Document& changed){
    Document current;int opened=0;
    check(SUCCEEDED(office::start({nullptr,[&](const Document& d){current=d;++opened;},[&](){return current;},editor::officePicture},false,false)),"headless editor setup failed");
    ComPtr<IDataObject> data;check(SUCCEEDED(office::createDataObject(original,&data)),"cannot create clipboard data contract");
    ComPtr<IOleObject> object;ComPtr<IPersistStorage> persist;check(SUCCEEDED(data.As(&object))&&SUCCEEDED(data.As(&persist)),"missing OLE editing interfaces");
    auto* site=new TestSite;site->object=persist.Get();ComPtr<IOleClientSite> ownedSite;ownedSite.Attach(site);object->SetClientSite(site);
    check(SUCCEEDED(object->DoVerb(OLEIVERB_PRIMARY,nullptr,site,0,nullptr,nullptr))&&opened==1&&toNative(current)==toNative(original),"primary OLE verb did not open exact editing source");
    current=changed;check(SUCCEEDED(office::update())&&site->saves==1,"editor update did not save to Office host");
    check(toNative(readStorage(site->storage.Get()))==toNative(changed),"Office host did not receive edited source and presentation");
    check(SUCCEEDED(object->Close(OLECLOSE_NOSAVE))&&!office::editing(),"OLE close did not finish editing");
    check(SUCCEEDED(object->DoVerb(OLEIVERB_OPEN,nullptr,site,0,nullptr,nullptr))&&opened==2&&toNative(current)==toNative(changed),"second OLE open lost saved edits");
    current=original;object->Close(OLECLOSE_NOSAVE);
    check(SUCCEEDED(object->DoVerb(OLEIVERB_OPEN,nullptr,site,0,nullptr,nullptr))&&toNative(current)==toNative(changed),"cancelled edits replaced committed source");
    current=original;check(SUCCEEDED(object->Close(OLECLOSE_SAVEIFDIRTY))&&site->saves==2,"close with save did not update Office host");
    check(toNative(readStorage(site->storage.Get()))==toNative(original),"close with save lost editing source");
    object->SetClientSite(nullptr);
    office::start({nullptr,{}, {},editor::officePicture},false,false);
}
static void verifyEmf(const std::wstring& path){
    auto image=GetEnhMetaFileW(path.c_str());check(image!=nullptr,"missing EMF vector presentation");
    ENHMETAHEADER info{};auto size=GetEnhMetaFileHeader(image,sizeof(info),&info);
    DeleteEnhMetaFile(image);
    check(size&&info.nRecords>5&&info.rclFrame.right>info.rclFrame.left&&info.rclFrame.bottom>info.rclFrame.top,"empty Office presentation");
}
int main(){
    auto hr=OleInitialize(nullptr);if(FAILED(hr))return 1;
    Gdiplus::GdiplusStartupInput startup;ULONG_PTR token=0;
    if(Gdiplus::GdiplusStartup(&token,&startup,nullptr)!=Gdiplus::Ok){OleUninitialize();return 1;}
    int result=0;
    try{
        check(SUCCEEDED(office::start({nullptr,{}, {},editor::officePicture},false,false)),"Office test setup failed");
        std::filesystem::create_directories("office-storage-verification");
        for(int scenario=0;scenario<6;++scenario){
            auto d=mechanismDemo();
            if(scenario){
                EnergyPlot p;p.id=d.nextId++;p.p={50,600};p.width=760;p.height=420;
                for(int state=0;state<7;++state)p.states.push_back({"I_{"+std::to_string(state)+"}",double(state)});
                for(int path=0;path<3+scenario;++path){EnergySeries series;series.name="Path "+std::to_string(path+1);series.dash=path%3;for(int state=0;state<7;++state)series.values.push_back((state%2?25.:0.)+path*.3-state);p.series.push_back(series);}
                if(scenario==2){p.yMin=-8;p.yMax=32;}
                if(scenario==3)p.series[1].present={true,true,false,true,true,true,true};
                if(scenario==4)p=bilateralPlot(p,0,"Alternative");
                if(scenario==5)p=publicationPlot(p);
                d.plots.push_back(p);
            }
            auto base=std::filesystem::path("office-storage-verification")/std::to_string(scenario);
            auto storage=base.wstring()+L".ole",image=base.wstring()+L".emf";
            check(SUCCEEDED(office::exportEmbedded(d,storage.c_str(),image.c_str())),"Office export failed");
            verifyEmf(image);auto reopened=readObject(storage);
            check(toNative(reopened)==toNative(d),"Office disk save/reopen changed editing source");
            addMechanismBranch(reopened.reactions[0],0);
            if(!reopened.plots.empty())reopened.plots[0].series[0].values[1]+=1.25;
            check(SUCCEEDED(office::exportEmbedded(reopened,storage.c_str(),image.c_str())),"Office re-edit save failed");
            verifyEmf(image);check(toNative(readObject(storage))==toNative(reopened),"Office re-edit was not preserved");
            editLifecycle(d,reopened);
        }
        std::cout<<"Office storage: mechanism and 5 complex graphs saved, reopened, edited and saved again; EMF presentations valid\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    Gdiplus::GdiplusShutdown(token);OleUninitialize();return result;
}

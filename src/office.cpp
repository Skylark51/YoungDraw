#include "office.h"
#include "cdxml.h"
#include "scene.h"
#include <wrl/client.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <cstring>
#include <new>

namespace yd::office {
using Microsoft::WRL::ComPtr;
// Stable identity: documents stored by Office retain this CLSID across releases.
const CLSID classId={0xa861b35e,0x76d4,0x43ed,{0x91,0x51,0x67,0x41,0x81,0x12,0x07,0xda}};
const wchar_t* classKey=L"{A861B35E-76D4-43ED-9151-6741811207DA}";
Editor editor{};
DWORD registration=0;
bool embeddingServer=false;
LONG serverObjects=0,serverLocks=0;
void idleExit(){if(embeddingServer&&!serverObjects&&!serverLocks)PostQuitMessage(0);}
CLIPFORMAT nativeFormat(){return CLIPFORMAT(RegisterClipboardFormatW(L"YoungDraw.Project.v2"));}
CLIPFORMAT sourceFormat(){return CLIPFORMAT(RegisterClipboardFormatW(L"Embed Source"));}
CLIPFORMAT descriptorFormat(){return CLIPFORMAT(RegisterClipboardFormatW(L"Object Descriptor"));}
HGLOBAL memory(const void* data,size_t size){auto h=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,size);if(!h)return nullptr;auto p=GlobalLock(h);if(!p){GlobalFree(h);return nullptr;}memcpy(p,data,size);GlobalUnlock(h);return h;}
SIZEL extent(const Document& d){auto b=makeScene(d).bounds;return {LONG(std::max(1.,b.w+12)*2540/72),LONG(std::max(1.,b.h+12)*2540/72)};}
HRESULT registerServer(){
    wchar_t executable[32768];if(!GetModuleFileNameW(nullptr,executable,32768))return HRESULT_FROM_WIN32(GetLastError());
    std::wstring base=L"Software\\Classes\\CLSID\\"+std::wstring(classKey);
    auto put=[](const std::wstring& path,const std::wstring& value){HKEY key{};auto result=RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);if(result!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(result);result=RegSetValueExW(key,nullptr,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),DWORD((value.size()+1)*sizeof(wchar_t)));RegCloseKey(key);return HRESULT_FROM_WIN32(result);};
    for(auto item:std::vector<std::pair<std::wstring,std::wstring>>{{base,L"YoungDraw Drawing"},{base+L"\\LocalServer32",L"\""+std::wstring(executable)+L"\" /Embedding"},{base+L"\\InprocHandler32",L"ole32.dll"},{base+L"\\ProgID",L"YoungDraw.Drawing.1"},{base+L"\\Insertable",L""},{base+L"\\Verb\\0",L"편집(&E),0,2"},{base+L"\\Verb\\1",L"열기(&O),0,2"},{L"Software\\Classes\\YoungDraw.Drawing.1",L"YoungDraw Drawing"},{L"Software\\Classes\\YoungDraw.Drawing.1\\CLSID",classKey}}){auto hr=put(item.first,item.second);if(FAILED(hr))return hr;}
    return S_OK;
}
class Object;
Object* active=nullptr;
class Object final:public IOleObject,public IPersistStorage,public IDataObject,public IViewObject2 {
    LONG refs=1;Document document;ComPtr<IStorage> storage;ComPtr<IOleClientSite> site;ComPtr<IOleAdviseHolder> advises;ComPtr<IDataAdviseHolder> dataAdvises;
    ComPtr<IAdviseSink> viewSink;DWORD viewFlags=0;bool modified=false,visible=false;
    Document current()const{return active==this&&visible?editor.current():document;}
    std::vector<FORMATETC> formats()const{return {{sourceFormat(),nullptr,DVASPECT_CONTENT,-1,TYMED_ISTORAGE},{descriptorFormat(),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL},{CF_ENHMETAFILE,nullptr,DVASPECT_CONTENT,-1,TYMED_ENHMF},{nativeFormat(),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL}};}
    HRESULT persist(IStorage* target){
        document=current();auto hr=WriteClassStg(target,classId);if(FAILED(hr))return hr;
        hr=WriteFmtUserTypeStg(target,nativeFormat(),const_cast<LPOLESTR>(L"YoungDraw Drawing"));if(FAILED(hr))return hr;
        ComPtr<IStream> stream;hr=target->CreateStream(L"YoungDrawDocument",STGM_CREATE|STGM_WRITE|STGM_SHARE_EXCLUSIVE,0,0,&stream);if(FAILED(hr))return hr;
        auto bytes=toNative(document);ULONG written=0;hr=stream->Write(bytes.data(),ULONG(bytes.size()),&written);if(FAILED(hr)||written!=bytes.size())return STG_E_WRITEFAULT;stream.Reset();
        // Persist a standard presentation cache so Office can display a closed object.
        ComPtr<IOleCache> cache;hr=CreateDataCache(nullptr,classId,IID_PPV_ARGS(&cache));if(FAILED(hr))return hr;
        ComPtr<IPersistStorage> cacheStorage;hr=cache.As(&cacheStorage);if(FAILED(hr))return hr;
        hr=cacheStorage->InitNew(target);if(FAILED(hr))return hr;
        FORMATETC format{CF_ENHMETAFILE,nullptr,DVASPECT_CONTENT,-1,TYMED_ENHMF};DWORD connection=0;hr=cache->Cache(&format,ADVF_PRIMEFIRST,&connection);if(FAILED(hr))return hr;
        STGMEDIUM medium{};medium.tymed=TYMED_ENHMF;medium.hEnhMetaFile=editor.picture(document);if(!medium.hEnhMetaFile)return E_FAIL;
        hr=cache->SetData(&format,&medium,TRUE);if(FAILED(hr)){ReleaseStgMedium(&medium);return hr;}
        hr=cacheStorage->Save(target,TRUE);if(SUCCEEDED(hr))hr=cacheStorage->SaveCompleted(nullptr);
        // The data cache owns only presentations, not the document identity.
        if(SUCCEEDED(hr))hr=WriteClassStg(target,classId);
        return hr;
    }
public:
    explicit Object(Document d={}):document(std::move(d)){++serverObjects;CreateOleAdviseHolder(&advises);CreateDataAdviseHolder(&dataAdvises);}
    ~Object(){if(active==this)active=nullptr;--serverObjects;idleExit();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid==IID_IUnknown||iid==IID_IOleObject)*out=static_cast<IOleObject*>(this);else if(iid==IID_IPersist||iid==IID_IPersistStorage)*out=static_cast<IPersistStorage*>(this);else if(iid==IID_IDataObject)*out=static_cast<IDataObject*>(this);else if(iid==IID_IViewObject||iid==IID_IViewObject2)*out=static_cast<IViewObject2*>(this);else return E_NOINTERFACE;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ULONG(InterlockedIncrement(&refs));}
    ULONG STDMETHODCALLTYPE Release()override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return ULONG(n);}
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* id)override{if(!id)return E_POINTER;*id=classId;return S_OK;}
    HRESULT STDMETHODCALLTYPE IsDirty()override{return modified||active==this?S_OK:S_FALSE;}
    HRESULT STDMETHODCALLTYPE InitNew(IStorage* s)override{storage=s;modified=true;return S_OK;}
    HRESULT STDMETHODCALLTYPE Load(IStorage* s)override{try{ComPtr<IStream> stream;auto hr=s->OpenStream(L"YoungDrawDocument",nullptr,STGM_READ|STGM_SHARE_EXCLUSIVE,0,&stream);if(FAILED(hr))return hr;STATSTG stat{};hr=stream->Stat(&stat,STATFLAG_NONAME);if(FAILED(hr))return hr;if(stat.cbSize.QuadPart>128*1024*1024)return STG_E_INVALIDHEADER;std::string bytes(size_t(stat.cbSize.QuadPart),'\0');ULONG read=0;hr=stream->Read(bytes.data(),ULONG(bytes.size()),&read);if(FAILED(hr)||read!=bytes.size())return STG_E_READFAULT;document=fromNative(bytes);storage=s;modified=false;return S_OK;}catch(...){return STG_E_INVALIDHEADER;}}
    HRESULT STDMETHODCALLTYPE Save(IStorage* s,BOOL)override{try{return persist(s);}catch(...){return E_FAIL;}}
    HRESULT STDMETHODCALLTYPE SaveCompleted(IStorage* s)override{if(s)storage=s;modified=false;return S_OK;}
    HRESULT STDMETHODCALLTYPE HandsOffStorage()override{storage.Reset();return S_OK;}
    HRESULT STDMETHODCALLTYPE SetClientSite(IOleClientSite* s)override{site=s;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetClientSite(IOleClientSite** s)override{return site.CopyTo(s);}
    HRESULT STDMETHODCALLTYPE SetHostNames(LPCOLESTR,LPCOLESTR)override{return S_OK;}
    HRESULT commit(){if(active!=this)return S_OK;document=editor.current();modified=true;if(dataAdvises)dataAdvises->SendOnDataChange(this,0,0);if(viewSink)viewSink->OnViewChange(DVASPECT_CONTENT,-1);auto hr=site?site->SaveObject():S_OK;if(SUCCEEDED(hr)&&advises)advises->SendOnSave();return hr;}
    HRESULT STDMETHODCALLTYPE Close(DWORD option)override{if(option!=OLECLOSE_NOSAVE&&active==this){auto hr=commit();if(FAILED(hr))return hr;}if(active==this){active=nullptr;visible=false;ShowWindow(editor.window,SW_HIDE);}if(site)site->OnShowWindow(FALSE);if(advises)advises->SendOnClose();return S_OK;}
    HRESULT STDMETHODCALLTYPE SetMoniker(DWORD,IMoniker*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetMoniker(DWORD,DWORD,IMoniker** m)override{if(m)*m=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE InitFromData(IDataObject*,BOOL,DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetClipboardData(DWORD,IDataObject** d)override{if(!d)return E_POINTER;*d=new(std::nothrow) Object(current());return *d?S_OK:E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE DoVerb(LONG verb,LPMSG,IOleClientSite* client,LONG,HWND,LPCRECT)override{if(verb==OLEIVERB_HIDE)return Close(OLECLOSE_SAVEIFDIRTY);if(verb!=OLEIVERB_PRIMARY&&verb!=OLEIVERB_SHOW&&verb!=OLEIVERB_OPEN&&verb!=0&&verb!=1)return OLEOBJ_E_INVALIDVERB;if(active&&active!=this)return E_UNEXPECTED;if(client)site=client;active=this;visible=true;editor.open(document);ShowWindow(editor.window,SW_SHOWNORMAL);SetForegroundWindow(editor.window);if(site){site->ShowObject();site->OnShowWindow(TRUE);}return S_OK;}
    HRESULT STDMETHODCALLTYPE EnumVerbs(IEnumOLEVERB** verbs)override{return OleRegEnumVerbs(classId,verbs);}
    HRESULT STDMETHODCALLTYPE Update()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE IsUpToDate()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE GetUserClassID(CLSID* id)override{return GetClassID(id);}
    HRESULT STDMETHODCALLTYPE GetUserType(DWORD,LPOLESTR* text)override{if(!text)return E_POINTER;constexpr wchar_t name[]=L"YoungDraw Drawing";*text=static_cast<LPOLESTR>(CoTaskMemAlloc(sizeof(name)));if(!*text)return E_OUTOFMEMORY;memcpy(*text,name,sizeof(name));return S_OK;}
    HRESULT STDMETHODCALLTYPE SetExtent(DWORD aspect,SIZEL*)override{return aspect==DVASPECT_CONTENT?S_OK:DV_E_DVASPECT;}
    HRESULT STDMETHODCALLTYPE GetExtent(DWORD aspect,SIZEL* size)override{if(!size)return E_POINTER;if(aspect!=DVASPECT_CONTENT)return DV_E_DVASPECT;*size=extent(current());return S_OK;}
    HRESULT STDMETHODCALLTYPE Advise(IAdviseSink* sink,DWORD* cookie)override{return advises?advises->Advise(sink,cookie):E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE Unadvise(DWORD cookie)override{return advises?advises->Unadvise(cookie):E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE EnumAdvise(IEnumSTATDATA** e)override{return advises?advises->EnumAdvise(e):E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE GetMiscStatus(DWORD,DWORD* flags)override{if(!flags)return E_POINTER;*flags=OLEMISC_CANTLINKINSIDE|OLEMISC_RENDERINGISDEVICEINDEPENDENT;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetColorScheme(LOGPALETTE*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* f)override{if(!f)return E_POINTER;if(f->dwAspect!=DVASPECT_CONTENT)return DV_E_DVASPECT;if(f->lindex!=-1)return DV_E_LINDEX;for(auto& item:formats())if(f->cfFormat==item.cfFormat)return f->tymed&item.tymed?S_OK:DV_E_TYMED;return DV_E_FORMATETC;}
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* f,STGMEDIUM* m)override{if(!m)return E_POINTER;*m={};auto hr=QueryGetData(f);if(FAILED(hr))return hr;try{auto d=current();
        if(f->cfFormat==sourceFormat()){ComPtr<ILockBytes> bytes;hr=CreateILockBytesOnHGlobal(nullptr,TRUE,&bytes);if(FAILED(hr))return hr;ComPtr<IStorage> target;hr=StgCreateDocfileOnILockBytes(bytes.Get(),STGM_CREATE|STGM_READWRITE|STGM_SHARE_EXCLUSIVE,0,&target);if(FAILED(hr))return hr;hr=persist(target.Get());if(FAILED(hr))return hr;hr=target->Commit(STGC_DEFAULT);if(FAILED(hr))return hr;m->tymed=TYMED_ISTORAGE;m->pstg=target.Detach();}
        else if(f->cfFormat==CF_ENHMETAFILE){m->hEnhMetaFile=editor.picture(d);if(!m->hEnhMetaFile)return E_FAIL;m->tymed=TYMED_ENHMF;}
        else if(f->cfFormat==descriptorFormat()){constexpr wchar_t name[]=L"YoungDraw Drawing";std::vector<BYTE> data(sizeof(OBJECTDESCRIPTOR)+sizeof(name));auto p=reinterpret_cast<OBJECTDESCRIPTOR*>(data.data());p->cbSize=DWORD(data.size());p->clsid=classId;p->dwDrawAspect=DVASPECT_CONTENT;p->sizel=extent(d);p->dwFullUserTypeName=sizeof(OBJECTDESCRIPTOR);memcpy(data.data()+sizeof(OBJECTDESCRIPTOR),name,sizeof(name));m->hGlobal=memory(data.data(),data.size());m->tymed=TYMED_HGLOBAL;}
        else{auto data=toNative(d);m->hGlobal=memory(data.c_str(),data.size()+1);m->tymed=TYMED_HGLOBAL;}
        return m->tymed==TYMED_HGLOBAL&&!m->hGlobal?E_OUTOFMEMORY:S_OK;
    }catch(...){return E_FAIL;}}
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC* f,STGMEDIUM* m)override{if(!f||!m)return E_POINTER;if(f->cfFormat!=sourceFormat()||m->tymed!=TYMED_ISTORAGE||!m->pstg)return DV_E_TYMED;return Save(m->pstg,FALSE);}
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*,FORMATETC* out)override{if(out)out->ptd=nullptr;return DATA_S_SAMEFORMATETC;}
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*,STGMEDIUM*,BOOL)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction,IEnumFORMATETC** out)override{if(direction!=DATADIR_GET)return E_NOTIMPL;auto f=formats();return SHCreateStdEnumFmtEtc(UINT(f.size()),f.data(),out);}
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC* f,DWORD flags,IAdviseSink* sink,DWORD* cookie)override{return dataAdvises?dataAdvises->Advise(this,f,flags,sink,cookie):E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD cookie)override{return dataAdvises?dataAdvises->Unadvise(cookie):E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** e)override{return dataAdvises?dataAdvises->EnumAdvise(e):E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE Draw(DWORD aspect,LONG,void*,DVTARGETDEVICE*,HDC,HDC dc,LPCRECTL bounds,LPCRECTL,BOOL(CALLBACK*)(ULONG_PTR),ULONG_PTR)override{if(aspect!=DVASPECT_CONTENT)return DV_E_DVASPECT;if(!bounds)return E_POINTER;auto emf=editor.picture(current());if(!emf)return E_FAIL;RECT rect{bounds->left,bounds->top,bounds->right,bounds->bottom};BOOL ok=PlayEnhMetaFile(dc,emf,&rect);DeleteEnhMetaFile(emf);return ok?S_OK:E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetColorSet(DWORD,LONG,void*,DVTARGETDEVICE*,HDC,LOGPALETTE**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Freeze(DWORD,LONG,void*,DWORD*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Unfreeze(DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetAdvise(DWORD aspect,DWORD flags,IAdviseSink* sink)override{if(aspect!=DVASPECT_CONTENT)return DV_E_DVASPECT;viewSink=sink;viewFlags=flags;if(sink&&(flags&ADVF_PRIMEFIRST))sink->OnViewChange(aspect,-1);return S_OK;}
    HRESULT STDMETHODCALLTYPE GetAdvise(DWORD* aspect,DWORD* flags,IAdviseSink** sink)override{if(aspect)*aspect=DVASPECT_CONTENT;if(flags)*flags=viewFlags;return sink?viewSink.CopyTo(sink):S_OK;}
    HRESULT STDMETHODCALLTYPE GetExtent(DWORD aspect,LONG,DVTARGETDEVICE*,LPSIZEL size)override{return GetExtent(aspect,size);}
};
class Factory final:public IClassFactory {
    LONG refs=1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown&&iid!=IID_IClassFactory)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ULONG(InterlockedIncrement(&refs));}
    ULONG STDMETHODCALLTYPE Release()override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return ULONG(n);}
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** out)override{if(outer)return CLASS_E_NOAGGREGATION;auto object=new(std::nothrow) Object;if(!object)return E_OUTOFMEMORY;auto hr=object->QueryInterface(iid,out);object->Release();return hr;}
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock)override{serverLocks+=lock?1:-1;idleExit();return S_OK;}
};
HRESULT start(Editor value,bool embedding,bool registerClass){editor=std::move(value);embeddingServer=embedding;auto hr=registerClass?registerServer():S_OK;if(FAILED(hr))return hr;if(embedding){auto factory=new Factory;hr=CoRegisterClassObject(classId,factory,CLSCTX_LOCAL_SERVER,REGCLS_SINGLEUSE,&registration);factory->Release();}return hr;}
void stop(){OleFlushClipboard();if(registration)CoRevokeClassObject(registration);registration=0;}
HRESULT exportEmbedded(const Document& document,const wchar_t* storagePath,const wchar_t* imagePath){
    ComPtr<IDataObject> data;
    data.Attach(new Object(document));
    FORMATETC format{sourceFormat(),nullptr,DVASPECT_CONTENT,-1,TYMED_ISTORAGE};
    STGMEDIUM medium{};
    auto hr=data->GetData(&format,&medium);
    if(FAILED(hr))return hr;
    ComPtr<IStorage> target;
    hr=StgCreateDocfile(storagePath,STGM_CREATE|STGM_WRITE|STGM_SHARE_EXCLUSIVE,0,&target);
    if(SUCCEEDED(hr))hr=medium.pstg->CopyTo(0,nullptr,nullptr,target.Get());
    if(SUCCEEDED(hr))hr=WriteClassStg(target.Get(),classId);
    if(SUCCEEDED(hr))hr=target->Commit(STGC_DEFAULT);
    ReleaseStgMedium(&medium);
    if(FAILED(hr))return hr;
    auto image=editor.picture(document);
    if(!image)return E_FAIL;
    auto copy=CopyEnhMetaFileW(image,imagePath);
    DeleteEnhMetaFile(image);
    if(!copy)return E_FAIL;
    DeleteEnhMetaFile(copy);
    return S_OK;
}
HRESULT copy(const Document& d){auto object=new(std::nothrow) Object(d);if(!object)return E_OUTOFMEMORY;auto hr=OleSetClipboard(object);object->Release();return hr;}
bool editing(){return active!=nullptr;}
HRESULT update(){return active?active->commit():S_FALSE;}
void close(){if(active)active->Close(OLECLOSE_NOSAVE);}
HRESULT selfTest(){
    // Exercise the exact data offered to Office, including persisted presentation.
    auto d=coordinateDemo();Object* object=new Object(d);ComPtr<IDataObject> data;data.Attach(object);FORMATETC f{sourceFormat(),nullptr,DVASPECT_CONTENT,-1,TYMED_ISTORAGE};STGMEDIUM m{};auto hr=data->GetData(&f,&m);if(FAILED(hr))return hr;
    CLSID persisted{};hr=ReadClassStg(m.pstg,&persisted);if(FAILED(hr)||persisted!=classId){ReleaseStgMedium(&m);return E_UNEXPECTED;}
    Object* restored=new Object;ComPtr<IPersistStorage> saved;saved.Attach(restored);hr=saved->Load(m.pstg);if(SUCCEEDED(hr)){ComPtr<IOleCache> cache;hr=CreateDataCache(nullptr,classId,IID_PPV_ARGS(&cache));if(SUCCEEDED(hr)){ComPtr<IPersistStorage> p;hr=cache.As(&p);if(SUCCEEDED(hr))hr=p->Load(m.pstg);ComPtr<IDataObject> picture;if(SUCCEEDED(hr))hr=cache.As(&picture);FORMATETC emf{CF_ENHMETAFILE,nullptr,DVASPECT_CONTENT,-1,TYMED_ENHMF};STGMEDIUM image{};if(SUCCEEDED(hr))hr=picture->GetData(&emf,&image);if(SUCCEEDED(hr))ReleaseStgMedium(&image);}}
    ReleaseStgMedium(&m);if(FAILED(hr))return hr;
    f={nativeFormat(),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};hr=restored->GetData(&f,&m);if(FAILED(hr))return hr;auto p=static_cast<const char*>(GlobalLock(m.hGlobal));bool same=p&&toNative(d)==p;if(p)GlobalUnlock(m.hGlobal);ReleaseStgMedium(&m);return same?S_OK:E_FAIL;
}
}

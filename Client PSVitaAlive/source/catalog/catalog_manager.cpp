#include "catalog/catalog_manager.hpp"

#include "catalog/catalog_parser.hpp"
#include "diagnostic_logger.hpp"
#include "network/http_client.hpp"
#include "storage/storage_manager.hpp"
#include "update/update_checker.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <cstring>
#include <string>
#include <utility>

namespace psvitaalive {
namespace {
constexpr const char* RAW_BASE = "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/";
constexpr const char* CACHE_DIR = "ux0:data/psvitaalive/cache/catalog";
constexpr int WORKER_PRIORITY = 0x10000100;
constexpr int WORKER_STACK = 64 * 1024;

bool fileExists(const std::string& path) { SceIoStat stat={}; return sceIoGetstat(path.c_str(),&stat)>=0&&stat.st_size>0; }
bool readTextFile(const std::string& path,std::string& out){out.clear();SceIoStat stat={};if(sceIoGetstat(path.c_str(),&stat)<0||stat.st_size<=0||stat.st_size>4096)return false;SceUID fd=sceIoOpen(path.c_str(),SCE_O_RDONLY,0);if(fd<0)return false;out.resize((size_t)stat.st_size);const int read=sceIoRead(fd,&out[0],(unsigned int)out.size());sceIoClose(fd);if(read<=0){out.clear();return false;}out.resize((size_t)read);return true;}
bool writeTextFile(const std::string& path,const std::string& text){SceUID fd=sceIoOpen(path.c_str(),SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);if(fd<0)return false;size_t written=0;while(written<text.size()){const int result=sceIoWrite(fd,text.data()+written,(unsigned int)(text.size()-written));if(result<=0){sceIoClose(fd);return false;}written+=(size_t)result;}sceIoClose(fd);return true;}
void parseValidators(const std::string& text,std::string& etag,std::string& modified){etag.clear();modified.clear();const size_t e=text.find("etag=");if(e!=std::string::npos){const size_t end=text.find('\n',e);etag=text.substr(e+5,end==std::string::npos?std::string::npos:end-(e+5));}const size_t m=text.find("last_modified=");if(m!=std::string::npos){const size_t end=text.find('\n',m);modified=text.substr(m+14,end==std::string::npos?std::string::npos:end-(m+14));}}
bool validatorsMatch(const std::string& oldEtag,const std::string& oldModified,const std::string& newEtag,const std::string& newModified){const bool remoteHas=!newEtag.empty()||!newModified.empty();const bool storedHas=!oldEtag.empty()||!oldModified.empty();if(!remoteHas||!storedHas)return false;if(!newEtag.empty()&&oldEtag!=newEtag)return false;if(!newModified.empty()&&oldModified!=newModified)return false;return true;}
}

CatalogManager::CatalogManager()=default;
CatalogManager::~CatalogManager(){shutdown();}
bool CatalogManager::init(){
    if(workerThread_>=0)return true;
    StorageManager storage;
    if(!storage.createDirectories(CACHE_DIR)){diagnostics::log("[CatalogManager] cannot create cache directory");return false;}
    mutex_=sceKernelCreateMutex("PSVitaAliveCatalog",0,0,nullptr);
    if(mutex_<0)return false;
    stopping_=false;

    // IMPORTANT: perform the application update phase synchronously before
    // starting the CatalogManager worker. ImageCache is initialized later by
    // main(), so no catalog/image HTTP worker can overlap this phase.
    updateChecked_ = true;
    diagnostics::log("[CatalogManager] startup update check begins before worker startup");
    const UpdateChecker::Result update = UpdateChecker::checkLatest(PSVITAALIVE_VERSION);
    if(update.state == UpdateChecker::State::UpdateAvailable){
        diagnostics::log("[CatalogManager] startup update available remote=" + update.remoteVersion + " asset=" + update.assetName);
        const bool ok = UpdateChecker::applyUpdate(
            update,
            [](const UpdateChecker::ApplyProgress& p){
                if(p.stage == UpdateChecker::ApplyStage::Downloading){
                    diagnostics::log("[CatalogManager] startup update download=" + std::to_string(p.current) + "/" + std::to_string(p.total));
                }else if(p.stage == UpdateChecker::ApplyStage::Extracting){
                    diagnostics::log("[CatalogManager] startup update install=" + std::to_string(p.current) + "/" + std::to_string(p.total));
                }else if(!p.message.empty()){
                    diagnostics::log(std::string("[CatalogManager] startup update: ") + p.message);
                }
            },
            nullptr
        );
        if(ok){
            diagnostics::log("[CatalogManager] startup update installed successfully; press START to exit");
            sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
            while(!stopping_){
                SceCtrlData pad{};
                sceCtrlPeekBufferPositive(0,&pad,1);
                if(pad.buttons & SCE_CTRL_START){
                    diagnostics::log("[CatalogManager] START pressed after startup update; exiting");
                    sceKernelExitProcess(0);
                }
                sceKernelDelayThread(50 * 1000);
            }
            return false;
        }
        diagnostics::log("[CatalogManager] startup update apply failed; continuing with current version");
    }else if(update.state == UpdateChecker::State::UpToDate){
        diagnostics::log("[CatalogManager] startup update check complete local=" + update.localVersion + " remote=" + update.remoteVersion);
    }else{
        diagnostics::log(std::string("[CatalogManager] startup update check unavailable: ") + update.error);
    }

    workerThread_=sceKernelCreateThread("PSVitaAliveCatalogWorker",&CatalogManager::workerEntry,WORKER_PRIORITY,WORKER_STACK,0,0,nullptr);
    if(workerThread_<0){sceKernelDeleteMutex(mutex_);mutex_=-1;return false;}
    CatalogManager*self=this;
    const int result=sceKernelStartThread(workerThread_,sizeof(self),&self);
    if(result<0){sceKernelDeleteThread(workerThread_);workerThread_=-1;sceKernelDeleteMutex(mutex_);mutex_=-1;return false;}
    diagnostics::log("[CatalogManager] initialized");
    return true;
}
void CatalogManager::shutdown(){stopping_=true;if(workerThread_>=0){sceKernelWaitThreadEnd(workerThread_,nullptr,nullptr);sceKernelDeleteThread(workerThread_);workerThread_=-1;}if(mutex_>=0){sceKernelDeleteMutex(mutex_);mutex_=-1;}diagnostics::log("[CatalogManager] shutdown");}
const char* CatalogManager::fileName(ui::CatalogType catalog)const{switch(catalog){case ui::CatalogType::VitaGames:return"catalog_psvita_games.json";case ui::CatalogType::PspGames:return"catalog_psp_games.json";case ui::CatalogType::Ps1Games:return"catalog_ps1_games.json";default:return"catalog.json";}}
const char* CatalogManager::label(ui::CatalogType catalog)const{return ui::catalogName(catalog);}
std::string CatalogManager::cachePath(ui::CatalogType catalog)const{return std::string(CACHE_DIR)+"/"+fileName(catalog);}
std::string CatalogManager::metadataPath(ui::CatalogType catalog)const{return cachePath(catalog)+".meta";}
void CatalogManager::setStatus(State state,ui::CatalogType catalog,const char* message,const char* error){sceKernelLockMutex(mutex_,1,nullptr);status_.state=state;status_.catalog=catalog;status_.label=label(catalog);status_.message=message?message:"";status_.error=error?error:"";sceKernelUnlockMutex(mutex_,1);}

bool CatalogManager::isBusy() const {
    if (mutex_ < 0) return false;
    sceKernelLockMutex(mutex_, 1, nullptr);
    const bool busy = (status_.state == State::Loading) || requestPending_;
    sceKernelUnlockMutex(mutex_, 1);
    return busy;
}

bool CatalogManager::request(ui::CatalogType catalog) {
    if (mutex_ < 0) return false;
    sceKernelLockMutex(mutex_, 1, nullptr);

    if (status_.state == State::Loading && requestedCatalog_ == catalog) {
        sceKernelUnlockMutex(mutex_, 1);
        return true;
    }

    // Mid-load retarget: keep Loading, only change what the worker should finish next.
    if (status_.state == State::Loading) {
        ++requestGeneration_;
        requestedCatalog_ = catalog;
        requestPending_ = true;
        status_.catalog = catalog;
        status_.label = label(catalog);
        status_.message = "Switching catalog...";
        status_.error.clear();
        sceKernelUnlockMutex(mutex_, 1);
        diagnostics::log(std::string("[CatalogManager] supersede in-flight load -> ") + label(catalog));
        return true;
    }

    const int idx = static_cast<int>(catalog);
    if (idx >= 0 && idx < static_cast<int>(ui::CatalogType::Count) &&
        cachedValid_[idx] && !cachedItems_[idx].empty()) {
        readyItems_ = cachedItems_[idx];
        readyCatalog_ = catalog;
        readyPending_ = true;
        status_.state = State::Ready;
        status_.catalog = catalog;
        status_.current = 1;
        status_.total = 1;
        status_.label = label(catalog);
        status_.message = "Ready (memory cache)";
        status_.error.clear();
        requestedCatalog_ = catalog;
        requestPending_ = false;
        sceKernelUnlockMutex(mutex_, 1);
        diagnostics::log(std::string("[CatalogManager] memory cache hit for ") + label(catalog));
        return true;
    }

    ++requestGeneration_;
    status_.state = State::Loading;
    status_.catalog = catalog;
    status_.current = 0;
    status_.total = 0;
    status_.label = label(catalog);
    status_.message = "Checking catalog cache...";
    status_.error.clear();
    requestPending_ = true;
    requestedCatalog_ = catalog;
    sceKernelUnlockMutex(mutex_, 1);
    return true;
}

CatalogManager::Status CatalogManager::status()const{Status result;if(mutex_<0)return result;sceKernelLockMutex(mutex_,1,nullptr);result=status_;sceKernelUnlockMutex(mutex_,1);return result;}
bool CatalogManager::takeReady(std::vector<ui::CatalogItem>&outItems,ui::CatalogType&catalog){if(mutex_<0)return false;sceKernelLockMutex(mutex_,1,nullptr);if(!readyPending_){sceKernelUnlockMutex(mutex_,1);return false;}outItems=std::move(readyItems_);catalog=readyCatalog_;readyPending_=false;sceKernelUnlockMutex(mutex_,1);return true;}

bool CatalogManager::loadCatalog(ui::CatalogType catalog,std::vector<ui::CatalogItem>&outItems){
    // Startup update work is completed in init() before this worker exists.
    // Keep this guard for process lifetime compatibility; it must remain true.
    if (!updateChecked_) {
        updateChecked_ = true;
        diagnostics::log("[CatalogManager] unexpected update gate fallback; catalog worker should already have completed update phase");
    }

    const int idx=(int)catalog;const std::string path=cachePath(catalog),meta=metadataPath(catalog),temp=path+".new",url=std::string(RAW_BASE)+fileName(catalog);const bool haveCache=fileExists(path);HttpClient http;if(http.init()!=HttpResult::Ok)return false;
    if(haveCache){std::vector<ui::CatalogItem>cached;if(CatalogParser::parseFile(path,cached)&&!cached.empty()){std::string storedText,storedEtag,storedModified;parseValidators(readTextFile(meta,storedText)?storedText:std::string(),storedEtag,storedModified);std::string remoteEtag,remoteModified;setStatus(State::Loading,catalog,"Checking remote catalog...");const HttpResult validatorResult=http.fetchRemoteValidators(url,remoteEtag,remoteModified);if(validatorResult==HttpResult::Ok&&validatorsMatch(storedEtag,storedModified,remoteEtag,remoteModified)){outItems=std::move(cached);sceKernelLockMutex(mutex_,1,nullptr);cachedItems_[idx]=outItems;cachedValid_[idx]=true;sceKernelUnlockMutex(mutex_,1);setStatus(State::Ready,catalog,"Using cached catalog");diagnostics::log(std::string("[CatalogManager] cache valid: ")+label(catalog));http.shutdown();return true;}if(validatorResult!=HttpResult::Ok&&!storedEtag.empty()){outItems=std::move(cached);sceKernelLockMutex(mutex_,1,nullptr);cachedItems_[idx]=outItems;cachedValid_[idx]=true;sceKernelUnlockMutex(mutex_,1);setStatus(State::Ready,catalog,"Using cached catalog (offline)");diagnostics::log(std::string("[CatalogManager] offline cache: ")+label(catalog));http.shutdown();return true;}}}
    setStatus(State::Loading,catalog,"Downloading catalog...");diagnostics::log(std::string("[CatalogManager] downloading: ")+label(catalog));const HttpResult result=http.downloadToFile(url,temp,0,[this,catalog](const HttpProgress&progress){sceKernelLockMutex(mutex_,1,nullptr);status_.current=progress.downloaded;status_.total=progress.total;status_.message="Downloading catalog...";sceKernelUnlockMutex(mutex_,1);(void)catalog;});
    if(result==HttpResult::Ok){std::vector<ui::CatalogItem>fresh;if(CatalogParser::parseFile(temp,fresh)&&!fresh.empty()){sceIoRemove(path.c_str());if(sceIoRename(temp.c_str(),path.c_str())>=0){std::string etag,modified;http.fetchRemoteValidators(url,etag,modified);std::string metadata="etag="+etag+"\nlast_modified="+modified+"\n";writeTextFile(meta,metadata);outItems=std::move(fresh);sceKernelLockMutex(mutex_,1,nullptr);cachedItems_[idx]=outItems;cachedValid_[idx]=true;sceKernelUnlockMutex(mutex_,1);diagnostics::log(std::string("[CatalogManager] downloaded and cached: ")+label(catalog));http.shutdown();return true;}}}
    sceIoRemove(temp.c_str());if(haveCache&&CatalogParser::parseFile(path,outItems)&&!outItems.empty()){sceKernelLockMutex(mutex_,1,nullptr);cachedItems_[idx]=outItems;cachedValid_[idx]=true;sceKernelUnlockMutex(mutex_,1);diagnostics::log(std::string("[CatalogManager] refresh failed; retained valid cache: ")+label(catalog));http.shutdown();return true;}diagnostics::log(std::string("[CatalogManager] catalog unavailable: ")+label(catalog)+" error="+http.lastError());http.shutdown();return false;}

int CatalogManager::workerEntry(SceSize args,void*argp){(void)args;CatalogManager*self=nullptr;if(argp)std::memcpy(&self,argp,sizeof(self));return self?self->workerMain():-1;}
int CatalogManager::workerMain() {
    while (!stopping_) {
        ui::CatalogType catalog = ui::CatalogType::Homebrew;
        bool haveRequest = false;
        unsigned gen = 0;

        sceKernelLockMutex(mutex_, 1, nullptr);
        if (requestPending_) {
            catalog = requestedCatalog_;
            gen = requestGeneration_;
            loadingGeneration_ = gen;
            requestPending_ = false;
            haveRequest = true;
            status_.state = State::Loading;
            status_.catalog = catalog;
            status_.label = label(catalog);
            status_.message = "Checking catalog cache...";
            status_.error.clear();
        }
        sceKernelUnlockMutex(mutex_, 1);

        if (!haveRequest) {
            sceKernelDelayThread(50 * 1000);
            continue;
        }

        std::vector<ui::CatalogItem> loaded;
        const bool ok = loadCatalog(catalog, loaded) && !loaded.empty();

        sceKernelLockMutex(mutex_, 1, nullptr);
        if (requestPending_ || catalog != requestedCatalog_ || gen != requestGeneration_) {
            diagnostics::log(std::string("[CatalogManager] discard stale result for ") + label(catalog));
            status_.state = State::Loading;
            status_.catalog = requestedCatalog_;
            status_.label = label(requestedCatalog_);
            status_.message = "Switching catalog...";
            sceKernelUnlockMutex(mutex_, 1);
            continue;
        }

        if (ok) {
            readyItems_ = std::move(loaded);
            readyCatalog_ = catalog;
            readyPending_ = true;
            status_.state = State::Ready;
            status_.catalog = catalog;
            status_.current = status_.total > 0 ? status_.total : 1;
            status_.message = "Catalog ready";
            status_.error.clear();
        } else {
            status_.state = State::Failed;
            status_.catalog = catalog;
            status_.message = "Unable to load catalog";
            status_.error = "Remote catalog and cache unavailable";
        }
        sceKernelUnlockMutex(mutex_, 1);
    }
    return 0;
}

} // namespace psvitaalive

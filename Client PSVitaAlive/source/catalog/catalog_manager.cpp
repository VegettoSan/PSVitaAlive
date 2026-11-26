#include "catalog/catalog_manager.hpp"
#include "catalog/catalog_memory_policy.hpp"
#include "storage/saved_zip_metadata.hpp"

#include "catalog/catalog_parser.hpp"
#include "catalog/catalog_trace.hpp"
#include "diagnostic_logger.hpp"
#include "localization/localization.hpp"
#include "network/http_client.hpp"
#include "storage/storage_manager.hpp"

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
constexpr int WORKER_STACK = 512 * 1024; // large JSON parse needs headroom on real Vita

bool fileExists(const std::string& path) { SceIoStat stat={}; return sceIoGetstat(path.c_str(),&stat)>=0&&stat.st_size>0; }
bool readTextFile(const std::string& path,std::string& out){out.clear();SceIoStat stat={};if(sceIoGetstat(path.c_str(),&stat)<0||stat.st_size<=0||stat.st_size>4096)return false;SceUID fd=sceIoOpen(path.c_str(),SCE_O_RDONLY,0);if(fd<0)return false;out.resize((size_t)stat.st_size);const int read=sceIoRead(fd,&out[0],(unsigned int)out.size());sceIoClose(fd);if(read<=0){out.clear();return false;}out.resize((size_t)read);return true;}
bool writeTextFile(const std::string& path,const std::string& text){SceUID fd=sceIoOpen(path.c_str(),SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);if(fd<0)return false;size_t written=0;while(written<text.size()){const int result=sceIoWrite(fd,text.data()+written,(unsigned int)(text.size()-written));if(result<=0){sceIoClose(fd);return false;}written+=(size_t)result;}sceIoClose(fd);return true;}
void parseValidators(const std::string& text,std::string& etag,std::string& modified){etag.clear();modified.clear();const size_t e=text.find("etag=");if(e!=std::string::npos){const size_t end=text.find('\n',e);etag=text.substr(e+5,end==std::string::npos?std::string::npos:end-(e+5));}const size_t m=text.find("last_modified=");if(m!=std::string::npos){const size_t end=text.find('\n',m);modified=text.substr(m+14,end==std::string::npos?std::string::npos:end-(m+14));}}
bool validatorsMatch(const std::string& oldEtag,const std::string& oldModified,const std::string& newEtag,const std::string& newModified){const bool remoteHas=!newEtag.empty()||!newModified.empty();const bool storedHas=!oldEtag.empty()||!oldModified.empty();if(!remoteHas||!storedHas)return false;if(!newEtag.empty()&&oldEtag!=newEtag)return false;if(!newModified.empty()&&oldModified!=newModified)return false;return true;}
// Replace only a validated payload. If the filesystem cannot replace in
// place, retain the previous file until the new rename has succeeded.
bool promoteCatalogCache(const std::string& temp, const std::string& path) {
    SceIoStat st{};
    const bool exists = sceIoGetstat(path.c_str(), &st) >= 0;
    // Never rename a directory masquerading as a catalog file.
    if (exists && (st.st_mode & SCE_S_IFDIR) != 0) return false;
    if (sceIoRename(temp.c_str(), path.c_str()) >= 0) return true;
    if (!exists) return false;
    const std::string backup = path + ".previous";
    // Never overwrite an existing recovery copy.
    if (sceIoGetstat(backup.c_str(), &st) >= 0) return false;
    if (sceIoRename(path.c_str(), backup.c_str()) < 0) return false;
    if (sceIoRename(temp.c_str(), path.c_str()) >= 0) {
        sceIoRemove(backup.c_str());
        return true;
    }
    if (sceIoRename(backup.c_str(), path.c_str()) < 0)
        diagnostics::log("[CatalogManager] cache restore failed; previous file retained");
    return false;
}
}
// Compare identical remote payload without allocating a second JSON tree.
bool filesIdentical(const std::string& left,const std::string& right){
    SceIoStat a{},b{};
    if(sceIoGetstat(left.c_str(),&a)<0||sceIoGetstat(right.c_str(),&b)<0||
       a.st_size<=0||a.st_size!=b.st_size)return false;
    const SceUID fa=sceIoOpen(left.c_str(),SCE_O_RDONLY,0);
    if(fa<0)return false;
    const SceUID fb=sceIoOpen(right.c_str(),SCE_O_RDONLY,0);
    if(fb<0){sceIoClose(fa);return false;}
    unsigned char aa[4096],bb[4096];bool same=true;
    for(;;){
        const int n=sceIoRead(fa,aa,sizeof(aa));
        if(n<0){same=false;break;}
        if(n==0)break;
        const int m=sceIoRead(fb,bb,(unsigned)n);
        if(m!=n||std::memcmp(aa,bb,(size_t)n)!=0){same=false;break;}
    }
    sceIoClose(fb);sceIoClose(fa);return same;
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
    // Soft refresh after cache-first show must not hard-lock L/R tab switches.
    const bool busy = requestPending_ ||
        (status_.state == State::Loading && !status_.softRefresh);
    sceKernelUnlockMutex(mutex_, 1);
    return busy;
}

bool CatalogManager::setInstallSessionGate(bool blocked) {
    if (mutex_ < 0 || sceKernelLockMutex(mutex_, 1, nullptr) < 0) return false;
    installSessionGate_ = blocked;
    sceKernelUnlockMutex(mutex_, 1);
    return true;
}

bool CatalogManager::installSessionQuiescent() const {
    if (mutex_ < 0 || sceKernelLockMutex(mutex_, 1, nullptr) < 0) return false;
    // Ready can be published before the worker finishes its soft refresh or
    // frees its parse vectors. A pending publication must be consumed too.
    const bool ready = installSessionGate_ && !requestPending_ &&
        !workerOperationActive_ && !readyPending_;
    sceKernelUnlockMutex(mutex_, 1);
    return ready;
}

bool CatalogManager::actionBlockedByCatalog() const {
    if (mutex_ < 0 || sceKernelLockMutex(mutex_, 1, nullptr) < 0) return true;
    const bool blocked = requestPending_ || workerOperationActive_ || readyPending_;
    sceKernelUnlockMutex(mutex_, 1);
    return blocked;
}

bool CatalogManager::request(ui::CatalogType catalog, bool forceNetwork,
                             ui::CatalogType outgoingCatalog,
                             std::vector<ui::CatalogItem>* outgoingItems) {
    if (mutex_ < 0) return false;
    sceKernelLockMutex(mutex_, 1, nullptr);

    if (installSessionGate_) {
        sceKernelUnlockMutex(mutex_, 1);
        return false;
    }
    // A tab request and the start of a real refresh share this mutex.
    // Reject before touching either the outgoing UI vector or cached data.
    if (outgoingItems && catalog != outgoingCatalog &&
        catalog_memory::catalogSwitchBlocked(
            status_.refreshPhase == RefreshPhase::Downloading ||
            status_.refreshPhase == RefreshPhase::Applying,
            status_.refreshPhase == RefreshPhase::Updated,
            workerOperationActive_, readyPending_)) {
        sceKernelUnlockMutex(mutex_, 1);
        return false;
    }
    if (forceNetwork && readyPending_ && readyCatalog_ == catalog && !readyItems_.empty()) {
        // A valid publication is waiting for the main thread.
        sceKernelUnlockMutex(mutex_, 1);
        return true;
    }
    if (!forceNetwork && status_.state == State::Loading && requestedCatalog_ == catalog) {
        sceKernelUnlockMutex(mutex_, 1);
        return true;
    }

    // Normal navigation retains only the selected and immediately previous
    // catalogs. During an actual transfer/apply, never restore inactive data.
    const bool updating = workerOperationActive_ &&
        (status_.refreshPhase == RefreshPhase::Downloading ||
         status_.refreshPhase == RefreshPhase::Applying);
    const auto previous = outgoingItems ? outgoingCatalog : requestedCatalog_;
    const size_t released = catalog_memory::retainForSelection(
        cachedItems_, cachedValid_, static_cast<size_t>(catalog),
        static_cast<size_t>(previous), outgoingItems, updating);
    if (released)
        diagnostics::log("[CatalogManager] navigation RAM eviction: entries=" +
                         std::to_string(released));

    // Mid-load retarget: keep Loading, only change what the worker should finish next.
    if (status_.state == State::Loading) {
        ++requestGeneration_;
        requestedCatalog_ = catalog;
        requestPending_ = true;
        forceNetworkPending_ = forceNetwork;
        status_.catalog = catalog;
        status_.label = label(catalog);
        status_.message = "Switching catalog — please wait...";
        status_.error.clear();
        status_.softRefresh = false;
        status_.refreshPhase = RefreshPhase::None;
        sceKernelUnlockMutex(mutex_, 1);
        diagnostics::log(std::string("[CatalogManager] supersede in-flight load -> ") + label(catalog));
        return true;
    }

    const int idx = static_cast<int>(catalog);
    if (!forceNetwork && idx >= 0 && idx < static_cast<int>(ui::CatalogType::Count) &&
        cachedValid_[idx] && !cachedItems_[idx].empty()) {
        ++requestGeneration_; // invalidate any older background validator
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
        status_.softRefresh = false;
        status_.refreshPhase = RefreshPhase::None;
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
    status_.message = ::psvitaalive::L(::psvitaalive::TextId::LoadingCatalogLocalCache);
    status_.error.clear();
    status_.refreshPhase = RefreshPhase::None;
    requestPending_ = true;
    forceNetworkPending_ = forceNetwork;
    requestedCatalog_ = catalog;
    sceKernelUnlockMutex(mutex_, 1);
    return true;
}

bool CatalogManager::savedZipAppDisplay(const std::string& appId, const std::string& titleId,
                                       std::string& name, std::string& icon) const {
    if (mutex_ < 0 || (appId.empty() && titleId.empty())) return false;
    if (sceKernelLockMutex(mutex_, 1, nullptr) < 0) return false;
    const ui::CatalogItem* found = nullptr;
    bool ambiguous = false;
    for (int i = 0; i < static_cast<int>(ui::CatalogType::Count) && !ambiguous; ++i) {
        if (!cachedValid_[i]) continue;
        for (const auto& app : cachedItems_[i]) {
            if (!storage::savedZipMatchesApp(appId, titleId, app.id, app.titleId)) continue;
            if (found) { ambiguous = true; break; }
            found = &app;
        }
    }
    const bool matched = found && !ambiguous;
    if (matched) {
        name = found->name;
        icon = found->icon.empty() ? found->cover : found->icon;
    }
    sceKernelUnlockMutex(mutex_, 1);
    return matched;
}

CatalogManager::Status CatalogManager::status()const{Status result;if(mutex_<0)return result;sceKernelLockMutex(mutex_,1,nullptr);result=status_;sceKernelUnlockMutex(mutex_,1);return result;}
bool CatalogManager::takeReady(std::vector<ui::CatalogItem>&outItems,ui::CatalogType&catalog){
    if(mutex_<0)return false;
    const uint64_t t=catalog_trace::nowUs();
    sceKernelLockMutex(mutex_,1,nullptr);
    if(!readyPending_){sceKernelUnlockMutex(mutex_,1);return false;}
    outItems=std::move(readyItems_);
    catalog=readyCatalog_;
    const bool soft=status_.softRefresh;
    readyPending_=false;
    sceKernelUnlockMutex(mutex_,1);
    PSVA_TRACE_DURATION("ready_consumed",t,std::string("catalog=")+label(catalog)+
        " items="+std::to_string(outItems.size())+" soft="+std::to_string(soft?1:0));
    return true;
}

void ensureZrifIndexDownloaded(ui::CatalogType catalog, HttpClient& http) {
    if (catalog != ui::CatalogType::VitaGames) return;
    const std::string idxName = "catalog_psvita_games.zrifidx";
    const std::string local = std::string(CACHE_DIR) + "/" + idxName;
    const std::string remote = std::string(RAW_BASE) + idxName;

    // Detect legacy URL-keyed index (larger / starts with "http") and refresh.
    bool needDownload = true;
    SceIoStat st{};
    if (sceIoGetstat(local.c_str(), &st) >= 0 && st.st_size > 1024) {
        bool legacy = st.st_size > 2500000; // old URL-keyed file was ~3.5MB
        if (!legacy) {
            const SceUID fd = sceIoOpen(local.c_str(), SCE_O_RDONLY, 0);
            if (fd >= 0) {
                char head[8] = {};
                sceIoRead(fd, head, 7);
                sceIoClose(fd);
                if (std::strncmp(head, "http://", 7) == 0 || std::strncmp(head, "https:/", 7) == 0) {
                    legacy = true;
                }
            }
        }
        if (!legacy) {
            diagnostics::log("[CatalogManager] zRIF index present on disk (content_id keys)");
            needDownload = false;
        } else {
            diagnostics::log("[CatalogManager] legacy zRIF index detected — re-downloading content_id index");
            sceIoRemove(local.c_str());
        }
    }
    if (!needDownload) return;

    diagnostics::log("[CatalogManager] downloading zRIF index...");
    const std::string temp = local + ".new";
    const HttpResult r = http.downloadToFile(remote, temp, 0, nullptr, nullptr);
    if (r == HttpResult::Ok) {
        sceIoRemove(local.c_str());
        sceIoRename(temp.c_str(), local.c_str());
        diagnostics::log("[CatalogManager] zRIF index downloaded (content_id keys)");
    } else {
        sceIoRemove(temp.c_str());
        diagnostics::log(std::string("[CatalogManager] zRIF index download failed: ") + http.lastError());
    }
}


// ---------------------------------------------------------------------------
// Feature flags — set to 0 to restore previous blocking load behaviour.
// ---------------------------------------------------------------------------
#ifndef PSVITAALIVE_CACHE_FIRST_REFRESH
#define PSVITAALIVE_CACHE_FIRST_REFRESH 1
#endif

bool CatalogManager::tryLoadDiskCache(ui::CatalogType catalog, std::vector<ui::CatalogItem>& outItems) {
    outItems.clear();
    const std::string path = cachePath(catalog);
    // A power loss between the two rename operations can leave the
    // previous file alone. Restore it before validating the disk cache.
    SceIoStat original{}, previous{};
    const std::string backup = path + ".previous";
    if (sceIoGetstat(path.c_str(), &original) < 0 &&
        sceIoGetstat(backup.c_str(), &previous) >= 0 &&
        (previous.st_mode & SCE_S_IFDIR) == 0 &&
        sceIoRename(backup.c_str(), path.c_str()) >= 0) {
        sceIoRemove(metadataPath(catalog).c_str());
        diagnostics::log("[CatalogManager] restored previous catalog after interrupted cache replacement");
    }
    const uint64_t t=catalog_trace::nowUs();
    SceIoStat stat{};
    if(!fileExists(path)){PSVA_TRACE_EVENT("disk_miss",label(catalog));return false;}
    sceIoGetstat(path.c_str(),&stat);
    PSVA_TRACE_EVENT("disk_parse_start",std::string("catalog=")+label(catalog)+
        " bytes="+std::to_string((uint64_t)stat.st_size));
    std::vector<ui::CatalogItem> cached;
    if(!CatalogParser::parseFile(path,cached)||cached.empty()){
        PSVA_TRACE_DURATION("disk_parse_failed",t,label(catalog));return false;
    }
    outItems=std::move(cached);
    PSVA_TRACE_DURATION("disk_parse_ok",t,std::string("catalog=")+label(catalog)+
        " items="+std::to_string(outItems.size()));
    return true;
}

void CatalogManager::releaseInactiveCachesLocked() {
    const auto keep = static_cast<size_t>(requestedCatalog_);
    const size_t released = catalog_memory::releaseExcept(cachedItems_, cachedValid_, keep);
    if (released)
        diagnostics::log("[CatalogManager] RAM eviction: inactive entries=" +
                         std::to_string(released));
}

void CatalogManager::releaseRefreshCopyLocked(ui::CatalogType catalog) {
    releaseInactiveCachesLocked();
    // Cache-first publication has already been consumed by the screen.
    // The old validated disk file stays intact until fresh parsing succeeds.
    // If the user switched tabs, never evict the new requested catalog.
    if (catalog == requestedCatalog_ && !readyPending_) {
        const auto idx = static_cast<size_t>(catalog);
        const size_t released = catalog_memory::releaseEntry(cachedItems_, cachedValid_, idx);
        if (released)
            diagnostics::log("[CatalogManager] RAM eviction: redundant refresh entries=" +
                             std::to_string(released));
    }
}

bool CatalogManager::trySoftNetworkRefresh(ui::CatalogType catalog,
                                           std::vector<ui::CatalogItem>& outItems,
                                           unsigned generation) {
    outItems.clear();
    const std::string path = cachePath(catalog);
    const std::string meta = metadataPath(catalog);
    const std::string temp = path + ".new";
    const std::string url = std::string(RAW_BASE) + fileName(catalog);

    HttpClient http;
    if (http.init() != HttpResult::Ok) {
        diagnostics::log("[CatalogManager] soft refresh: HTTP init failed (keep cache)");
        return false;
    }
    ensureZrifIndexDownloaded(catalog, http);

    std::string storedText, storedEtag, storedModified;
    parseValidators(readTextFile(meta, storedText) ? storedText : std::string(),
                    storedEtag, storedModified);
    std::string remoteEtag, remoteModified;

    // Called with mutex held. A switch can supersede a soft refresh even
    // when the destination is already cached and needs no worker request.
    const auto currentLocked = [&]() {
        return !requestPending_ && requestedCatalog_ == catalog &&
               requestGeneration_ == generation;
    };
    sceKernelLockMutex(mutex_, 1, nullptr);
    if (!currentLocked()) {
        sceKernelUnlockMutex(mutex_, 1);
        http.shutdown();
        return false;
    }
    status_.softRefresh = true;
    status_.state = State::Ready;
    status_.message = ::psvitaalive::L(::psvitaalive::TextId::CheckingForAppUpdates);
    sceKernelUnlockMutex(mutex_, 1);

    const uint64_t tValidator=catalog_trace::nowUs();
    const HttpResult validatorResult=http.fetchRemoteValidators(url,remoteEtag,remoteModified);
    PSVA_TRACE_DURATION("validator_result",tValidator,
        std::string("catalog=")+label(catalog)+" rc="+std::to_string((int)validatorResult)+
        " etag_old="+std::to_string(!storedEtag.empty())+
        " etag_new="+std::to_string(!remoteEtag.empty())+
        " lm_old="+std::to_string(!storedModified.empty())+
        " lm_new="+std::to_string(!remoteModified.empty()));
    if (validatorResult == HttpResult::Ok &&
        validatorsMatch(storedEtag, storedModified, remoteEtag, remoteModified)) {
        diagnostics::log(std::string("[CatalogManager] soft refresh: unchanged ") + label(catalog));
        http.shutdown();
        sceKernelLockMutex(mutex_, 1, nullptr);
        if (currentLocked()) {
            status_.softRefresh = false;
            status_.message = ::psvitaalive::L(::psvitaalive::TextId::CatalogReadyMsg);
        }
        sceKernelUnlockMutex(mutex_, 1);
        return false;
    }
    if (validatorResult != HttpResult::Ok) {
        diagnostics::log(std::string("[CatalogManager] soft refresh: validator skip ") +
                         label(catalog) + " err=" + http.lastError());
        http.shutdown();
        sceKernelLockMutex(mutex_, 1, nullptr);
        if (currentLocked()) {
            status_.softRefresh = false;
            status_.message = ::psvitaalive::L(::psvitaalive::TextId::CatalogReadyMsg);
        }
        sceKernelUnlockMutex(mutex_, 1);
        return false;
    }

    PSVA_TRACE_EVENT("soft_download_start",label(catalog));
    diagnostics::log(std::string("[CatalogManager] soft refresh: downloading ") + label(catalog));
    sceKernelLockMutex(mutex_, 1, nullptr);
    if (!currentLocked()) {
        sceKernelUnlockMutex(mutex_, 1);
        http.shutdown();
        return false;
    }
    releaseInactiveCachesLocked();
    status_.softRefresh = true;
    status_.state = State::Ready;
    status_.message = "Updating catalog from network — please wait...";
    status_.refreshPhase = RefreshPhase::Downloading;
    ++status_.refreshSerial;
    status_.current = 0;
    status_.total = 0;
    sceKernelUnlockMutex(mutex_, 1);

    const HttpResult result = http.downloadToFile(
        url, temp, 0,
        [this, &currentLocked](const HttpProgress& progress) {
            sceKernelLockMutex(mutex_, 1, nullptr);
            if (currentLocked()) {
                status_.current = progress.downloaded;
                status_.total = progress.total;
                status_.message = "Updating catalog from network — please wait...";
                status_.softRefresh = true;
                status_.state = State::Ready;
            }
            sceKernelUnlockMutex(mutex_, 1);
        });

    if(result==HttpResult::Ok){
        const uint64_t cmpStart=catalog_trace::nowUs();
        const bool same=filesIdentical(path,temp);
        diagnostics::log(std::string("[CatalogPerf] compare catalog=") + label(catalog) +
            " ms=" + std::to_string((catalog_trace::nowUs()-cmpStart)/1000ULL));
        PSVA_TRACE_DURATION("catalog_content_compare",cmpStart,
            std::string("catalog=")+label(catalog)+" identical="+std::to_string(same?1:0));
        if(same){
            sceIoRemove(temp.c_str());
            // Use metadata from the downloaded bytes, never the earlier HEAD.
            writeTextFile(meta,"etag="+http.lastEtag()+"\nlast_modified="+http.lastModified()+"\n");
            http.shutdown();
            sceKernelLockMutex(mutex_,1,nullptr);
            if (currentLocked()) {
                status_.softRefresh=false;
                status_.refreshPhase=RefreshPhase::None;
                status_.message=::psvitaalive::L(::psvitaalive::TextId::CatalogReadyMsg);
            }
            sceKernelUnlockMutex(mutex_,1);
            diagnostics::log(std::string("[CatalogManager] byte-identical remote, skip JSON/hot-swap: ")+label(catalog));
            return false;
        }
        sceKernelLockMutex(mutex_,1,nullptr);
        if (!currentLocked()) {
            sceKernelUnlockMutex(mutex_,1);
            sceIoRemove(temp.c_str());
            http.shutdown();
            return false;
        }
        releaseRefreshCopyLocked(catalog);
        status_.message="Processing updated catalog...";
        status_.refreshPhase=RefreshPhase::Applying;
        status_.current=0;status_.total=0;
        sceKernelUnlockMutex(mutex_,1);
        std::vector<ui::CatalogItem> fresh;
        const uint64_t parseStart = catalog_trace::nowUs();
        if (CatalogParser::parseFile(temp, fresh) && !fresh.empty()) {
            const uint64_t saveStart = catalog_trace::nowUs();
            const bool saved = promoteCatalogCache(temp, path);
            diagnostics::log(std::string("[CatalogPerf] apply catalog=") + label(catalog) +
                " parse_ms=" + std::to_string((saveStart-parseStart)/1000ULL) +
                " save_ms=" + std::to_string((catalog_trace::nowUs()-saveStart)/1000ULL) +
                " items=" + std::to_string(fresh.size()));
            if (saved) {
                // Successful GET/range transfer already carries its resource validators.
                // If missing, store empty metadata and recheck on a later launch;
                // never wait for a second HEAD or label bytes with a newer revision.
                writeTextFile(meta, "etag=" + http.lastEtag() + "\nlast_modified=" + http.lastModified() + "\n");
                outItems = std::move(fresh);
                diagnostics::log(std::string("[CatalogManager] soft refresh: updated ") + label(catalog));
                http.shutdown();
                return true;
            }
            // The validated download can still be displayed if caching fails.
            diagnostics::log("[CatalogManager] soft refresh: disk save failed; using validated payload in RAM");
            sceIoRemove(temp.c_str());
            outItems = std::move(fresh);
            http.shutdown();
            return true;
        }
    }
    sceIoRemove(temp.c_str());
    diagnostics::log(std::string("[CatalogManager] soft refresh: failed, keep cache ") +
                     label(catalog) + " err=" + http.lastError());
    http.shutdown();
    sceKernelLockMutex(mutex_, 1, nullptr);
    if (currentLocked()) {
        status_.softRefresh = false;
        status_.refreshPhase = RefreshPhase::None;
        status_.message = ::psvitaalive::L(::psvitaalive::TextId::CatalogReadyMsg);
    }
    sceKernelUnlockMutex(mutex_, 1);
    return false;
}

bool CatalogManager::loadCatalog(ui::CatalogType catalog, std::vector<ui::CatalogItem>& outItems,
                                 bool forceNetwork) {
    outItems.clear();
    const std::string path = cachePath(catalog);
    const std::string meta = metadataPath(catalog);
    const std::string temp = path + ".new";
    const std::string url = std::string(RAW_BASE) + fileName(catalog);
    const bool haveCache = fileExists(path);

    HttpClient http;
    if (http.init() != HttpResult::Ok) return false;
    ensureZrifIndexDownloaded(catalog, http);

    if (haveCache && !forceNetwork) {
        std::vector<ui::CatalogItem> cached;
        if (CatalogParser::parseFile(path, cached) && !cached.empty()) {
            std::string storedText, storedEtag, storedModified;
            parseValidators(readTextFile(meta, storedText) ? storedText : std::string(),
                            storedEtag, storedModified);
            std::string remoteEtag, remoteModified;
            setStatus(State::Loading, catalog, "Checking remote catalog (quick)...");
            const HttpResult validatorResult =
                http.fetchRemoteValidators(url, remoteEtag, remoteModified);
            if (validatorResult == HttpResult::Ok &&
                validatorsMatch(storedEtag, storedModified, remoteEtag, remoteModified)) {
                outItems = std::move(cached);
                // Worker publishes Ready together with its non-empty payload.
                diagnostics::log(std::string("[CatalogManager] cache valid: ") + label(catalog));
                http.shutdown();
                return true;
            }
            if (validatorResult != HttpResult::Ok) {
                outItems = std::move(cached);
                // Worker publishes Ready together with its non-empty payload.
                diagnostics::log(std::string("[CatalogManager] offline/cache fallback: ") +
                                 label(catalog) + " validatorErr=" + http.lastError());
                http.shutdown();
                return true;
            }
        }
    }

    setStatus(State::Loading, catalog, forceNetwork ? "Recovering catalog from network..." : "Downloading catalog...");
    // Never resume a previous incomplete catalog download.
    sceIoRemove(temp.c_str());
    sceKernelLockMutex(mutex_, 1, nullptr);
    releaseInactiveCachesLocked();
    status_.refreshPhase = RefreshPhase::Downloading;
    ++status_.refreshSerial;
    status_.current = status_.total = 0;
    sceKernelUnlockMutex(mutex_, 1);
    diagnostics::log(std::string("[CatalogManager] downloading: ") + label(catalog));
    const HttpResult result = http.downloadToFile(
        url, temp, 0,
        [this](const HttpProgress& progress) {
            sceKernelLockMutex(mutex_, 1, nullptr);
            status_.current = progress.downloaded;
            status_.total = progress.total;
            status_.message = "Downloading catalog from network — please wait...";
            sceKernelUnlockMutex(mutex_, 1);
        });

    if (result == HttpResult::Ok) {
        sceKernelLockMutex(mutex_,1,nullptr);
        releaseInactiveCachesLocked();
        status_.message="Processing downloaded catalog...";
        status_.refreshPhase=RefreshPhase::Applying;
        status_.current=0;status_.total=0;
        sceKernelUnlockMutex(mutex_,1);
        std::vector<ui::CatalogItem> fresh;
        const uint64_t parseStart = catalog_trace::nowUs();
        if (CatalogParser::parseFile(temp, fresh) && !fresh.empty()) {
            const uint64_t saveStart = catalog_trace::nowUs();
            const bool saved = promoteCatalogCache(temp, path);
            diagnostics::log(std::string("[CatalogPerf] apply catalog=") + label(catalog) +
                " parse_ms=" + std::to_string((saveStart-parseStart)/1000ULL) +
                " save_ms=" + std::to_string((catalog_trace::nowUs()-saveStart)/1000ULL) +
                " items=" + std::to_string(fresh.size()));
            if (saved) {
                // Successful GET/range transfer already carries its resource validators.
                // If missing, store empty metadata and recheck on a later launch;
                // never wait for a second HEAD or label bytes with a newer revision.
                writeTextFile(meta, "etag=" + http.lastEtag() + "\nlast_modified=" + http.lastModified() + "\n");
                outItems = std::move(fresh);
                sceKernelLockMutex(mutex_, 1, nullptr);
                status_.refreshPhase = RefreshPhase::Updated;
                sceKernelUnlockMutex(mutex_, 1);
                diagnostics::log(std::string("[CatalogManager] downloaded and cached: ") + label(catalog));
                http.shutdown();
                return true;
            }
            diagnostics::log("[CatalogManager] disk save failed; using validated catalog in RAM");
            sceIoRemove(temp.c_str());
            sceKernelLockMutex(mutex_, 1, nullptr);
            status_.refreshPhase = RefreshPhase::Updated;
            sceKernelUnlockMutex(mutex_, 1);
            outItems = std::move(fresh);
            http.shutdown();
            return true;
        }
    }
    sceIoRemove(temp.c_str());
    sceKernelLockMutex(mutex_, 1, nullptr);
    status_.refreshPhase = RefreshPhase::None;
    sceKernelUnlockMutex(mutex_, 1);
    if (haveCache && CatalogParser::parseFile(path, outItems) && !outItems.empty()) {
        diagnostics::log(std::string("[CatalogManager] refresh failed; retained valid cache: ") +
                         label(catalog));
        http.shutdown();
        return true;
    }
    diagnostics::log(std::string("[CatalogManager] catalog unavailable: ") + label(catalog) +
                     " error=" + http.lastError());
    http.shutdown();
    return false;
}

void CatalogManager::publishReady(ui::CatalogType catalog, std::vector<ui::CatalogItem>& items,
                                  const char* message, bool softRefresh, bool keepRamCopy) {
    const int cidx = static_cast<int>(catalog);
    const size_t itemCount = items.size();
    if (cidx >= 0 && cidx < static_cast<int>(ui::CatalogType::Count)) {
        if (keepRamCopy) {
            cachedItems_[cidx] = items;
            cachedValid_[cidx] = true;
        } else {
            // A fresh network result is moved directly to the UI. Avoid a
            // third decoded copy while the UI still owns the previous version.
            catalog_memory::releaseEntry(cachedItems_, cachedValid_, static_cast<size_t>(cidx));
        }
    }
    // Unexpected overwrite is relevant to catalog error reports even when CATTRACE is off.
    if(readyPending_)diagnostics::log(std::string("[CatalogManager] WARNING unconsumed ready overwrite old=")
        +label(readyCatalog_)+" new="+label(catalog));
    readyItems_=std::move(items);
    readyCatalog_=catalog;
    readyPending_=true;
    PSVA_TRACE_EVENT("publish_ready",std::string("catalog=")+label(catalog)+
        " items="+std::to_string(itemCount)+
        " soft="+std::to_string(softRefresh?1:0));
    status_.state = State::Ready;
    status_.catalog = catalog;
    status_.current = status_.total > 0 ? status_.total : 1;
    status_.message = message ? message : ::psvitaalive::L(::psvitaalive::TextId::CatalogReadyMsg);
    status_.error.clear();
    status_.softRefresh = softRefresh;
    if (message && std::strcmp(message, "Catalog updated") == 0)
        status_.refreshPhase = RefreshPhase::Updated;
}

int CatalogManager::workerEntry(SceSize args, void* argp) {
    (void)args;
    CatalogManager* self = nullptr;
    if (argp) std::memcpy(&self, argp, sizeof(self));
    return self ? self->workerMain() : -1;
}

int CatalogManager::workerMain() {
    while (!stopping_) {
        ui::CatalogType catalog = ui::CatalogType::Homebrew;
        bool haveRequest = false;
        bool forceNetwork = false;
        unsigned gen = 0;

        sceKernelLockMutex(mutex_, 1, nullptr);
        if (requestPending_) {
            catalog = requestedCatalog_;
            gen = requestGeneration_;
            loadingGeneration_ = gen;
            requestPending_ = false;
            forceNetwork = forceNetworkPending_;
            forceNetworkPending_ = false;
            haveRequest = true;
            workerOperationActive_ = true;
            // request() already bounded normal browsing to current + previous.
            // Actual network transfers evict inactive catalogs before allocation.
            status_.state = State::Loading;
            status_.catalog = catalog;
            status_.label = label(catalog);
            status_.message = ::psvitaalive::L(::psvitaalive::TextId::LoadingCatalogLocalCache);
            status_.error.clear();
            status_.softRefresh = false;
            status_.refreshPhase = RefreshPhase::None;
            status_.current = 0;
            status_.total = 0;
        }
        sceKernelUnlockMutex(mutex_, 1);

        if (!haveRequest) {
            sceKernelDelayThread(50 * 1000);
            continue;
        }

        // Every exit path (including stale/soft-refresh continue) releases the
        // gate only after local parse vectors and HTTP objects are destroyed.
        struct OperationScope {
            SceUID mutex;
            bool& active;
            ~OperationScope() {
                sceKernelLockMutex(mutex, 1, nullptr);
                active = false;
                sceKernelUnlockMutex(mutex, 1);
            }
        } operation{mutex_, workerOperationActive_};

        PSVA_TRACE_EVENT("worker_request",std::string("catalog=")+label(catalog)+
            " gen="+std::to_string(gen));
        auto isStale = [&]() -> bool {
            sceKernelLockMutex(mutex_, 1, nullptr);
            const bool stale =
                requestPending_ || catalog != requestedCatalog_ || gen != requestGeneration_;
            if (stale && requestPending_) {
                status_.state = State::Loading;
                status_.catalog = requestedCatalog_;
                status_.label = label(requestedCatalog_);
                status_.message = "Switching catalog — please wait...";
                status_.softRefresh = false;
                status_.refreshPhase = RefreshPhase::None;
            }
            sceKernelUnlockMutex(mutex_, 1);
            return stale;
        };

#if PSVITAALIVE_CACHE_FIRST_REFRESH
        std::vector<ui::CatalogItem> diskItems;
        if (!forceNetwork && tryLoadDiskCache(catalog, diskItems)) {
            diagnostics::log(std::string("[CatalogManager] cache-first show: ") + label(catalog) +
                             " items=" + std::to_string(diskItems.size()));
            sceKernelLockMutex(mutex_, 1, nullptr);
            if (!(requestPending_ || catalog != requestedCatalog_ || gen != requestGeneration_)) {
                publishReady(catalog, diskItems, "Using cached catalog", true);
            }
            sceKernelUnlockMutex(mutex_, 1);

            if (isStale()) continue;
            // Never overwrite a not-yet-consumed cache-first payload.
            while(!stopping_){
                sceKernelLockMutex(mutex_,1,nullptr);
                const bool pending=readyPending_ && readyCatalog_==catalog;
                const bool stale=requestPending_||catalog!=requestedCatalog_||
                    gen!=requestGeneration_;
                sceKernelUnlockMutex(mutex_,1);
                if(stale||!pending)break;
                sceKernelDelayThread(10*1000);
            }
            if(stopping_||isStale())continue;
            PSVA_TRACE_EVENT("cache_consumed_network_next",label(catalog));
            std::vector<ui::CatalogItem> fresh;
            if (trySoftNetworkRefresh(catalog, fresh, gen) && !fresh.empty()) {
                if (isStale()) continue;
                sceKernelLockMutex(mutex_, 1, nullptr);
                if (!(requestPending_ || catalog != requestedCatalog_ || gen != requestGeneration_)) {
                    publishReady(catalog, fresh, "Catalog updated", false, false);
                    diagnostics::log(std::string("[CatalogManager] hot-swap after soft refresh: ") +
                                     label(catalog));
                }
                sceKernelUnlockMutex(mutex_, 1);
            } else {
                sceKernelLockMutex(mutex_, 1, nullptr);
                if (!requestPending_ && catalog == requestedCatalog_ && gen == requestGeneration_) {
                    status_.softRefresh = false;
                    if (status_.state == State::Ready)
                        status_.message = ::psvitaalive::L(::psvitaalive::TextId::CatalogReadyMsg);
                }
                sceKernelUnlockMutex(mutex_, 1);
            }
            continue;
        }
        PSVA_TRACE_EVENT("hard_load_start",label(catalog));
        diagnostics::log(std::string("[CatalogManager] no disk cache, hard load: ") + label(catalog));
#endif

        std::vector<ui::CatalogItem> loaded;
        bool ok = loadCatalog(catalog, loaded, forceNetwork) && !loaded.empty();
        // No valid payload: one bounded full network recovery per load.
        // Keep this on the existing worker and never retry a stale request.
        if (!ok && !forceNetwork && !stopping_ && !isStale()) {
            diagnostics::log(std::string("[CatalogManager] empty/unavailable catalog; forced recovery: ") + label(catalog));
            sceKernelDelayThread(750 * 1000);
            if (!stopping_ && !isStale())
                ok = loadCatalog(catalog, loaded, true) && !loaded.empty();
        }

        sceKernelLockMutex(mutex_, 1, nullptr);
        if (requestPending_ || catalog != requestedCatalog_ || gen != requestGeneration_) {
            diagnostics::log(std::string("[CatalogManager] discard stale result for ") + label(catalog));
            status_.state = State::Loading;
            status_.catalog = requestedCatalog_;
            status_.label = label(requestedCatalog_);
            status_.message = "Switching catalog — please wait...";
            status_.softRefresh = false;
            status_.refreshPhase = RefreshPhase::None;
            sceKernelUnlockMutex(mutex_, 1);
            continue;
        }

        if (ok) {
            const bool downloaded = status_.refreshPhase == RefreshPhase::Updated;
            publishReady(catalog, loaded, ::psvitaalive::L(::psvitaalive::TextId::CatalogReadyMsg), false, !downloaded);
        } else {
            status_.state = State::Failed;
            status_.catalog = catalog;
            status_.message = "Unable to load catalog";
            status_.error = "Remote catalog and cache unavailable";
            status_.softRefresh = false;
            status_.refreshPhase = RefreshPhase::None;
        }
        sceKernelUnlockMutex(mutex_, 1);
    }
    return 0;
}

} // namespace psvitaalive

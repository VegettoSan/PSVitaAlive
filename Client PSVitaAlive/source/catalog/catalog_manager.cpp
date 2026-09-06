#include "catalog/catalog_manager.hpp"

#include "catalog/catalog_parser.hpp"
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
        status_.message = "Switching catalog — please wait...";
        status_.error.clear();
        status_.softRefresh = false;
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
        status_.softRefresh = false;
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
    requestPending_ = true;
    requestedCatalog_ = catalog;
    sceKernelUnlockMutex(mutex_, 1);
    return true;
}

CatalogManager::Status CatalogManager::status()const{Status result;if(mutex_<0)return result;sceKernelLockMutex(mutex_,1,nullptr);result=status_;sceKernelUnlockMutex(mutex_,1);return result;}
bool CatalogManager::takeReady(std::vector<ui::CatalogItem>&outItems,ui::CatalogType&catalog){if(mutex_<0)return false;sceKernelLockMutex(mutex_,1,nullptr);if(!readyPending_){sceKernelUnlockMutex(mutex_,1);return false;}outItems=std::move(readyItems_);catalog=readyCatalog_;readyPending_=false;sceKernelUnlockMutex(mutex_,1);return true;}

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
    if (!fileExists(path)) return false;
    std::vector<ui::CatalogItem> cached;
    if (!CatalogParser::parseFile(path, cached) || cached.empty()) return false;
    outItems = std::move(cached);
    return true;
}

bool CatalogManager::trySoftNetworkRefresh(ui::CatalogType catalog,
                                           std::vector<ui::CatalogItem>& outItems) {
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

    sceKernelLockMutex(mutex_, 1, nullptr);
    status_.softRefresh = true;
    status_.state = State::Ready;
    status_.message = ::psvitaalive::L(::psvitaalive::TextId::CheckingForAppUpdates);
    sceKernelUnlockMutex(mutex_, 1);

    const HttpResult validatorResult = http.fetchRemoteValidators(url, remoteEtag, remoteModified);
    if (validatorResult == HttpResult::Ok &&
        validatorsMatch(storedEtag, storedModified, remoteEtag, remoteModified)) {
        diagnostics::log(std::string("[CatalogManager] soft refresh: unchanged ") + label(catalog));
        http.shutdown();
        sceKernelLockMutex(mutex_, 1, nullptr);
        status_.softRefresh = false;
        status_.message = ::psvitaalive::L(::psvitaalive::TextId::CatalogReadyMsg);
        sceKernelUnlockMutex(mutex_, 1);
        return false;
    }
    if (validatorResult != HttpResult::Ok) {
        diagnostics::log(std::string("[CatalogManager] soft refresh: validator skip ") +
                         label(catalog) + " err=" + http.lastError());
        http.shutdown();
        sceKernelLockMutex(mutex_, 1, nullptr);
        status_.softRefresh = false;
        status_.message = ::psvitaalive::L(::psvitaalive::TextId::CatalogReadyMsg);
        sceKernelUnlockMutex(mutex_, 1);
        return false;
    }

    diagnostics::log(std::string("[CatalogManager] soft refresh: downloading ") + label(catalog));
    sceKernelLockMutex(mutex_, 1, nullptr);
    status_.softRefresh = true;
    status_.state = State::Ready;
    status_.message = "Updating catalog from network — please wait...";
    status_.current = 0;
    status_.total = 0;
    sceKernelUnlockMutex(mutex_, 1);

    const HttpResult result = http.downloadToFile(
        url, temp, 0,
        [this](const HttpProgress& progress) {
            sceKernelLockMutex(mutex_, 1, nullptr);
            status_.current = progress.downloaded;
            status_.total = progress.total;
            status_.message = "Updating catalog from network — please wait...";
            status_.softRefresh = true;
            status_.state = State::Ready;
            sceKernelUnlockMutex(mutex_, 1);
        });

    if (result == HttpResult::Ok) {
        std::vector<ui::CatalogItem> fresh;
        if (CatalogParser::parseFile(temp, fresh) && !fresh.empty()) {
            sceIoRemove(path.c_str());
            if (sceIoRename(temp.c_str(), path.c_str()) >= 0) {
                std::string etag, modified;
                http.fetchRemoteValidators(url, etag, modified);
                writeTextFile(meta, "etag=" + etag + "\nlast_modified=" + modified + "\n");
                outItems = std::move(fresh);
                diagnostics::log(std::string("[CatalogManager] soft refresh: updated ") + label(catalog));
                http.shutdown();
                return true;
            }
        }
    }
    sceIoRemove(temp.c_str());
    diagnostics::log(std::string("[CatalogManager] soft refresh: failed, keep cache ") +
                     label(catalog) + " err=" + http.lastError());
    http.shutdown();
    sceKernelLockMutex(mutex_, 1, nullptr);
    status_.softRefresh = false;
    status_.message = ::psvitaalive::L(::psvitaalive::TextId::CatalogReadyMsg);
    sceKernelUnlockMutex(mutex_, 1);
    return false;
}

bool CatalogManager::loadCatalog(ui::CatalogType catalog, std::vector<ui::CatalogItem>& outItems) {
    const std::string path = cachePath(catalog);
    const std::string meta = metadataPath(catalog);
    const std::string temp = path + ".new";
    const std::string url = std::string(RAW_BASE) + fileName(catalog);
    const bool haveCache = fileExists(path);

    HttpClient http;
    if (http.init() != HttpResult::Ok) return false;
    ensureZrifIndexDownloaded(catalog, http);

    if (haveCache) {
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
                setStatus(State::Ready, catalog, "Using cached catalog");
                diagnostics::log(std::string("[CatalogManager] cache valid: ") + label(catalog));
                http.shutdown();
                return true;
            }
            if (validatorResult != HttpResult::Ok) {
                outItems = std::move(cached);
                setStatus(State::Ready, catalog, "Using cached catalog (offline)");
                diagnostics::log(std::string("[CatalogManager] offline/cache fallback: ") +
                                 label(catalog) + " validatorErr=" + http.lastError());
                http.shutdown();
                return true;
            }
        }
    }

    setStatus(State::Loading, catalog, "Downloading catalog...");
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
        std::vector<ui::CatalogItem> fresh;
        if (CatalogParser::parseFile(temp, fresh) && !fresh.empty()) {
            sceIoRemove(path.c_str());
            if (sceIoRename(temp.c_str(), path.c_str()) >= 0) {
                std::string etag, modified;
                http.fetchRemoteValidators(url, etag, modified);
                writeTextFile(meta, "etag=" + etag + "\nlast_modified=" + modified + "\n");
                outItems = std::move(fresh);
                diagnostics::log(std::string("[CatalogManager] downloaded and cached: ") + label(catalog));
                http.shutdown();
                return true;
            }
        }
    }
    sceIoRemove(temp.c_str());
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
                                  const char* message, bool softRefresh) {
    const int cidx = static_cast<int>(catalog);
    if (cidx >= 0 && cidx < static_cast<int>(ui::CatalogType::Count)) {
        cachedItems_[cidx] = items;
        cachedValid_[cidx] = true;
    }
    readyItems_ = items;
    readyCatalog_ = catalog;
    readyPending_ = true;
    status_.state = State::Ready;
    status_.catalog = catalog;
    status_.current = status_.total > 0 ? status_.total : 1;
    status_.message = message ? message : ::psvitaalive::L(::psvitaalive::TextId::CatalogReadyMsg);
    status_.error.clear();
    status_.softRefresh = softRefresh;
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
            status_.message = ::psvitaalive::L(::psvitaalive::TextId::LoadingCatalogLocalCache);
            status_.error.clear();
            status_.softRefresh = false;
            status_.current = 0;
            status_.total = 0;
        }
        sceKernelUnlockMutex(mutex_, 1);

        if (!haveRequest) {
            sceKernelDelayThread(50 * 1000);
            continue;
        }

        auto isStale = [&]() -> bool {
            sceKernelLockMutex(mutex_, 1, nullptr);
            const bool stale =
                requestPending_ || catalog != requestedCatalog_ || gen != requestGeneration_;
            if (stale) {
                status_.state = State::Loading;
                status_.catalog = requestedCatalog_;
                status_.label = label(requestedCatalog_);
                status_.message = "Switching catalog — please wait...";
                status_.softRefresh = false;
            }
            sceKernelUnlockMutex(mutex_, 1);
            return stale;
        };

#if PSVITAALIVE_CACHE_FIRST_REFRESH
        std::vector<ui::CatalogItem> diskItems;
        if (tryLoadDiskCache(catalog, diskItems)) {
            diagnostics::log(std::string("[CatalogManager] cache-first show: ") + label(catalog) +
                             " items=" + std::to_string(diskItems.size()));
            sceKernelLockMutex(mutex_, 1, nullptr);
            if (!(requestPending_ || catalog != requestedCatalog_ || gen != requestGeneration_)) {
                publishReady(catalog, diskItems, "Using cached catalog", true);
            }
            sceKernelUnlockMutex(mutex_, 1);

            if (isStale()) continue;

            std::vector<ui::CatalogItem> fresh;
            if (trySoftNetworkRefresh(catalog, fresh) && !fresh.empty()) {
                if (isStale()) continue;
                sceKernelLockMutex(mutex_, 1, nullptr);
                if (!(requestPending_ || catalog != requestedCatalog_ || gen != requestGeneration_)) {
                    publishReady(catalog, fresh, "Catalog updated", false);
                    diagnostics::log(std::string("[CatalogManager] hot-swap after soft refresh: ") +
                                     label(catalog));
                }
                sceKernelUnlockMutex(mutex_, 1);
            } else {
                sceKernelLockMutex(mutex_, 1, nullptr);
                status_.softRefresh = false;
                if (status_.state == State::Ready)
                    status_.message = ::psvitaalive::L(::psvitaalive::TextId::CatalogReadyMsg);
                sceKernelUnlockMutex(mutex_, 1);
            }
            continue;
        }
        diagnostics::log(std::string("[CatalogManager] no disk cache, hard load: ") + label(catalog));
#endif

        std::vector<ui::CatalogItem> loaded;
        const bool ok = loadCatalog(catalog, loaded) && !loaded.empty();

        sceKernelLockMutex(mutex_, 1, nullptr);
        if (requestPending_ || catalog != requestedCatalog_ || gen != requestGeneration_) {
            diagnostics::log(std::string("[CatalogManager] discard stale result for ") + label(catalog));
            status_.state = State::Loading;
            status_.catalog = requestedCatalog_;
            status_.label = label(requestedCatalog_);
            status_.message = "Switching catalog — please wait...";
            status_.softRefresh = false;
            sceKernelUnlockMutex(mutex_, 1);
            continue;
        }

        if (ok) {
            publishReady(catalog, loaded, ::psvitaalive::L(::psvitaalive::TextId::CatalogReadyMsg), false);
        } else {
            status_.state = State::Failed;
            status_.catalog = catalog;
            status_.message = "Unable to load catalog";
            status_.error = "Remote catalog and cache unavailable";
            status_.softRefresh = false;
        }
        sceKernelUnlockMutex(mutex_, 1);
    }
    return 0;
}

} // namespace psvitaalive

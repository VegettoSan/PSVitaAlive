#include "catalog/catalog_manager.hpp"

#include "catalog/catalog_parser.hpp"
#include "network/http_client.hpp"
#include "storage/storage_manager.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/io/stat.h>

#include <cstring>
#include <utility>

namespace psvitaalive {

namespace {
constexpr const char* RAW_BASE = "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/";
constexpr const char* CACHE_DIR = "ux0:data/psvitaalive/cache/catalog";
constexpr int WORKER_PRIORITY = 0x10000100;
constexpr int WORKER_STACK = 64 * 1024;
}

CatalogManager::CatalogManager() = default;
CatalogManager::~CatalogManager() { shutdown(); }

bool CatalogManager::init() {
    if (workerThread_ >= 0) return true;

    StorageManager storage;
    if (!storage.createDirectories(CACHE_DIR)) {
        sceClibPrintf("[CatalogManager] cannot create cache directory\n");
        return false;
    }

    mutex_ = sceKernelCreateMutex("PSVitaAliveCatalog", 0, 0, nullptr);
    if (mutex_ < 0) return false;

    stopping_ = false;
    workerThread_ = sceKernelCreateThread(
        "PSVitaAliveCatalogWorker",
        &CatalogManager::workerEntry,
        WORKER_PRIORITY,
        WORKER_STACK,
        0,
        0,
        nullptr
    );
    if (workerThread_ < 0) {
        sceKernelDeleteMutex(mutex_);
        mutex_ = -1;
        return false;
    }

    CatalogManager* self = this;
    const int result = sceKernelStartThread(workerThread_, sizeof(self), &self);
    if (result < 0) {
        sceKernelDeleteThread(workerThread_);
        workerThread_ = -1;
        sceKernelDeleteMutex(mutex_);
        mutex_ = -1;
        return false;
    }

    return true;
}

void CatalogManager::shutdown() {
    stopping_ = true;
    if (workerThread_ >= 0) {
        sceKernelWaitThreadEnd(workerThread_, nullptr, nullptr);
        sceKernelDeleteThread(workerThread_);
        workerThread_ = -1;
    }
    if (mutex_ >= 0) {
        sceKernelDeleteMutex(mutex_);
        mutex_ = -1;
    }
}

const char* CatalogManager::fileName(ui::CatalogType catalog) const {
    switch (catalog) {
        case ui::CatalogType::VitaGames: return "catalog_psvita_games.json";
        case ui::CatalogType::PspGames: return "catalog_psp_games.json";
        case ui::CatalogType::Ps1Games: return "catalog_ps1_games.json";
        default: return "catalog.json";
    }
}

const char* CatalogManager::label(ui::CatalogType catalog) const {
    return ui::catalogName(catalog);
}

std::string CatalogManager::cachePath(ui::CatalogType catalog) const {
    return std::string(CACHE_DIR) + "/" + fileName(catalog);
}

void CatalogManager::setStatus(State state, ui::CatalogType catalog, const char* message, const char* error) {
    sceKernelLockMutex(mutex_, 1, nullptr);
    status_.state = state;
    status_.catalog = catalog;
    status_.label = label(catalog);
    status_.message = message ? message : "";
    status_.error = error ? error : "";
    sceKernelUnlockMutex(mutex_, 1);
}

bool CatalogManager::request(ui::CatalogType catalog) {
    if (catalog == ui::CatalogType::Homebrew || mutex_ < 0) return false;

    sceKernelLockMutex(mutex_, 1, nullptr);
    if (status_.state == State::Loading) {
        sceKernelUnlockMutex(mutex_, 1);
        return false;
    }
    requestedCatalog_ = catalog;
    requestPending_ = true;
    readyPending_ = false;
    status_.state = State::Loading;
    status_.catalog = catalog;
    status_.current = 0;
    status_.total = 0;
    status_.label = label(catalog);
    status_.message = "Connecting to catalog...";
    status_.error.clear();
    sceKernelUnlockMutex(mutex_, 1);
    return true;
}

CatalogManager::Status CatalogManager::status() const {
    Status result;
    if (mutex_ < 0) return result;
    sceKernelLockMutex(mutex_, 1, nullptr);
    result = status_;
    sceKernelUnlockMutex(mutex_, 1);
    return result;
}

bool CatalogManager::takeReady(std::vector<ui::CatalogItem>& outItems, ui::CatalogType& catalog) {
    if (mutex_ < 0) return false;
    sceKernelLockMutex(mutex_, 1, nullptr);
    if (!readyPending_) {
        sceKernelUnlockMutex(mutex_, 1);
        return false;
    }
    outItems = std::move(readyItems_);
    catalog = readyCatalog_;
    readyPending_ = false;
    sceKernelUnlockMutex(mutex_, 1);
    return true;
}

bool CatalogManager::loadCatalog(ui::CatalogType catalog, std::vector<ui::CatalogItem>& outItems) {
    const std::string path = cachePath(catalog);
    const std::string url = std::string(RAW_BASE) + fileName(catalog);

    HttpClient http;
    if (http.init() != HttpResult::Ok) return false;

    setStatus(State::Loading, catalog, "Downloading catalog...");
    const HttpResult result = http.downloadToFile(
        url,
        path,
        0,
        [this, catalog](const HttpProgress& progress) {
            sceKernelLockMutex(mutex_, 1, nullptr);
            status_.current = progress.downloaded;
            status_.total = progress.total;
            status_.message = "Downloading catalog...";
            sceKernelUnlockMutex(mutex_, 1);
            (void)catalog;
        }
    );

    if (result == HttpResult::Ok && CatalogParser::parseFile(path, outItems)) {
        http.shutdown();
        return true;
    }

    // Network failure must not destroy a valid cached catalog. This follows the
    // same loading/error separation used by the web catalog loader.
    SceIoStat stat = {};
    if (sceIoGetstat(path.c_str(), &stat) >= 0 && stat.st_size > 0) {
        outItems.clear();
        if (CatalogParser::parseFile(path, outItems)) {
            http.shutdown();
            return true;
        }
    }

    http.shutdown();
    return false;
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

        sceKernelLockMutex(mutex_, 1, nullptr);
        if (requestPending_) {
            catalog = requestedCatalog_;
            requestPending_ = false;
            haveRequest = true;
        }
        sceKernelUnlockMutex(mutex_, 1);

        if (!haveRequest) {
            sceKernelDelayThread(50 * 1000);
            continue;
        }

        std::vector<ui::CatalogItem> loaded;
        if (loadCatalog(catalog, loaded) && !loaded.empty()) {
            sceKernelLockMutex(mutex_, 1, nullptr);
            readyItems_ = std::move(loaded);
            readyCatalog_ = catalog;
            readyPending_ = true;
            status_.state = State::Ready;
            status_.current = status_.total > 0 ? status_.total : 1;
            status_.message = "Catalog ready";
            status_.error.clear();
            sceKernelUnlockMutex(mutex_, 1);
        } else {
            sceKernelLockMutex(mutex_, 1, nullptr);
            status_.state = State::Failed;
            status_.message = "Unable to load catalog";
            status_.error = "Remote catalog and cache unavailable";
            sceKernelUnlockMutex(mutex_, 1);
        }
    }
    return 0;
}

} // namespace psvitaalive

#pragma once

#include "ui/ui_types.hpp"

#include <atomic>
#include <string>
#include <vector>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

namespace psvitaalive {

class CatalogManager {
public:
    enum class State { Idle, Loading, Ready, Failed };
    enum class RefreshPhase { None, Downloading, Applying, Updated };

    struct Status {
        State state = State::Idle;
        ui::CatalogType catalog = ui::CatalogType::Homebrew;
        uint64_t current = 0;
        uint64_t total = 0;
        std::string label;
        std::string message;
        std::string error;
        /** True while a background soft refresh runs after cache was already shown.
         *  UI must NOT treat this as a hard lock (tabs stay usable). */
        bool softRefresh = false;
        /** Only active for an actual remote payload transfer, not cache checks. */
        RefreshPhase refreshPhase = RefreshPhase::None;
        unsigned refreshSerial = 0;
    };

    CatalogManager();
    ~CatalogManager();

    bool init();
    void shutdown();
    bool request(ui::CatalogType catalog, bool forceNetwork = false,
                 ui::CatalogType outgoingCatalog = ui::CatalogType::Count,
                 std::vector<ui::CatalogItem>* outgoingItems = nullptr);
    /** True while a load is in flight (UI must ignore further tab switches). */
    bool isBusy() const;
    // Queue gate: reject new catalog requests while allowing an already
    // accepted request to drain, including cache-first/soft-refresh tails.
    bool setInstallSessionGate(bool blocked);
    bool installSessionQuiescent() const;
    // Stronger than isBusy(): includes soft refresh, cache-first handoff and pending publication.
    bool actionBlockedByCatalog() const;
    Status status() const;
    bool takeReady(std::vector<ui::CatalogItem>& outItems, ui::CatalogType& catalog);
    // Read only display fields from already-loaded catalogs. No new parsing/downloads.
    bool savedZipAppDisplay(const std::string& appId, const std::string& titleId,
                            std::string& name, std::string& icon) const;

private:
    SceUID mutex_ = -1;
    SceUID workerThread_ = -1;
    volatile bool stopping_ = false;

    ui::CatalogType requestedCatalog_ = ui::CatalogType::Homebrew;
    bool requestPending_ = false;
    bool installSessionGate_ = false; // mutex-owned
    bool workerOperationActive_ = false; // includes local vector destruction
    bool forceNetworkPending_ = false;
    /** Bumped on every request; worker only publishes matching generation. */
    unsigned requestGeneration_ = 0;
    unsigned loadingGeneration_ = 0;

    Status status_;
    std::vector<ui::CatalogItem> readyItems_;
    bool readyPending_ = false;
    ui::CatalogType readyCatalog_ = ui::CatalogType::Homebrew;

    // In-memory cache of validated catalogs (zRIF lives in separate .zrifidx files).
    std::vector<ui::CatalogItem> cachedItems_[static_cast<int>(ui::CatalogType::Count)];
    bool cachedValid_[static_cast<int>(ui::CatalogType::Count)] = {};


    static int workerEntry(SceSize args, void* argp);
    int workerMain();
    bool loadCatalog(ui::CatalogType catalog, std::vector<ui::CatalogItem>& outItems,
                     bool forceNetwork = false);
    bool tryLoadDiskCache(ui::CatalogType catalog, std::vector<ui::CatalogItem>& outItems);
    bool trySoftNetworkRefresh(ui::CatalogType catalog, std::vector<ui::CatalogItem>& outItems,
                               unsigned generation);
    // Caller holds mutex_. Only evict decoded RAM copies, never disk cache.
    void releaseInactiveCachesLocked();
    void releaseRefreshCopyLocked(ui::CatalogType catalog);
    void publishReady(ui::CatalogType catalog, std::vector<ui::CatalogItem>& items,
                      const char* message, bool softRefresh, bool keepRamCopy = true);
    const char* fileName(ui::CatalogType catalog) const;
    const char* label(ui::CatalogType catalog) const;
    std::string cachePath(ui::CatalogType catalog) const;
    std::string metadataPath(ui::CatalogType catalog) const;
    void setStatus(State state, ui::CatalogType catalog, const char* message, const char* error = nullptr);
};

} // namespace psvitaalive

#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace psvitaalive { namespace storage {

struct InstalledDataPath {
    std::string path; // validated absolute ux0:data/ path
    uint64_t bytes = 0;
    bool exists = false;
    bool complete = true;
    bool shared = false; // another catalog app claims same or overlapping path
    bool sizeKnown = false; // an entire tree was measured successfully
    bool sizeCached = false; // previous measurement; only a historical estimate
    uint64_t rootStamp = 0; // stat hint, not proof that descendants are unchanged
};

struct InstalledDataApp {
    std::string appId;
    std::string titleId;
    std::string name;
    // Inputs are compact, extracted from links[].installed_paths on demand.
    std::vector<std::string> roots;
    bool unresolved = false; // metadata missing, conflicting or ambiguous
    bool packagePresent = false;
    bool blocked = false; // at least one path is shared, invalid or unresolved
    uint64_t bytes = 0; // deduplicated within this app
    std::vector<InstalledDataPath> paths;
};

struct InstalledDataSnapshot {
    std::vector<InstalledDataApp> apps;
    uint64_t totalExternalBytes = 0; // physical roots counted once across apps
    bool complete = true; // traversal limit, I/O errors => false
    bool totalSizeKnown = true; // false if an installed root has no verified size
};

// Read-only worker -> UI counters. Every field is atomic: the render thread
// must never read the in-progress InstalledDataSnapshot.
struct InstalledDataScanProgress {
    std::atomic<uint32_t> phase{0}; // 1 discover, 2 measure, 3 completed
    std::atomic<uint32_t> appsTotal{0};
    std::atomic<uint32_t> appsCompleted{0};
    std::atomic<uint32_t> rootsFound{0};
    std::atomic<uint32_t> dirsVisited{0};
    std::atomic<uint32_t> filesVisited{0};
    std::atomic<bool> cancelRequested{false};
    std::atomic<bool> truncated{false};
    void reset() {
        phase.store(0, std::memory_order_relaxed);
        appsTotal.store(0, std::memory_order_relaxed);
        appsCompleted.store(0, std::memory_order_relaxed);
        rootsFound.store(0, std::memory_order_relaxed);
        dirsVisited.store(0, std::memory_order_relaxed);
        filesVisited.store(0, std::memory_order_relaxed);
        cancelRequested.store(false, std::memory_order_relaxed);
        truncated.store(false, std::memory_order_relaxed);
    }
};

// Exact applications completed, not an estimate of time remaining.
// A long root can keep the number static; file/folder counters still advance.
inline uint32_t installedDataProgressPercent(uint32_t done, uint32_t total) {
    return total ? static_cast<uint32_t>(
        100ULL * (done > total ? total : done) / total) : 100;
}

class InstalledDataManager {
public:
    // Empty onlyTitleId scans all Homebrew; a non-empty Title ID only visits
    // that application's roots, while keeping *all* other catalog claims
    // for collision detection. timeBudgetUs is per scan, not per folder.
    static InstalledDataSnapshot scan(
        std::vector<InstalledDataApp> candidates,
        const std::string& onlyTitleId = {},
        InstalledDataScanProgress* progress = nullptr,
        uint64_t timeBudgetUs = 30000000ULL,
        bool measureSizes = true);
    // Persistent discovery index: the canonical catalog always owns routes.
    // Saved bytes are display-only; neither directory cache nor size can
    // authorize deletion. Explicit invalidation after known data changes.
    static void invalidateCachedTitle(const std::string& titleId);
    // Recalculate de-duplicated external bytes after a single app refresh.
    static uint64_t knownExternalBytes(const InstalledDataSnapshot& snapshot);
    // Revalidates the catalog snapshot and every path before deletion.
    // Never attempts ux0:app/ or PSVitaAlive-owned directories.
    static bool removeExternal(const InstalledDataSnapshot& snapshot,
                               const std::string& titleId);
    // Vita package manager only; no direct ux0:app recursive deletion.
    static bool uninstallPackage(const std::string& titleId);
};

}} // namespace psvitaalive::storage

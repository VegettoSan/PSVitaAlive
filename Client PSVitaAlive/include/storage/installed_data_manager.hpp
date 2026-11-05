#pragma once
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
};

class InstalledDataManager {
public:
    static InstalledDataSnapshot scan(std::vector<InstalledDataApp> candidates);
    // Revalidates the catalog snapshot and every path before deletion.
    // Never attempts ux0:app/ or PSVitaAlive-owned directories.
    static bool removeExternal(const InstalledDataSnapshot& snapshot,
                               const std::string& titleId);
    // Vita package manager only; no direct ux0:app recursive deletion.
    static bool uninstallPackage(const std::string& titleId);
};

}} // namespace psvitaalive::storage

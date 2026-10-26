#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace psvitaalive {
namespace storage {

struct UsageItem {
    std::string path;
    std::string label;
    uint64_t bytes = 0;
    bool savedZip = false;
    std::vector<std::string> ownedPaths;
};

struct UsageSnapshot {
    std::vector<UsageItem> items;
    uint64_t temporaryBytes = 0;
    uint64_t savedZipBytes = 0;
    uint64_t catalogBytes = 0; // read-only
    uint64_t imageBytes = 0;   // read-only
    uint64_t totalBytes() const { return temporaryBytes + savedZipBytes + catalogBytes + imageBytes; }
};

// Only PSVitaAlive-owned temporary files and deliberately saved ZIP archives.
// Catalog, image cache, config, logs, and installed applications are excluded.
class StorageMaintenance {
public:
    static UsageSnapshot scan(bool includeReadOnlyCache = true);
    // Refuses to delete a path that no longer appears in a fresh owned-file scan.
    static bool removeSelected(const UsageItem& selected);
    // Remove abandoned temporary work, never saved ZIPs or unknown user files.
    static uint32_t cleanupTransient();
    // Only numeric PSVitaAlive-generated PSVA_ZIP_STAGE directories are eligible.
    static bool removeOwnedZipStage(const std::string& path);
};
} // namespace storage
} // namespace psvitaalive

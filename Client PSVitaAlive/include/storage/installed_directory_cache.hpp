#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace psvitaalive { namespace storage {

// A persistent *directory discovery* index. Exact roots ALWAYS come from
// catalog links[].installed_paths, not this file. Sizes are optional
// historical display hints and must never grant permission to delete.
struct CachedDirectory {
    std::string titleId;
    std::string path;
    uint64_t rootStamp = 0;
    uint64_t bytes = 0;
    bool sizeKnown = false;
};

class InstalledDirectoryCache {
public:
    static const char* filePath();
    static bool load(std::vector<CachedDirectory>& entries);
    static bool save(const std::vector<CachedDirectory>& entries);
    static void invalidateTitle(const std::string& titleId);
    // Pure, host-testable serialization, with fail-closed validation.
    static bool encode(const std::vector<CachedDirectory>& entries,
                       std::string& out);
    static bool decode(const std::string& raw,
                       std::vector<CachedDirectory>& out);
    static const CachedDirectory* find(const std::vector<CachedDirectory>& entries,
                                       const std::string& titleId,
                                       const std::string& path);
};

}} // namespace psvitaalive::storage

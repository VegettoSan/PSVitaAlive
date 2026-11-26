#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_set>

namespace psvitaalive { namespace storage {

// Persistent read-only measurements. External roots ALWAYS come from
// catalog links[].installed_paths; package roots are exactly ux0:app/<title_id>.
// Saved bytes are historical hints only; NEVER a deletion authorization.
struct CachedDirectory {
    std::string titleId;
    std::string path;
    uint64_t rootStamp = 0;
    uint64_t bytes = 0;
    bool sizeKnown = false;
};

enum class DirectoryCacheScope { Homebrew, Vita };
struct CachedMedia {
    std::string path;
    uint64_t rootStamp=0,payloadStamp=0,bytes=0;
    bool directory=false,sizeKnown=false,validPbp=false;
    uint8_t platform=0;
    std::string title,id,contentId;
};
// A completed parent listing establishes absence, not a failed stat/open.
// Rows with uncertain observations (or an unvisited parent) survive on disk,
// but are never injected into the live inventory. New observations win by path.
template<class Row>
void reconcileCacheObservations(const std::vector<Row>& previous,
        std::vector<Row>& observed,
        const std::unordered_set<std::string>& verifiedParents,
        const std::unordered_set<std::string>& uncertainPaths) {
    std::unordered_set<std::string> keys;
    for(const auto& row:observed)keys.insert(row.path);
    for(const auto& row:previous) {
        const auto slash=row.path.find_last_of('/');
        const auto parent=row.path.substr(0,slash);
        if((!verifiedParents.count(parent)||uncertainPaths.count(row.path)) &&
           keys.insert(row.path).second)observed.push_back(row);
    }
}

class InstalledDirectoryCache {
public:
    static const char* filePath(DirectoryCacheScope scope=DirectoryCacheScope::Homebrew);
    static bool loadMedia(std::vector<CachedMedia>& entries);
    static bool saveMedia(const std::vector<CachedMedia>& entries);
    static bool encodeMedia(const std::vector<CachedMedia>& entries,std::string& out);
    static bool decodeMedia(const std::string& raw,std::vector<CachedMedia>& out);
    static void invalidateMediaPath(const std::string& path);
    static void invalidateMediaId(const std::string& id);
    static bool load(std::vector<CachedDirectory>& entries,DirectoryCacheScope scope=DirectoryCacheScope::Homebrew);
    static bool save(const std::vector<CachedDirectory>& entries,DirectoryCacheScope scope=DirectoryCacheScope::Homebrew);
    static void invalidateTitle(const std::string& titleId);
    // Only ux0:app/<titleId>; preserve external Game Files measurements.
    static void invalidatePackageTitle(const std::string& titleId,DirectoryCacheScope scope=DirectoryCacheScope::Homebrew);
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

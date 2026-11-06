#include "storage/installed_directory_cache.hpp"
#include "storage/installed_data_policy.hpp"
#include "storage/storage_manager.hpp"
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace psvitaalive { namespace storage {
namespace {
constexpr size_t MAX_ROWS = 1024;
constexpr size_t MAX_BYTES = 128 * 1024;
constexpr const char* MAGIC = "PSVA_DIR_INDEX_V1\n";
constexpr const char* PATH =
    "ux0:data/psvitaalive/cache/installed_directory_index.tsv";

bool readFile(const std::string& path, std::string& raw) {
    raw.clear();
    const SceUID file = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (file < 0) return false;
    char buffer[2048];
    int n = 0;
    bool ok = true;
    while ((n = sceIoRead(file, buffer, sizeof(buffer))) > 0) {
        if (raw.size() + static_cast<size_t>(n) > MAX_BYTES) {
            ok = false;
            break;
        }
        raw.append(buffer, static_cast<size_t>(n));
    }
    if (n < 0) ok = false;
    sceIoClose(file);
    if (!ok) raw.clear();
    return ok;
}
bool writeFile(const std::string& path, const std::string& raw) {
    const SceUID file = sceIoOpen(path.c_str(),
        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (file < 0) return false;
    size_t offset = 0;
    while (offset < raw.size()) {
        const int count = sceIoWrite(file, raw.data() + offset,
            static_cast<unsigned int>(raw.size() - offset));
        if (count <= 0) break;
        offset += static_cast<size_t>(count);
    }
    sceIoClose(file);
    return offset == raw.size();
}
} // namespace

const char* InstalledDirectoryCache::filePath() { return PATH; }

bool InstalledDirectoryCache::load(std::vector<CachedDirectory>& entries) {
    std::string raw;
    if (readFile(PATH, raw) && decode(raw, entries)) return true;
    // Incomplete power-loss writes are never trusted. A previous complete
    // version can be recovered without using it as a deletion authority.
    if (readFile(std::string(PATH) + ".bak", raw) &&
        decode(raw, entries)) return true;
    entries.clear();
    return false;
}
bool InstalledDirectoryCache::save(
    const std::vector<CachedDirectory>& entries) {
    std::string encoded;
    if (!encode(entries, encoded)) return false;
    std::string existing;
    if (readFile(PATH, existing) && existing == encoded) return true;
    StorageManager storage;
    if (!storage.createDirectories(StorageManager::CACHE_DIR)) return false;
    const std::string tmp = std::string(PATH) + ".tmp";
    const std::string backup = std::string(PATH) + ".bak";
    if (!writeFile(tmp, encoded)) {
        sceIoRemove(tmp.c_str());
        return false;
    }
    // Two-phase replacement: recover .bak if interrupted during rename.
    sceIoRemove(backup.c_str());
    const bool previous = sceIoRename(PATH, backup.c_str()) >= 0;
    if (sceIoRename(tmp.c_str(), PATH) < 0) {
        if (previous) sceIoRename(backup.c_str(), PATH);
        sceIoRemove(tmp.c_str());
        return false;
    }
    if (previous) sceIoRemove(backup.c_str());
    return true;
}
void InstalledDirectoryCache::invalidateTitle(const std::string& titleId) {
    if (!validInstalledTitleId(titleId)) return;
    std::vector<CachedDirectory> entries;
    if (!load(entries)) return;
    const size_t oldSize = entries.size();
    entries.erase(std::remove_if(entries.begin(), entries.end(),
        [&](const CachedDirectory& row) { return row.titleId == titleId; }),
        entries.end());
    if (oldSize != entries.size()) save(entries);
}

}} // namespace psvitaalive::storage

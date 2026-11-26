#pragma once
// Local-only installed media index. PKGj-compatible paths and PS1 PBP content IDs.
// No catalog JSON is modified; scan on demand and invalidate on catalog change.
#include "ui/ui_types.hpp"
#include "storage/installed_data_policy.hpp"
#include <vector>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace psvitaalive {
namespace ui {

struct InstalledGameIndex {
    bool initialized = false;
    bool appScanOk = false;
    std::unordered_set<std::string> liveAreaIds;
    // Preserve the first matching mount (ux0 preferred) without scanning on every UI frame.
    std::unordered_map<std::string, const char*> pspFolderIds, pspIsoIds, psxContentIds;
    // Populated only for the local storage worker, never the ordinary badge index.
    std::unordered_map<std::string, std::vector<std::string>> pspStoragePaths, ps1StoragePaths;

    static std::string normalize(std::string id) {
        if (id.size() != 9) return {};
        for (char& c : id) {
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return {};
        }
        return id;
    }
    static bool exists(const std::string& path) {
        SceIoStat st{};
        return sceIoGetstat(path.c_str(), &st) >= 0;
    }
    static uint32_t le32(const unsigned char* p) {
        return static_cast<uint32_t>(p[0]) |
               (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
    }
    // PKGj also reads the PSN content ID from PS1 PBP files. Their folder
    // can be named for DISC_ID rather than the catalog's PSN Title ID.
    static std::string psxTitleIdFromPbp(const std::string& path) {
        const SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
        if (fd < 0) return {};
        std::string id;
        unsigned char header[40]{};
        if (sceIoRead(fd, header, 40) == 40 &&
            std::memcmp(header, "\0PBP", 4) == 0) {
            const uint32_t dataPsp = le32(header + 32);
            const uint32_t dataPsar = le32(header + 36);
            char magic[8]{};
            if (dataPsp >= 40 && dataPsar > dataPsp &&
                sceIoLseek(fd, dataPsar, SCE_SEEK_SET) >= 0 &&
                sceIoRead(fd, magic, 8) == 8 &&
                (std::memcmp(magic, "PSISOIMG", 8) == 0 ||
                 std::memcmp(magic, "PSTITLEI", 8) == 0) &&
                sceIoLseek(fd, dataPsp, SCE_SEEK_SET) >= 0) {
                // A strict 36-byte Sony Content ID, not an arbitrary substring.
                unsigned char buf[2048]{};
                const uint32_t available = dataPsar - dataPsp;
                const int length = static_cast<int>(
                    available < sizeof(buf) ? available : sizeof(buf));
                const int n = sceIoRead(fd, buf, length);
                for (int i = 0; n >= 36 && i + 36 <= n; ++i) {
                    if (buf[i+6] != '-' || buf[i+16] != '_' || buf[i+19] != '-')
                        continue;
                    bool valid = true;
                    for (int j = 0; j < 6; ++j) {
                        const unsigned char c = buf[i+j];
                        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
                            valid = false; break;
                        }
                    }
                    if (!valid || buf[i+17] < '0' || buf[i+17] > '9' ||
                        buf[i+18] < '0' || buf[i+18] > '9') continue;
                    const std::string found = normalize(std::string(
                        reinterpret_cast<const char*>(buf + i + 7), 9));
                    if (!found.empty()) { id = found; break; }
                }
            }
        }
        sceIoClose(fd);
        return id;
    }

    // Unknown/custom PBP payloads are not deletion targets.
    static storage::GameMediaKind pbpStorageKind(const std::string& path) {
        using storage::GameMediaKind;
        const SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
        if (fd < 0) return GameMediaKind::None;
        GameMediaKind kind = GameMediaKind::None;
        unsigned char header[40]{};
        if (sceIoRead(fd, header, 40) == 40 &&
            std::memcmp(header, "\0PBP", 4) == 0) {
            const uint32_t psar = le32(header + 36);
            char magic[8]{};
            if (psar >= 40 && sceIoLseek(fd, psar, SCE_SEEK_SET) >= 0 &&
                sceIoRead(fd, magic, 8) == 8) {
                if (std::memcmp(magic, "NPUMDIMG", 8) == 0)
                    kind = GameMediaKind::Psp;
                else if (std::memcmp(magic, "PSISOIMG", 8) == 0 ||
                         std::memcmp(magic, "PSTITLEI", 8) == 0)
                    kind = GameMediaKind::Ps1;
            }
        }
        sceIoClose(fd);
        return kind;
    }
    // Recheck the actual media type and identity, including PS1 embedded
    // content ID when the directory is named for a different disc ID.
    static bool storagePathMatches(storage::GameMediaKind kind,
                                   const std::string& id,
                                   const std::string& path) {
        using storage::GameMediaKind;
        if (!storage::safeGameMediaPath(kind, id, path)) return false;
        SceIoStat st{};
        if (sceIoGetstat(path.c_str(), &st) < 0) return false;
        const std::string folder = storage::gameMediaChild(path, "PSP/GAME/");
        if (folder.empty())
            return kind == GameMediaKind::Psp && !(st.st_mode & SCE_S_IFDIR) &&
                   st.st_size > 0;
        if (!(st.st_mode & SCE_S_IFDIR)) return false;
        const std::string pbp = path + "/EBOOT.PBP";
        if (pbpStorageKind(pbp) != kind) return false;
        if (kind == GameMediaKind::Ps1) {
            const std::string embedded = psxTitleIdFromPbp(pbp);
            if (!embedded.empty()) return embedded == id;
        }
        return normalize(folder) == id;
    }
    const std::vector<std::string>* storagePathsFor(
        storage::GameMediaKind kind, const std::string& id) const {
        if (kind == storage::GameMediaKind::None) return nullptr;
        const auto& paths = kind == storage::GameMediaKind::Psp ?
            pspStoragePaths : ps1StoragePaths;
        const auto it = paths.find(id);
        return it == paths.end() ? nullptr : &it->second;
    }

    void invalidate() {
        initialized = false; appScanOk = false;
        liveAreaIds.clear(); pspFolderIds.clear();
        pspIsoIds.clear(); psxContentIds.clear();
        pspStoragePaths.clear(); ps1StoragePaths.clear();
    }

    void refresh(bool collectStoragePaths = false, bool scanAdrenaline = true) {
        if (initialized) return;
        initialized = true;
        SceUID fd = sceIoDopen("ux0:app");
        if (fd >= 0) {
            appScanOk = true;
            SceIoDirent e{};
            while (sceIoDread(fd, &e) > 0) {
                const std::string id = normalize(e.d_name);
                if (!id.empty()) liveAreaIds.insert(id);
                e = SceIoDirent{};
            }
            sceIoDclose(fd);
        }
        if (!scanAdrenaline) return;
        static const char* roots[] = {"ux0", "uma0", "imc0", "xmc0"};
        for (const char* root : roots) {
            const std::string base = std::string(root) + ":pspemu/";
            const std::string gameDir = base + "PSP/GAME";
            SceUID games = sceIoDopen(gameDir.c_str());
            if (games >= 0) {
                SceIoDirent e{};
                while (sceIoDread(games, &e) > 0) {
                    const std::string name = e.d_name;
                    if (name != "." && name != "..") {
                        const std::string pbp = gameDir + "/" + name + "/EBOOT.PBP";
                        if (exists(pbp)) {
                            const std::string id = normalize(name);
                            if (!id.empty()) pspFolderIds.emplace(id, root);
                            const std::string psx = psxTitleIdFromPbp(pbp);
                            if (!psx.empty()) psxContentIds.emplace(psx, root);
                            if (collectStoragePaths && storage::validGameMediaName(name)) {
                                const auto kind = pbpStorageKind(pbp);
                                const std::string folderPath = gameDir + "/" + name;
                                if (kind == storage::GameMediaKind::Psp && !id.empty() &&
                                    storagePathMatches(kind, id, folderPath))
                                    pspStoragePaths[id].push_back(folderPath);
                                else if (kind == storage::GameMediaKind::Ps1) {
                                    const std::string resolved = psx.empty() ? id : psx;
                                    if (!resolved.empty() && storagePathMatches(kind, resolved, folderPath))
                                        ps1StoragePaths[resolved].push_back(folderPath);
                                }
                            }
                        }
                    }
                    e = SceIoDirent{};
                }
                sceIoDclose(games);
            }
            SceUID iso = sceIoDopen((base + "ISO").c_str());
            if (iso >= 0) {
                SceIoDirent e{};
                while (sceIoDread(iso, &e) > 0) {
                    std::string name = e.d_name;
                    for (char& c : name)
                        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
                    const size_t dot = name.find_last_of('.');
                    if (dot != std::string::npos &&
                        (name.substr(dot) == ".ISO" || name.substr(dot) == ".CSO")) {
                        const std::string stem = name.substr(0, dot);
                        std::string id = normalize(stem);
                        // PSVitaAlive installs ISO as "<name> [<ID>].iso".
                        if (id.empty() && stem.size() >= 11 && stem.back() == ']') {
                            const size_t open = stem.size() - 11;
                            if (stem[open] == '[')
                                id = normalize(stem.substr(open + 1, 9));
                        }
                        if (!id.empty()) {
                            pspIsoIds.emplace(id, root);
                            if (collectStoragePaths) {
                                const std::string path = base + "ISO/" + e.d_name;
                                if (storagePathMatches(storage::GameMediaKind::Psp, id, path))
                                    pspStoragePaths[id].push_back(path);
                            }
                        }
                    }
                    e = SceIoDirent{};
                }
                sceIoDclose(iso);
            }
        }
    }

    // Returns a stable mount label, or nullptr if not installed.
    // Scans remain cached and are invalidated only by the existing UI flow.
    const char* locationFor(CatalogType catalog, const std::string& primary,
                            const std::string& alternate) {
        refresh();
        auto match = [&](const std::string& raw) -> const char* {
            const std::string id = normalize(raw);
            if (id.empty()) return nullptr;
            if (liveAreaIds.count(id)) return "ux0";
            if (!appScanOk && exists("ux0:app/" + id + "/sce_sys/param.sfo"))
                return "ux0";
            if (catalog == CatalogType::PspGames) {
                const auto folder = pspFolderIds.find(id);
                if (folder != pspFolderIds.end()) return folder->second;
                const auto iso = pspIsoIds.find(id);
                if (iso != pspIsoIds.end()) return iso->second;
            }
            if (catalog == CatalogType::Ps1Games) {
                const auto folder = pspFolderIds.find(id);
                if (folder != pspFolderIds.end()) return folder->second;
                const auto psx = psxContentIds.find(id);
                if (psx != psxContentIds.end()) return psx->second;
            }
            return nullptr;
        };
        if (const char* mount = match(primary)) return mount;
        size_t start = 0;
        while (start < alternate.size()) {
            const size_t sep = alternate.find('|', start);
            const size_t end = sep == std::string::npos ? alternate.size() : sep;
            if (const char* mount = match(alternate.substr(start, end - start)))
                return mount;
            if (sep == std::string::npos) break;
            start = sep + 1;
        }
        return nullptr;
    }

    bool contains(CatalogType catalog, const std::string& primary,
                  const std::string& alternate) {
        return locationFor(catalog, primary, alternate) != nullptr;
    }
};

inline InstalledGameIndex& installedGameIndex() {
    static InstalledGameIndex idx;
    return idx;
}

} // namespace ui
} // namespace psvitaalive

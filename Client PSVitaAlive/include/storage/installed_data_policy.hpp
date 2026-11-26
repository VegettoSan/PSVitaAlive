#pragma once
// Pure, host-testable security decisions. Never trust a catalog path as
// filesystem deletion authority without rechecking these rules at execution.
#include <cctype>
#include <string>
#include <vector>
#include "storage/installed_data_manager.hpp"

namespace psvitaalive { namespace storage {

inline bool validInstalledTitleId(const std::string& titleId) {
    if (titleId.size() != 9) return false;
    for (unsigned char c : titleId)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

inline bool validInstalledRelativePath(const std::string& path) {
    if (path.empty() || path.size() > 200 || path.front() == '/' ||
        path.back() == '/' || path.find('\\') != std::string::npos ||
        path.find(':') != std::string::npos) return false;
    size_t begin = 0;
    unsigned count = 0;
    while (begin < path.size()) {
        const size_t end = path.find('/', begin);
        const std::string part = path.substr(begin, end == std::string::npos ?
            std::string::npos : end - begin);
        if (part.empty() || part == "." || part == ".." || ++count > 16)
            return false;
        for (unsigned char c : part)
            if (c < 32 || c == 127) return false;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    // Never grant a catalog link ownership over PSVitaAlive, settings, system
    // or recovery/staging directories even if metadata happens to name them.
    std::string first = path.substr(0, path.find('/'));
    for (char& c : first) c = static_cast<char>(std::tolower(
        static_cast<unsigned char>(c)));
    if (first == "psvitaalive" || first == "psva_vpk" ||
        first == "tai" || first == "temp" || first == "tmp" ||
        first == "sce_sys" || first == "app" || first == "license" ||
        first.compare(0, 15, "psva_zip_stage_") == 0) return false;
    return true;
}

inline bool safeExternalDataPath(const std::string& path) {
    constexpr const char* prefix = "ux0:data/";
    if (path.compare(0, 9, prefix) != 0) return false;
    return validInstalledRelativePath(path.substr(9));
}

// A Data Files dependency on ur0:data is outside this inspector's
// deletion scope. It must never become an ux0 root or veto independent
// Game Files. Conflicting installed_paths and malformed paths still fail
// closed through the normal routing checks.
inline bool installedDataPreservesUr0Dependency(const std::string& extract,
                                                bool isData,
                                                bool hasInstalledPaths) {
    if (!isData || hasInstalledPaths) return false;
    if (extract == "ur0:data" || extract == "ur0:data/") return true;
    if (extract.compare(0, 9, "ur0:data/") != 0) return false;
    std::string relative = extract.substr(9);
    if (!relative.empty() && relative.back() == '/') relative.pop_back();
    return validInstalledRelativePath(relative);
}

// The installer may extract directly into the declared data root, or into
// one of its subfolders. Different prefixes such as game and game2 are distinct.
inline bool installedDataExtractWithinRoot(const std::string& extract,
                                           const std::string& root) {
    return extract == "ux0:data/" || extract == root ||
        (extract.size() > root.size() &&
         extract.compare(0, root.size(), root) == 0 &&
         extract[root.size()] == '/');
}

// Alternative mirrors cannot establish ownership over multiple independent
// directories merely because all happen to exist on the card.
inline bool installedDataAlternativesAmbiguous(bool hasAlternatives,
                                               unsigned existingRootCount) {
    return hasAlternatives && existingRootCount > 1;
}

// A matching catalog path alone is not proof that two apps are installed.
// It still matters for informed deletion: the user can see every claimant.
inline bool installedDataSharedDeletePermitted(bool confirmed, bool hasOverlap) {
    return !hasOverlap || confirmed;
}

// Component-aligned collision test. "game" does not overlap "game2".
inline bool installedPathsOverlap(const std::string& a,
                                 const std::string& b) {
    return a == b ||
        (a.size() > b.size() && a.compare(0, b.size(), b) == 0 &&
         a[b.size()] == '/') ||
        (b.size() > a.size() && b.compare(0, a.size(), a) == 0 &&
         b[a.size()] == '/');
}

// Directory-entry names are *inside* an already-validated ux0:data/ root.
// Do not treat nested 'tai', 'temp' or 'sce_sys' as protected root names:
// those are legitimate child filenames inside many game-data archives.
inline bool validInstalledChildName(const std::string& name) {
    if (name.empty() || name.size() > 255 ||
        name == "." || name == "..") return false;
    for (unsigned char c : name)
        if (c < 32 || c == 127 || c == '/' || c == '\\' || c == ':')
            return false;
    return true;
}

// Separate commercial-media policy: no change to the Homebrew data allowlist.
inline std::string normalizeGameMediaId(std::string id) {
    for (char& c : id)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return validInstalledTitleId(id) ? id : std::string();
}
inline bool validGameMediaName(const std::string& name) {
    return validInstalledChildName(name) && name.back() != '.' && name.back() != ' ';
}
inline std::string gameMediaIsoId(std::string name) {
    if (!validGameMediaName(name)) return {};
    for (char& c : name)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos ||
        (name.substr(dot) != ".ISO" && name.substr(dot) != ".CSO")) return {};
    const std::string stem = name.substr(0, dot);
    std::string id = normalizeGameMediaId(stem);
    if (id.empty() && stem.size() >= 11 && stem.back() == ']' &&
        stem[stem.size() - 11] == '[')
        id = normalizeGameMediaId(stem.substr(stem.size() - 10, 9));
    return id;
}
inline std::string gameMediaChild(const std::string& path, const char* section) {
    static const char* mounts[] = {"ux0", "uma0", "imc0", "xmc0"};
    for (const char* mount : mounts) {
        const std::string prefix = std::string(mount) + ":pspemu/" + section;
        if (path.compare(0, prefix.size(), prefix) != 0) continue;
        const std::string name = path.substr(prefix.size());
        return validGameMediaName(name) ? name : std::string();
    }
    return {};
}
inline bool safeGameMediaPath(GameMediaKind kind, const std::string& titleId,
                              const std::string& path) {
    if (!validInstalledTitleId(titleId) || titleId == "PSVAS1178" ||
        kind == GameMediaKind::None) return false;
    const std::string folder = gameMediaChild(path, "PSP/GAME/");
    if (!folder.empty())
        return kind == GameMediaKind::Ps1 || normalizeGameMediaId(folder) == titleId;
    if (kind != GameMediaKind::Psp) return false;
    const std::string iso = gameMediaChild(path, "ISO/");
    return !iso.empty() && gameMediaIsoId(iso) == titleId;
}
inline bool installedDataAllowsPath(const InstalledDataApp& app,
                                    const std::string& path) {
    return app.commercial ? safeGameMediaPath(app.mediaKind, app.titleId, path)
                          : safeExternalDataPath(path);
}

// A catalog collision by itself is NOT proof that two installed apps share
// data. This predicate is only part of the destructive-operation preflight.
inline bool installedDataActiveCollision(const std::string& target,
                                         const std::string& other,
                                         bool otherInstalled) {
    return otherInstalled && installedPathsOverlap(target, other);
}

// A known root is deletable even without a VPK, unless another installed VPK
// is actively claiming that same root. Metadata ambiguity and unsafe paths
// remain blocked independently of directory size measurement.
inline bool installedDataCanOfferExternalDelete(const InstalledDataApp& app) {
    if (app.blocked || app.unresolved ||
        !validInstalledTitleId(app.titleId) || app.titleId == "PSVAS1178" ||
        app.appId == "psvitaalive" || app.appId == "ps-vita-alive") return false;
    bool anyPresent = false;
    for (const auto& p : app.paths) {
        if (!p.exists) continue;
        if (!installedDataAllowsPath(app, p.path)) return false;
        anyPresent = true;
    }
    return anyPresent;
}

// Read-only, cached collision hint for the UI. The deletion preflight still
// checks actual package presence and root overlap immediately before use.
inline bool installedDataHasActiveShare(const InstalledDataSnapshot& snapshot,
                                       const InstalledDataApp& target) {
    for (const auto& p : target.paths) {
        if (!p.exists || !installedDataAllowsPath(target, p.path)) continue;
        for (const auto& other : snapshot.apps) {
            if (other.titleId == target.titleId || !other.packagePresent ||
                !validInstalledTitleId(other.titleId)) continue;
            for (const auto& q : other.paths)
                if (installedDataAllowsPath(other, q.path) &&
                    installedPathsOverlap(p.path, q.path)) return true;
        }
    }
    return false;
}

struct InstalledDataShareInfo {
    std::string titleId;
    std::string name;
    bool installed = false;
};

// Return one record per different title, including titles without installed
// VPKs. Called when entering the delete confirmation, not every frame.
inline std::vector<InstalledDataShareInfo> installedDataCatalogShares(
    const InstalledDataSnapshot& snapshot, const InstalledDataApp& target) {
    std::vector<InstalledDataShareInfo> found;
    for (const auto& other : snapshot.apps) {
        if (other.titleId == target.titleId ||
            !validInstalledTitleId(other.titleId)) continue;
        bool overlap = false;
        for (const auto& p : target.paths) {
            if (!p.exists || !installedDataAllowsPath(target, p.path)) continue;
            for (const auto& q : other.paths) {
                if (installedDataAllowsPath(other, q.path) &&
                    installedPathsOverlap(p.path, q.path)) {
                    overlap = true;
                    break;
                }
            }
            if (overlap) break;
        }
        if (!overlap) continue;
        bool alreadyAdded = false;
        for (const auto& row : found)
            if (row.titleId == other.titleId) { alreadyAdded = true; break; }
        if (!alreadyAdded)
            found.push_back({other.titleId, other.name, other.packagePresent});
    }
    return found;
}
inline bool installedDataHasCatalogShare(const InstalledDataSnapshot& snapshot,
                                        const InstalledDataApp& target) {
    for (const auto& other : snapshot.apps) {
        if (other.titleId == target.titleId ||
            !validInstalledTitleId(other.titleId)) continue;
        for (const auto& p : target.paths) {
            if (!p.exists || !installedDataAllowsPath(target, p.path)) continue;
            for (const auto& q : other.paths)
                if (installedDataAllowsPath(other, q.path) &&
                    installedPathsOverlap(p.path, q.path)) return true;
        }
    }
    return false;
}

// UI wording must reflect the observed installed roots, not just the
// existence of a Game Files link in the catalog. Pure and host-testable.
inline const char* installedDataActionNoteKey(int action, bool hasExternal) {
    if (action == 0)
        return hasExternal ? "ID_DELETE_APP_NOTE" : "ID_DELETE_APP_ONLY_NOTE";
    if (action == 1) return "ID_DELETE_FILES_NOTE";
    if (action == 2) return "ID_DELETE_ALL_NOTE";
    return "ID_UNSAFE";
}
inline const char* installedDataConfirmImpactKey(int action, bool hasExternal) {
    if (action == 0)
        return hasExternal ? "ID_CONFIRM_IMPACT_APP_KEEP"
                           : "ID_CONFIRM_IMPACT_APP_ONLY";
    if (action == 1) return "ID_CONFIRM_IMPACT_FILES_ONLY";
    if (action == 2) return "ID_CONFIRM_IMPACT_ALL";
    return "ID_UNSAFE";
}

// Menu entries use stable action IDs (0 APP, 1 Game Files, 2 both).
// All input methods and rendering must consult these same predicates.
inline bool installedDataActionAvailable(int action, bool canApp, bool canData) {
    return action == 0 ? canApp :
           action == 1 ? canData :
           action == 2 ? (canApp && canData) : false;
}
// User-facing order: BOTH, APP, GAME FILES. Stable internal IDs stay 2,0,1.
inline int installedDataVisualAction(unsigned position) {
    static const int order[3] = {2, 0, 1};
    return position < 3 ? order[position] : -1;
}
inline int installedDataFirstAction(bool canApp, bool canData) {
    for (unsigned i = 0; i < 3; ++i) {
        const int action = installedDataVisualAction(i);
        if (installedDataActionAvailable(action, canApp, canData)) return action;
    }
    return -1;
}
inline int installedDataActionSlot(int action, bool canApp, bool canData) {
    int slot = 0;
    for (unsigned i = 0; i < 3; ++i) {
        const int candidate = installedDataVisualAction(i);
        if (!installedDataActionAvailable(candidate, canApp, canData)) continue;
        if (candidate == action) return slot;
        ++slot;
    }
    return -1;
}
inline int installedDataMoveAction(int action, int delta,
                                   bool canApp, bool canData) {
    if (!installedDataActionAvailable(action, canApp, canData))
        return installedDataFirstAction(canApp, canData);
    int index = 0;
    for (unsigned i = 0; i < 3; ++i)
        if (installedDataVisualAction(i) == action) index = static_cast<int>(i);
    for (int step = 1; step <= 3; ++step) {
        const int next = installedDataVisualAction(
            static_cast<unsigned>((index + 3 + step * (delta >= 0 ? 1 : 2)) % 3));
        if (installedDataActionAvailable(next, canApp, canData)) return next;
    }
    return -1;
}

}} // namespace psvitaalive::storage

#pragma once
// Pure, host-testable security decisions. Never trust a catalog path as
// filesystem deletion authority without rechecking these rules at execution.
#include <cctype>
#include <string>
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

// Explicit warning acceptance + installed-target proof are both required.
// targetJustUninstalled is only set after successful PromoterUtility uninstall
// within the same preflighted "delete both" operation.
inline bool installedDataSharedDeletePermitted(bool confirmed,
                                               bool targetWasInstalled,
                                               bool targetPresentNow,
                                               bool targetJustUninstalled) {
    return confirmed && targetWasInstalled &&
        (targetPresentNow || targetJustUninstalled);
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
        app.appId == "psvitaalive" || app.appId == "ps-vita-alive" ||
        (app.activeShared && !app.packagePresent)) return false;
    bool anyPresent = false;
    for (const auto& p : app.paths) {
        if (!p.exists) continue;
        if (!safeExternalDataPath(p.path)) return false;
        anyPresent = true;
    }
    return anyPresent;
}

// Read-only, cached collision hint for the UI. The deletion preflight still
// checks actual package presence and root overlap immediately before use.
inline bool installedDataHasActiveShare(const InstalledDataSnapshot& snapshot,
                                       const InstalledDataApp& target) {
    for (const auto& p : target.paths) {
        if (!p.exists || !safeExternalDataPath(p.path)) continue;
        for (const auto& other : snapshot.apps) {
            if (other.titleId == target.titleId || !other.packagePresent ||
                !validInstalledTitleId(other.titleId)) continue;
            for (const auto& q : other.paths)
                if (safeExternalDataPath(q.path) &&
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

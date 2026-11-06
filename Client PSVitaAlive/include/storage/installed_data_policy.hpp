#pragma once
// Pure, host-testable security decisions. Never trust a catalog path as
// filesystem deletion authority without rechecking these rules at execution.
#include <cctype>
#include <string>

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

// Menu entries use stable action IDs (0 APP, 1 Game Files, 2 both).
// All input methods and rendering must consult these same predicates.
inline bool installedDataActionAvailable(int action, bool canApp, bool canData) {
    return action == 0 ? canApp :
           action == 1 ? canData :
           action == 2 ? (canApp && canData) : false;
}
inline int installedDataFirstAction(bool canApp, bool canData) {
    for (int a = 0; a < 3; ++a)
        if (installedDataActionAvailable(a, canApp, canData)) return a;
    return -1;
}
inline int installedDataActionSlot(int action, bool canApp, bool canData) {
    int slot = 0;
    for (int a = 0; a < 3; ++a) {
        if (!installedDataActionAvailable(a, canApp, canData)) continue;
        if (a == action) return slot;
        ++slot;
    }
    return -1;
}
inline int installedDataMoveAction(int action, int delta,
                                   bool canApp, bool canData) {
    if (!installedDataActionAvailable(action, canApp, canData))
        return installedDataFirstAction(canApp, canData);
    for (int step = 1; step <= 3; ++step) {
        const int next = (action + 3 + step * (delta >= 0 ? 1 : 2)) % 3;
        if (installedDataActionAvailable(next, canApp, canData))
            return next;
    }
    return -1;
}

}} // namespace psvitaalive::storage

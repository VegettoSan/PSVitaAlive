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

}} // namespace psvitaalive::storage

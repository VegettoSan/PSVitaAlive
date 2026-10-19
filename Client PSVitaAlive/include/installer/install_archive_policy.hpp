#pragma once

#include <cctype>
#include <string>

namespace psvitaalive {

// Catalog type, not ZIP magic, is authoritative for payload-only downloads.
inline bool isDataOrGameFilesLinkType(const std::string& rawType) {
    std::string normalized;
    normalized.reserve(rawType.size());
    bool previousSpace = true;
    for (unsigned char c : rawType) {
        if (std::isspace(c) || c == '_' || c == '-') {
            if (!previousSpace) normalized.push_back(' ');
            previousSpace = true;
        } else {
            normalized.push_back(static_cast<char>(std::tolower(c)));
            previousSpace = false;
        }
    }
    if (!normalized.empty() && normalized.back() == ' ') normalized.pop_back();
    return normalized == "game files" || normalized == "game file" ||
           normalized == "data files" || normalized == "data file";
}

// A ZIP with a chosen destination is always extracted, even when VPK markers
// inside the archive make FormatDetector return Vpk. Opaque download names
// are supported for explicitly typed Game Files / Data Files only.
inline bool shouldExtractZipArchive(const std::string& extension,
                                   bool hasDestination,
                                   bool gameOrDataFiles,
                                   bool detectedZipContainer) {
    if (!hasDestination) return false;
    if (extension == "zip") return true;
    // An explicitly .vpk file must not be silently extracted as game data.
    if (extension == "vpk") return false;
    return gameOrDataFiles && detectedZipContainer;
}

} // namespace psvitaalive

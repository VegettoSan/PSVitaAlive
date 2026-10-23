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

// This override applies ONLY to Game Files / Data Files. Every other link type
// retains the installer's previous format-detection behavior (including ZIP
// releases containing VPKs). Typed data archives may have opaque URL names.
inline bool shouldExtractZipArchive(const std::string& extension,
                                   bool hasDestination,
                                   bool gameOrDataFiles,
                                   bool detectedZipContainer) {
    if (!gameOrDataFiles || !hasDestination) return false;
    // A file explicitly named .vpk must not be promoted or extracted as data.
    if (extension == "vpk") return false;
    // For typed data, .zip wins over any inferred VPK layout; ZipExtractor
    // will still reject a corrupt or non-ZIP payload.
    if (extension == "zip") return true;
    // Signed redirects / opaque filenames: require recognizable ZIP magic.
    return detectedZipContainer;
}

// Opt in to verified, on-disk ranges for ZIP-container payloads only.
// VPK files are ZIP containers; the VPK promoter still receives an extracted
// *directory*, not range parts. Opaque/redirect names keep serial/join fallback.
// PKG, PSP/PS1 payloads and plugins must retain their established code paths.
inline bool shouldKeepSegmentedContainer(const std::string& extension,
                                         bool systemBgdl, bool plugin,
                                         bool gameOrDataFiles,
                                         bool zipExtraction,
                                         bool hasDestination) {
    if (systemBgdl || plugin) return false;
    if (gameOrDataFiles)
        return zipExtraction && hasDestination && extension == "zip";
    return !hasDestination && extension == "vpk";
}

} // namespace psvitaalive

#pragma once
#include <cstdint>
#include <limits>
#include <string>

namespace psvitaalive {

// The catalog hint is the size of the DOWNLOAD, not an extraction guarantee.
enum class InstallSpaceMode {
    DirectPackage, // VPK and direct PSP/PS1 PKG: preserve conservative 2.1x
    ZipArchive,    // compressed payload + modest overhead; ZipExtractor checks uncompressed
    Plugin,        // payload on ux0, destination typically ur0
    SystemBgdl     // console enqueues and manages PKG disk space
};

inline InstallSpaceMode selectInstallSpaceMode(bool systemBgdl, bool zipExtraction, bool plugin) {
    if (systemBgdl) return InstallSpaceMode::SystemBgdl;
    if (plugin) return InstallSpaceMode::Plugin;
    if (zipExtraction) return InstallSpaceMode::ZipArchive;
    return InstallSpaceMode::DirectPackage;
}

inline uint64_t saturatingSpaceAdd(uint64_t a, uint64_t b) {
    return b > std::numeric_limits<uint64_t>::max() - a
        ? std::numeric_limits<uint64_t>::max() : a + b;
}

inline uint64_t preflightRequiredSpaceBytes(uint64_t payload, InstallSpaceMode mode) {
    if (!payload || mode == InstallSpaceMode::SystemBgdl) return 0;
    if (mode == InstallSpaceMode::Plugin)
        return saturatingSpaceAdd(payload, 4ULL * 1024 * 1024);
    if (mode == InstallSpaceMode::ZipArchive)
        return saturatingSpaceAdd(saturatingSpaceAdd(payload, payload / 20ULL),
                                  32ULL * 1024 * 1024);
    // Existing 2.1x VPK/direct PKG policy without overflow.
    const uint64_t q = payload / 10ULL;
    const uint64_t r = (payload % 10ULL) * 21ULL / 10ULL;
    if (q > (std::numeric_limits<uint64_t>::max() - r) / 21ULL)
        return std::numeric_limits<uint64_t>::max();
    return q * 21ULL + r;
}

// Combined Install All budget. Catalog sizes describe compressed download bytes;
// the ZIP extractor still checks true uncompressed growth before extracting.
// largestExtra is the maximum Game/Data candidate in the catalog. If BOTH a
// Game Files and a Data Files payload are selected, selectedExtraSum is larger
// and protects the combined installation from an undersized MAX-only estimate.
// A 25% reserve + 128 MiB accommodates transient files and filesystem churn.
// The 2.1x VPK minimum retains the original direct-package install safeguard.
inline uint64_t installAllRequiredSpaceBytes(
    uint64_t appBytes, uint64_t largestExtraBytes,
    uint64_t selectedExtraSumBytes, uint64_t pluginsBytes = 0) {
    const uint64_t extraBytes = largestExtraBytes > selectedExtraSumBytes
        ? largestExtraBytes : selectedExtraSumBytes;
    const uint64_t payload = saturatingSpaceAdd(
        saturatingSpaceAdd(appBytes, extraBytes), pluginsBytes);
    if (!payload) return 0;
    constexpr uint64_t BASE_RESERVE = 128ULL * 1024 * 1024;
    const uint64_t conservative = saturatingSpaceAdd(
        saturatingSpaceAdd(payload, payload / 4), BASE_RESERVE);
    const uint64_t vpkFloor = saturatingSpaceAdd(
        saturatingSpaceAdd(preflightRequiredSpaceBytes(appBytes, InstallSpaceMode::DirectPackage),
                           extraBytes),
        saturatingSpaceAdd(pluginsBytes, BASE_RESERVE));
    return conservative > vpkFloor ? conservative : vpkFloor;
}

// Top-level app.size describes the PRIMARY download, never a secondary Data ZIP.
inline uint64_t chooseSelectedLinkSizeBytes(uint64_t linkBytes, uint64_t appBytes,
                                             const std::string& linkUrl,
                                             const std::string& primaryUrl) {
    if (linkBytes) return linkBytes;
    return !linkUrl.empty() && linkUrl == primaryUrl ? appBytes : 0;
}
} // namespace psvitaalive

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

// Once the HTTP response exposes an authoritative size, re-check the
// remaining space before its body is written. Existing bytes in a resumable
// job are already on disk and must not be counted twice.
inline uint64_t remainingInstallSpaceBytes(uint64_t remoteBytes,
                                           uint64_t existingBytes,
                                           InstallSpaceMode mode) {
    const uint64_t required = preflightRequiredSpaceBytes(remoteBytes, mode);
    return existingBytes >= required ? 0 : required - existingBytes;
}

// Install All uses ONLY the links enqueued for this installation. This is a
// compressed-download estimate; ZipExtractor still checks the true expanded
// size before writing data. The 25% + 128 MiB margin and the old 2.1x VPK
// staging floor are deliberately unchanged.
inline uint64_t installAllRequiredSpaceBytes(
    uint64_t appBytes, uint64_t extraBytes, uint64_t pluginsBytes = 0) {
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

// A queue entry is added only after an actionable link is selected (automatically
// or manually). Keep the classification explicit; an ordinary "Download" link
// can have a game/data-sounding name, and category alternatives aren't queued.
enum class InstallAllSpaceLinkKind { App, Extra, Plugin };

struct InstallAllSpaceBudget {
    uint64_t appBytes = 0;
    uint64_t extraBytes = 0;
    uint64_t pluginBytes = 0;
    unsigned unknownSizeCount = 0; // Missing catalog hints cannot prove capacity.
};

inline void addInstallAllSpaceEntry(InstallAllSpaceBudget& budget,
                                   InstallAllSpaceLinkKind kind, uint64_t bytes) {
    if (!bytes) ++budget.unknownSizeCount;
    uint64_t* sum = &budget.extraBytes;
    if (kind == InstallAllSpaceLinkKind::App) sum = &budget.appBytes;
    else if (kind == InstallAllSpaceLinkKind::Plugin) sum = &budget.pluginBytes;
    *sum = saturatingSpaceAdd(*sum, bytes);
}

inline uint64_t installAllQueueRequiredSpaceBytes(const InstallAllSpaceBudget& budget) {
    return installAllRequiredSpaceBytes(
        budget.appBytes, budget.extraBytes, budget.pluginBytes);
}

// Top-level app.size describes the PRIMARY download, never a secondary Data ZIP.
inline uint64_t chooseSelectedLinkSizeBytes(uint64_t linkBytes, uint64_t appBytes,
                                             const std::string& linkUrl,
                                             const std::string& primaryUrl) {
    if (linkBytes) return linkBytes;
    return !linkUrl.empty() && linkUrl == primaryUrl ? appBytes : 0;
}
} // namespace psvitaalive

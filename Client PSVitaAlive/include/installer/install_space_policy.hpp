#pragma once
#include <cstdint>
#include <limits>
#include <string>

namespace psvitaalive {

// A catalog size is the downloaded package size. For PSVitaAlive STORE ZIP
// archives both the package and extracted files occupy ux0 concurrently.
// The ZIP central directory is not known until after downloading; compressed
// archives with larger expansion still require the extractor's safety guard.
// Use saturating budgets throughout to avoid wraparound.
constexpr uint64_t kSpaceMiB = 1024ULL * 1024ULL;
constexpr uint64_t kSpaceZipReserve = 64ULL * kSpaceMiB;
constexpr uint64_t kSpaceInstallAllReserve = 128ULL * kSpaceMiB;
constexpr uint64_t kSpaceUnknownZipReserve = 256ULL * kSpaceMiB;
constexpr uint64_t kSpaceUnknownDirectReserve = 64ULL * kSpaceMiB;
constexpr uint64_t kSpaceUnknownPluginReserve = 16ULL * kSpaceMiB;
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
    if (!payload) return 0;
    if (mode == InstallSpaceMode::Plugin)
        return saturatingSpaceAdd(payload, 4ULL * kSpaceMiB);
    if (mode == InstallSpaceMode::ZipArchive) {
        // A STORE ZIP leaves both the downloaded ZIP/ranges and extracted files
        // resident at peak usage; add 25% for filesystem allocations/metadata.
        return saturatingSpaceAdd(
            saturatingSpaceAdd(saturatingSpaceAdd(payload, payload), payload / 4ULL),
            kSpaceZipReserve);
    }
    // Direct VPK, PSP/PS1 packages and OS-managed BGDL PKG all need a
    // pre-download availability check. BGDL keeps its own final safeguards.
    // Existing 2.1x VPK/direct PKG policy without overflow.
    const uint64_t q = payload / 10ULL;
    const uint64_t r = (payload % 10ULL) * 21ULL / 10ULL;
    if (q > (std::numeric_limits<uint64_t>::max() - r) / 21ULL)
        return std::numeric_limits<uint64_t>::max();
    return q * 21ULL + r;
}

// For a missing catalog size, block a nearly full card BEFORE queueing.
// The server's Content-Length/Content-Range, when available, replaces the
// estimate before the first body byte. Unknown size can never guarantee
// enough space for arbitrarily large files without remote metadata.
inline uint64_t initialInstallSpaceBytes(uint64_t payload, InstallSpaceMode mode) {
    if (payload) return preflightRequiredSpaceBytes(payload, mode);
    if (mode == InstallSpaceMode::ZipArchive) return kSpaceUnknownZipReserve;
    if (mode == InstallSpaceMode::Plugin) return kSpaceUnknownPluginReserve;
    return kSpaceUnknownDirectReserve;
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

// Install All reserves space for all selected links BEFORE queueing the first
// download. Game/Data ZIPs retain their downloaded ZIPs during extraction.
// Every previously extracted data tree remains on disk when a later step starts.
// Do not treat the compressed ZIP alone as the output-space requirement.
inline uint64_t installAllRequiredSpaceBytes(
    uint64_t appBytes, uint64_t extraBytes, uint64_t pluginsBytes = 0) {
    const uint64_t payload = saturatingSpaceAdd(
        saturatingSpaceAdd(appBytes, extraBytes), pluginsBytes);
    if (!payload) return 0;
    const uint64_t app = preflightRequiredSpaceBytes(appBytes, InstallSpaceMode::DirectPackage);
    const uint64_t extra = preflightRequiredSpaceBytes(extraBytes, InstallSpaceMode::ZipArchive);
    // Retain the historical 25% plugin reserve in the aggregate budget.
    const uint64_t plugins = saturatingSpaceAdd(pluginsBytes, pluginsBytes / 4ULL);
    return saturatingSpaceAdd(
        saturatingSpaceAdd(saturatingSpaceAdd(app, extra), plugins),
        kSpaceInstallAllReserve);
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
    const uint64_t known = installAllRequiredSpaceBytes(
        budget.appBytes, budget.extraBytes, budget.pluginBytes);
    if (budget.unknownSizeCount == 0) return known;
    // Even with unknown links, NEVER discard the combined known ZIP budget.
    // Remote HTTP metadata is still authoritative before payload writes.
    const uint64_t unknownCount = budget.unknownSizeCount;
    const uint64_t unknown = unknownCount > UINT64_MAX / kSpaceUnknownZipReserve
        ? UINT64_MAX : unknownCount * kSpaceUnknownZipReserve;
    return saturatingSpaceAdd(
        known ? known : kSpaceInstallAllReserve, unknown);
}

// Top-level app.size describes the PRIMARY download, never a secondary Data ZIP.
inline uint64_t chooseSelectedLinkSizeBytes(uint64_t linkBytes, uint64_t appBytes,
                                             const std::string& linkUrl,
                                             const std::string& primaryUrl) {
    if (linkBytes) return linkBytes;
    return !linkUrl.empty() && linkUrl == primaryUrl ? appBytes : 0;
}
} // namespace psvitaalive

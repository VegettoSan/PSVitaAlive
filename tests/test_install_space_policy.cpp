#include "installer/install_space_policy.hpp"
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>

int main() {
    using namespace psvitaalive;
    constexpr uint64_t MiB = 1024ULL * 1024ULL;
    const auto z = selectInstallSpaceMode(false, true, false);
    const auto p = selectInstallSpaceMode(false, false, false);
    const auto pl = selectInstallSpaceMode(false, false, true);
    const auto bg = selectInstallSpaceMode(true, false, false);
    assert(z == InstallSpaceMode::ZipArchive);
    assert(p == InstallSpaceMode::DirectPackage);
    assert(pl == InstallSpaceMode::Plugin);
    assert(bg == InstallSpaceMode::SystemBgdl);
    assert(selectInstallSpaceMode(true, true, true) == bg);
    assert(preflightRequiredSpaceBytes(0, z) == 0);
    assert(preflightRequiredSpaceBytes(100 * MiB, z) == 289 * MiB);
    // For STORE, download (100 MiB) + extracted copy (100 MiB)
    // coexist. Reject a card with only 180 MiB free BEFORE download.
    assert(preflightRequiredSpaceBytes(100 * MiB, z) > 180 * MiB);
    assert(preflightRequiredSpaceBytes(100 * MiB, p) == 210 * MiB);
    assert(preflightRequiredSpaceBytes(100 * MiB, pl) == 104 * MiB);
    assert(preflightRequiredSpaceBytes(100 * MiB, bg) == 210 * MiB);
    assert(initialInstallSpaceBytes(0, z) == 256 * MiB);
    assert(initialInstallSpaceBytes(0, p) == 64 * MiB);
    assert(initialInstallSpaceBytes(0, pl) == 16 * MiB);
    assert(initialInstallSpaceBytes(0, bg) == 64 * MiB);
    assert(initialInstallSpaceBytes(100 * MiB, z) == 289 * MiB);
    assert(preflightRequiredSpaceBytes(std::numeric_limits<uint64_t>::max(), z)
           == std::numeric_limits<uint64_t>::max());
    assert(preflightRequiredSpaceBytes(std::numeric_limits<uint64_t>::max(), p)
           == std::numeric_limits<uint64_t>::max());
    assert(chooseSelectedLinkSizeBytes(20 * MiB, 300 * MiB, "data.zip", "main.vpk") == 20 * MiB);
    assert(chooseSelectedLinkSizeBytes(0, 300 * MiB, "data.zip", "main.vpk") == 0);
    assert(chooseSelectedLinkSizeBytes(0, 300 * MiB, "main.vpk", "main.vpk") == 300 * MiB);
    assert(chooseSelectedLinkSizeBytes(0, 300 * MiB, "", "") == 0);
    // Only selected links enter the estimator: the 800-MiB *alternative*
    // Game Files is deliberately absent from this queue.
    InstallAllSpaceBudget manual;
    addInstallAllSpaceEntry(manual, InstallAllSpaceLinkKind::App, 100 * MiB);
    addInstallAllSpaceEntry(manual, InstallAllSpaceLinkKind::Extra, 200 * MiB);
    assert(manual.appBytes == 100 * MiB);
    assert(manual.extraBytes == 200 * MiB);
    assert(manual.pluginBytes == 0);
    assert(manual.unknownSizeCount == 0);
    assert(installAllQueueRequiredSpaceBytes(manual) == 852 * MiB); // VPK + ZIP archive + extract + 128 MiB

    // An auto-selected link uses exactly the same accounting as a manual pick.
    InstallAllSpaceBudget automatic;
    addInstallAllSpaceEntry(automatic, InstallAllSpaceLinkKind::App, 100 * MiB);
    addInstallAllSpaceEntry(automatic, InstallAllSpaceLinkKind::Extra, 200 * MiB);
    assert(installAllQueueRequiredSpaceBytes(automatic) ==
           installAllQueueRequiredSpaceBytes(manual));

    // Selecting the bigger alternative changes the requirement accordingly.
    InstallAllSpaceBudget bigger;
    addInstallAllSpaceEntry(bigger, InstallAllSpaceLinkKind::App, 100 * MiB);
    addInstallAllSpaceEntry(bigger, InstallAllSpaceLinkKind::Extra, 800 * MiB);
    assert(installAllQueueRequiredSpaceBytes(bigger) == (210 + 1800 + 64 + 128) * MiB);

    // If both Game Files and Data Files go in the queue, count both.
    InstallAllSpaceBudget both;
    addInstallAllSpaceEntry(both, InstallAllSpaceLinkKind::App, 100 * MiB);
    addInstallAllSpaceEntry(both, InstallAllSpaceLinkKind::Extra, 800 * MiB);
    addInstallAllSpaceEntry(both, InstallAllSpaceLinkKind::Extra, 200 * MiB);
    assert(both.extraBytes == 1000 * MiB);
    assert(installAllQueueRequiredSpaceBytes(both) == (210 + 2250 + 64 + 128) * MiB);

    // Approved plugins are included; unapproved plugins never enter the queue.
    InstallAllSpaceBudget withPlugin = manual;
    addInstallAllSpaceEntry(withPlugin, InstallAllSpaceLinkKind::Plugin, 30 * MiB);
    assert(withPlugin.pluginBytes == 30 * MiB);
    assert(installAllQueueRequiredSpaceBytes(withPlugin) ==
           852 * MiB + 30 * MiB + 30 * MiB / 4);

    // No app selected: do NOT charge item.size. ZIP-only queue is supported.
    InstallAllSpaceBudget dataOnly;
    addInstallAllSpaceEntry(dataOnly, InstallAllSpaceLinkKind::Extra, 500 * MiB);
    assert(installAllQueueRequiredSpaceBytes(dataOnly) == (1125 + 64 + 128) * MiB);

    // Missing sizes are explicitly reported, not replaced with item.size.
    InstallAllSpaceBudget missing;
    addInstallAllSpaceEntry(missing, InstallAllSpaceLinkKind::Extra, 0);
    assert(missing.extraBytes == 0);
    assert(missing.unknownSizeCount == 1);
    assert(installAllQueueRequiredSpaceBytes(missing) == (128 + 256) * MiB);
    // Missing metadata must NOT disable the preflight for known files.
    InstallAllSpaceBudget mixed = manual;
    addInstallAllSpaceEntry(mixed, InstallAllSpaceLinkKind::Extra, 0);
    assert(installAllQueueRequiredSpaceBytes(mixed) == (852 + 256) * MiB);
    assert(chooseSelectedLinkSizeBytes(0, 300 * MiB, "mirror.vpk", "main.vpk") == 0);

    // Retain the existing 2.1x VPK staging and saturating-overflow guarantees.
    assert(installAllRequiredSpaceBytes(1024 * MiB, 0) ==
           (1024 * MiB * 21 / 10 + 128 * MiB));
    assert(installAllRequiredSpaceBytes(0, 0) == 0);
    assert(installAllRequiredSpaceBytes(0, 0, 30 * MiB) ==
           30 * MiB + (30 * MiB / 4) + 128 * MiB);
    assert(installAllRequiredSpaceBytes(std::numeric_limits<uint64_t>::max(), 42) ==
           std::numeric_limits<uint64_t>::max());

    // The same per-link budget is applied once the server reports the real
    // Content-Length/Content-Range; no second HTTP fetch is required.
    assert(remainingInstallSpaceBytes(100 * MiB, 0, InstallSpaceMode::DirectPackage)
           == 210 * MiB);
    assert(remainingInstallSpaceBytes(100 * MiB, 25 * MiB,
                                      InstallSpaceMode::DirectPackage) == 185 * MiB);
    assert(remainingInstallSpaceBytes(100 * MiB, 0, InstallSpaceMode::ZipArchive)
           == 289 * MiB);
    assert(remainingInstallSpaceBytes(100 * MiB, 100 * MiB, InstallSpaceMode::ZipArchive)
           == 189 * MiB);
    assert(remainingInstallSpaceBytes(100 * MiB, 200 * MiB, InstallSpaceMode::ZipArchive)
           == 89 * MiB);
    assert(remainingInstallSpaceBytes(0, 0, InstallSpaceMode::DirectPackage) == 0);
    assert(remainingInstallSpaceBytes(std::numeric_limits<uint64_t>::max(), 42,
                                      InstallSpaceMode::DirectPackage) ==
           std::numeric_limits<uint64_t>::max() - 42);
    // Minecraft and BT4 regression: metadata for huge STORE ZIPs cannot
    // pass with room for just one copy of their downloaded archive.
    const uint64_t minecraftZip = 1181116006ULL; // Catalog hint; real response can differ.
    const uint64_t bt4Zip = 5905580032ULL;
    const uint64_t minecraftRequired = preflightRequiredSpaceBytes(minecraftZip, z);
    const uint64_t bt4Required = preflightRequiredSpaceBytes(bt4Zip, z);
    assert(minecraftRequired == minecraftZip * 2 + minecraftZip / 4 + 64 * MiB);
    assert(bt4Required == bt4Zip * 2 + bt4Zip / 4 + 64 * MiB);
    assert(minecraftRequired > minecraftZip * 2);
    assert(bt4Required > bt4Zip * 2);
    InstallAllSpaceBudget minecraftAll;
    addInstallAllSpaceEntry(minecraftAll, InstallAllSpaceLinkKind::App, 18035507ULL);
    addInstallAllSpaceEntry(minecraftAll, InstallAllSpaceLinkKind::Extra, minecraftZip);
    assert(installAllQueueRequiredSpaceBytes(minecraftAll) >
           minecraftRequired + 128 * MiB);
    // Missing metadata is detected by the existing queue budget.
    assert(missing.unknownSizeCount != 0);
    assert(manual.unknownSizeCount == 0);
    return 0;
}

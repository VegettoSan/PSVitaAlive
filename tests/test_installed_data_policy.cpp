#include "storage/installed_data_policy.hpp"
#include "storage/installed_data_manager.hpp"
#include <cassert>
#include <string>
using namespace psvitaalive::storage;
int main() {
    assert(validInstalledTitleId("ABCD12345"));
    assert(!validInstalledTitleId("abcD12345"));
    assert(!validInstalledTitleId("PSVA123456"));
    assert(validInstalledRelativePath("AnimalCrossing/rom/game.dat"));
    assert(validInstalledRelativePath("My.Game"));
    for (const std::string p : {"", "/game", ".", "..", "game/../x",
         "game//x", "game/", "game\\x", "ux0:data/game",
         "psvitaalive/cache", "PSVA_ZIP_STAGE_123", "psva_vpk",
         "sce_sys", "tai/config.txt", "game/./a"}) {
        assert(!validInstalledRelativePath(p));
    }
    assert(safeExternalDataPath("ux0:data/kotor"));
    assert(!safeExternalDataPath("ux0:app/KOTR00001"));
    assert(!safeExternalDataPath("ux0:data/"));
    assert(!safeExternalDataPath("ux0:data/psvitaalive/cache"));
    assert(installedPathsOverlap("ux0:data/game", "ux0:data/game"));
    assert(installedPathsOverlap("ux0:data/game", "ux0:data/game/saves"));
    assert(!installedPathsOverlap("ux0:data/game", "ux0:data/game2"));
    assert(installedDataProgressPercent(0, 5) == 0);
    assert(installedDataProgressPercent(1, 5) == 20);
    assert(installedDataProgressPercent(3, 5) == 60);
    assert(installedDataProgressPercent(5, 5) == 100);
    assert(installedDataProgressPercent(10, 5) == 100);
    // No known work remains: the empty list is complete.
    assert(installedDataProgressPercent(0, 0) == 100);
    InstalledDataScanProgress live;
    live.appsTotal.store(5);
    live.appsCompleted.fetch_add(1);
    assert(installedDataProgressPercent(live.appsCompleted.load(),
                                        live.appsTotal.load()) == 20);
    live.cancelRequested.store(true);
    assert(live.cancelRequested.load());
    live.reset();
    assert(!live.cancelRequested.load());
    assert(live.appsTotal.load() == 0);
    // Real progress uses completed roots, not an inferred file percentage.
    InstalledDataOperationProgress operation;
    operation.phase.store(2);
    operation.rootsTotal.store(4);
    operation.rootsCompleted.store(1);
    operation.filesRemoved.store(527);
    operation.foldersRemoved.store(18);
    assert(installedDataProgressPercent(operation.rootsCompleted.load(),
                                       operation.rootsTotal.load()) == 25);
    assert(operation.filesRemoved.load() == 527);
    operation.promoterPolls.fetch_add(1);
    assert(operation.promoterPolls.load() == 1);
    operation.reset();
    assert(operation.phase.load() == 0);
    assert(operation.rootsTotal.load() == 0);
    assert(operation.rootsCompleted.load() == 0);
    assert(operation.filesRemoved.load() == 0);
    assert(operation.foldersRemoved.load() == 0);
    assert(operation.promoterPolls.load() == 0);
}

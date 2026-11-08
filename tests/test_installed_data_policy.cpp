#include "storage/installed_data_policy.hpp"
#include "storage/installed_data_manager.hpp"
#include "ui/game_files_notice_policy.hpp"
#include "ui/installed_data_texture_policy.hpp"
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
    // Catalog overlap alone does not mean data is shared by installed apps.
    assert(!installedDataActiveCollision(
        "ux0:data/ctr", "ux0:data/ctr", false));
    assert(installedDataActiveCollision(
        "ux0:data/ctr", "ux0:data/ctr", true));
    assert(installedDataActiveCollision(
        "ux0:data/game", "ux0:data/game/saves", true));
    assert(!installedDataActiveCollision(
        "ux0:data/game", "ux0:data/game2", true));
    // Nested game-file entries are NOT catalog roots. Legitimate 'temp',
    // 'tai' and 'sce_sys' directories must not break size measurement.
    for (const auto& name : {"temp", "tmp", "tai", "sce_sys",
                             "app", "psvitaalive", "data.dat"})
        assert(validInstalledChildName(name));
    for (const auto& bad : {"", ".", "..", "x/y", "bad\\name",
                            "bad:name", "x\n"})
        assert(!validInstalledChildName(bad));
    assert(!validInstalledChildName(std::string(256, 'A')));
    assert(validInstalledChildName(std::string(255, 'A')));
    assert(!safeExternalDataPath("ux0:data/psvitaalive"));
    assert(!safeExternalDataPath("ux0:data/tai"));
    // Game Files warning only for installed apps with missing, known roots.
    {
        using ::psvitaalive::ui::CatalogItem;
        using ::psvitaalive::ui::CatalogLink;
        using ::psvitaalive::ui::missingInstalledGameFiles;
        CatalogItem item;
        CatalogLink game;
        game.type = "Game Files";
        game.installedPaths = {"gtasa", "gtasa_alt"};
        item.linkDetails.push_back(game);
        const auto isGame = [](const CatalogLink& l) { return l.type == "Game Files"; };
        const auto absent = [](const std::string&) { return false; };
        assert(!missingInstalledGameFiles(item, false, isGame, absent));
        assert(missingInstalledGameFiles(item, true, isGame, absent));
        assert(!missingInstalledGameFiles(item, true, isGame,
            [](const std::string& path) { return path == "ux0:data/gtasa_alt"; }));
        item.linkDetails[0].installedPaths.clear();
        assert(!missingInstalledGameFiles(item, true, isGame, absent));
        item.linkDetails[0].installedPaths = {"../unsafe"};
        assert(!missingInstalledGameFiles(item, true, isGame, absent));
        item.linkDetails[0].installedPaths = {"gtasa"};
        CatalogLink unknownMirror;
        unknownMirror.type = "Game Files";
        item.linkDetails.push_back(unknownMirror);
        assert(!missingInstalledGameFiles(item, true, isGame, absent));
        item.linkDetails.clear();
        CatalogLink data;
        data.type = "Data Files";
        data.installedPaths = {"gtasa"};
        item.linkDetails.push_back(data);
        assert(!missingInstalledGameFiles(item, true, isGame, absent));
    }
    // Action buttons are hidden, not merely greyed out. Focus and touch
    // hit-testing use the exact same ordered slot mapping as the renderer.
    assert(installedDataFirstAction(true, false) == 0);
    assert(installedDataFirstAction(true, true) == 2);
    assert(installedDataFirstAction(false, true) == 1);
    assert(installedDataFirstAction(false, false) == -1);
    assert(installedDataActionAvailable(0, true, false));
    assert(!installedDataActionAvailable(1, true, false));
    assert(!installedDataActionAvailable(2, true, false));
    assert(!installedDataActionAvailable(0, false, true));
    assert(installedDataActionAvailable(1, false, true));
    assert(!installedDataActionAvailable(2, false, true));
    for (int i = 0; i < 3; ++i) {
        assert(installedDataActionAvailable(i, true, true));
        assert(installedDataActionSlot(i, true, true) ==
               (i == 2 ? 0 : i + 1));
    }
    assert(installedDataActionSlot(0, true, false) == 0);
    assert(installedDataActionSlot(1, true, false) == -1);
    assert(installedDataActionSlot(1, false, true) == 0);
    assert(installedDataActionSlot(2, false, true) == -1);
    assert(installedDataMoveAction(0, 1, true, true) == 1);
    assert(installedDataMoveAction(0, -1, true, true) == 2);
    assert(installedDataMoveAction(2, 1, true, true) == 0);
    assert(installedDataActionSlot(2, true, true) == 0);
    assert(installedDataMoveAction(0, 1, true, false) == 0);
    assert(installedDataMoveAction(-1, 1, false, true) == 1);
    assert(installedDataMoveAction(-1, 1, false, false) == -1);

    // Copy for APP-only installs must not imply nonexistent data was retained.
    assert(std::string(installedDataActionNoteKey(0, false)) == "ID_DELETE_APP_ONLY_NOTE");
    assert(std::string(installedDataActionNoteKey(0, true)) == "ID_DELETE_APP_NOTE");
    assert(std::string(installedDataActionNoteKey(1, false)) == "ID_DELETE_FILES_NOTE");
    assert(std::string(installedDataActionNoteKey(2, true)) == "ID_DELETE_ALL_NOTE");
    assert(std::string(installedDataConfirmImpactKey(0, false)) == "ID_CONFIRM_IMPACT_APP_ONLY");
    assert(std::string(installedDataConfirmImpactKey(0, true)) == "ID_CONFIRM_IMPACT_APP_KEEP");
    assert(std::string(installedDataConfirmImpactKey(1, true)) == "ID_CONFIRM_IMPACT_FILES_ONLY");
    assert(std::string(installedDataConfirmImpactKey(2, true)) == "ID_CONFIRM_IMPACT_ALL");

    // GPU icon lifetime: dialog and catalog share the same path/texture.
    // An icon loaded by the dialog but still visible on returning to the
    // catalog must NOT be freed.
    {
        using ::psvitaalive::ui::installedDataExclusiveTextureKeys;
        const std::unordered_set<std::string> introduced = {
            "ux0:cache/app_only_data.png",
            "ux0:cache/app_shared.png",
            "ux0:cache/app_scrolled.png"
        };
        const std::unordered_set<std::string> neededByCatalog = {
            "ux0:cache/app_shared.png",
            "ux0:cache/app_preexisting.png"
        };
        const auto toRelease = installedDataExclusiveTextureKeys(
            introduced, neededByCatalog);
        assert(toRelease.size() == 2);
        bool removedData = false, removedScrolled = false;
        for (const auto& path : toRelease) {
            assert(path != "ux0:cache/app_shared.png");
            assert(path != "ux0:cache/app_preexisting.png");
            removedData |= path == "ux0:cache/app_only_data.png";
            removedScrolled |= path == "ux0:cache/app_scrolled.png";
        }
        assert(removedData && removedScrolled);
        assert(installedDataExclusiveTextureKeys({}, neededByCatalog).empty());
        assert(installedDataExclusiveTextureKeys(
            introduced, introduced).empty());
        assert(installedDataExclusiveTextureKeys(
            {"ux0:cache/app_one.png"}, {}).size() == 1);
    }

    // Percentage reflects confirmed file + folder deletions, NEVER roots.
    assert(installedDataDeletionPercent(0, 1000) == 0);
    assert(installedDataDeletionPercent(650, 1000) == 65);
    assert(installedDataDeletionPercent(999, 1000) == 99);
    assert(installedDataDeletionPercent(1000, 1000) == 99);
    assert(installedDataDeletionPercent(1000, 1000, true) == 100);
    assert(installedDataDeletionPercent(1500, 1000) == 99);
    assert(installedDataDeletionPercent(0, 0) == 0);
    assert(installedDataDeletionPercent(0, 0, true) == 100);
    // Big inputs keep the calculation in 64-bit units.
    assert(installedDataDeletionPercent(200000000ULL, 400000000ULL) == 50);

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
    // Root counters remain for diagnostics; the deletion UI uses entries.
    InstalledDataOperationProgress operation;
    operation.phase.store(2);
    operation.entriesTotal.store(2000);
    operation.rootsTotal.store(4);
    operation.rootsCompleted.store(1);
    operation.filesRemoved.store(527);
    operation.foldersRemoved.store(18);
    assert(installedDataProgressPercent(operation.rootsCompleted.load(),
                                       operation.rootsTotal.load()) == 25);
    assert(operation.filesRemoved.load() == 527);
    assert(installedDataDeletionPercent(
        static_cast<uint64_t>(operation.filesRemoved.load()) +
        operation.foldersRemoved.load(), operation.entriesTotal.load()) == 27);
    operation.promoterPolls.fetch_add(1);
    assert(operation.promoterPolls.load() == 1);
    operation.reset();
    assert(operation.phase.load() == 0);
    assert(operation.entriesTotal.load() == 0);
    assert(operation.rootsTotal.load() == 0);
    assert(operation.rootsCompleted.load() == 0);
    assert(operation.filesRemoved.load() == 0);
    assert(operation.foldersRemoved.load() == 0);
    assert(operation.promoterPolls.load() == 0);
}

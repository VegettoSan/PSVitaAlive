#include "storage/installed_data_policy.hpp"
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
}

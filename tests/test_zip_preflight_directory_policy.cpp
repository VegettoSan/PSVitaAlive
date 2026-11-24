#include "archive/zip_preflight_directory_policy.hpp"

#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

int main() {
    using psvitaalive::zipParentReadyFromPreflight;
    std::vector<std::string> dirs{
        "ux0:data/Minecraft",
        "ux0:data/Minecraft/assets",
        "ux0:data/Minecraft/assets/lang",
        "ux0:data/Minecraft/texturepacks"
    };
    std::sort(dirs.begin(), dirs.end());
    // Only parent directories actually prepared by the preflight qualify.
    assert(zipParentReadyFromPreflight(dirs, "ux0:data/Minecraft/assets", false));
    assert(zipParentReadyFromPreflight(dirs, "ux0:data/Minecraft/assets/lang", false));
    assert(!zipParentReadyFromPreflight(dirs, "ux0:data/Minecraft/missing", false));
    assert(!zipParentReadyFromPreflight(dirs, "ux0:data/Minecraft/assets", true));
    assert(!zipParentReadyFromPreflight(dirs, "ux0:data/PSVA_ZIP_STAGE_1/assets", true));
    assert(!zipParentReadyFromPreflight(dirs, "uma0:data/Minecraft/assets", false));
    return 0;
}

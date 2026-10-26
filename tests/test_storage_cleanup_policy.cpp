#include "storage/storage_cleanup_policy.hpp"
#include <cassert>
#include <string>
using namespace psvitaalive::storage;
int main() {
    assert(isOwnedZipStagePath("ux0:data/PSVA_ZIP_STAGE_29741690_0"));
    assert(isOwnedZipStagePath("uma0:data/PSVA_ZIP_STAGE_123_0"));
    assert(!isOwnedZipStagePath("ux0:data/PSVA_ZIP_STAGE_"));
    assert(!isOwnedZipStagePath("ux0:data/PSVA_ZIP_STAGE_3/../../app"));
    assert(!isOwnedZipStagePath("ux0:data/psvitaalive/config.json"));
    assert(!isOwnedZipStagePath("ux0:data/psvitaalive/cache/images"));
    assert(!isOwnedZipStagePath("ur0:data/PSVA_ZIP_STAGE_12"));
    assert(isOwnedTempName("nested_source.vpk"));
    assert(isOwnedTempName("nested_vpk_stage"));
    assert(isOwnedTempName("pkg_1234.pkg"));
    assert(isOwnedTempName("work.bin"));
    assert(!isOwnedTempName("config.json"));
    assert(!isOwnedTempName("image.png"));
    assert(!isOwnedTempName("catalog.json"));
    assert(!isOwnedTempName("pkg_../../game.pkg"));
    return 0;
}

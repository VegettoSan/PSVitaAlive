#include "archive/zip_existing_open_policy.hpp"

#include <cassert>
#include <string>

int main() {
    using psvitaalive::ZipExistingOpenPolicy;
    ZipExistingOpenPolicy p;

    // Existing output: one direct open with TRUNC (without O_CREAT), no EXCL.
    assert(p.tryExistingFirst("ux0:data/Minecraft/sound/bee"));
    p.existingOpened();
    assert(p.tryExistingFirst("ux0:data/Minecraft/sound/bee"));

    // First ENOENT: new-file run switches to exclusive creation.
    p.existingMissing();
    assert(!p.tryExistingFirst("ux0:data/Minecraft/sound/bee"));
    p.newFileCreated();
    assert(!p.tryExistingFirst("ux0:data/Minecraft/sound/bee"));

    // EXCL returned EEXIST in a mixed folder: use fast existing path again.
    p.existingOpened();
    assert(p.tryExistingFirst("ux0:data/Minecraft/sound/bee"));

    // Independent directory: reset the policy rather than assuming all missing.
    p.existingMissing();
    assert(!p.tryExistingFirst("ux0:data/Minecraft/sound/bee"));
    assert(p.tryExistingFirst("ux0:data/Minecraft/texturepacks"));
    p.newFileCreated();
    assert(!p.tryExistingFirst("ux0:data/Minecraft/texturepacks"));
    assert(p.tryExistingFirst("ux0:data/Minecraft/sound/bee"));

    // Staging paths are independent groups, never treated as the target parent.
    p.existingMissing();
    assert(p.tryExistingFirst("ux0:data/PSVA_ZIP_STAGE_1/b0"));
    return 0;
}

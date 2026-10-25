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
    assert(preflightRequiredSpaceBytes(100 * MiB, z) == 137 * MiB);
    assert(preflightRequiredSpaceBytes(100 * MiB, p) == 210 * MiB);
    assert(preflightRequiredSpaceBytes(100 * MiB, pl) == 104 * MiB);
    assert(preflightRequiredSpaceBytes(100 * MiB, bg) == 0);
    assert(preflightRequiredSpaceBytes(std::numeric_limits<uint64_t>::max(), z)
           == std::numeric_limits<uint64_t>::max());
    assert(preflightRequiredSpaceBytes(std::numeric_limits<uint64_t>::max(), p)
           == std::numeric_limits<uint64_t>::max());
    assert(chooseSelectedLinkSizeBytes(20 * MiB, 300 * MiB, "data.zip", "main.vpk") == 20 * MiB);
    assert(chooseSelectedLinkSizeBytes(0, 300 * MiB, "data.zip", "main.vpk") == 0);
    assert(chooseSelectedLinkSizeBytes(0, 300 * MiB, "main.vpk", "main.vpk") == 300 * MiB);
    assert(chooseSelectedLinkSizeBytes(0, 300 * MiB, "", "") == 0);
    // App 100MiB, largest secondary 800MiB, selected Data 200MiB:
    // budget still uses the bigger catalog candidate (900MiB + 25% + 128MiB).
    assert(installAllRequiredSpaceBytes(100*MiB, 800*MiB, 200*MiB)
           == (900+225+128)*MiB);
    // Both selected -> reserve for both (1.1GiB payload), not only the larger.
    assert(installAllRequiredSpaceBytes(100*MiB, 800*MiB, 1000*MiB)
           == (1100+275+128)*MiB);
    // Preserve conservative VPK staging rule for large primary packages.
    assert(installAllRequiredSpaceBytes(1024*MiB, 0, 0)
           == (1024*MiB*21/10 + 128*MiB));
    assert(installAllRequiredSpaceBytes(0,0,0) == 0);
    assert(installAllRequiredSpaceBytes(0,0,0,30*MiB)
           == (30+128+7)*MiB + (30*MiB/4 - 7*MiB));
    assert(installAllRequiredSpaceBytes(std::numeric_limits<uint64_t>::max(), 42, 22)
           == std::numeric_limits<uint64_t>::max());
    return 0;
}

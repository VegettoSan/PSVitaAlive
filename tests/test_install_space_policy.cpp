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
    return 0;
}

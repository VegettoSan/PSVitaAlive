#pragma once

#include <string>
#include <vector>

namespace psvitaalive {

/**
 * PSM Runtime installer bridge.
 *
 * The runtime is a special system PKG set. PSVitaAlive does not decrypt or
 * promote these packages itself; after the three downloads are present it
 * loads the small helper kernel module and delegates installation to the
 * system Package Installer (NPXS10031) using its BATCH parameter.
 */
class PsmRuntimeInstaller {
public:
    static constexpr const char* kRuntimeDirectory = "ux0:/data/psvitaalive/psm_runtime";

    /** Exact package order required by the PSM Runtime installation flow. */
    struct Package {
        const char* fileName;
        const char* url;
    };

    static const std::vector<Package>& packages();

    /**
     * Returns true when all three runtime PKGs exist and have non-zero size.
     * This does not start or modify the system installer.
     */
    static bool packagesReady(std::string* missingFile = nullptr);

    /**
     * Load the temporary kernel bridge and launch NPXS10031 with the three
     * packages in the documented order. The caller should invoke this only
     * after packagesReady() succeeds.
     */
    static bool launchPackageInstaller(std::string& error);
};

} // namespace psvitaalive

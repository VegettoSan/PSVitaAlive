#include "installer/psm_runtime_installer.hpp"

#include "diagnostic_logger.hpp"

#include <psp2/appmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <taihen.h>

#include <vector>

namespace psvitaalive {
namespace {

// The three packages are intentionally kept in the historical installation
// order: base 1.00, update 2.00, update 2.01.
const std::vector<PsmRuntimeInstaller::Package> kPackages = {
    {
        "IP9100-PCSI00011_00-PSMRUNTIME000000.pkg",
        "https://psmreborn.com/psm-runtime/IP9100-PCSI00011_00-PSMRUNTIME000000.pkg"
    },
    {
        "IP9100-PCSI00011_00-PSMRUNTIME000000-A0200-V0100-0c5cb04d07c9d9e135249594631feab513322db9-PE.pkg",
        "https://psmreborn.com/psm-runtime/IP9100-PCSI00011_00-PSMRUNTIME000000-A0200-V0100-0c5cb04d07c9d9e135249594631feab513322db9-PE.pkg"
    },
    {
        "IP9100-PCSI00011_00-PSMRUNTIME000000-A0201-V0100-e4708b1c1c71116c29632c23df590f68edbfc341-PE.pkg",
        "https://psmreborn.com/psm-runtime/IP9100-PCSI00011_00-PSMRUNTIME000000-A0201-V0100-e4708b1c1c71116c29632c23df590f68edbfc341-PE.pkg"
    }
};

std::string packagePath(const char* fileName) {
    return std::string(PsmRuntimeInstaller::kRuntimeDirectory) + "/" + fileName;
}

bool fileExistsAndNonEmpty(const std::string& path) {
    SceIoStat st{};
    return sceIoGetstat(path.c_str(), &st) >= 0 && st.st_size > 0;
}

} // namespace

const std::vector<PsmRuntimeInstaller::Package>& PsmRuntimeInstaller::packages() {
    return kPackages;
}

bool PsmRuntimeInstaller::packagesReady(std::string* missingFile) {
    for (const Package& pkg : kPackages) {
        const std::string path = packagePath(pkg.fileName);
        if (!fileExistsAndNonEmpty(path)) {
            if (missingFile) *missingFile = pkg.fileName;
            return false;
        }
    }
    return true;
}

bool PsmRuntimeInstaller::launchPackageInstaller(std::string& error) {
    error.clear();

    std::string missing;
    if (!packagesReady(&missing)) {
        error = "Missing PSM Runtime package: " + missing;
        diagnostics::log("[PsmRuntimeInstaller] " + error);
        return false;
    }

    // The helper is bundled with PSVitaAlive. It redirects host0:/package to
    // our private runtime staging directory and enables NPXS10031 on retail
    // firmware, matching the proven CrystalPSM mechanism.
    const SceUID module = taiLoadStartKernelModule(
        "app0:/psm_runtime_driver.skprx", 0, nullptr, 0);
    if (module < 0) {
        char msg[160];
        sceClibSnprintf(msg, sizeof(msg),
                        "Could not load PSM Runtime helper: 0x%08X",
                        static_cast<unsigned>(module));
        error = msg;
        diagnostics::log(std::string("[PsmRuntimeInstaller] ") + error);
        return false;
    }

    // NPXS10031 consumes one package path per line after the [BATCH] marker.
    // The paths are deliberately relative to the overlay exposed as
    // host0:/package; no package is moved into ux0:/package itself.
    std::string batch = "[BATCH]";
    for (const Package& pkg : kPackages) {
        batch += "host0:/package/";
        batch += pkg.fileName;
        batch += '\n';
    }
    if (!batch.empty() && batch.back() == '\n') batch.pop_back();

    diagnostics::log(std::string("[PsmRuntimeInstaller] launching NPXS10031 BATCH: ") + batch);
    const int result = sceAppMgrLaunchAppByName(0x60000, "NPXS10031", batch.c_str());
    if (result < 0) {
        char msg[160];
        sceClibSnprintf(msg, sizeof(msg),
                        "Could not launch Package Installer: 0x%08X",
                        static_cast<unsigned>(result));
        error = msg;
        diagnostics::log(std::string("[PsmRuntimeInstaller] ") + error);
        return false;
    }

    diagnostics::log("[PsmRuntimeInstaller] NPXS10031 launched successfully");
    return true;
}

} // namespace psvitaalive

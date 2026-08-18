#include "installer/refresh_manager.hpp"
#include "storage/storage_manager.hpp"

#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>

#include <string>

namespace psvitaalive {

bool RefreshManager::appTreeExists(const std::string& titleId) {
    if (titleId.size() < 4 || titleId.size() > 16) return false;
    StorageManager st;
    const std::string base = std::string("ux0:app/") + titleId;
    if (!st.exists(base) || !st.isDirectory(base)) return false;
    // Prefer param.sfo presence as a stronger signal.
    if (st.exists(base + "/sce_sys/param.sfo")) return true;
    if (st.exists(base + "/eboot.bin")) return true;
    return true; // directory exists
}

bool RefreshManager::verifyAfterInstall(
    const std::string& titleId,
    const std::string& installPath,
    bool liveAreaHint,
    std::string& messageOut
) {
    StorageManager st;

    if (!installPath.empty()) {
        if (installPath.find("pspemu") != std::string::npos) {
            messageOut = liveAreaHint
                ? "PSP media in pspemu. Open Adrenaline (or LiveArea if NoPspEmuDrm is active)."
                : "PSP media installed under pspemu for Adrenaline.";
            return st.exists(installPath);
        }
        if (installPath.find("ux0:data") != std::string::npos ||
            installPath.find("ux0:repatch") != std::string::npos) {
            messageOut = "Files extracted to " + installPath;
            return st.exists(installPath) || st.isDirectory(installPath);
        }
    }

    if (!titleId.empty() && appTreeExists(titleId)) {
        messageOut = std::string("LiveArea tree OK: ux0:app/") + titleId;
        return true;
    }

    if (liveAreaHint) {
        messageOut = "Promote finished; if the bubble is missing: VitaShell → Refresh LiveArea, or reboot.";
        return true; // soft success — promoter may lag on Vita3K
    }

    if (!titleId.empty()) {
        messageOut = std::string("App tree not found yet for ") + titleId +
                     ". Check ux0:app/" + titleId + " then Refresh LiveArea or reboot.";
        return false;
    }

    messageOut = installPath.empty() ? "Install finished" : ("Installed: " + installPath);
    return true;
}

} // namespace psvitaalive

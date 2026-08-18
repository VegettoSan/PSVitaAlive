#include "installer/refresh_manager.hpp"
#include "storage/storage_manager.hpp"
#include "diagnostic_logger.hpp"

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/promoterutil.h>
#include <psp2/sysmodule.h>

#include <cstring>
#include <string>

namespace psvitaalive {
namespace {

constexpr const char* kAppTemp = "ux0:temp/app";
constexpr const char* kTempRoot = "ux0:temp";

bool removeTreeRecursive(const std::string& path) {
    SceUID fd = sceIoDopen(path.c_str());
    if (fd >= 0) {
        SceIoDirent ent;
        while (sceIoDread(fd, &ent) > 0) {
            if (std::strcmp(ent.d_name, ".") == 0 || std::strcmp(ent.d_name, "..") == 0) continue;
            const std::string child = path + "/" + ent.d_name;
            if (SCE_S_ISDIR(ent.d_stat.st_mode)) {
                removeTreeRecursive(child);
            } else {
                sceIoRemove(child.c_str());
            }
        }
        sceIoDclose(fd);
        sceIoRmdir(path.c_str());
        return true;
    }
    // Not a directory — try file remove.
    sceIoRemove(path.c_str());
    return true;
}

struct PromoterScope {
    bool pafLoaded = false;
    bool promoterLoaded = false;

    bool load() {
        pafLoaded = false;
        promoterLoaded = false;

        if (sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PAF) < 0) {
            uint32_t ptr[0x100] = {0};
            ptr[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&ptr[0]));
            uint32_t scepafArgp[] = { 0x400000u, 0xEA60u, 0x40000u, 0u, 0u };
            const int r = sceSysmoduleLoadModuleInternalWithArg(
                SCE_SYSMODULE_INTERNAL_PAF,
                sizeof(scepafArgp),
                scepafArgp,
                reinterpret_cast<SceSysmoduleOpt*>(ptr)
            );
            if (r < 0) {
                diagnostics::log("[RefreshManager] load PAF failed");
                return false;
            }
            pafLoaded = true;
        }

        if (sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL) < 0) {
            const int r = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
            if (r < 0) {
                diagnostics::log("[RefreshManager] load PROMOTER_UTIL failed");
                unload();
                return false;
            }
            promoterLoaded = true;
        }
        return true;
    }

    void unload() {
        if (promoterLoaded) {
            sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
            promoterLoaded = false;
        }
        if (pafLoaded) {
            SceSysmoduleOpt opt{};
            std::memset(&opt.flags, 0, sizeof(opt.flags));
            sceSysmoduleUnloadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, 0, nullptr, &opt);
            pafLoaded = false;
        }
    }

    ~PromoterScope() { unload(); }
};

} // namespace

bool RefreshManager::appTreeExists(const std::string& titleId) {
    if (titleId.size() < 4 || titleId.size() > 16) return false;
    StorageManager st;
    const std::string base = std::string("ux0:app/") + titleId;
    if (!st.exists(base) || !st.isDirectory(base)) return false;
    if (st.exists(base + "/sce_sys/param.sfo")) return true;
    if (st.exists(base + "/eboot.bin")) return true;
    return true;
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
        return true;
    }

    if (!titleId.empty()) {
        messageOut = std::string("App tree not found yet for ") + titleId +
                     ". Check ux0:app/" + titleId + " then Refresh LiveArea or reboot.";
        return false;
    }

    messageOut = installPath.empty() ? "Install finished" : ("Installed: " + installPath);
    return true;
}

bool RefreshManager::refreshTitleLiveArea(const std::string& titleId, std::string& messageOut) {
    messageOut.clear();
    if (titleId.size() < 4 || titleId.size() > 16) {
        messageOut = "invalid titleId for LiveArea refresh";
        return false;
    }

    StorageManager st;
    const std::string appPath = std::string("ux0:app/") + titleId;
    if (!st.exists(appPath) || !st.isDirectory(appPath)) {
        messageOut = "app folder missing; cannot refresh LiveArea";
        return false;
    }

    diagnostics::log("[RefreshManager] LiveArea refresh begin title=" + titleId);

    // VitaShell uses ux0:temp/app as the promote staging directory.
    sceIoMkdir(kTempRoot, 0777);
    // Clear any leftover staging tree.
    SceIoStat stStat{};
    if (sceIoGetstat(kAppTemp, &stStat) >= 0) {
        removeTreeRecursive(kAppTemp);
    }

    const int ren = sceIoRename(appPath.c_str(), kAppTemp);
    if (ren < 0) {
        char buf[96];
        sceClibSnprintf(buf, sizeof(buf), "rename to temp failed: 0x%08X", ren);
        messageOut = buf;
        diagnostics::log(std::string("[RefreshManager] ") + buf);
        return false;
    }
    diagnostics::log("[RefreshManager] moved ux0:app/" + titleId + " -> ux0:temp/app");

    PromoterScope scope;
    if (!scope.load()) {
        // Restore app folder.
        sceIoRename(kAppTemp, appPath.c_str());
        messageOut = "unable to load promoter for LiveArea refresh";
        diagnostics::log("[RefreshManager] " + messageOut);
        return false;
    }

    int initRc = scePromoterUtilityInit();
    if (initRc < 0) {
        char buf[96];
        sceClibSnprintf(buf, sizeof(buf), "scePromoterUtilityInit: 0x%08X", initRc);
        messageOut = buf;
        diagnostics::log(std::string("[RefreshManager] ") + buf);
        sceIoRename(kAppTemp, appPath.c_str());
        return false;
    }

    diagnostics::log("[RefreshManager] PromotePkg sync for LiveArea refresh...");
    const int promoteRc = scePromoterUtilityPromotePkg(kAppTemp, 1);
    int operationResult = 0;
    const int getRc = scePromoterUtilityGetResult(&operationResult);
    scePromoterUtilityExit();

    {
        char buf[192];
        sceClibSnprintf(
            buf, sizeof(buf),
            "[RefreshManager] promote=0x%08X getResult=0x%08X op=0x%08X",
            promoteRc, getRc, operationResult
        );
        diagnostics::log(buf);
    }

    // Allow FS to settle (same idea as post-promote checks in HomebrewInstaller).
    sceKernelDelayThread(400 * 1000);

    if (promoteRc >= 0 && (getRc < 0 || operationResult == 0) && appTreeExists(titleId)) {
        messageOut = "LiveArea refresh promote succeeded";
        diagnostics::log("[RefreshManager] " + messageOut);
        // Staging dir is consumed by the system on success; clean leftovers if any.
        if (sceIoGetstat(kAppTemp, &stStat) >= 0) {
            removeTreeRecursive(kAppTemp);
        }
        return true;
    }

    // Restore if the app folder did not come back.
    if (!appTreeExists(titleId)) {
        if (sceIoGetstat(kAppTemp, &stStat) >= 0) {
            sceIoRename(kAppTemp, appPath.c_str());
            diagnostics::log("[RefreshManager] restored app folder after failed refresh");
        }
        char buf[128];
        sceClibSnprintf(
            buf, sizeof(buf),
            "LiveArea refresh promote failed (0x%08X); app folder restored if possible",
            promoteRc < 0 ? promoteRc : operationResult
        );
        messageOut = buf;
        diagnostics::log(std::string("[RefreshManager] ") + buf);
        return false;
    }

    // Tree exists despite non-zero codes — treat as soft success (Vita3K quirks).
    messageOut = "LiveArea refresh finished with app tree present";
    diagnostics::log("[RefreshManager] " + messageOut);
    if (sceIoGetstat(kAppTemp, &stStat) >= 0) {
        removeTreeRecursive(kAppTemp);
    }
    return true;
}

} // namespace psvitaalive

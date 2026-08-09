#include "installer/homebrew_installer.hpp"
#include "archive/format_detector.hpp"
#include "archive/zip_extractor.hpp"
#include "storage/storage_manager.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/promoterutil.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>

#include <cstring>
#include <string>

namespace psvitaalive {
namespace {
constexpr const char* TMP_ROOT = "ux0:data/psvitaalive/tmp";
bool isDotEntry(const char* name) {
    return name && (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0);
}
}

const char* toString(InstallResult r) {
    switch (r) {
        case InstallResult::Ok: return "Ok";
        case InstallResult::InvalidArgument: return "InvalidArgument";
        case InstallResult::NotVpk: return "NotVpk";
        case InstallResult::ExtractFailed: return "ExtractFailed";
        case InstallResult::PromoteFailed: return "PromoteFailed";
        case InstallResult::ModuleFailed: return "ModuleFailed";
        case InstallResult::IoError: return "IoError";
        case InstallResult::Cancelled: return "Cancelled";
        case InstallResult::UnknownError: return "UnknownError";
        default: return "Unknown";
    }
}

void HomebrewInstaller::setError(const std::string& msg) {
    lastError_ = msg;
    sceClibPrintf("[HomebrewInstaller] %s\n", msg.c_str());
}

bool HomebrewInstaller::loadPromoterModule() {
    pafLoadedByUs_ = false;
    promoterLoadedByUs_ = false;

    // Match the module-loading sequence used by VitaShell/VHBB-style
    // installers. The PAF module is a dependency of Promoter Utility.
    if (sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PAF) < 0) {
        uint32_t pafArgs[] = { 0x180000, 0xFFFFFFFF, 0xFFFFFFFF, 1, 0xFFFFFFFF, 0xFFFFFFFF };

        // VitaSDK's current SceSysmodule API expects a SceSysmoduleOpt here.
        // Older code passed a raw result buffer, which no longer matches the
        // current prototype and causes the build error seen with modern VitaSDK.
        SceSysmoduleOpt pafOpt = { 0 };
        pafOpt.result = &pafOpt.flags;

        const int r = sceSysmoduleLoadModuleInternalWithArg(
            SCE_SYSMODULE_INTERNAL_PAF,
            sizeof(pafArgs),
            pafArgs,
            &pafOpt
        );
        if (r < 0) {
            char buf[80];
            sceClibSnprintf(buf, sizeof(buf), "load PAF failed: 0x%08X", r);
            setError(buf);
            return false;
        }
        pafLoadedByUs_ = true;
    }

    if (sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL) < 0) {
        const int r = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
        if (r < 0) {
            char buf[80];
            sceClibSnprintf(buf, sizeof(buf), "load promoter failed: 0x%08X", r);
            setError(buf);
            unloadPromoterModules();
            return false;
        }
        promoterLoadedByUs_ = true;
    }
    return true;
}

void HomebrewInstaller::unloadPromoterModules() {
    if (promoterLoadedByUs_) {
        const int r = sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
        if (r < 0) sceClibPrintf("[HomebrewInstaller] unload promoter failed: 0x%08X\n", r);
        promoterLoadedByUs_ = false;
    }
    if (pafLoadedByUs_) {
        const int r = sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PAF);
        if (r < 0) sceClibPrintf("[HomebrewInstaller] unload PAF failed: 0x%08X\n", r);
        pafLoadedByUs_ = false;
    }
}

bool HomebrewInstaller::removeTree(const std::string& path) {
    StorageManager st;
    if (!st.exists(path)) return true;
    if (!st.isDirectory(path)) return st.removeFile(path);

    SceUID uid = sceIoDopen(path.c_str());
    if (uid < 0) return false;
    bool ok = true;
    SceIoDirent ent;
    while (sceIoDread(uid, &ent) > 0) {
        if (isDotEntry(ent.d_name)) continue;
        const std::string child = path + "/" + ent.d_name;
        const bool childIsDir = (ent.d_stat.st_mode & SCE_S_IFDIR) != 0;
        if (childIsDir) {
            if (!removeTree(child)) { ok = false; break; }
        } else if (!st.removeFile(child)) {
            ok = false;
            break;
        }
    }
    sceIoDclose(uid);
    if (!ok) return false;
    return st.removeDirectory(path);
}

InstallResult HomebrewInstaller::promoteExtractedDir(const std::string& dir) {
    const int initResult = scePromoterUtilityInit();
    if (initResult < 0) {
        char buf[64];
        sceClibSnprintf(buf, sizeof(buf), "scePromoterUtilityInit: 0x%08X", initResult);
        setError(buf);
        lastPromoteResult_ = initResult;
        return InstallResult::PromoteFailed;
    }

    // Promoter Utility installs the extracted package under ux0:app/<TITLE_ID>
    // and registers its LiveArea bubble. With sync=1 the operation completes
    // before returning.
    const int promoteResult = scePromoterUtilityPromotePkgWithRif(dir.c_str(), 1);
    lastPromoteResult_ = promoteResult;

    const int exitResult = scePromoterUtilityExit();
    if (promoteResult < 0) {
        char buf[80];
        sceClibSnprintf(buf, sizeof(buf), "scePromoterUtilityPromotePkgWithRif: 0x%08X", promoteResult);
        setError(buf);
        return InstallResult::PromoteFailed;
    }
    if (exitResult < 0) {
        char buf[80];
        sceClibSnprintf(buf, sizeof(buf), "scePromoterUtilityExit: 0x%08X", exitResult);
        setError(buf);
        return InstallResult::PromoteFailed;
    }

    return InstallResult::Ok;
}

InstallResult HomebrewInstaller::installVpk(
    const std::string& vpkPath,
    InstallProgressFn onProgress,
    InstallCancelFn shouldCancel,
    bool deleteTempOnSuccess
) {
    lastError_.clear();
    lastPromoteResult_ = 0;
    pafLoadedByUs_ = false;
    promoterLoadedByUs_ = false;

    if (vpkPath.empty()) { setError("empty vpk path"); return InstallResult::InvalidArgument; }
    StorageManager st;
    if (!st.exists(vpkPath)) { setError("vpk not found"); return InstallResult::IoError; }

    if (onProgress) {
        InstallProgress p;
        p.stage = InstallProgress::Preparing;
        p.message = "detecting VPK";
        onProgress(p);
    }

    FormatDetector detector;
    const DetectResult det = detector.detectFile(vpkPath);
    const std::string ext = FormatDetector::extensionOf(vpkPath);
    if (ext != "vpk" || det.format != FileFormat::Vpk) {
        setError(std::string("invalid VPK: format=") + toString(det.format) + " ext=" + ext);
        return InstallResult::NotVpk;
    }
    if (shouldCancel && shouldCancel()) { setError("cancelled"); return InstallResult::Cancelled; }
    if (!st.createDirectories(TMP_ROOT)) { setError("cannot create tmp root"); return InstallResult::IoError; }

    char tmpName[160];
    sceClibSnprintf(tmpName, sizeof(tmpName), "%s/inst_%llu", TMP_ROOT,
        (unsigned long long)sceKernelGetProcessTimeWide());
    const std::string tmpDir = tmpName;
    if (!st.createDirectories(tmpDir)) { setError("cannot create VPK temp directory"); return InstallResult::IoError; }

    if (onProgress) {
        InstallProgress p;
        p.stage = InstallProgress::Extracting;
        p.message = "extracting VPK";
        onProgress(p);
    }

    ZipExtractor zip;
    const ZipResult zr = zip.extract(
        vpkPath,
        tmpDir,
        [&](const ZipProgress& zp) {
            if (!onProgress) return;
            InstallProgress p;
            p.stage = InstallProgress::Extracting;
            p.entriesDone = zp.entriesDone;
            p.entriesTotal = zp.entriesTotal;
            p.bytesWritten = zp.bytesWritten;
            p.bytesTotal = zp.bytesTotal;
            p.message = zp.currentEntry;
            onProgress(p);
        },
        shouldCancel
    );

    if (zr == ZipResult::Cancelled) {
        removeTree(tmpDir);
        setError("extract cancelled");
        return InstallResult::Cancelled;
    }
    if (zr != ZipResult::Ok) {
        removeTree(tmpDir);
        setError(std::string("extract failed: ") + zip.lastError());
        return InstallResult::ExtractFailed;
    }

    const std::string ebootPath = tmpDir + "/eboot.bin";
    const std::string paramPath = tmpDir + "/sce_sys/param.sfo";
    if (!st.exists(ebootPath) || !st.exists(paramPath)) {
        removeTree(tmpDir);
        setError("invalid VPK layout: expected eboot.bin and sce_sys/param.sfo");
        return InstallResult::ExtractFailed;
    }
    if (shouldCancel && shouldCancel()) {
        removeTree(tmpDir);
        setError("cancelled before promote");
        return InstallResult::Cancelled;
    }

    if (onProgress) {
        InstallProgress p;
        p.stage = InstallProgress::Promoting;
        p.message = "installing with Promoter Utility";
        onProgress(p);
    }

    if (!loadPromoterModule()) {
        removeTree(tmpDir);
        return InstallResult::ModuleFailed;
    }

    const InstallResult result = promoteExtractedDir(tmpDir);
    unloadPromoterModules();

    if (onProgress) {
        InstallProgress p;
        p.stage = result == InstallResult::Ok ? InstallProgress::Cleaning : InstallProgress::Error;
        p.message = result == InstallResult::Ok ? "cleaning temporary files" : lastError_;
        onProgress(p);
    }

    if (result == InstallResult::Ok && deleteTempOnSuccess) {
        if (!removeTree(tmpDir)) {
            sceClibPrintf("[HomebrewInstaller] warning: cleanup failed for %s\n", tmpDir.c_str());
        }
    }

    if (result == InstallResult::Ok && onProgress) {
        InstallProgress p;
        p.stage = InstallProgress::Done;
        p.bytesWritten = 1;
        p.bytesTotal = 1;
        p.message = "VPK installed";
        onProgress(p);
    }
    return result;
}

} // namespace psvitaalive

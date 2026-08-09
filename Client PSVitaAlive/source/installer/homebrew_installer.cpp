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
    // SCE_SYSMODULE_PROMOTER_UTIL = 0x0165 typically
    int r = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    if (r < 0) {
        // Fallback public load if available
        r = sceSysmoduleLoadModule(static_cast<SceSysmoduleModuleId>(0x165));
    }
    if (r < 0) {
        char buf[64];
        sceClibSnprintf(buf, sizeof(buf), "load promoter failed: 0x%08X", r);
        setError(buf);
        return false;
    }
    return true;
}

InstallResult HomebrewInstaller::promoteExtractedDir(const std::string& dir) {
    int r = scePromoterUtilityInit();
    if (r < 0) {
        char buf[64];
        sceClibSnprintf(buf, sizeof(buf), "scePromoterUtilityInit: 0x%08X", r);
        setError(buf);
        lastPromoteResult_ = r;
        return InstallResult::PromoteFailed;
    }

    // Promote package directory (extracted VPK contents)
    r = scePromoterUtilityPromotePkg(dir.c_str(), 0);
    lastPromoteResult_ = r;

    // Wait until finished
    int state = 0;
    if (r >= 0) {
        do {
            r = scePromoterUtilityGetState(&state);
            if (r < 0) break;
            sceKernelDelayThread(100 * 1000);
        } while (state != 0);

        int result = 0;
        r = scePromoterUtilityGetResult(&result);
        lastPromoteResult_ = (r >= 0) ? result : r;
    }

    scePromoterUtilityExit();

    if (lastPromoteResult_ < 0) {
        char buf[80];
        sceClibSnprintf(buf, sizeof(buf), "promote failed: 0x%08X", lastPromoteResult_);
        setError(buf);
        return InstallResult::PromoteFailed;
    }

    sceClibPrintf("[HomebrewInstaller] promote OK for %s\n", dir.c_str());
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

    if (vpkPath.empty()) {
        setError("empty vpk path");
        return InstallResult::InvalidArgument;
    }

    StorageManager st;
    if (!st.exists(vpkPath)) {
        setError("vpk not found");
        return InstallResult::IoError;
    }

    if (onProgress) {
        InstallProgress p;
        p.stage = InstallProgress::Preparing;
        p.message = "detecting";
        onProgress(p);
    }

    FormatDetector detector;
    DetectResult det = detector.detectFile(vpkPath);
    if (det.format != FileFormat::Vpk && det.format != FileFormat::Zip) {
        setError(std::string("not a vpk/zip: ") + toString(det.format));
        return InstallResult::NotVpk;
    }

    // Prefer .vpk extension for homebrew install path; still allow zip with vpk layout
    std::string ext = FormatDetector::extensionOf(vpkPath);
    if (ext != "vpk" && det.format != FileFormat::Vpk) {
        // Allow zip only if caller insists — Phase 6 treats non-.vpk as NotVpk for safety
        if (ext != "vpk") {
            setError("extension is not .vpk");
            return InstallResult::NotVpk;
        }
    }

    if (shouldCancel && shouldCancel()) {
        setError("cancelled");
        return InstallResult::Cancelled;
    }

    if (!st.createDirectories(TMP_ROOT)) {
        setError("cannot create tmp root");
        return InstallResult::IoError;
    }

    // Unique temp dir
    char tmpName[128];
    sceClibSnprintf(tmpName, sizeof(tmpName), "%s/inst_%u",
                    TMP_ROOT, (unsigned)sceKernelGetProcessTimeLow());
    std::string tmpDir = tmpName;

    // Clean if exists
    // (simple: create fresh)
    st.createDirectories(tmpDir);

    if (onProgress) {
        InstallProgress p;
        p.stage = InstallProgress::Extracting;
        p.message = "extracting vpk";
        onProgress(p);
    }

    ZipExtractor zip;
    ZipResult zr = zip.extract(
        vpkPath,
        tmpDir,
        [&](const ZipProgress& zp) {
            if (onProgress) {
                InstallProgress p;
                p.stage = InstallProgress::Extracting;
                p.entriesDone = zp.entriesDone;
                p.entriesTotal = zp.entriesTotal;
                p.bytesWritten = zp.bytesWritten;
                p.message = zp.currentEntry;
                onProgress(p);
            }
        },
        shouldCancel
    );

    if (zr == ZipResult::Cancelled) {
        setError("extract cancelled");
        return InstallResult::Cancelled;
    }
    if (zr != ZipResult::Ok) {
        setError(std::string("extract failed: ") + zip.lastError());
        return InstallResult::ExtractFailed;
    }

    // Basic layout check: sce_sys should exist
    if (!st.exists(tmpDir + "/sce_sys") && !st.exists(tmpDir + "/eboot.bin")) {
        setError("extracted package missing sce_sys/eboot.bin");
        return InstallResult::ExtractFailed;
    }

    if (shouldCancel && shouldCancel()) {
        setError("cancelled before promote");
        return InstallResult::Cancelled;
    }

    if (onProgress) {
        InstallProgress p;
        p.stage = InstallProgress::Promoting;
        p.message = "promoter";
        onProgress(p);
    }

    if (!loadPromoterModule()) {
        return InstallResult::ModuleFailed;
    }

    InstallResult pr = promoteExtractedDir(tmpDir);

    if (onProgress) {
        InstallProgress p;
        p.stage = (pr == InstallResult::Ok) ? InstallProgress::Cleaning : InstallProgress::Error;
        p.message = (pr == InstallResult::Ok) ? "cleanup" : lastError_;
        onProgress(p);
    }

    if (pr == InstallResult::Ok && deleteTempOnSuccess) {
        // Best-effort recursive cleanup is complex; remove known files shallowly
        // Full recursive delete can be improved later.
        // For Phase 6 we remove eboot and try rmdir tree via simple approach:
        // leave tmp if remove fails — does not block install success.
        st.removeFile(tmpDir + "/eboot.bin");
        // Attempt remove dir (may fail if not empty)
        sceIoRmdir((tmpDir + "/sce_sys").c_str());
        sceIoRmdir(tmpDir.c_str());
    }

    if (pr == InstallResult::Ok && onProgress) {
        InstallProgress p;
        p.stage = InstallProgress::Done;
        p.message = "installed";
        onProgress(p);
    }

    return pr;
}

} // namespace psvitaalive

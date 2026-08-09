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
} // namespace

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

    // PromoterUtil depends on the internal PAF module on common Vita
    // homebrew environments. This follows the dependency-loading pattern
    // used by established Vita homebrew browsers.
    if (sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PAF) < 0) {
        SceSysmoduleOpt opt;
        std::memset(&opt, 0, sizeof(opt));
        opt.result = &opt.flags;

        uint32_t pafArgs[] = {
            0x400000,
            0xEA60,
            0x40000,
            0,
            0
        };

        int r = sceSysmoduleLoadModuleInternalWithArg(
            SCE_SYSMODULE_INTERNAL_PAF,
            sizeof(pafArgs),
            pafArgs,
            &opt
        );

        if (r < 0) {
            char buf[80];
            sceClibSnprintf(buf, sizeof(buf),
                            "load PAF failed: 0x%08X", r);
            setError(buf);
            return false;
        }

        pafLoadedByUs_ = true;
    }

    if (sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL) < 0) {
        int r = sceSysmoduleLoadModuleInternal(
            SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL
        );

        if (r < 0) {
            char buf[80];
            sceClibSnprintf(buf, sizeof(buf),
                            "load promoter failed: 0x%08X", r);
            setError(buf);
            unloadPromoterModules();
            return false;
        }

        promoterLoadedByUs_ = true;
    }

    sceClibPrintf("[HomebrewInstaller] promoter dependencies loaded\n");
    return true;
}

void HomebrewInstaller::unloadPromoterModules() {
    if (promoterLoadedByUs_) {
        int r = sceSysmoduleUnloadModuleInternal(
            SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL
        );
        if (r < 0) {
            sceClibPrintf(
                "[HomebrewInstaller] unload promoter failed: 0x%08X\n",
                r
            );
        }
        promoterLoadedByUs_ = false;
    }

    if (pafLoadedByUs_) {
        int r = sceSysmoduleUnloadModuleInternal(
            SCE_SYSMODULE_INTERNAL_PAF
        );
        if (r < 0) {
            sceClibPrintf(
                "[HomebrewInstaller] unload PAF failed: 0x%08X\n",
                r
            );
        }
        pafLoadedByUs_ = false;
    }
}

bool HomebrewInstaller::removeTree(const std::string& path) {
    StorageManager st;

    if (!st.exists(path)) {
        return true;
    }

    if (!st.isDirectory(path)) {
        return st.removeFile(path);
    }

    SceUID uid = sceIoDopen(path.c_str());
    if (uid < 0) {
        return false;
    }

    bool ok = true;
    SceIoDirent ent;

    while (sceIoDread(uid, &ent) > 0) {
        if (isDotEntry(ent.d_name)) {
            continue;
        }

        const std::string child = path + "/" + ent.d_name;
        const bool childIsDir =
            (ent.d_stat.st_mode & SCE_S_IFDIR) != 0;

        if (childIsDir) {
            if (!removeTree(child)) {
                ok = false;
                break;
            }
        } else if (!st.removeFile(child)) {
            ok = false;
            break;
        }
    }

    sceIoDclose(uid);

    if (!ok) {
        return false;
    }

    return st.removeDirectory(path);
}

InstallResult HomebrewInstaller::promoteExtractedDir(
    const std::string& dir
) {
    int r = scePromoterUtilityInit();
    if (r < 0) {
        char buf[64];
        sceClibSnprintf(buf, sizeof(buf),
                        "scePromoterUtilityInit: 0x%08X", r);
        setError(buf);
        lastPromoteResult_ = r;
        return InstallResult::PromoteFailed;
    }

    // PromoterUtil expects the directory containing the extracted package
    // contents, not the original .vpk file.
    r = scePromoterUtilityPromotePkg(dir.c_str(), 0);
    lastPromoteResult_ = r;

    if (r >= 0) {
        int state = 0;

        do {
            r = scePromoterUtilityGetState(&state);
            if (r < 0) {
                break;
            }

            sceKernelDelayThread(100 * 1000);
        } while (state != 0);

        int result = 0;
        r = scePromoterUtilityGetResult(&result);
        lastPromoteResult_ = (r >= 0) ? result : r;
    }

    scePromoterUtilityExit();

    if (lastPromoteResult_ < 0) {
        char buf[80];
        sceClibSnprintf(buf, sizeof(buf),
                        "promote failed: 0x%08X",
                        lastPromoteResult_);
        setError(buf);
        return InstallResult::PromoteFailed;
    }

    sceClibPrintf(
        "[HomebrewInstaller] promote OK for %s\n",
        dir.c_str()
    );

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
        p.message = "detecting VPK";
        onProgress(p);
    }

    FormatDetector detector;
    DetectResult det = detector.detectFile(vpkPath);
    const std::string ext = FormatDetector::extensionOf(vpkPath);

    // A VPK is a ZIP container, but a generic .zip must never be treated as
    // an installable homebrew package. Require both the extension and magic.
    if (ext != "vpk" || det.format != FileFormat::Vpk) {
        setError(
            std::string("invalid VPK: format=") +
            toString(det.format) +
            " ext=" + ext
        );
        return InstallResult::NotVpk;
    }

    if (shouldCancel && shouldCancel()) {
        setError("cancelled");
        return InstallResult::Cancelled;
    }

    if (!st.createDirectories(TMP_ROOT)) {
        setError("cannot create tmp root");
        return InstallResult::IoError;
    }

    char tmpName[160];
    sceClibSnprintf(
        tmpName,
        sizeof(tmpName),
        "%s/inst_%llu",
        TMP_ROOT,
        (unsigned long long)sceKernelGetProcessTimeWide()
    );
    const std::string tmpDir = tmpName;

    if (!st.createDirectories(tmpDir)) {
        setError("cannot create VPK temp directory");
        return InstallResult::IoError;
    }

    if (onProgress) {
        InstallProgress p;
        p.stage = InstallProgress::Extracting;
        p.message = "extracting VPK";
        onProgress(p);
    }

    ZipExtractor zip;
    ZipResult zr = zip.extract(
        vpkPath,
        tmpDir,
        [&](const ZipProgress& zp) {
            if (!onProgress) {
                return;
            }

            InstallProgress p;
            p.stage = InstallProgress::Extracting;
            p.entriesDone = zp.entriesDone;
            p.entriesTotal = zp.entriesTotal;
            p.bytesWritten = zp.bytesWritten;
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
        setError(
            std::string("extract failed: ") + zip.lastError()
        );
        return InstallResult::ExtractFailed;
    }

    // Minimum Vita homebrew package layout. PromoterUtil consumes the
    // extracted directory and relies on the package metadata in param.sfo.
    const std::string ebootPath = tmpDir + "/eboot.bin";
    const std::string paramPath = tmpDir + "/sce_sys/param.sfo";

    if (!st.exists(ebootPath) || !st.exists(paramPath)) {
        removeTree(tmpDir);
        setError(
            "invalid VPK layout: expected eboot.bin and "
            "sce_sys/param.sfo"
        );
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
        // Keep the extracted package for diagnostics if module loading fails.
        return InstallResult::ModuleFailed;
    }

    InstallResult result = promoteExtractedDir(tmpDir);
    unloadPromoterModules();

    if (onProgress) {
        InstallProgress p;
        p.stage = (result == InstallResult::Ok)
            ? InstallProgress::Cleaning
            : InstallProgress::Error;
        p.message = (result == InstallResult::Ok)
            ? "cleaning temporary files"
            : lastError_;
        onProgress(p);
    }

    if (result == InstallResult::Ok && deleteTempOnSuccess) {
        if (!removeTree(tmpDir)) {
            // Installation already succeeded. Do not turn a cleanup warning
            // into an installation failure.
            sceClibPrintf(
                "[HomebrewInstaller] warning: cleanup failed for %s\n",
                tmpDir.c_str()
            );
        }
    }

    if (result == InstallResult::Ok && onProgress) {
        InstallProgress p;
        p.stage = InstallProgress::Done;
        p.message = "VPK installed";
        onProgress(p);
    }

    return result;
}

} // namespace psvitaalive

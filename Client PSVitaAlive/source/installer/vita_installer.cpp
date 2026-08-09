#include "installer/vita_installer.hpp"
#include "archive/format_detector.hpp"
#include "storage/storage_manager.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/promoterutil.h>
#include <psp2/io/fcntl.h>

#include <cstring>
#include <string>
#include <vector>

namespace psvitaalive {

namespace {
constexpr const char* TMP_ROOT = "ux0:data/psvitaalive/tmp";
}

const char* toString(VitaInstallResult r) {
    switch (r) {
        case VitaInstallResult::Ok: return "Ok";
        case VitaInstallResult::InvalidArgument: return "InvalidArgument";
        case VitaInstallResult::NotPkg: return "NotPkg";
        case VitaInstallResult::ModuleFailed: return "ModuleFailed";
        case VitaInstallResult::PromoteFailed: return "PromoteFailed";
        case VitaInstallResult::IoError: return "IoError";
        case VitaInstallResult::Cancelled: return "Cancelled";
        case VitaInstallResult::UnknownError: return "UnknownError";
        default: return "Unknown";
    }
}

void VitaInstaller::setError(const std::string& msg) {
    lastError_ = msg;
    sceClibPrintf("[VitaInstaller] %s\n", msg.c_str());
}

bool VitaInstaller::loadPromoterModule() {
    int r = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    if (r < 0) {
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

VitaInstallResult VitaInstaller::promotePath(const std::string& path) {
    int r = scePromoterUtilityInit();
    if (r < 0) {
        char buf[64];
        sceClibSnprintf(buf, sizeof(buf), "scePromoterUtilityInit: 0x%08X", r);
        setError(buf);
        lastPromoteResult_ = r;
        return VitaInstallResult::PromoteFailed;
    }

    // Promote package. For PKG files, promoter accepts package path on supported setups.
    r = scePromoterUtilityPromotePkg(path.c_str(), 0);
    lastPromoteResult_ = r;

    if (r >= 0) {
        int state = 0;
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
        return VitaInstallResult::PromoteFailed;
    }

    sceClibPrintf("[VitaInstaller] promote OK for %s\n", path.c_str());
    return VitaInstallResult::Ok;
}

VitaInstallResult VitaInstaller::installPkg(
    const std::string& pkgPath,
    VitaInstallProgressFn onProgress,
    VitaInstallCancelFn shouldCancel,
    bool deleteTempOnSuccess
) {
    lastError_.clear();
    lastPromoteResult_ = 0;

    if (pkgPath.empty()) {
        setError("empty pkg path");
        return VitaInstallResult::InvalidArgument;
    }

    StorageManager st;
    if (!st.exists(pkgPath)) {
        setError("pkg not found");
        return VitaInstallResult::IoError;
    }

    if (onProgress) {
        VitaInstallProgress p;
        p.stage = VitaInstallProgress::Preparing;
        p.message = "detecting";
        onProgress(p);
    }

    FormatDetector detector;
    DetectResult det = detector.detectFile(pkgPath);
    const std::string ext = FormatDetector::extensionOf(pkgPath);

    const bool looksPkg =
        (det.format == FileFormat::Pkg) ||
        (ext == "pkg");

    if (!looksPkg) {
        setError(std::string("not a pkg: format=") + toString(det.format) + " ext=" + ext);
        return VitaInstallResult::NotPkg;
    }

    if (shouldCancel && shouldCancel()) {
        setError("cancelled");
        return VitaInstallResult::Cancelled;
    }

    if (!loadPromoterModule()) {
        return VitaInstallResult::ModuleFailed;
    }

    // Stage copy into tmp (avoids installing from arbitrary locations / partial downloads)
    if (!st.createDirectories(TMP_ROOT)) {
        setError("cannot create tmp");
        return VitaInstallResult::IoError;
    }

    char staged[256];
    sceClibSnprintf(staged, sizeof(staged), "%s/pkg_%u.pkg",
                    TMP_ROOT, (unsigned)sceKernelGetProcessTimeLow());
    std::string stagedPath = staged;

    // Copy file in chunks
    SceUID in = sceIoOpen(pkgPath.c_str(), SCE_O_RDONLY, 0);
    if (in < 0) {
        setError("open source pkg failed");
        return VitaInstallResult::IoError;
    }
    SceUID out = sceIoOpen(stagedPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (out < 0) {
        sceIoClose(in);
        setError("open staged pkg failed");
        return VitaInstallResult::IoError;
    }

    std::vector<char> buf(64 * 1024);
    while (true) {
        if (shouldCancel && shouldCancel()) {
            sceIoClose(in);
            sceIoClose(out);
            st.removeFile(stagedPath);
            setError("cancelled during stage");
            return VitaInstallResult::Cancelled;
        }
        int n = sceIoRead(in, buf.data(), buf.size());
        if (n < 0) {
            sceIoClose(in);
            sceIoClose(out);
            setError("read pkg failed");
            return VitaInstallResult::IoError;
        }
        if (n == 0) break;
        int off = 0;
        while (off < n) {
            int w = sceIoWrite(out, buf.data() + off, n - off);
            if (w <= 0) {
                sceIoClose(in);
                sceIoClose(out);
                setError("write staged pkg failed");
                return VitaInstallResult::IoError;
            }
            off += w;
        }
    }
    sceIoClose(in);
    sceIoClose(out);

    if (onProgress) {
        VitaInstallProgress p;
        p.stage = VitaInstallProgress::Promoting;
        p.message = "promoter";
        onProgress(p);
    }

    VitaInstallResult pr = promotePath(stagedPath);

    if (pr == VitaInstallResult::Ok && deleteTempOnSuccess) {
        st.removeFile(stagedPath);
    }

    if (pr == VitaInstallResult::Ok && onProgress) {
        VitaInstallProgress p;
        p.stage = VitaInstallProgress::Done;
        p.message = "installed";
        onProgress(p);
    }

    return pr;
}

} // namespace psvitaalive

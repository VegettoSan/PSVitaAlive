#include "installer/vita_installer.hpp"
#include "localization/localization.hpp"
#include "archive/format_detector.hpp"
#include "storage/storage_manager.hpp"
#include "installer/license_helper.hpp"

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
    // Same sequence as HomebrewInstaller / VitaShell / LPP:
    // ScePaf must be loaded with args before PROMOTER_UTIL, or load fails (0x805A1000).
    pafLoadedByUs_ = false;
    promoterLoadedByUs_ = false;

    if (sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PAF) < 0) {
        uint32_t ptr[0x100] = {0};
        ptr[0] = 0;
        ptr[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&ptr[0]));
        uint32_t scepafArgp[] = { 0x400000u, 0xEA60u, 0x40000u, 0u, 0u };
        const int r = sceSysmoduleLoadModuleInternalWithArg(
            SCE_SYSMODULE_INTERNAL_PAF,
            sizeof(scepafArgp),
            scepafArgp,
            reinterpret_cast<SceSysmoduleOpt*>(ptr)
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
        int r = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
        if (r < 0) {
            r = sceSysmoduleLoadModule(static_cast<SceSysmoduleModuleId>(0x165));
        }
        if (r < 0) {
            char buf[64];
            sceClibSnprintf(buf, sizeof(buf), "load promoter failed: 0x%08X", r);
            setError(buf);
            unloadPromoterModules();
            return false;
        }
        promoterLoadedByUs_ = true;
    }
    return true;
}

void VitaInstaller::unloadPromoterModules() {
    if (promoterLoadedByUs_) {
        const int r = sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
        if (r < 0) {
            sceClibPrintf("[VitaInstaller] unload promoter failed: 0x%08X\n", r);
        }
        promoterLoadedByUs_ = false;
    }
    if (pafLoadedByUs_) {
        SceSysmoduleOpt opt{};
        std::memset(&opt.flags, 0, sizeof(opt.flags));
        const int r = sceSysmoduleUnloadModuleInternalWithArg(
            SCE_SYSMODULE_INTERNAL_PAF, 0, nullptr, &opt
        );
        if (r < 0) {
            sceClibPrintf("[VitaInstaller] unload PAF failed: 0x%08X\n", r);
        }
        pafLoadedByUs_ = false;
    }
}

VitaInstallResult VitaInstaller::promotePath(const std::string& path, bool withRif) {
    int r = scePromoterUtilityInit();
    if (r < 0) {
        char buf[64];
        sceClibSnprintf(buf, sizeof(buf), "scePromoterUtilityInit: 0x%08X", r);
        setError(buf);
        lastPromoteResult_ = r;
        return VitaInstallResult::PromoteFailed;
    }

    if (withRif) {
        r = scePromoterUtilityPromotePkgWithRif(path.c_str(), 0);
    } else {
        r = scePromoterUtilityPromotePkg(path.c_str(), 0);
    }
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
        setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgEmptyPath));
        return VitaInstallResult::InvalidArgument;
    }

    StorageManager st;
    if (!st.exists(pkgPath)) {
        setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgPkgNotFound));
        return VitaInstallResult::IoError;
    }

    if (onProgress) {
        VitaInstallProgress p;
        p.stage = VitaInstallProgress::Preparing;
        p.message = "validating PKG";
        onProgress(p);
    }

    FormatDetector detector;
    DetectResult det = detector.detectFile(pkgPath);
    const std::string ext = FormatDetector::extensionOf(pkgPath);
    const bool looksPkg = (det.format == FileFormat::Pkg) || (ext == "pkg");
    if (!looksPkg) {
        setError(std::string(::psvitaalive::L(::psvitaalive::TextId::InstMsgNotPkg)) + ": " + toString(det.format) + " ext=" + ext);
        return VitaInstallResult::NotPkg;
    }

    if (shouldCancel && shouldCancel()) {
        setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgCancelled));
        return VitaInstallResult::Cancelled;
    }

    if (!loadPromoterModule()) {
        return VitaInstallResult::ModuleFailed;
    }
    // Unload PAF/Promoter on every exit path after a successful load.
    struct PromoterScope {
        VitaInstaller* self;
        ~PromoterScope() { if (self) self->unloadPromoterModules(); }
    } promoterScope{this};

    if (!st.createDirectories(TMP_ROOT)) {
        setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgCannotCreateDir));
        return VitaInstallResult::IoError;
    }

    char staged[256];
    sceClibSnprintf(
        staged, sizeof(staged), "%s/pkg_%u.pkg",
        TMP_ROOT, (unsigned)sceKernelGetProcessTimeLow()
    );
    const std::string stagedPath = staged;

    SceUID in = sceIoOpen(pkgPath.c_str(), SCE_O_RDONLY, 0);
    if (in < 0) {
        setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgCannotOpenSource));
        return VitaInstallResult::IoError;
    }

    const int64_t sourceSizeSigned = st.fileSize(pkgPath);
    const uint64_t sourceSize = sourceSizeSigned > 0 ? static_cast<uint64_t>(sourceSizeSigned) : 0;

    SceUID out = sceIoOpen(stagedPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (out < 0) {
        sceIoClose(in);
        setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgCannotOpenDest));
        return VitaInstallResult::IoError;
    }

    std::vector<char> buf(64 * 1024);
    uint64_t copied = 0;
    while (true) {
        if (shouldCancel && shouldCancel()) {
            sceIoClose(in);
            sceIoClose(out);
            st.removeFile(stagedPath);
            setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgCancelled));
            return VitaInstallResult::Cancelled;
        }

        int n = sceIoRead(in, buf.data(), buf.size());
        if (n < 0) {
            sceIoClose(in);
            sceIoClose(out);
            st.removeFile(stagedPath);
            setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgReadFailed));
            return VitaInstallResult::IoError;
        }
        if (n == 0) break;

        int off = 0;
        while (off < n) {
            int w = sceIoWrite(out, buf.data() + off, n - off);
            if (w <= 0) {
                sceIoClose(in);
                sceIoClose(out);
                st.removeFile(stagedPath);
                setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgWriteFailed));
                return VitaInstallResult::IoError;
            }
            off += w;
        }

        copied += static_cast<uint64_t>(n);
        if (onProgress) {
            VitaInstallProgress p;
            p.stage = VitaInstallProgress::Preparing;
            p.current = copied;
            p.total = sourceSize;
            p.message = "staging PKG";
            onProgress(p);
        }
    }

    sceIoClose(in);
    sceIoClose(out);

    if (onProgress) {
        VitaInstallProgress p;
        p.stage = VitaInstallProgress::Promoting;
        p.current = 0;
        p.total = 0;
        p.message = "Installing PKG with Promoter Utility";
        onProgress(p);
    }

    VitaInstallResult pr = promotePath(stagedPath, false);

    if (pr == VitaInstallResult::Ok && deleteTempOnSuccess) {
        st.removeFile(stagedPath);
    } else if (pr != VitaInstallResult::Ok) {
        // A failed promotion must not leave a large staged package behind.
        st.removeFile(stagedPath);
    }

    if (pr == VitaInstallResult::Ok && onProgress) {
        VitaInstallProgress p;
        p.stage = VitaInstallProgress::Done;
        p.message = "PKG installed";
        onProgress(p);
    }

    return pr;
}


VitaInstallResult VitaInstaller::installPkgWithRif(
    const std::string& pkgPath,
    const std::string& rifPath,
    VitaInstallProgressFn onProgress,
    VitaInstallCancelFn shouldCancel,
    bool deleteTempOnSuccess
) {
    if (rifPath.empty()) {
        return installPkg(pkgPath, onProgress, shouldCancel, deleteTempOnSuccess);
    }
    lastError_.clear();
    lastPromoteResult_ = 0;
    if (pkgPath.empty()) { setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgEmptyPath)); return VitaInstallResult::InvalidArgument; }
    StorageManager st;
    if (!st.exists(pkgPath)) { setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgPkgNotFound)); return VitaInstallResult::IoError; }
    if (!st.exists(rifPath)) { setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgRifNotFound)); return VitaInstallResult::IoError; }
    if (onProgress) {
        VitaInstallProgress p; p.stage = VitaInstallProgress::Preparing; p.message = "validating PKG + RIF"; onProgress(p);
    }
    FormatDetector detector;
    DetectResult det = detector.detectFile(pkgPath);
    const std::string ext = FormatDetector::extensionOf(pkgPath);
    if (!(det.format == FileFormat::Pkg || ext == "pkg")) {
        setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgNotPkg)); return VitaInstallResult::NotPkg;
    }
    if (shouldCancel && shouldCancel()) { setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgCancelled)); return VitaInstallResult::Cancelled; }
    if (!loadPromoterModule()) return VitaInstallResult::ModuleFailed;
    struct PromoterScope {
        VitaInstaller* self;
        ~PromoterScope() { if (self) self->unloadPromoterModules(); }
    } promoterScope{this};
    if (!st.createDirectories(TMP_ROOT)) { setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgCannotCreateDir)); return VitaInstallResult::IoError; }

    char staged[256];
    sceClibSnprintf(staged, sizeof(staged), "%s/pkg_%u.pkg", TMP_ROOT, (unsigned)sceKernelGetProcessTimeLow());
    const std::string stagedPath = staged;

    SceUID in = sceIoOpen(pkgPath.c_str(), SCE_O_RDONLY, 0);
    if (in < 0) { setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgCannotOpenSource)); return VitaInstallResult::IoError; }
    SceUID out = sceIoOpen(stagedPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (out < 0) { sceIoClose(in); setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgCannotOpenDest)); return VitaInstallResult::IoError; }
    std::vector<char> buf(64 * 1024);
    while (true) {
        if (shouldCancel && shouldCancel()) {
            sceIoClose(in); sceIoClose(out); st.removeFile(stagedPath);
            setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgCancelled)); return VitaInstallResult::Cancelled;
        }
        const int rd = sceIoRead(in, buf.data(), buf.size());
        if (rd < 0) { sceIoClose(in); sceIoClose(out); setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgReadFailed)); return VitaInstallResult::IoError; }
        if (rd == 0) break;
        if (sceIoWrite(out, buf.data(), rd) != rd) {
            sceIoClose(in); sceIoClose(out); setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgWriteFailed)); return VitaInstallResult::IoError;
        }
    }
    sceIoClose(in); sceIoClose(out);

    const std::string stagedRif = std::string(TMP_ROOT) + "/work.bin";
    std::string err;
    if (!LicenseHelper::copyRifFile(rifPath, stagedRif, err)) {
        setError(std::string(::psvitaalive::L(::psvitaalive::TextId::InstMsgInstallationFailed)) + ": " + err);
        st.removeFile(stagedPath);
        return VitaInstallResult::IoError;
    }

    if (onProgress) {
        VitaInstallProgress p; p.stage = VitaInstallProgress::Promoting; p.message = "promoting PKG with RIF"; onProgress(p);
    }
    const VitaInstallResult pr = promotePath(stagedPath, true);
    if (deleteTempOnSuccess) {
        st.removeFile(stagedPath);
        st.removeFile(stagedRif);
    }
    if (pr != VitaInstallResult::Ok) return pr;
    if (onProgress) {
        VitaInstallProgress p; p.stage = VitaInstallProgress::Done; p.current = 1; p.total = 1;
        p.message = "PKG installed with RIF"; onProgress(p);
    }
    return VitaInstallResult::Ok;
}

} // namespace psvitaalive

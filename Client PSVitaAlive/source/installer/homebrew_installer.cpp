#include "installer/homebrew_installer.hpp"
#include "installer/fake_package_builder.hpp"
#include "archive/format_detector.hpp"
#include "archive/zip_extractor.hpp"
#include "storage/storage_manager.hpp"
#include "diagnostic_logger.hpp"

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
constexpr const char* LOG_ROOT = "ux0:data/psvitaalive/logs";
constexpr const char* INSTALL_LOG = "ux0:data/psvitaalive/logs/install.log";

bool isDotEntry(const char* name) {
    return name && (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0);
}

bool copyFileBytes(const std::string& src, const std::string& dst) {
    SceUID in = sceIoOpen(src.c_str(), SCE_O_RDONLY, 0);
    if (in < 0) return false;
    SceUID out = sceIoOpen(dst.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (out < 0) {
        sceIoClose(in);
        return false;
    }
    char buf[16 * 1024];
    bool ok = true;
    while (true) {
        const int n = sceIoRead(in, buf, sizeof(buf));
        if (n < 0) { ok = false; break; }
        if (n == 0) break;
        if (sceIoWrite(out, buf, n) != n) { ok = false; break; }
    }
    sceIoClose(in);
    sceIoClose(out);
    return ok;
}

bool copyTreeRecursive(const std::string& src, const std::string& dst) {
    StorageManager st;
    if (!st.exists(src)) return false;
    if (!st.isDirectory(src)) {
        return copyFileBytes(src, dst);
    }
    if (!st.createDirectories(dst) && !st.exists(dst)) return false;

    SceUID uid = sceIoDopen(src.c_str());
    if (uid < 0) return false;
    bool ok = true;
    SceIoDirent ent;
    while (sceIoDread(uid, &ent) > 0) {
        if (isDotEntry(ent.d_name)) continue;
        const std::string childSrc = src + "/" + ent.d_name;
        const std::string childDst = dst + "/" + ent.d_name;
        const bool childIsDir = (ent.d_stat.st_mode & SCE_S_IFDIR) != 0;
        if (childIsDir) {
            if (!copyTreeRecursive(childSrc, childDst)) { ok = false; break; }
        } else if (!copyFileBytes(childSrc, childDst)) {
            ok = false;
            break;
        }
    }
    sceIoDclose(uid);
    return ok;
}


void ensureLogDirectory() {
    sceIoMkdir("ux0:data/psvitaalive", 0777);
    sceIoMkdir(LOG_ROOT, 0777);
}

void logLine(const std::string& message) {
    ensureLogDirectory();
    SceUID fd = sceIoOpen(INSTALL_LOG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd < 0) {
        sceClibPrintf("[InstallLog] open failed: 0x%08X\n", fd);
        return;
    }

    char line[1024];
    const uint64_t ms = sceKernelGetProcessTimeWide() / 1000ULL;
    sceClibSnprintf(line, sizeof(line), "[%llu ms] %s\n", (unsigned long long)ms, message.c_str());
    sceIoWrite(fd, line, std::strlen(line));
    sceIoClose(fd);
    // Force media flush so a crash mid-promote still leaves the last lines on disk.
    sceIoSync("ux0:", 0);
}

// Mirror critical steps to session.log (field reports only had a partial install.log).
void logMilestone(const std::string& message) {
    logLine(message);
    diagnostics::log(std::string("[Installer] ") + message);
}

void logResult(const char* name, int result) {
    char buf[160];
    sceClibSnprintf(buf, sizeof(buf), "%s => 0x%08X (%d)", name, result, result);
    logLine(buf);
}

bool readSfoTitleId(const std::string& path, std::string& titleId) {
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) return false;

    SceIoStat st;
    std::memset(&st, 0, sizeof(st));
    if (sceIoGetstat(path.c_str(), &st) < 0 || st.st_size < 0x14 || st.st_size > 1024 * 1024) {
        sceIoClose(fd);
        return false;
    }

    std::string data(static_cast<size_t>(st.st_size), '\0');
    size_t done = 0;
    while (done < data.size()) {
        const int r = sceIoRead(fd, &data[done], data.size() - done);
        if (r <= 0) {
            sceIoClose(fd);
            return false;
        }
        done += static_cast<size_t>(r);
    }
    sceIoClose(fd);

    const unsigned char* p = reinterpret_cast<const unsigned char*>(data.data());
    if (std::memcmp(p, "\0PSF", 4) != 0) return false;

    auto u16 = [](const unsigned char* q) -> uint16_t {
        return static_cast<uint16_t>(q[0]) | static_cast<uint16_t>(q[1] << 8);
    };
    auto u32 = [](const unsigned char* q) -> uint32_t {
        return static_cast<uint32_t>(q[0]) |
               (static_cast<uint32_t>(q[1]) << 8) |
               (static_cast<uint32_t>(q[2]) << 16) |
               (static_cast<uint32_t>(q[3]) << 24);
    };

    const uint32_t keyTableOffset = u32(p + 8);
    const uint32_t dataTableOffset = u32(p + 12);
    const uint32_t entryCount = u32(p + 16);
    if (keyTableOffset >= data.size() || dataTableOffset >= data.size() || entryCount > (data.size() - 0x14) / 16) return false;

    for (uint32_t i = 0; i < entryCount; ++i) {
        const size_t entry = 0x14 + static_cast<size_t>(i) * 16;
        const uint16_t keyOffset = u16(p + entry);
        const uint32_t dataLen = u32(p + entry + 4);
        const uint32_t dataOffset = u32(p + entry + 12);
        const size_t keyPos = static_cast<size_t>(keyTableOffset) + keyOffset;
        const size_t valuePos = static_cast<size_t>(dataTableOffset) + dataOffset;
        if (keyPos >= data.size() || valuePos >= data.size() || dataLen > data.size() - valuePos) return false;

        const char* key = reinterpret_cast<const char*>(p + keyPos);
        if (std::strcmp(key, "TITLE_ID") != 0) continue;

        size_t len = dataLen;
        while (len > 0 && p[valuePos + len - 1] == 0) --len;
        titleId.assign(reinterpret_cast<const char*>(p + valuePos), len);
        return !titleId.empty();
    }
    return false;
}

void logPathState(const char* label, const std::string& path) {
    SceIoStat st;
    std::memset(&st, 0, sizeof(st));
    const int r = sceIoGetstat(path.c_str(), &st);
    char buf[320];
    if (r < 0) {
        sceClibSnprintf(buf, sizeof(buf), "%s: MISSING path=%s result=0x%08X", label, path.c_str(), r);
    } else {
        sceClibSnprintf(buf, sizeof(buf), "%s: EXISTS path=%s size=%lld mode=0x%08X", label, path.c_str(), (long long)st.st_size, (unsigned)st.st_mode);
    }
    logLine(buf);
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
    logLine(std::string("ERROR: ") + msg);
}

bool HomebrewInstaller::loadPromoterModule() {
    pafLoadedByUs_ = false;
    promoterLoadedByUs_ = false;
    logLine("loadPromoterModule: begin");

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
        logResult("sceSysmoduleLoadModuleInternalWithArg(PAF)", r);
        if (r < 0) {
            char buf[80];
            sceClibSnprintf(buf, sizeof(buf), "load PAF failed: 0x%08X", r);
            setError(buf);
            return false;
        }
        pafLoadedByUs_ = true;
    } else {
        logLine("PAF already loaded");
    }

    if (sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL) < 0) {
        const int r = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
        logResult("sceSysmoduleLoadModuleInternal(PROMOTER_UTIL)", r);
        if (r < 0) {
            char buf[80];
            sceClibSnprintf(buf, sizeof(buf), "load promoter failed: 0x%08X", r);
            setError(buf);
            unloadPromoterModules();
            return false;
        }
        promoterLoadedByUs_ = true;
    } else {
        logLine("PROMOTER_UTIL already loaded");
    }

    logLine("loadPromoterModule: success");
    return true;
}

void HomebrewInstaller::unloadPromoterModules() {
    if (promoterLoadedByUs_) {
        const int r = sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
        logResult("sceSysmoduleUnloadModuleInternal(PROMOTER_UTIL)", r);
        if (r < 0) sceClibPrintf("[HomebrewInstaller] unload promoter failed: 0x%08X\n", r);
        promoterLoadedByUs_ = false;
    }

    if (pafLoadedByUs_) {
        SceSysmoduleOpt opt{};
        std::memset(&opt.flags, 0, sizeof(opt.flags));
        const int r = sceSysmoduleUnloadModuleInternalWithArg(
            SCE_SYSMODULE_INTERNAL_PAF,
            0,
            nullptr,
            &opt
        );
        logResult("sceSysmoduleUnloadModuleInternalWithArg(PAF)", r);
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
    logLine(std::string("promoteExtractedDir: ") + dir);

    const int initResult = scePromoterUtilityInit();
    logResult("scePromoterUtilityInit", initResult);
    if (initResult < 0) {
        char buf[64];
        sceClibSnprintf(buf, sizeof(buf), "scePromoterUtilityInit: 0x%08X", initResult);
        setError(buf);
        lastPromoteResult_ = initResult;
        return InstallResult::PromoteFailed;
    }

    // The previous implementation used sync=0 and immediately called Exit and
    // unloaded the promoter modules. That starts an asynchronous operation and
    // can leave the installer reporting success while ux0:app/<TITLE_ID> does
    // not exist yet (or ever completes). VitaDB/NeoVitaDB explicitly waits for
    // promoter completion. Use the synchronous mode here so the call only
    // returns after the package has been promoted and the LiveArea entry has
    // been created.
    logMilestone("PromotePkg sync begin (waiting for system...)");
    const int promoteResult = scePromoterUtilityPromotePkg(dir.c_str(), 1);
    logResult("scePromoterUtilityPromotePkg(sync=1)", promoteResult);
    lastPromoteResult_ = promoteResult;
    {
        char buf[128];
        sceClibSnprintf(buf, sizeof(buf), "PromotePkg returned 0x%08X (%d)", promoteResult, promoteResult);
        logMilestone(buf);
    }

    int operationResult = 0;
    const int getResultCall = scePromoterUtilityGetResult(&operationResult);
    {
        char buf[192];
        sceClibSnprintf(
            buf, sizeof(buf),
            "scePromoterUtilityGetResult call=0x%08X (%d) operationResult=0x%08X (%d)",
            getResultCall, getResultCall, operationResult, operationResult
        );
        logMilestone(buf);
    }
    if (getResultCall < 0) {
        lastPromoteResult_ = getResultCall;
    } else if (operationResult != 0) {
        lastPromoteResult_ = operationResult;
    }

    const int exitResult = scePromoterUtilityExit();
    logResult("scePromoterUtilityExit", exitResult);
    {
        char buf[96];
        sceClibSnprintf(buf, sizeof(buf), "scePromoterUtilityExit => 0x%08X (%d)", exitResult, exitResult);
        logMilestone(buf);
    }

    if (promoteResult < 0) {
        char buf[80];
        sceClibSnprintf(buf, sizeof(buf), "scePromoterUtilityPromotePkg: 0x%08X", promoteResult);
        setError(buf);
        return InstallResult::PromoteFailed;
    }
    if (getResultCall < 0) {
        char buf[80];
        sceClibSnprintf(buf, sizeof(buf), "scePromoterUtilityGetResult: 0x%08X", getResultCall);
        setError(buf);
        return InstallResult::PromoteFailed;
    }
    if (operationResult != 0) {
        char buf[80];
        sceClibSnprintf(buf, sizeof(buf), "promoter operation failed: 0x%08X", operationResult);
        setError(buf);
        return InstallResult::PromoteFailed;
    }
    if (exitResult < 0) {
        char buf[80];
        sceClibSnprintf(buf, sizeof(buf), "scePromoterUtilityExit: 0x%08X", exitResult);
        setError(buf);
        return InstallResult::PromoteFailed;
    }

    logLine("promoteExtractedDir: promoter completed successfully");
    return InstallResult::Ok;
}

InstallResult HomebrewInstaller::installVpk(
    const std::string& vpkPath,
    InstallProgressFn onProgress,
    InstallCancelFn shouldCancel,
    bool deleteTempOnSuccess
) {
    lastError_.clear();
    lastTitleId_.clear();
    lastInstallPath_.clear();
    lastLiveAreaOk_ = false;
    lastPromoteResult_ = 0;
    pafLoadedByUs_ = false;
    promoterLoadedByUs_ = false;

    logLine("============================================================");
    logLine(std::string("installVpk BEGIN path=") + vpkPath);

    if (vpkPath.empty()) { setError("empty vpk path"); return InstallResult::InvalidArgument; }
    StorageManager st;
    if (!st.exists(vpkPath)) { setError("vpk not found"); return InstallResult::IoError; }
    logPathState("VPK source", vpkPath);

    if (onProgress) {
        InstallProgress p;
        p.stage = InstallProgress::Preparing;
        p.message = "detecting VPK";
        onProgress(p);
    }

    FormatDetector detector;
    const DetectResult det = detector.detectFile(vpkPath);
    const std::string ext = FormatDetector::extensionOf(vpkPath);
    logLine(std::string("FormatDetector: format=") + toString(det.format) + " extension=" + ext);
    // VPK is always a ZIP container. Trust ZIP/VPK magic regardless of extension
    // (VitaDB redirectors often leave names like get_hb_url.php).
    const bool looksLikeVpk =
        det.format == FileFormat::Vpk ||
        det.format == FileFormat::Zip ||
        ext == "vpk";
    if (!looksLikeVpk) {
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
    logLine(std::string("Temporary directory: ") + tmpDir);

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

    logLine(std::string("ZipExtractor result=") + std::to_string(static_cast<int>(zr)) + " error=" + zip.lastError());
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
    logPathState("Extracted eboot.bin", ebootPath);
    logPathState("Extracted param.sfo", paramPath);

    std::string titleId;
    if (readSfoTitleId(paramPath, titleId)) {
        logLine(std::string("param.sfo TITLE_ID=") + titleId);
        lastTitleId_ = titleId;
        lastInstallPath_ = std::string("ux0:app/") + titleId;
    } else {
        logLine("param.sfo TITLE_ID could not be read");
    }

    if (!st.exists(ebootPath) || !st.exists(paramPath)) {
        removeTree(tmpDir);
        setError("invalid VPK layout: expected eboot.bin and sce_sys/param.sfo");
        return InstallResult::ExtractFailed;
    }
    if (shouldCancel && shouldCancel()) {
        removeTree(tmpDir);
        setError("cancelled before package preparation");
        return InstallResult::Cancelled;
    }

    if (onProgress) {
        InstallProgress p;
        p.stage = InstallProgress::Promoting;
        p.message = "preparing package metadata";
        onProgress(p);
    }

    FakePackageBuilder packageBuilder;
    if (!packageBuilder.build(tmpDir)) {
        logLine(std::string("FakePackageBuilder FAILED: ") + packageBuilder.lastError());
        removeTree(tmpDir);
        setError(std::string("package preparation failed: ") + packageBuilder.lastError());
        return InstallResult::PromoteFailed;
    }
    logLine("FakePackageBuilder: success");
    logPathState("Generated sce_sys/package/head.bin", tmpDir + "/sce_sys/package/head.bin");

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

    InstallResult result = promoteExtractedDir(tmpDir);
    unloadPromoterModules();

    if (!titleId.empty()) {
        lastTitleId_ = titleId;
        lastInstallPath_ = std::string("ux0:app/") + titleId;
    }

    if (result == InstallResult::Ok && !titleId.empty()) {
        const std::string appDir = std::string("ux0:app/") + titleId;
        const std::string paramSfo = appDir + "/sce_sys/param.sfo";
        const std::string icon0 = appDir + "/sce_sys/icon0.png";
        const std::string eboot = appDir + "/eboot.bin";

        logMilestone(std::string("Expected LiveArea/app path: ") + appDir);

        // Hardware can lag after PromotePkg; retry instead of a single short wait.
        bool hasTree = false;
        bool hasIcon = false;
        for (int attempt = 1; attempt <= 8; ++attempt) {
            sceKernelDelayThread((400 + attempt * 200) * 1000);

            hasTree = st.exists(appDir) && (st.exists(paramSfo) || st.exists(eboot));
            hasIcon = st.exists(icon0);

            char buf[160];
            sceClibSnprintf(
                buf, sizeof(buf),
                "Post-promote check #%d: dir=%d param=%d eboot=%d icon0=%d",
                attempt,
                st.exists(appDir) ? 1 : 0,
                st.exists(paramSfo) ? 1 : 0,
                st.exists(eboot) ? 1 : 0,
                hasIcon ? 1 : 0
            );
            logMilestone(buf);

            if (hasTree) break;
        }

        logPathState("Post-promote app directory", appDir);
        logPathState("Post-promote param.sfo", paramSfo);
        logPathState("Post-promote icon0.png", icon0);
        logPathState("Post-promote eboot.bin", eboot);

        if (!hasTree) {
            // Vita3K often returns PromotePkg success without creating ux0:app.
            // Fallback: copy the extracted package tree into place (homebrew layout).
            logMilestone("Promote returned OK but app tree missing — trying direct copy fallback");
            if (st.exists(tmpDir) && copyTreeRecursive(tmpDir, appDir)) {
                hasTree = st.exists(appDir) && (st.exists(paramSfo) || st.exists(eboot));
                hasIcon = st.exists(icon0);
                logMilestone(hasTree
                    ? "Fallback copy to ux0:app succeeded"
                    : "Fallback copy finished but verification still failed");
            } else {
                logMilestone("Fallback copy failed or tmpDir missing");
            }
        }

        lastLiveAreaOk_ = hasTree;

        if (hasTree) {
            logMilestone(std::string("Post-promote verification: app tree OK") +
                         (hasIcon ? " (icon0 present)" : " (icon0 missing, still OK)"));
        } else {
            logMilestone("WARNING: promoter success but ux0:app tree not visible yet");
            logMilestone("Hint: VitaShell → Refresh LiveArea, or reboot the console");
            lastLiveAreaOk_ = false;
        }
    }

    if (onProgress) {
        InstallProgress p;
        p.stage = result == InstallResult::Ok ? InstallProgress::Cleaning : InstallProgress::Error;
        p.message = result == InstallResult::Ok ? "cleaning temporary files" : lastError_;
        onProgress(p);
    }

    // Cleanup only after verification so mid-crash still leaves tmp + logs.
    if (result == InstallResult::Ok && deleteTempOnSuccess) {
        if (!removeTree(tmpDir)) {
            sceClibPrintf("[HomebrewInstaller] warning: cleanup failed for %s\n", tmpDir.c_str());
            logLine(std::string("WARNING: cleanup failed for ") + tmpDir);
        } else {
            logLine("Temporary directory cleanup: success");
        }
    }

    {
        char endBuf[192];
        sceClibSnprintf(
            endBuf, sizeof(endBuf),
            "installVpk END result=%s promote=0x%08X liveArea=%s titleId=%s",
            toString(result),
            static_cast<unsigned int>(lastPromoteResult_),
            lastLiveAreaOk_ ? "yes" : "no",
            titleId.c_str()
        );
        logMilestone(endBuf);
    }

    if (result == InstallResult::Ok && onProgress) {
        InstallProgress p;
        p.stage = InstallProgress::Done;
        p.bytesWritten = 1;
        p.bytesTotal = 1;
        if (lastLiveAreaOk_) {
            p.message = "Installed — open LiveArea (Refresh LiveArea if bubble missing)";
        } else if (!titleId.empty()) {
            p.message = "Promote OK — check ux0:app and Refresh LiveArea if needed";
        } else {
            p.message = "VPK install finished";
        }
        onProgress(p);
    }
    return result;
}

} // namespace psvitaalive

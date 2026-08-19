#include "installer/homebrew_installer.hpp"
#include "installer/refresh_manager.hpp"
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
// VitaDB uses ux0:/data/vdb_vpk - shallow path under ux0:data is required on real hardware.
constexpr const char* kVpkPromoteDir = "ux0:data/psva_vpk";

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
    // VitaDB install sequence (source/main.cpp + promoter.cpp):
    //   1) package lives at shallow ux0:/data/vdb_vpk
    //   2) makeHeadBin(dir) if head.bin missing
    //   3) scePromoterUtilityInit()
    //   4) scePromoterUtilityPromotePkg(dir, 0)   // ASYNC
    //   5) while (GetState(&state) >= 0 && state) wait
    //   6) scePromoterUtilityTerm/Exit
    //   7) success iff promote dir was consumed (stat fails)
    // We mirror that exactly. GetResult is logged only - VitaDB never uses it.
    logLine(std::string("promoteExtractedDir: ") + dir);

    StorageManager st;
    std::string promoteDir = kVpkPromoteDir;

    if (dir != promoteDir) {
        logLine(std::string("staging to VitaDB-style path: ") + promoteDir);
        if (st.exists(promoteDir)) {
            removeTree(promoteDir);
        }
        const int ren = sceIoRename(dir.c_str(), promoteDir.c_str());
        if (ren < 0) {
            char buf[112];
            sceClibSnprintf(buf, sizeof(buf), "sceIoRename failed: 0x%08X - recursive copy", ren);
            logLine(buf);
            if (!st.createDirectories(promoteDir) || !copyTreeRecursive(dir, promoteDir)) {
                setError("cannot stage package into ux0:data/psva_vpk");
                return InstallResult::IoError;
            }
            removeTree(dir);
        } else {
            logLine("package staged via rename");
        }
    }

    // Ensure package dir ends without requiring trailing slash for PromotePkg.
    logPathState("Promote package root", promoteDir);
    logPathState("Promote eboot.bin", promoteDir + "/eboot.bin");
    logPathState("Promote param.sfo", promoteDir + "/sce_sys/param.sfo");
    logPathState("Promote head.bin", promoteDir + "/sce_sys/package/head.bin");

    if (!st.exists(promoteDir + "/eboot.bin") || !st.exists(promoteDir + "/sce_sys/param.sfo")) {
        setError("invalid package layout before promote");
        return InstallResult::ExtractFailed;
    }
    if (!st.exists(promoteDir + "/sce_sys/package/head.bin")) {
        setError("missing sce_sys/package/head.bin before promote");
        return InstallResult::PromoteFailed;
    }

    const int initResult = scePromoterUtilityInit();
    logResult("scePromoterUtilityInit", initResult);
    if (initResult < 0) {
        char buf[64];
        sceClibSnprintf(buf, sizeof(buf), "scePromoterUtilityInit: 0x%08X", initResult);
        setError(buf);
        lastPromoteResult_ = initResult;
        return InstallResult::PromoteFailed;
    }

    logMilestone("PromotePkg async begin (VitaDB: sync=0 + GetState)");
    const int promoteResult = scePromoterUtilityPromotePkg(promoteDir.c_str(), 0);
    logResult("scePromoterUtilityPromotePkg(sync=0)", promoteResult);
    lastPromoteResult_ = promoteResult;
    {
        char buf[128];
        sceClibSnprintf(buf, sizeof(buf), "PromotePkg returned 0x%08X (%d)", promoteResult, promoteResult);
        logMilestone(buf);
    }

    if (promoteResult < 0) {
        scePromoterUtilityExit();
        char buf[80];
        sceClibSnprintf(buf, sizeof(buf), "scePromoterUtilityPromotePkg: 0x%08X", promoteResult);
        setError(buf);
        return InstallResult::PromoteFailed;
    }

    // Poll until idle - same loop structure as VitaDB (no frame swap required).
    int state = 1;
    int pollCount = 0;
    const int kMaxPolls = 12000; // ~120s @ 10ms
    while (pollCount < kMaxPolls) {
        state = 0;
        const int stRes = scePromoterUtilityGetState(&state);
        if (stRes < 0) {
            char buf[96];
            sceClibSnprintf(buf, sizeof(buf), "scePromoterUtilityGetState: 0x%08X", stRes);
            logLine(buf);
            lastPromoteResult_ = stRes;
            break;
        }
        if (state == 0) break;
        ++pollCount;
        if ((pollCount % 200) == 0) {
            char buf[72];
            sceClibSnprintf(buf, sizeof(buf), "PromotePkg waiting state=%d polls=%d", state, pollCount);
            logLine(buf);
        }
        sceKernelDelayThread(10 * 1000);
    }

    // Diagnostic only - VitaDB does not gate success on GetResult.
    int operationResult = 0;
    const int getResultCall = scePromoterUtilityGetResult(&operationResult);
    {
        char buf[200];
        sceClibSnprintf(
            buf, sizeof(buf),
            "GetResult (diagnostic) call=0x%08X op=0x%08X polls=%d finalState=%d",
            getResultCall, operationResult, pollCount, state
        );
        logMilestone(buf);
    }

    const int exitResult = scePromoterUtilityExit();
    logResult("scePromoterUtilityExit", exitResult);

    // VitaDB success criterion: TEMP_INSTALL_DIR no longer exists.
    const bool promoteDirGone = !st.exists(promoteDir);
    logLine(promoteDirGone
        ? "promote dir consumed (VitaDB success signal)"
        : "promote dir still present after PromotePkg");

    if (promoteResult < 0) {
        setError("PromotePkg failed");
        return InstallResult::PromoteFailed;
    }

    if (!promoteDirGone && state != 0) {
        setError("promoter did not finish (timeout)");
        lastPromoteResult_ = -1;
        return InstallResult::PromoteFailed;
    }

    // If dir still present but state==0, treat as soft failure for caller to recover
    // via copy fallback; do not hard-fail on GetResult==-1 (common on some FW).
    if (!promoteDirGone) {
        logLine("WARNING: promote finished but staging dir remains - caller may fallback");
        lastPromoteResult_ = (getResultCall >= 0 && operationResult != 0) ? operationResult : lastPromoteResult_;
        // Still return Ok so post-check / fallback can run; filesystem is source of truth.
    }

    logLine("promoteExtractedDir: promoter sequence completed");
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

    // Extract straight into the shallow promote root (VitaDB: ux0:/data/vdb_vpk).
    const std::string tmpDir = kVpkPromoteDir;
    if (st.exists(tmpDir)) {
        removeTree(tmpDir);
        logLine(std::string("cleared previous promote dir: ") + tmpDir);
    }
    if (!st.createDirectories(tmpDir)) {
        setError("cannot create VPK promote directory (ux0:data/psva_vpk)");
        return InstallResult::IoError;
    }
    logLine(std::string("VPK extract/promote directory: ") + tmpDir);

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

    // Verify / recover install whenever we have a Title ID: promoter OK, or
    // promoter failed but staged package may still be recovered via copy+refresh.
    if (!titleId.empty() &&
        (result == InstallResult::Ok || result == InstallResult::PromoteFailed)) {
        const std::string appDir = std::string("ux0:app/") + titleId;
        const std::string paramSfo = appDir + "/sce_sys/param.sfo";
        const std::string icon0 = appDir + "/sce_sys/icon0.png";
        const std::string eboot = appDir + "/eboot.bin";
        constexpr const char* kPromoteDir = "ux0:data/psva_vpk";

        logMilestone(std::string("Expected LiveArea/app path: ") + appDir);

        // Hardware can lag after PromotePkg; retry instead of a single short wait.
        bool hasTree = false;
        bool hasIcon = false;
        const int maxAttempts = (result == InstallResult::Ok) ? 8 : 2;
        for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
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

        if (hasTree && result == InstallResult::PromoteFailed) {
            logMilestone("App tree present after reported promote failure - treating as success");
            result = InstallResult::Ok;
            lastError_.clear();
        }

        if (!hasTree) {
            // Prefer staged promote dir (after rename); fall back to original extract dir.
            const std::string srcDir = st.exists(kPromoteDir) ? std::string(kPromoteDir)
                : (st.exists(tmpDir) ? tmpDir : std::string());
            logMilestone(std::string("App tree missing - trying direct copy fallback from ") +
                (srcDir.empty() ? "(none)" : srcDir));
            if (!srcDir.empty() && copyTreeRecursive(srcDir, appDir)) {
                hasTree = st.exists(appDir) && (st.exists(paramSfo) || st.exists(eboot));
                hasIcon = st.exists(icon0);
                logMilestone(hasTree
                    ? "Fallback copy to ux0:app succeeded"
                    : "Fallback copy finished but verification still failed");
                if (hasTree) {
                    result = InstallResult::Ok;
                    lastError_.clear();
                }

                // Fallback only copies files; it does not register the LiveArea bubble.
                // VitaShell-style refresh: move ux0:app/<id> → ux0:temp/app and PromotePkg.
                if (hasTree && !titleId.empty()) {
                    logMilestone("Fallback: attempting LiveArea refresh promote");
                    std::string refreshMsg;
                    if (RefreshManager::refreshTitleLiveArea(titleId, refreshMsg)) {
                        logMilestone(std::string("Fallback LiveArea refresh OK: ") + refreshMsg);
                        hasTree = st.exists(appDir) && (st.exists(paramSfo) || st.exists(eboot));
                        hasIcon = st.exists(icon0);
                        lastLiveAreaOk_ = hasTree;
                    } else {
                        logMilestone(std::string("Fallback LiveArea refresh soft-fail: ") + refreshMsg);
                        // Files should still be under ux0:app from the copy / restore path.
                        hasTree = st.exists(appDir) && (st.exists(paramSfo) || st.exists(eboot));
                        hasIcon = st.exists(icon0);
                    }
                }
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

    // Cleanup staging after verification.
    // Real hardware often consumes the package dir; Vita3K frequently leaves
    // ux0:data/psva_vpk behind - always scrub extract + promote paths.
    constexpr const char* kVpkPromoteDir = "ux0:data/psva_vpk";
    if (result == InstallResult::Ok && deleteTempOnSuccess) {
        if (st.exists(tmpDir)) {
            if (!removeTree(tmpDir)) {
                sceClibPrintf("[HomebrewInstaller] warning: cleanup failed for %s
", tmpDir.c_str());
                logLine(std::string("WARNING: cleanup failed for ") + tmpDir);
            } else {
                logLine("Temporary directory cleanup: success");
            }
        }
        if (st.exists(kVpkPromoteDir)) {
            if (!removeTree(kVpkPromoteDir)) {
                logLine(std::string("WARNING: cleanup failed for ") + kVpkPromoteDir);
            } else {
                logLine("Promote staging directory cleanup: success");
            }
        }
    } else if (result != InstallResult::Ok) {
        if (st.exists(kVpkPromoteDir)) {
            removeTree(kVpkPromoteDir);
            logLine("Promote staging directory removed after failed install");
        }
        if (st.exists(tmpDir)) {
            removeTree(tmpDir);
            logLine("Temporary directory removed after failed install");
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
            p.message = "Installed - open LiveArea (Refresh LiveArea if bubble missing)";
        } else if (!titleId.empty()) {
            p.message = "Promote OK - check ux0:app and Refresh LiveArea if needed";
        } else {
            p.message = "VPK install finished";
        }
        onProgress(p);
    }
    return result;
}

} // namespace psvitaalive

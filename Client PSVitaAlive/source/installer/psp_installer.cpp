#include "installer/psp_installer.hpp"
#include "localization/localization.hpp"
#include "archive/format_detector.hpp"
#include "storage/storage_manager.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>

#include <cctype>
#include <string>
#include <vector>

namespace psvitaalive {

namespace {
constexpr const char* ISO_DIR = "ux0:pspemu/ISO";
constexpr const char* GAME_DIR = "ux0:pspemu/PSP/GAME";

std::string baseName(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string stripExt(const std::string& name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return name;
    return name.substr(0, dot);
}

std::string sanitizeId(std::string s) {
    for (char& c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'))
            c = '_';
    }
    if (s.empty()) s = "GAME";
    if (s.size() > 32) s.resize(32);
    return s;
}
} // namespace

const char* toString(PspInstallResult r) {
    switch (r) {
        case PspInstallResult::Ok: return "Ok";
        case PspInstallResult::InvalidArgument: return "InvalidArgument";
        case PspInstallResult::Unsupported: return "Unsupported";
        case PspInstallResult::IoError: return "IoError";
        case PspInstallResult::Cancelled: return "Cancelled";
        case PspInstallResult::PluginMissing: return "PluginMissing";
        case PspInstallResult::UnknownError: return "UnknownError";
        default: return "Unknown";
    }
}

void PspInstaller::setError(const std::string& msg) {
    lastError_ = msg;
    sceClibPrintf("[PspInstaller] %s\n", msg.c_str());
}

PspInstallResult PspInstaller::copyFile(
    const std::string& src,
    const std::string& dst,
    PspInstallProgressFn onProgress,
    PspInstallCancelFn shouldCancel
) {
    StorageManager st;
    SceUID in = sceIoOpen(src.c_str(), SCE_O_RDONLY, 0);
    if (in < 0) {
        setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgCannotOpenSource));
        return PspInstallResult::IoError;
    }
    const int64_t total = st.fileSize(src);
    SceUID out = sceIoOpen(dst.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (out < 0) {
        sceIoClose(in);
        setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgCannotOpenDest));
        return PspInstallResult::IoError;
    }

    std::vector<char> buf(64 * 1024);
    uint64_t copied = 0;
    while (true) {
        if (shouldCancel && shouldCancel()) {
            sceIoClose(in);
            sceIoClose(out);
            st.removeFile(dst);
            setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgCancelled));
            return PspInstallResult::Cancelled;
        }
        const int rd = sceIoRead(in, buf.data(), buf.size());
        if (rd < 0) {
            sceIoClose(in);
            sceIoClose(out);
            setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgReadFailed));
            return PspInstallResult::IoError;
        }
        if (rd == 0) break;
        if (sceIoWrite(out, buf.data(), rd) != rd) {
            sceIoClose(in);
            sceIoClose(out);
            setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgWriteFailed));
            return PspInstallResult::IoError;
        }
        copied += static_cast<uint64_t>(rd);
        if (onProgress) {
            PspInstallProgress p;
            p.stage = PspInstallProgress::Copying;
            p.current = copied;
            p.total = total > 0 ? static_cast<uint64_t>(total) : 0;
            p.message = "Copying to pspemu";
            onProgress(p);
        }
    }
    sceIoClose(in);
    sceIoClose(out);
    return PspInstallResult::Ok;
}

PspInstallResult PspInstaller::installIsoCso(
    const std::string& path,
    PspInstallProgressFn onProgress,
    PspInstallCancelFn shouldCancel
) {
    lastError_.clear();
    lastInstallPath_.clear();
    if (path.empty()) {
        setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgEmptyPath));
        return PspInstallResult::InvalidArgument;
    }
    StorageManager st;
    if (!st.exists(path)) {
        setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgFileNotFound));
        return PspInstallResult::IoError;
    }

    FormatDetector det;
    const DetectResult d = det.detectFile(path);
    const std::string ext = FormatDetector::extensionOf(path);
    if (d.format != FileFormat::Iso && d.format != FileFormat::Cso &&
        ext != "iso" && ext != "cso") {
        setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgNotIsoCso));
        return PspInstallResult::Unsupported;
    }

    if (!st.createDirectories(ISO_DIR)) {
        setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgCannotCreateDir));
        return PspInstallResult::IoError;
    }

    const std::string name = baseName(path);
    const std::string dest = std::string(ISO_DIR) + "/" + name;
    if (onProgress) {
        PspInstallProgress p;
        p.stage = PspInstallProgress::Preparing;
        p.message = "Preparing Adrenaline ISO folder";
        onProgress(p);
    }

    const PspInstallResult r = copyFile(path, dest, onProgress, shouldCancel);
    if (r != PspInstallResult::Ok) return r;
    lastInstallPath_ = dest;
    if (onProgress) {
        PspInstallProgress p;
        p.stage = PspInstallProgress::Done;
        p.current = 1;
        p.total = 1;
        p.message = "ISO/CSO ready for Adrenaline";
        onProgress(p);
    }
    return PspInstallResult::Ok;
}

PspInstallResult PspInstaller::installPbp(
    const std::string& path,
    PspInstallProgressFn onProgress,
    PspInstallCancelFn shouldCancel
) {
    lastError_.clear();
    lastInstallPath_.clear();
    if (path.empty()) {
        setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgEmptyPath));
        return PspInstallResult::InvalidArgument;
    }
    StorageManager st;
    if (!st.exists(path)) {
        setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgFileNotFound));
        return PspInstallResult::IoError;
    }

    FormatDetector det;
    const DetectResult d = det.detectFile(path);
    const std::string ext = FormatDetector::extensionOf(path);
    if (d.format != FileFormat::Pbp && ext != "pbp") {
        setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgNotPbp));
        return PspInstallResult::Unsupported;
    }

    const std::string id = sanitizeId(stripExt(baseName(path)));
    const std::string gameDir = std::string(GAME_DIR) + "/" + id;
    if (!st.createDirectories(gameDir)) {
        setError(::psvitaalive::L(::psvitaalive::TextId::InstMsgCannotCreateDir));
        return PspInstallResult::IoError;
    }
    const std::string dest = gameDir + "/EBOOT.PBP";

    if (onProgress) {
        PspInstallProgress p;
        p.stage = PspInstallProgress::Preparing;
        p.message = "Preparing PSP GAME folder";
        onProgress(p);
    }

    const PspInstallResult r = copyFile(path, dest, onProgress, shouldCancel);
    if (r != PspInstallResult::Ok) return r;
    lastInstallPath_ = dest;
    if (onProgress) {
        PspInstallProgress p;
        p.stage = PspInstallProgress::Done;
        p.current = 1;
        p.total = 1;
        p.message = "PBP installed under pspemu (Adrenaline / NoPspEmuDrm)";
        onProgress(p);
    }
    return PspInstallResult::Ok;
}

} // namespace psvitaalive

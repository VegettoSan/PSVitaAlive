#include "installer/install_dispatcher.hpp"
#include "archive/format_detector.hpp"
#include "archive/zip_extractor.hpp"
#include "installer/homebrew_installer.hpp"
#include "installer/vita_installer.hpp"
#include "storage/storage_manager.hpp"

#include <psp2/kernel/clib.h>
#include <algorithm>
#include <cctype>

namespace psvitaalive {
namespace {
std::string lowerExtension(const std::string& path) {
    std::string out = FormatDetector::extensionOf(path);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}
}

const char* toString(InstallDispatchResult result) {
    switch (result) {
        case InstallDispatchResult::Ok: return "Ok";
        case InstallDispatchResult::InvalidArgument: return "InvalidArgument";
        case InstallDispatchResult::UnsupportedFormat: return "UnsupportedFormat";
        case InstallDispatchResult::DetectFailed: return "DetectFailed";
        case InstallDispatchResult::DownloadRequired: return "DownloadRequired";
        case InstallDispatchResult::InstallFailed: return "InstallFailed";
        case InstallDispatchResult::Cancelled: return "Cancelled";
        case InstallDispatchResult::IoError: return "IoError";
        case InstallDispatchResult::UnknownError: return "UnknownError";
        default: return "Unknown";
    }
}

void InstallDispatcher::setError(const std::string& message) {
    lastError_ = message;
    sceClibPrintf("[InstallDispatcher] %s\n", lastError_.c_str());
}

InstallDispatchResult InstallDispatcher::installFile(
    const std::string& path,
    InstallDispatchProgressFn onProgress,
    InstallDispatchCancelFn shouldCancel,
    const std::string& zipDestination
) {
    lastError_.clear();
    if (path.empty()) { setError("empty installation path"); return InstallDispatchResult::InvalidArgument; }
    StorageManager st;
    if (!st.exists(path)) { setError("installation file not found"); return InstallDispatchResult::IoError; }
    if (shouldCancel && shouldCancel()) { setError("cancelled"); return InstallDispatchResult::Cancelled; }

    if (onProgress) {
        InstallDispatchProgress p;
        p.stage = InstallDispatchProgress::Detecting;
        p.message = "Detecting format";
        onProgress(p);
    }

    FormatDetector detector;
    const DetectResult detected = detector.detectFile(path);
    const std::string ext = lowerExtension(path);

    if (detected.format == FileFormat::Vpk || ext == "vpk") {
        HomebrewInstaller installer;
        const InstallResult result = installer.installVpk(
            path,
            [&](const InstallProgress& ip) {
                if (!onProgress) return;
                InstallDispatchProgress p;
                p.current = ip.bytesWritten;
                p.total = ip.bytesTotal;
                switch (ip.stage) {
                    case InstallProgress::Preparing: p.stage = InstallDispatchProgress::Detecting; break;
                    case InstallProgress::Extracting: p.stage = InstallDispatchProgress::Extracting; break;
                    case InstallProgress::Promoting: p.stage = InstallDispatchProgress::Promoting; break;
                    case InstallProgress::Cleaning: p.stage = InstallDispatchProgress::Cleaning; break;
                    case InstallProgress::Done: p.stage = InstallDispatchProgress::Completed; break;
                    default: p.stage = InstallDispatchProgress::Error; break;
                }
                p.message = ip.message;
                onProgress(p);
            },
            shouldCancel,
            true
        );
        if (result == InstallResult::Ok) {
            if (onProgress) {
                InstallDispatchProgress p;
                p.stage = InstallDispatchProgress::Completed;
                p.current = 1;
                p.total = 1;
                p.message = "VPK installed";
                onProgress(p);
            }
            return InstallDispatchResult::Ok;
        }
        if (result == InstallResult::Cancelled) {
            setError(installer.lastError());
            return InstallDispatchResult::Cancelled;
        }
        setError(installer.lastError());
        return InstallDispatchResult::InstallFailed;
    }

    if (detected.format == FileFormat::Pkg || ext == "pkg") {
        VitaInstaller installer;
        const VitaInstallResult result = installer.installPkg(
            path,
            [&](const VitaInstallProgress& ip) {
                if (!onProgress) return;
                InstallDispatchProgress p;
                p.current = ip.current;
                p.total = ip.total;
                switch (ip.stage) {
                    case VitaInstallProgress::Preparing: p.stage = InstallDispatchProgress::Detecting; break;
                    case VitaInstallProgress::Promoting: p.stage = InstallDispatchProgress::Promoting; break;
                    case VitaInstallProgress::Done: p.stage = InstallDispatchProgress::Completed; break;
                    case VitaInstallProgress::Error: p.stage = InstallDispatchProgress::Error; break;
                    default: p.stage = InstallDispatchProgress::Installing; break;
                }
                p.message = ip.message;
                onProgress(p);
            },
            shouldCancel,
            true
        );
        if (result == VitaInstallResult::Ok) {
            if (onProgress) {
                InstallDispatchProgress p;
                p.stage = InstallDispatchProgress::Completed;
                p.current = 1;
                p.total = 1;
                p.message = "PKG installed";
                onProgress(p);
            }
            return InstallDispatchResult::Ok;
        }
        if (result == VitaInstallResult::Cancelled) {
            setError(installer.lastError());
            return InstallDispatchResult::Cancelled;
        }
        setError(installer.lastError());
        return InstallDispatchResult::InstallFailed;
    }

    if (detected.format == FileFormat::Zip || ext == "zip") {
        if (zipDestination.empty()) { setError("ZIP destination is required"); return InstallDispatchResult::InvalidArgument; }
        ZipExtractor extractor;
        const ZipResult result = extractor.extract(
            path, zipDestination,
            [&](const ZipProgress& zp) {
                if (!onProgress) return;
                InstallDispatchProgress p;
                p.stage = InstallDispatchProgress::Extracting;
                p.current = zp.bytesWritten;
                p.total = zp.bytesTotal;
                p.message = zp.currentEntry.empty() ? "Extracting ZIP" : std::string("Extracting: ") + zp.currentEntry;
                onProgress(p);
            },
            shouldCancel
        );
        if (result == ZipResult::Cancelled) { setError("ZIP extraction cancelled"); return InstallDispatchResult::Cancelled; }
        if (result != ZipResult::Ok) { setError(extractor.lastError()); return InstallDispatchResult::InstallFailed; }
        if (onProgress) {
            InstallDispatchProgress p;
            p.stage = InstallDispatchProgress::Completed;
            p.current = 1;
            p.total = 1;
            p.message = "ZIP extracted";
            onProgress(p);
        }
        return InstallDispatchResult::Ok;
    }

    if (detected.format == FileFormat::Pbp) { setError("PBP installer is not implemented yet"); return InstallDispatchResult::UnsupportedFormat; }
    if (detected.format == FileFormat::Iso) { setError("ISO installer is not implemented yet"); return InstallDispatchResult::UnsupportedFormat; }
    if (detected.format == FileFormat::Cso) { setError("CSO installer is not implemented yet"); return InstallDispatchResult::UnsupportedFormat; }
    setError(std::string("unsupported format: ") + toString(detected.format));
    return InstallDispatchResult::UnsupportedFormat;
}

} // namespace psvitaalive

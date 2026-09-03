#include "installer/install_dispatcher.hpp"
#include "archive/format_detector.hpp"
#include "archive/zip_extractor.hpp"
#include "installer/homebrew_installer.hpp"
#include "installer/vita_installer.hpp"
#include "installer/psp_installer.hpp"
#include "installer/app_settings.hpp"
#include "psp_pkg_unpack.h"
#include "storage/storage_manager.hpp"
#include "diagnostic_logger.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cctype>

namespace psvitaalive {
namespace {

std::string lowerExtension(const std::string& path) {
    std::string out = FormatDetector::extensionOf(path);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

} // namespace

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
    diagnostics::log(std::string("[InstallDispatcher] ") + lastError_);
}

void InstallDispatcher::clearResultMeta() {
    lastTitleId_.clear();
    lastInstallPath_.clear();
    lastLiveAreaOk_ = false;
}

InstallDispatchResult InstallDispatcher::installFile(
    const std::string& path,
    InstallDispatchProgressFn onProgress,
    InstallDispatchCancelFn shouldCancel,
    const std::string& zipDestination,
    const std::string& rifPath
) {
    lastError_.clear();
    clearResultMeta();
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
    diagnostics::log(std::string("[InstallDispatcher] detect format=") + toString(detected.format) + " ext=" + ext +
        " pspTarget=" + AppSettings::toString(pspTarget_));

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
        lastTitleId_ = installer.lastTitleId();
        lastInstallPath_ = installer.lastInstallPath();
        lastLiveAreaOk_ = installer.lastLiveAreaOk();
        if (result == InstallResult::Ok) {
            diagnostics::log(std::string("[InstallDispatcher] VPK OK path=") + lastInstallPath_ +
                " liveArea=" + (lastLiveAreaOk_ ? "yes" : "no"));
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
        // Adrenaline unpack ONLY for PSP/PS1 PKG content types.
        // Vita Games PKG must keep LiveArea / BGDL / promoter path regardless of pspTarget.
        if (pspTarget_ == PspTarget::Adrenaline) {
            diagnostics::log(std::string("[InstallDispatcher] probing PKG type path=") + path);
            const int isPspPsx = psp_pkg_probe_is_psp_psx(path.c_str());
            diagnostics::log(std::string("[InstallDispatcher] PKG probe is_psp_psx=") +
                std::to_string(isPspPsx) + " path=" + path);
            if (isPspPsx == 1) {
                if (onProgress) {
                    InstallDispatchProgress p;
                    p.stage = InstallDispatchProgress::Installing;
                    p.message = "Unpacking PSP/PS1 PKG to Adrenaline (pspemu)...";
                    onProgress(p);
                }
                char installed[512];
                installed[0] = 0;
                diagnostics::log(std::string("[InstallDispatcher] Adrenaline PKG unpack begin path=") + path);
                const int asIso = (pspMediaFormat_ == PspMediaFormat::Iso) ? 1 : 0;
                diagnostics::log(std::string("[InstallDispatcher] Adrenaline media format=") +
                    AppSettings::toString(pspMediaFormat_));
                diagnostics::log("[InstallDispatcher] calling psp_pkg_unpack_to_pspemu");
                const int ur = psp_pkg_unpack_to_pspemu(path.c_str(), "ux0:", asIso, installed, sizeof(installed));
                diagnostics::log(std::string("[InstallDispatcher] unpack returned ") + std::to_string(ur));
                if (ur != 0) {
                    const char* err = pkg2zip_last_error();
                    setError(err && err[0] ? err : "PSP/PS1 PKG unpack to pspemu failed");
                    diagnostics::log(std::string("[InstallDispatcher] Adrenaline PKG unpack failed: ") + lastError_);
                    return InstallDispatchResult::InstallFailed;
                }
                lastLiveAreaOk_ = false;
                lastTitleId_.clear();
                lastInstallPath_ = installed[0] ? installed : "ux0:pspemu";
                if (onProgress) {
                    InstallDispatchProgress p;
                    p.stage = InstallDispatchProgress::Completed;
                    p.current = 1;
                    p.total = 1;
                    p.message = "Installed to Adrenaline (pspemu)";
                    onProgress(p);
                }
                diagnostics::log(std::string("[InstallDispatcher] Adrenaline PKG unpack OK path=") + lastInstallPath_);
                return InstallDispatchResult::Ok;
            }
            if (isPspPsx == 0) {
                diagnostics::log("[InstallDispatcher] Vita (or non-PSP) PKG — ignoring Adrenaline target, using LiveArea/promoter");
            } else {
                diagnostics::log("[InstallDispatcher] PKG probe failed — falling back to LiveArea/promoter path");
            }
        }
        VitaInstaller installer;
        auto progressBridge = [&](const VitaInstallProgress& ip) {
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
        };
        const VitaInstallResult result = rifPath.empty()
            ? installer.installPkg(path, progressBridge, shouldCancel, true)
            : installer.installPkgWithRif(path, rifPath, progressBridge, shouldCancel, true);
        // PKG path: Promoter may create LiveArea entry; we do not always know TITLE_ID.
        lastInstallPath_ = "LiveArea (PKG via Promoter)";
        lastLiveAreaOk_ = (result == VitaInstallResult::Ok);
        if (result == VitaInstallResult::Ok) {
            diagnostics::log("[InstallDispatcher] PKG OK (promoter)");
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
        // VPKs are ZIP containers. If the download lost the .vpk extension (or
        // magic detection only saw ZIP), try HomebrewInstaller when the user did
        // not supply an explicit extract path. A real data ZIP still needs a
        // destination and will fail the VPK layout check with a clear error.
        if (zipDestination.empty()) {
            diagnostics::log("[InstallDispatcher] ZIP without destination — trying as VPK (ZIP-backed homebrew)");
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
            lastTitleId_ = installer.lastTitleId();
            lastInstallPath_ = installer.lastInstallPath();
            lastLiveAreaOk_ = installer.lastLiveAreaOk();
            if (result == InstallResult::Ok) {
                diagnostics::log(std::string("[InstallDispatcher] ZIP-as-VPK OK titleId=") + lastTitleId_);
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
            // Not a valid VPK layout — report clearly instead of the old
            // "ZIP destination is required" message that hid the real problem.
            setError(installer.lastError().empty()
                ? "ZIP file is not a VPK and no extract path was provided"
                : (std::string("Not a VPK / install failed: ") + installer.lastError()));
            return InstallDispatchResult::InstallFailed;
        }
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
        lastInstallPath_ = zipDestination;
        lastLiveAreaOk_ = false; // ZIP extract is not a LiveArea promote
        if (result == ZipResult::Cancelled) { setError("ZIP extraction cancelled"); return InstallDispatchResult::Cancelled; }
        if (result != ZipResult::Ok) { setError(extractor.lastError()); return InstallDispatchResult::InstallFailed; }
        diagnostics::log(std::string("[InstallDispatcher] ZIP extracted to ") + zipDestination);
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

    if (detected.format == FileFormat::Pbp) {
        PspInstaller psp;
        const PspInstallResult pr = psp.installPbp(path,
            [&](const PspInstallProgress& pp) {
                if (!onProgress) return;
                InstallDispatchProgress p;
                p.stage = InstallDispatchProgress::Installing;
                p.current = pp.current; p.total = pp.total;
                p.message = pp.message.empty() ? "Installing PBP" : pp.message;
                onProgress(p);
            }, shouldCancel);
        lastInstallPath_ = psp.lastInstallPath(); lastLiveAreaOk_ = false;
        if (pr == PspInstallResult::Cancelled) { setError("PBP install cancelled"); return InstallDispatchResult::Cancelled; }
        if (pr != PspInstallResult::Ok) { setError(psp.lastError()); return InstallDispatchResult::InstallFailed; }
        if (onProgress) {
            InstallDispatchProgress p; p.stage = InstallDispatchProgress::Completed; p.current=1; p.total=1;
            p.message = "PBP copied to pspemu"; onProgress(p);
        }
        return InstallDispatchResult::Ok;
    }
    if (detected.format == FileFormat::Iso || detected.format == FileFormat::Cso) {
        PspInstaller psp;
        const PspInstallResult pr = psp.installIsoCso(path,
            [&](const PspInstallProgress& pp) {
                if (!onProgress) return;
                InstallDispatchProgress p;
                p.stage = InstallDispatchProgress::Installing;
                p.current = pp.current; p.total = pp.total;
                p.message = pp.message.empty() ? "Installing ISO/CSO" : pp.message;
                onProgress(p);
            }, shouldCancel);
        lastInstallPath_ = psp.lastInstallPath(); lastLiveAreaOk_ = false;
        if (pr == PspInstallResult::Cancelled) { setError("ISO/CSO install cancelled"); return InstallDispatchResult::Cancelled; }
        if (pr != PspInstallResult::Ok) { setError(psp.lastError()); return InstallDispatchResult::InstallFailed; }
        if (onProgress) {
            InstallDispatchProgress p; p.stage = InstallDispatchProgress::Completed; p.current=1; p.total=1;
            p.message = "ISO/CSO ready for Adrenaline"; onProgress(p);
        }
        return InstallDispatchResult::Ok;
    }
    setError(std::string("unsupported format: ") + toString(detected.format));
    return InstallDispatchResult::UnsupportedFormat;
}

} // namespace psvitaalive

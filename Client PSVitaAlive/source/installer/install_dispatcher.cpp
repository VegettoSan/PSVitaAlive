#include "installer/install_dispatcher.hpp"

#include "archive/format_detector.hpp"
#include "installer/homebrew_installer.hpp"
#include "installer/vita_installer.hpp"

#include <psp2/kernel/clib.h>

namespace psvitaalive {

const char* toString(InstallDispatchResult result) {
    switch (result) {
        case InstallDispatchResult::Ok:
            return "Ok";

        case InstallDispatchResult::InvalidArgument:
            return "InvalidArgument";

        case InstallDispatchResult::UnsupportedFormat:
            return "UnsupportedFormat";

        case InstallDispatchResult::DetectFailed:
            return "DetectFailed";

        case InstallDispatchResult::DownloadRequired:
            return "DownloadRequired";

        case InstallDispatchResult::InstallFailed:
            return "InstallFailed";

        case InstallDispatchResult::Cancelled:
            return "Cancelled";

        case InstallDispatchResult::IoError:
            return "IoError";

        case InstallDispatchResult::UnknownError:
            return "UnknownError";

        default:
            return "Unknown";
    }
}

void InstallDispatcher::setError(const std::string& message) {
    lastError_ = message;

    sceClibPrintf(
        "[InstallDispatcher] %s\n",
        lastError_.c_str()
    );
}

InstallDispatchResult InstallDispatcher::installFile(
    const std::string& path,
    InstallDispatchProgressFn onProgress,
    InstallDispatchCancelFn shouldCancel
) {
    lastError_.clear();

    if (path.empty()) {
        setError("empty installation path");
        return InstallDispatchResult::InvalidArgument;
    }

    if (shouldCancel && shouldCancel()) {
        setError("cancelled");
        return InstallDispatchResult::Cancelled;
    }

    if (onProgress) {
        InstallDispatchProgress p;
        p.stage = InstallDispatchProgress::Detecting;
        p.message = "detecting format";
        onProgress(p);
    }

    FormatDetector detector;
    DetectResult detected = detector.detectFile(path);

    sceClibPrintf(
        "[InstallDispatcher] file=%s format=%s kind=%s detail=%s\n",
        path.c_str(),
        toString(detected.format),
        toString(detected.kind),
        detected.detail.c_str()
    );

    switch (detected.format) {

        case FileFormat::Vpk: {
            if (onProgress) {
                InstallDispatchProgress p;
                p.stage = InstallDispatchProgress::Installing;
                p.message = "installing VPK";
                onProgress(p);
            }

            HomebrewInstaller installer;

            InstallResult result = installer.installVpk(
                path,
                [&](const InstallProgress& ip) {
                    if (!onProgress)
                        return;

                    InstallDispatchProgress p;

                    switch (ip.stage) {
                        case InstallProgress::Preparing:
                            p.stage = InstallDispatchProgress::Detecting;
                            break;

                        case InstallProgress::Extracting:
                            p.stage = InstallDispatchProgress::Extracting;
                            break;

                        case InstallProgress::Promoting:
                            p.stage = InstallDispatchProgress::Promoting;
                            break;

                        case InstallProgress::Cleaning:
                            p.stage = InstallDispatchProgress::Cleaning;
                            break;

                        case InstallProgress::Done:
                            p.stage = InstallDispatchProgress::Completed;
                            break;

                        case InstallProgress::Error:
                            p.stage = InstallDispatchProgress::Error;
                            break;
                    }

                    p.current = ip.entriesDone;
                    p.total = ip.entriesTotal;
                    p.message = ip.message;

                    onProgress(p);
                },
                shouldCancel
            );

            if (result == InstallResult::Ok) {
                if (onProgress) {
                    InstallDispatchProgress p;
                    p.stage = InstallDispatchProgress::Completed;
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

        case FileFormat::Pkg: {
            if (onProgress) {
                InstallDispatchProgress p;
                p.stage = InstallDispatchProgress::Installing;
                p.message = "installing PKG";
                onProgress(p);
            }

            VitaInstaller installer;

            VitaInstallResult result = installer.installPkg(
                path,
                [&](const VitaInstallProgress& ip) {
                    if (!onProgress)
                        return;

                    InstallDispatchProgress p;

                    switch (ip.stage) {
                        case VitaInstallProgress::Preparing:
                            p.stage = InstallDispatchProgress::Detecting;
                            break;

                        case VitaInstallProgress::Promoting:
                            p.stage = InstallDispatchProgress::Promoting;
                            break;

                        case VitaInstallProgress::Done:
                            p.stage = InstallDispatchProgress::Completed;
                            break;

                        case VitaInstallProgress::Error:
                            p.stage = InstallDispatchProgress::Error;
                            break;
                    }

                    p.message = ip.message;

                    onProgress(p);
                },
                shouldCancel
            );

            if (result == VitaInstallResult::Ok) {
                if (onProgress) {
                    InstallDispatchProgress p;
                    p.stage = InstallDispatchProgress::Completed;
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

        case FileFormat::Zip:
            setError("generic ZIP installation is not connected yet");
            return InstallDispatchResult::UnsupportedFormat;

        case FileFormat::Pbp:
            setError("PBP installer is not implemented yet");
            return InstallDispatchResult::UnsupportedFormat;

        case FileFormat::Iso:
            setError("ISO installer is not implemented yet");
            return InstallDispatchResult::UnsupportedFormat;

        case FileFormat::Cso:
            setError("CSO installer is not implemented yet");
            return InstallDispatchResult::UnsupportedFormat;

        default:
            setError(
                std::string("unsupported format: ") +
                toString(detected.format)
            );

            return InstallDispatchResult::UnsupportedFormat;
    }
}

} // namespace psvitaalive

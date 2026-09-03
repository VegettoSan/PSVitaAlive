#pragma once

#include <functional>
#include <string>
#include "installer/app_settings.hpp"

namespace psvitaalive {

enum class InstallDispatchResult {
    Ok = 0,
    InvalidArgument,
    UnsupportedFormat,
    DetectFailed,
    DownloadRequired,
    InstallFailed,
    Cancelled,
    IoError,
    UnknownError
};

const char* toString(InstallDispatchResult result);

struct InstallDispatchProgress {
    enum Stage {
        Detecting,
        Installing,
        Extracting,
        Promoting,
        Cleaning,
        Completed,
        Error
    } stage = Detecting;

    uint64_t current = 0;
    uint64_t total = 0;

    std::string message;
};

using InstallDispatchProgressFn =
    std::function<void(const InstallDispatchProgress&)>;

using InstallDispatchCancelFn =
    std::function<bool()>;

/**
 * InstallDispatcher
 * Decide qué operación utilizar según el formato detectado:
 * - VPK -> HomebrewInstaller / Promoter Utility
 * - PKG -> VitaInstaller / Promoter Utility
 * - ZIP -> ZipExtractor a la ruta elegida por el usuario
 * No maneja descargas ni DRM/licencias.
 */
class InstallDispatcher {
public:
    InstallDispatchResult installFile(
        const std::string& path,
        InstallDispatchProgressFn onProgress = nullptr,
        InstallDispatchCancelFn shouldCancel = nullptr,
        const std::string& zipDestination = std::string(),
        const std::string& rifPath = std::string()
    );

    const std::string& lastError() const { return lastError_; }
    const std::string& lastTitleId() const { return lastTitleId_; }
    const std::string& lastInstallPath() const { return lastInstallPath_; }
    bool lastLiveAreaOk() const { return lastLiveAreaOk_; }

    /** Where PSP/PS1 media should land: Adrenaline (pspemu only) vs LiveArea (VPK/PKG bubbles). */
    void setPspTarget(PspTarget t) { pspTarget_ = t; }
    PspTarget pspTarget() const { return pspTarget_; }
    void setPspMediaFormat(PspMediaFormat f) { pspMediaFormat_ = f; }
    PspMediaFormat pspMediaFormat() const { return pspMediaFormat_; }

private:
    std::string lastError_;
    std::string lastTitleId_;
    std::string lastInstallPath_;
    bool lastLiveAreaOk_ = false;
    PspTarget pspTarget_ = PspTarget::Adrenaline;
    PspMediaFormat pspMediaFormat_ = PspMediaFormat::Folder;

    void setError(const std::string& message);
    void clearResultMeta();
};

} // namespace psvitaalive

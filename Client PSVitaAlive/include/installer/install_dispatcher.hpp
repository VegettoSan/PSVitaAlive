#pragma once

#include <functional>
#include <string>
#include <cstdint>

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
 *
 * Decide qué operación utilizar según el formato detectado:
 * - VPK -> HomebrewInstaller / Promoter Utility
 * - PKG -> VitaInstaller / Promoter Utility
 * - ZIP -> ZipExtractor a la ruta elegida por el usuario
 *
 * No maneja descargas ni DRM/licencias.
 */
class InstallDispatcher {
public:
    InstallDispatchResult installFile(
        const std::string& path,
        InstallDispatchProgressFn onProgress = nullptr,
        InstallDispatchCancelFn shouldCancel = nullptr,
        const std::string& zipDestination = std::string()
    );

    const std::string& lastError() const {
        return lastError_;
    }

private:
    std::string lastError_;

    void setError(const std::string& message);
};

} // namespace psvitaalive

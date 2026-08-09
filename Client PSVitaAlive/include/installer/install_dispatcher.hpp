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
 * Decide qué instalador utilizar según el formato detectado.
 *
 * Fase 10:
 * - VPK  -> HomebrewInstaller
 * - PKG  -> VitaInstaller
 *
 * No maneja descargas.
 * No llama directamente a la UI.
 */
class InstallDispatcher {
public:
    InstallDispatchResult installFile(
        const std::string& path,
        InstallDispatchProgressFn onProgress = nullptr,
        InstallDispatchCancelFn shouldCancel = nullptr
    );

    const std::string& lastError() const {
        return lastError_;
    }

private:
    std::string lastError_;

    void setError(const std::string& message);
};

} // namespace psvitaalive

#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace psvitaalive {

enum class PspInstallResult {
    Ok = 0,
    InvalidArgument,
    Unsupported,
    IoError,
    Cancelled,
    PluginMissing,
    UnknownError
};

const char* toString(PspInstallResult r);

struct PspInstallProgress {
    enum Stage { Preparing, Copying, Done, Error } stage = Preparing;
    uint64_t current = 0;
    uint64_t total = 0;
    std::string message;
};

using PspInstallProgressFn = std::function<void(const PspInstallProgress&)>;
using PspInstallCancelFn = std::function<bool()>;

/**
 * PspInstaller — copy PSP/PS1 media into pspemu folders for Adrenaline
 * or NoPspEmuDrm LiveArea (plugin is a runtime prerequisite for bubbles).
 *
 * Does not implement DRM bypass.
 */
class PspInstaller {
public:
    /** ISO/CSO → ux0:pspemu/ISO/<filename> */
    PspInstallResult installIsoCso(
        const std::string& path,
        PspInstallProgressFn onProgress = nullptr,
        PspInstallCancelFn shouldCancel = nullptr
    );

    /** PBP → ux0:pspemu/PSP/GAME/<id>/EBOOT.PBP */
    PspInstallResult installPbp(
        const std::string& path,
        PspInstallProgressFn onProgress = nullptr,
        PspInstallCancelFn shouldCancel = nullptr
    );

    const std::string& lastError() const { return lastError_; }
    const std::string& lastInstallPath() const { return lastInstallPath_; }

private:
    std::string lastError_;
    std::string lastInstallPath_;
    void setError(const std::string& msg);
    PspInstallResult copyFile(
        const std::string& src,
        const std::string& dst,
        PspInstallProgressFn onProgress,
        PspInstallCancelFn shouldCancel
    );
};

} // namespace psvitaalive

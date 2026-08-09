#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace psvitaalive {

enum class VitaInstallResult {
    Ok = 0,
    InvalidArgument,
    NotPkg,
    ModuleFailed,
    PromoteFailed,
    IoError,
    Cancelled,
    UnknownError
};

const char* toString(VitaInstallResult r);

struct VitaInstallProgress {
    enum Stage {
        Preparing,
        Promoting,
        Done,
        Error
    } stage = Preparing;
    uint64_t current = 0;
    uint64_t total = 0;
    std::string message;
};

using VitaInstallProgressFn = std::function<void(const VitaInstallProgress&)>;
using VitaInstallCancelFn = std::function<bool()>;

/**
 * VitaInstaller — installs supported/unprotected Vita .pkg content through
 * the system Promoter Utility. It intentionally does not implement DRM
 * bypass, license generation, or commercial-content acquisition.
 */
class VitaInstaller {
public:
    VitaInstallResult installPkg(
        const std::string& pkgPath,
        VitaInstallProgressFn onProgress = nullptr,
        VitaInstallCancelFn shouldCancel = nullptr,
        bool deleteTempOnSuccess = true
    );

    const std::string& lastError() const { return lastError_; }
    int lastPromoteResult() const { return lastPromoteResult_; }

private:
    std::string lastError_;
    int lastPromoteResult_ = 0;

    void setError(const std::string& msg);
    bool loadPromoterModule();
    VitaInstallResult promotePath(const std::string& path);
};

} // namespace psvitaalive

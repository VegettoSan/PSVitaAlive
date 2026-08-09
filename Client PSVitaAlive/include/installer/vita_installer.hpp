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
    std::string message;
};

using VitaInstallProgressFn = std::function<void(const VitaInstallProgress&)>;
using VitaInstallCancelFn = std::function<bool()>;

/**
 * VitaInstaller — Phase 7
 *
 * Installs Vita .pkg content using Promoter Utility.
 *
 * Notes:
 * - Does NOT implement DRM bypass or license generation.
 * - Delegates validation to system/homebrew environment.
 * - PKG path is the package file (or prepared directory, depending on API usage).
 *
 * For many homebrew/system flows, promote works on an extracted/package path.
 * This installer first copies/stages the PKG into a temp work dir when needed.
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

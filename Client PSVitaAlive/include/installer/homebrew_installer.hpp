#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace psvitaalive {

enum class InstallResult {
    Ok = 0,
    InvalidArgument,
    NotVpk,
    ExtractFailed,
    PromoteFailed,
    ModuleFailed,
    IoError,
    Cancelled,
    UnknownError
};

const char* toString(InstallResult r);

struct InstallProgress {
    enum Stage {
        Preparing,
        Extracting,
        Promoting,
        Cleaning,
        Done,
        Error
    } stage = Preparing;
    uint64_t entriesDone = 0;
    uint64_t entriesTotal = 0;
    uint64_t bytesWritten = 0;
    std::string message;
};

using InstallProgressFn = std::function<void(const InstallProgress&)>;
using InstallCancelFn = std::function<bool()>;

/**
 * HomebrewInstaller — Phase 6
 *
 * Installs .vpk homebrew packages:
 * 1) Detect format (must be VPK/ZIP)
 * 2) Extract to temp under ux0:data/psvitaalive/tmp/
 * 3) scePromoterUtilityPromotePkg
 * 4) Cleanup temp on success (optional keep on failure)
 *
 * Does NOT implement DRM bypass. Only legitimate homebrew VPK flow.
 */
class HomebrewInstaller {
public:
    InstallResult installVpk(
        const std::string& vpkPath,
        InstallProgressFn onProgress = nullptr,
        InstallCancelFn shouldCancel = nullptr,
        bool deleteTempOnSuccess = true
    );

    const std::string& lastError() const { return lastError_; }
    int lastPromoteResult() const { return lastPromoteResult_; }

private:
    std::string lastError_;
    int lastPromoteResult_ = 0;

    void setError(const std::string& msg);
    bool loadPromoterModule();
    InstallResult promoteExtractedDir(const std::string& dir);
};

} // namespace psvitaalive

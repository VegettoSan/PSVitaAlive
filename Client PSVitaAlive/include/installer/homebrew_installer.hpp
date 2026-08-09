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
 * HomebrewInstaller — VPK installer.
 *
 * Installs legitimate PS Vita homebrew VPK packages:
 * 1) Verify the input is a .vpk ZIP container.
 * 2) Extract it to a private temporary directory.
 * 3) Validate the minimum VPK layout (eboot.bin + sce_sys/param.sfo).
 * 4) Load the Promoter Utility dependencies required by the Vita runtime.
 * 5) Promote the extracted package directory.
 * 6) Recursively remove temporary files after a successful install.
 *
 * This class does NOT implement DRM bypass, license generation, or
 * installation of encrypted commercial content.
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

    bool pafLoadedByUs_ = false;
    bool promoterLoadedByUs_ = false;

    void setError(const std::string& msg);
    bool loadPromoterModule();
    void unloadPromoterModules();
    bool removeTree(const std::string& path);
    InstallResult promoteExtractedDir(const std::string& dir);
};

} // namespace psvitaalive

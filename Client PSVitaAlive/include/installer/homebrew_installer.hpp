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
    uint64_t bytesTotal = 0;
    std::string message;
};

using InstallProgressFn = std::function<void(const InstallProgress&)>;
using InstallCancelFn = std::function<bool()>;

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
    /** TITLE_ID from param.sfo when available (empty on failure). */
    const std::string& lastTitleId() const { return lastTitleId_; }
    /** True when ux0:app/<TITLE_ID> tree was verified after promote. */
    bool lastLiveAreaOk() const { return lastLiveAreaOk_; }
    /** Install path shown to the user, e.g. ux0:app/ABCD12345 */
    const std::string& lastInstallPath() const { return lastInstallPath_; }

private:
    std::string lastError_;
    std::string lastTitleId_;
    std::string lastInstallPath_;
    bool lastLiveAreaOk_ = false;
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

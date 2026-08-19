#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace psvitaalive {

/**
 * Client self-update against GitHub Releases.
 *
 * The update VPK is downloaded to ux0:data, extracted into a staging
 * directory, converted to a fake package header, and installed through
 * ScePromoterUtil. The running ux0:/app/PSVAS1178 tree is never used as the
 * extraction destination.
 */
class UpdateChecker {
public:
    enum class State {
        UpToDate,
        UpdateAvailable,
        Failed
    };

    struct Result {
        State state = State::Failed;
        std::string localVersion;
        std::string remoteVersion;
        std::string releaseTag;
        std::string releaseName;
        std::string downloadUrl;
        std::string assetName;
        std::string digest;
        uint64_t assetSize = 0;
        std::string error;
    };

    enum class ApplyStage {
        Preparing = 0,
        Downloading,
        Extracting,
        Finalizing,
        Done,
        Error
    };

    struct ApplyProgress {
        ApplyStage stage = ApplyStage::Preparing;
        uint64_t current = 0;
        uint64_t total = 0;
        uint64_t bytesPerSecond = 0;
        std::string message;
    };

    using ApplyProgressFn = std::function<void(const ApplyProgress&)>;
    using ApplyCancelFn = std::function<bool()>;

    static constexpr const char* kTitleId = "PSVAS1178";
    static constexpr const char* kAppDir = "ux0:/app/PSVAS1178";
    static constexpr const char* kUpdateDir = "ux0:data/psvitaalive/update";
    static constexpr const char* kVpkPath = "ux0:data/psvitaalive/update/PSVitaAlive.vpk";

    static Result checkLatest(const std::string& currentVersion);

    /**
     * Download the client update VPK, stage it for PSVAUPDT1, install the helper
     * process and hand off (VitaShell-style). Never writes into the running app dir.
     */
    static bool applyUpdate(
        const Result& info,
        ApplyProgressFn onProgress = nullptr,
        ApplyCancelFn shouldCancel = nullptr
    );

    /** Remove temporary PSVAUPDT1 bubble after a successful self-update (VitaShell pattern). */
    static bool cleanupUpdaterBubble();
};

} // namespace psvitaalive

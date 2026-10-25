#pragma once

#include "network/http_client.hpp"
#include "network/download_manager.hpp"
#include "installer/install_dispatcher.hpp"
#include "installer/app_settings.hpp"
#include "installer/plugin_detector.hpp"

#include <psp2/types.h>

#include <atomic>
#include <string>
#include <vector>

namespace psvitaalive {

struct InstallStatus {
    enum class State {
        Idle = 0,
        Downloading,
        Installing,
        Completed,
        Failed,
        Cancelled
    };
    State state = State::Idle;
    uint64_t current = 0;
    uint64_t total = 0;
    uint64_t bytesPerSecond = 0;
    std::string message;
    std::string fileName;
    std::string stage;
    std::string installPath;
    std::string titleId;
    bool liveAreaOk = false;
    bool archiveSourceUnavailable = false; // Only failed Archive.org user downloads.
    /** Remaining ms before Completed auto-dismisses (0 = no auto-dismiss). */
    uint64_t resultAutoCloseRemainingMs = 0;
    /** True when a Plugin link finished and the user must reboot for taiHEN. */
    bool needsReboot = false;
    /** True only when a fully downloaded data ZIP failed during extraction. */
    bool zipRecoveryAvailable = false;
    bool zipRecoveryMayBeCorrupt = false;
    std::string zipRecoveryPath;
    std::string zipRecoveryExtractPath;
};

/**
 * Coordinates download + install on a worker thread so the UI thread never performs filesystem/network work.
 *
 * Completed stays visible until acknowledgeResult() or a short timeout.
 * Failed / Cancelled stay until the user acknowledges (no auto-dismiss).
 * Cancelled is shown as "Download cancelled" without an error Report button.
 */
struct SavedZipArchive {
    std::string fileName; // visible basename in app details
    std::string archivePath; // exact validated path, not caller-supplied
    std::string target;
    std::string infoPath;
    unsigned partCount = 1; // 1=complete ZIP, 2/4=raw byte ranges
};

class InstallController {
public:
    InstallController();
    ~InstallController();

    bool init();
    void shutdown();
    /** Run the visible startup orphan-download cleanup before catalog loading. */
    int cleanupStartupDownloads(const StartupCleanupProgressFn& onProgress);

    bool requestInstall(
        const std::string& url,
        const std::string& fileName,
        const std::string& zipDestination = std::string(),
        const std::string& zrif = std::string(),
        const std::string& linkType = std::string(),
        const std::string& contentId = std::string(),
        const std::string& displayTitle = std::string(),
        uint64_t expectedBytes = 0,
        const std::string& pluginSection = std::string(),
        const std::string& pluginLine = std::string()
    );
    void cancel();
    /** User dismissed the success/error result panel (or UI timeout). */
    void acknowledgeResult();
    /** Resolve a pending ZIP extraction recovery. keep=true preserves it; false deletes it. */
    bool resolveZipRecovery(bool keep, std::string* savedPath = nullptr);
    /** Existing preserved manual ZIP associated with a catalog app, with no network. */
    bool hasSavedZip(const std::string& appId, const std::string& titleId) const;
    /** Validated recovery ZIPs belonging to one app, sorted by display name. */
    std::vector<SavedZipArchive> savedZips(const std::string& appId, const std::string& titleId) const;
    /** Delete only an owned, validated manual recovery ZIP and its metadata. */
    bool deleteSavedZip(const std::string& appId, const std::string& titleId,
                        const std::string& archivePath);
    /** Re-extract a selected preserved ZIP via the standard background installer worker. */
    bool requestSavedZipExtraction(const std::string& appId, const std::string& titleId,
                                   const std::string& archivePath);
    InstallStatus status() const;

    bool busy() const;

    /** Loaded from config.json (install_method, psp_target, plugin warnings). */
    const AppSettingsData& settings() const { return settings_; }
    void setSettings(const AppSettingsData& s);

    /** Last plugin scan (updated in init). */
    const PluginStatus& plugins() const { return plugins_; }

    /** Catalog identity for the install about to start (optional; used for receipts). */
    void setPendingCatalogMeta(const std::string& appId, const std::string& version,
                               const std::string& versionDate = std::string(), int revision = 0,
                               const std::string& titleId = std::string());

private:
    HttpClient http_;
    DownloadManager downloads_;
    InstallDispatcher dispatcher_;
    AppSettingsData settings_{};
    PluginStatus plugins_{};

    SceUID workerThread_ = -1;
    /** Background tick: prevent auto-suspend while download/extract is active. */
    SceUID keepAwakeThread_ = -1;
    std::atomic<bool> keepAwakeStop_{true};
    bool shellUtilReady_ = false;
    bool shellLocked_ = false;

    /**
     * BGDL must stay on the main thread because ShellSvc/IPMI enqueue proved
     * unreliable when moved to the install worker.  requestInstall() arms this
     * flag after publishing the "BGDL / Preparing" state.  The first status()
     * poll deliberately reads as false so main can copy that state into the UI;
     * after the following updateAndDraw() has presented the preparation frame,
     * the next status() poll reads true and runs the existing BGDL enqueue path.
     *
     * This is intentionally a one-poll deferral, not a background BGDL worker.
     */
    struct DeferredBgdlFlag {
        bool active = false;
        mutable bool skipNextRead = false;

        DeferredBgdlFlag& operator=(bool value) {
            active = value;
            skipNextRead = value;
            return *this;
        }

        operator bool() const {
            if (!active) return false;
            if (skipNextRead) {
                skipNextRead = false;
                return false;
            }
            return true;
        }
    };
    DeferredBgdlFlag activeBgdlJob_;

    std::string pendingAppId_;
    std::string pendingCatalogVersion_;
    std::string pendingVersionDate_;
    int pendingReleaseRevision_ = 0;
    std::string pendingTitleId_;
    std::string activeBgdlUrl_;
    std::string activeBgdlTitle_;
    std::string activeBgdlLinkType_;
    std::string activeJobId_;
    std::string activeZipDestination_;
    std::string activeFileName_;
    /** Direct PKG path only: zRIF / content_id carried from requestInstall (VPK ignores these). */
    std::string activeZrif_;
    std::string activeContentId_;
    std::string activeLinkType_;
    std::string activePluginSection_;
    std::string activePluginLine_;
    std::string activeDisplayTitle_;
    std::atomic<bool> needsReboot_{false};
    // Shared UI-to-worker signal; persists across installation retry attempts.
    std::atomic<bool> installCancelRequested_{false};

    // ZIP-only manual recovery. This is never populated for direct VPK/PKG/plugin jobs.
    std::atomic<bool> archiveSourceUnavailable_{false};
    std::atomic<bool> zipRecoveryAvailable_{false};
    std::atomic<bool> zipRecoveryMayBeCorrupt_{false};
    std::string zipRecoveryPath_;
    std::string zipRecoveryInfoPath_;
    std::string zipRecoveryExtractPath_;
    std::string zipRecoveryJobId_; // nonempty only if relocation needs retry
    unsigned zipRecoveryPartCount_ = 1;
    bool persistPendingZipRecovery();
    bool savedZipMode_ = false;
    unsigned savedZipPartCount_ = 1;
    std::string savedZipPath_;
    std::string savedZipTarget_;
    std::string savedZipInfoPath_;
     int workerSavedZipMain();

    std::atomic<int> state_{static_cast<int>(InstallStatus::State::Idle)};
    std::atomic<uint64_t> current_{0};
    std::atomic<uint64_t> total_{0};
    std::atomic<uint64_t> speed_{0};
    std::atomic<bool> workerDone_{true};
    std::atomic<bool> liveAreaOk_{false};
    std::atomic<uint64_t> resultShownAtMs_{0};

    char message_[384] = {};
    char fileName_[256] = {};
    char stage_[64] = {};
    char installPath_[256] = {};
    char titleId_[32] = {};

    static int workerEntry(SceSize args, void* argp);
    int workerMain();

    static int keepAwakeEntry(SceSize args, void* argp);
    void startKeepAwakeThread();
    void stopKeepAwakeThread();

    /** Block PS button (+ soft power-off menu) while a job is running so the user
     *  cannot exit to LiveArea mid-download/extract. Always unlocked on finish. */
    void lockShellDuringJob();
    void unlockShellDuringJob();

    void setMessage(const char* text);
    void setFileName(const char* text);
    void setStage(const char* text);
    void setInstallPath(const char* text);
    void setTitleId(const char* text);
    void setState(InstallStatus::State state, const char* message);
    void maybeAutoAcknowledgeResult();
};

} // namespace psvitaalive

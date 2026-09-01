#pragma once

#include "network/http_client.hpp"
#include "network/download_manager.hpp"
#include "installer/install_dispatcher.hpp"
#include "installer/app_settings.hpp"
#include "installer/plugin_detector.hpp"

#include <atomic>
#include <string>

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
    /** Remaining ms before Completed auto-dismisses (0 = no auto-dismiss). */
    uint64_t resultAutoCloseRemainingMs = 0;
};

/**
 * Coordinates download + install on a worker thread so the UI thread never performs filesystem/network work.
 *
 * Completed stays visible until acknowledgeResult() or a short timeout.
 * Failed / Cancelled stay until the user acknowledges (no auto-dismiss).
 * Cancelled is shown as "Download cancelled" without an error Report button.
 */
class InstallController {
public:
    InstallController();
    ~InstallController();

    bool init();
    void shutdown();

    bool requestInstall(
        const std::string& url,
        const std::string& fileName,
        const std::string& zipDestination = std::string(),
        const std::string& zrif = std::string(),
        const std::string& linkType = std::string(),
        const std::string& contentId = std::string(),
        const std::string& displayTitle = std::string(),
        uint64_t expectedBytes = 0
    );
    void cancel();
    /** User dismissed the success/error result panel (or UI timeout). */
    void acknowledgeResult();
    InstallStatus status() const;

    bool busy() const;

    /** Loaded from config.json (install_method, psp_target, plugin warnings). */
    const AppSettingsData& settings() const { return settings_; }
    void setSettings(const AppSettingsData& s);

    /** Last plugin scan (updated in init). */
    const PluginStatus& plugins() const { return plugins_; }

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

    /** When true, worker runs PKG BGDL enqueue instead of HTTP download. */
    bool activeBgdlJob_ = false;
    std::string activeBgdlUrl_;
    std::string activeBgdlTitle_;
    std::string activeBgdlLinkType_;
    std::string activeJobId_;
    std::string activeZipDestination_;
    std::string activeFileName_;
    /** Direct PKG path only: zRIF / content_id carried from requestInstall (VPK ignores these). */
    std::string activeZrif_;
    std::string activeContentId_;

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

#include "installer/install_controller.hpp"
#include "diagnostic_logger.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <cstring>

namespace psvitaalive {
namespace {
// Keep success/error panel visible long enough to read; user can dismiss earlier.
constexpr uint64_t RESULT_AUTO_DISMISS_MS = 8000;
}

InstallController::InstallController() : downloads_(http_) {
    std::memset(message_, 0, sizeof(message_));
    std::memset(fileName_, 0, sizeof(fileName_));
    std::memset(stage_, 0, sizeof(stage_));
    std::memset(installPath_, 0, sizeof(installPath_));
    std::memset(titleId_, 0, sizeof(titleId_));
}

InstallController::~InstallController() { shutdown(); }

bool InstallController::init() {
    if (http_.isInitialized()) return true;
    const HttpResult result = http_.init();
    if (result != HttpResult::Ok) {
        setState(InstallStatus::State::Failed, http_.lastError().c_str());
        diagnostics::log(std::string("[Installer] HTTP init failed: ") + http_.lastError());
        return false;
    }

    downloads_.setProgressCallback([this](const DownloadProgressEvent& event) {
        current_.store(event.downloaded);
        total_.store(event.total);
        speed_.store(event.bytesPerSecond);
        setFileName(event.fileName.c_str());
        setStage("Downloading");
        state_.store(static_cast<int>(InstallStatus::State::Downloading));
        setMessage("Downloading...");
    });

    const int purged = downloads_.purgeIncompleteJobs();
    if (purged > 0) {
        char m[96];
        sceClibSnprintf(m, sizeof(m), "[Installer] purged %d residual download jobs", purged);
        diagnostics::log(m);
    }
    setStage("Idle");
    setState(InstallStatus::State::Idle, "Ready");
    diagnostics::log("[Installer] initialized");
    return true;
}

void InstallController::shutdown() {
    if (workerThread_ >= 0) {
        sceKernelWaitThreadEnd(workerThread_, nullptr, nullptr);
        sceKernelDeleteThread(workerThread_);
        workerThread_ = -1;
    }
    http_.shutdown();
    activeJobId_.clear();
    activeZipDestination_.clear();
    activeFileName_.clear();
    workerDone_.store(true);
    setState(InstallStatus::State::Idle, "Stopped");
    diagnostics::log("[Installer] shutdown");
}

void InstallController::cancel() {
    const auto currentState = static_cast<InstallStatus::State>(state_.load());
    if (currentState != InstallStatus::State::Downloading || activeJobId_.empty()) return;
    downloads_.cancel(activeJobId_);
    setStage("Cancelling");
    setState(InstallStatus::State::Downloading, "Cancelling download...");
    diagnostics::log(std::string("[Installer] cancel requested job=") + activeJobId_);
}

void InstallController::acknowledgeResult() {
    const auto s = static_cast<InstallStatus::State>(state_.load());
    if (s != InstallStatus::State::Completed && s != InstallStatus::State::Failed) return;
    diagnostics::log("[Installer] result acknowledged by UI");
    resultShownAtMs_.store(0);
    liveAreaOk_.store(false);
    setInstallPath("");
    setTitleId("");
    setStage("Idle");
    setState(InstallStatus::State::Idle, "Ready");
}

bool InstallController::busy() const {
    const auto s = static_cast<InstallStatus::State>(state_.load());
    return s == InstallStatus::State::Downloading || s == InstallStatus::State::Installing;
}

bool InstallController::requestInstall(const std::string& url, const std::string& fileName, const std::string& zipDestination) {
    if (url.empty() || fileName.empty() || busy()) return false;

    // Allow starting a new job after a result panel is still showing.
    const auto s = static_cast<InstallStatus::State>(state_.load());
    if (s == InstallStatus::State::Completed || s == InstallStatus::State::Failed) {
        acknowledgeResult();
    }

    if (workerThread_ >= 0 && workerDone_.load()) {
        sceKernelWaitThreadEnd(workerThread_, nullptr, nullptr);
        sceKernelDeleteThread(workerThread_);
        workerThread_ = -1;
    }
    if (!http_.isInitialized() && !init()) return false;

    const std::string jobId = downloads_.enqueue(url, fileName);
    if (jobId.empty()) {
        setState(InstallStatus::State::Failed, "Could not create download job");
        diagnostics::log("[Installer] could not create download job");
        return false;
    }

    activeJobId_ = jobId;
    activeZipDestination_ = zipDestination;
    activeFileName_ = fileName;
    current_.store(0);
    total_.store(0);
    speed_.store(0);
    liveAreaOk_.store(false);
    resultShownAtMs_.store(0);
    setInstallPath("");
    setTitleId("");
    setFileName(fileName.c_str());
    setStage("Downloading");
    workerDone_.store(false);
    setState(InstallStatus::State::Downloading, "Starting download...");
    diagnostics::log(std::string("[Installer] request job=") + jobId + " file=" + fileName);

    workerThread_ = sceKernelCreateThread("PSVitaAliveInstall", &InstallController::workerEntry,
        0x10000100, 64 * 1024, 0, 0, nullptr);
    if (workerThread_ < 0) {
        setState(InstallStatus::State::Failed, "Could not create worker thread");
        workerDone_.store(true);
        downloads_.cleanupCompletedJob(activeJobId_);
        activeJobId_.clear();
        diagnostics::log("[Installer] worker thread creation failed; job cleaned");
        return false;
    }

    InstallController* self = this;
    const int result = sceKernelStartThread(workerThread_, sizeof(self), &self);
    if (result < 0) {
        sceKernelDeleteThread(workerThread_);
        workerThread_ = -1;
        workerDone_.store(true);
        setState(InstallStatus::State::Failed, "Could not start worker thread");
        downloads_.cleanupCompletedJob(activeJobId_);
        activeJobId_.clear();
        diagnostics::log("[Installer] worker start failed; job cleaned");
        return false;
    }
    return true;
}

InstallStatus InstallController::status() const {
    // Auto-dismiss long-lived result panels so the UI does not stick forever.
    const_cast<InstallController*>(this)->maybeAutoAcknowledgeResult();

    InstallStatus result;
    result.state = static_cast<InstallStatus::State>(state_.load());
    result.current = current_.load();
    result.total = total_.load();
    result.bytesPerSecond = speed_.load();
    result.fileName = fileName_;
    result.stage = stage_;
    result.message = message_;
    result.installPath = installPath_;
    result.titleId = titleId_;
    result.liveAreaOk = liveAreaOk_.load();
    return result;
}

void InstallController::maybeAutoAcknowledgeResult() {
    const auto s = static_cast<InstallStatus::State>(state_.load());
    if (s != InstallStatus::State::Completed && s != InstallStatus::State::Failed) return;
    const uint64_t shown = resultShownAtMs_.load();
    if (shown == 0) return;
    const uint64_t now = sceKernelGetSystemTimeWide() / 1000ULL;
    if (now >= shown && (now - shown) >= RESULT_AUTO_DISMISS_MS) {
        diagnostics::log("[Installer] result auto-dismiss after timeout");
        acknowledgeResult();
    }
}

void InstallController::setMessage(const char* text) {
    if (!text) text = "";
    sceClibSnprintf(message_, sizeof(message_), "%s", text);
}
void InstallController::setFileName(const char* text) {
    if (!text) text = "";
    sceClibSnprintf(fileName_, sizeof(fileName_), "%s", text);
}
void InstallController::setStage(const char* text) {
    if (!text) text = "";
    sceClibSnprintf(stage_, sizeof(stage_), "%s", text);
}
void InstallController::setInstallPath(const char* text) {
    if (!text) text = "";
    sceClibSnprintf(installPath_, sizeof(installPath_), "%s", text);
}
void InstallController::setTitleId(const char* text) {
    if (!text) text = "";
    sceClibSnprintf(titleId_, sizeof(titleId_), "%s", text);
}
void InstallController::setState(InstallStatus::State state, const char* message) {
    setMessage(message);
    state_.store(static_cast<int>(state));
}

int InstallController::workerEntry(SceSize args, void* argp) {
    (void)args;
    InstallController* self = nullptr;
    if (argp) std::memcpy(&self, argp, sizeof(self));
    return self ? self->workerMain() : -1;
}

int InstallController::workerMain() {
    const bool downloaded = downloads_.processQueue();
    DownloadJob* job = downloads_.findJob(activeJobId_);

    if (!downloaded || !job || job->state != DownloadState::Completed) {
        const bool cancelled = job && job->state == DownloadState::Cancelled;
        const std::string error = cancelled ? "Download cancelled"
            : (job && !job->lastError.empty() ? job->lastError : "Download failed");
        setStage(cancelled ? "Cancelled" : "Error");
        setState(InstallStatus::State::Failed, error.c_str());
        liveAreaOk_.store(false);
        setInstallPath("");
        diagnostics::log(std::string("[Installer] ") + (cancelled ? "download cancelled" : "download failed") + ": " + error);
        if (!activeJobId_.empty()) downloads_.cleanupCompletedJob(activeJobId_);
        activeJobId_.clear();
        resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
        workerDone_.store(true);
        return 0;
    }

    current_.store(job->downloadedSize);
    total_.store(job->expectedSize ? job->expectedSize : job->downloadedSize);
    speed_.store(0);
    setStage("Installing");
    setState(InstallStatus::State::Installing, "Preparing installation...");
    diagnostics::log(std::string("[Installer] installing job=") + activeJobId_ + " file=" + job->finalPath);

    const InstallDispatchResult result = dispatcher_.installFile(
        job->finalPath,
        [&](const InstallDispatchProgress& progress) {
            current_.store(progress.current);
            total_.store(progress.total);
            setStage(progress.message.empty() ? "Installing" : progress.message.c_str());
            if (!progress.message.empty()) setMessage(progress.message.c_str());
        },
        nullptr,
        activeZipDestination_
    );

    if (result != InstallDispatchResult::Ok) {
        const std::string error = dispatcher_.lastError().empty() ? "Installation failed" : dispatcher_.lastError();
        setStage("Error");
        setState(InstallStatus::State::Failed, error.c_str());
        liveAreaOk_.store(false);
        setInstallPath(dispatcher_.lastInstallPath().c_str());
        setTitleId(dispatcher_.lastTitleId().c_str());
        diagnostics::log(std::string("[Installer] installation failed: ") + error);
        downloads_.cleanupCompletedJob(activeJobId_);
        activeJobId_.clear();
        resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
    } else {
        setInstallPath(dispatcher_.lastInstallPath().c_str());
        setTitleId(dispatcher_.lastTitleId().c_str());
        liveAreaOk_.store(dispatcher_.lastLiveAreaOk());

        char okMsg[320];
        if (!dispatcher_.lastInstallPath().empty()) {
            sceClibSnprintf(okMsg, sizeof(okMsg), "Installed at %s", dispatcher_.lastInstallPath().c_str());
        } else {
            sceClibSnprintf(okMsg, sizeof(okMsg), "Installation completed");
        }
        setStage("Completed");
        setState(InstallStatus::State::Completed, okMsg);
        diagnostics::log(std::string("[Installer] installation completed path=") +
            dispatcher_.lastInstallPath() + " titleId=" + dispatcher_.lastTitleId() +
            " liveArea=" + (dispatcher_.lastLiveAreaOk() ? "yes" : "no"));
        downloads_.cleanupCompletedJob(activeJobId_);
        activeJobId_.clear();
        resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
    }

    workerDone_.store(true);
    return 0;
}

} // namespace psvitaalive

#include "installer/install_controller.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <cstring>

namespace psvitaalive {

InstallController::InstallController() : downloads_(http_) {
    std::memset(message_, 0, sizeof(message_));
    std::memset(fileName_, 0, sizeof(fileName_));
    std::memset(stage_, 0, sizeof(stage_));
}

InstallController::~InstallController() { shutdown(); }

bool InstallController::init() {
    if (http_.isInitialized()) return true;
    const HttpResult result = http_.init();
    if (result != HttpResult::Ok) {
        setState(InstallStatus::State::Failed, http_.lastError().c_str());
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

    setStage("Idle");
    setState(InstallStatus::State::Idle, "Ready");
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
    workerDone_.store(true);
    setState(InstallStatus::State::Idle, "Stopped");
}

bool InstallController::busy() const {
    const auto s = static_cast<InstallStatus::State>(state_.load());
    return s == InstallStatus::State::Downloading || s == InstallStatus::State::Installing;
}

bool InstallController::requestInstall(const std::string& url, const std::string& fileName, const std::string& zipDestination) {
    if (url.empty() || fileName.empty() || busy()) return false;

    if (workerThread_ >= 0 && workerDone_.load()) {
        sceKernelWaitThreadEnd(workerThread_, nullptr, nullptr);
        sceKernelDeleteThread(workerThread_);
        workerThread_ = -1;
    }
    if (!http_.isInitialized() && !init()) return false;

    const std::string jobId = downloads_.enqueue(url, fileName);
    if (jobId.empty()) {
        setState(InstallStatus::State::Failed, "Could not create download job");
        return false;
    }

    activeJobId_ = jobId;
    activeZipDestination_ = zipDestination;
    current_.store(0);
    total_.store(0);
    speed_.store(0);
    setFileName(fileName.c_str());
    setStage("Downloading");
    workerDone_.store(false);
    setState(InstallStatus::State::Downloading, "Starting download...");

    workerThread_ = sceKernelCreateThread("PSVitaAliveInstall", &InstallController::workerEntry,
        0x10000100, 64 * 1024, 0, 0, nullptr);
    if (workerThread_ < 0) {
        setState(InstallStatus::State::Failed, "Could not create worker thread");
        workerDone_.store(true);
        return false;
    }

    InstallController* self = this;
    const int result = sceKernelStartThread(workerThread_, sizeof(self), &self);
    if (result < 0) {
        sceKernelDeleteThread(workerThread_);
        workerThread_ = -1;
        workerDone_.store(true);
        setState(InstallStatus::State::Failed, "Could not start worker thread");
        return false;
    }
    return true;
}

InstallStatus InstallController::status() const {
    InstallStatus result;
    result.state = static_cast<InstallStatus::State>(state_.load());
    result.current = current_.load();
    result.total = total_.load();
    result.bytesPerSecond = speed_.load();
    result.fileName = fileName_;
    result.stage = stage_;
    result.message = message_;
    return result;
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
        const char* error = job && !job->lastError.empty() ? job->lastError.c_str() : "Download failed";
        setStage("Error");
        setState(InstallStatus::State::Failed, error);
        sceKernelDelayThread(2500 * 1000);
        setState(InstallStatus::State::Idle, "Ready");
        workerDone_.store(true);
        return 0;
    }

    current_.store(job->downloadedSize);
    total_.store(job->expectedSize ? job->expectedSize : job->downloadedSize);
    speed_.store(0);
    setStage("Installing");
    setState(InstallStatus::State::Installing, "Preparing installation...");

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
        setStage("Error");
        setState(InstallStatus::State::Failed, dispatcher_.lastError().c_str());
        // Keep the completed payload on failure so the user can retry without
        // downloading it again. Successful operations clean it below.
        sceKernelDelayThread(2500 * 1000);
        setState(InstallStatus::State::Idle, "Ready");
    } else {
        setStage("Completed");
        setState(InstallStatus::State::Completed, "Installation completed");
        downloads_.cleanupCompletedJob(activeJobId_);
        sceKernelDelayThread(2500 * 1000);
        setState(InstallStatus::State::Idle, "Ready");
    }

    workerDone_.store(true);
    return 0;
}

} // namespace psvitaalive

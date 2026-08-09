#include "installer/install_controller.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <cstring>

namespace psvitaalive {

InstallController::InstallController()
    : downloads_(http_) {
    std::memset(message_, 0, sizeof(message_));
}

InstallController::~InstallController() {
    shutdown();
}

bool InstallController::init() {
    if (http_.isInitialized()) {
        return true;
    }

    const HttpResult result = http_.init();
    if (result != HttpResult::Ok) {
        setState(InstallStatus::State::Failed, http_.lastError().c_str());
        return false;
    }

    downloads_.setProgressCallback([this](const DownloadProgressEvent& event) {
        current_.store(event.downloaded);
        total_.store(event.total);
        state_.store(static_cast<int>(InstallStatus::State::Downloading));
    });

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
    workerDone_.store(true);
    setState(InstallStatus::State::Idle, "Stopped");
}

bool InstallController::busy() const {
    const auto s = static_cast<InstallStatus::State>(state_.load());
    return s == InstallStatus::State::Downloading ||
           s == InstallStatus::State::Installing;
}

bool InstallController::requestInstall(
    const std::string& url,
    const std::string& fileName
) {
    if (url.empty() || fileName.empty() || busy()) {
        return false;
    }

    if (!http_.isInitialized() && !init()) {
        return false;
    }

    const std::string jobId = downloads_.enqueue(url, fileName);
    if (jobId.empty()) {
        setState(InstallStatus::State::Failed, "Could not create download job");
        return false;
    }

    activeJobId_ = jobId;
    current_.store(0);
    total_.store(0);
    workerDone_.store(false);
    setState(InstallStatus::State::Downloading, "Starting download...");

    workerThread_ = sceKernelCreateThread(
        "PSVitaAliveInstall",
        &InstallController::workerEntry,
        0x10000100,
        64 * 1024,
        0,
        nullptr
    );

    if (workerThread_ < 0) {
        setState(InstallStatus::State::Failed, "Could not create worker thread");
        workerDone_.store(true);
        return false;
    }

    InstallController* self = this;
    const int result = sceKernelStartThread(
        workerThread_,
        sizeof(self),
        &self
    );

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
    result.message = message_;
    return result;
}

void InstallController::setMessage(const char* text) {
    if (!text) {
        text = "";
    }

    sceClibSnprintf(message_, sizeof(message_), "%s", text);
}

void InstallController::setState(
    InstallStatus::State state,
    const char* message
) {
    setMessage(message);
    state_.store(static_cast<int>(state));
}

int InstallController::workerEntry(SceSize args, void* argp) {
    (void)args;

    InstallController* self = nullptr;
    if (argp) {
        std::memcpy(&self, argp, sizeof(self));
    }

    if (self) {
        return self->workerMain();
    }

    return -1;
}

int InstallController::workerMain() {
    sceClibPrintf("[InstallController] worker started job=%s\n", activeJobId_.c_str());

    const bool downloaded = downloads_.processQueue();
    DownloadJob* job = downloads_.findJob(activeJobId_);

    if (!downloaded || !job || job->state != DownloadState::Completed) {
        const char* error = job && !job->lastError.empty()
            ? job->lastError.c_str()
            : "Download failed";
        setState(InstallStatus::State::Failed, error);
        workerDone_.store(true);
        return 0;
    }

    current_.store(job->downloadedSize);
    total_.store(job->expectedSize ? job->expectedSize : job->downloadedSize);
    setState(InstallStatus::State::Installing, "Installing...");

    const InstallDispatchResult result = dispatcher_.installFile(
        job->finalPath,
        [this](const InstallDispatchProgress& progress) {
            current_.store(progress.current);
            total_.store(progress.total);

            if (!progress.message.empty()) {
                setMessage(progress.message.c_str());
            }
        }
    );

    if (result != InstallDispatchResult::Ok) {
        setState(InstallStatus::State::Failed, dispatcher_.lastError().c_str());
    } else {
        setState(InstallStatus::State::Completed, "Installation completed");
    }

    sceClibPrintf(
        "[InstallController] worker finished result=%s\n",
        toString(result)
    );

    workerDone_.store(true);
    return 0;
}

} // namespace psvitaalive

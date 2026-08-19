#include "update/startup_update_manager.hpp"

#include "diagnostic_logger.hpp"

#include <cstring>

namespace psvitaalive {

StartupUpdateManager::StartupUpdateManager() = default;
StartupUpdateManager::~StartupUpdateManager() { requestCancel(); wait(); }

bool StartupUpdateManager::startApplyWorker() {
    thread_ = sceKernelCreateThread(
        "PSVitaAliveUpdateWorker",
        &StartupUpdateManager::workerEntry,
        0x10000100,
        64 * 1024,
        0,
        0,
        nullptr
    );
    if (thread_ < 0) {
        state_.store(static_cast<int>(State::Failed), std::memory_order_release);
        setError("Unable to create update worker thread");
        return false;
    }
    StartupUpdateManager* self = this;
    const int rc = sceKernelStartThread(thread_, sizeof(self), &self);
    if (rc < 0) {
        sceKernelDeleteThread(thread_);
        thread_ = -1;
        state_.store(static_cast<int>(State::Failed), std::memory_order_release);
        setError("Unable to start update worker thread");
        return false;
    }
    diagnostics::log("[StartupUpdate] apply worker started (download/install only)");
    return true;
}

bool StartupUpdateManager::start(const std::string& currentVersion) {
    if (thread_ >= 0) return false;
    version_ = currentVersion;
    cancelRequested_.store(false, std::memory_order_release);
    restartRequired_.store(false, std::memory_order_release);
    hasPendingCheck_ = false;
    pendingCheck_ = UpdateChecker::Result{};
    state_.store(static_cast<int>(State::Checking), std::memory_order_release);
    current_.store(0, std::memory_order_release);
    total_.store(0, std::memory_order_release);
    bytesPerSecond_.store(0, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        message_ = "Checking for application updates...";
        error_.clear();
    }

    // CRITICAL (real PS Vita): run GitHub fetch + sce::Json on the main thread.
    // Worker-thread JSON load/parse/unload was observed to abort the process
    // immediately after a successful "up to date" result on hardware, while
    // Vita3K continued. Download/extract may still use a worker later.
    diagnostics::log("[StartupUpdate] version check on main thread (Vita-safe)");
    const UpdateChecker::Result check = UpdateChecker::checkLatest(version_);

    if (cancelRequested_.load(std::memory_order_acquire)) {
        state_.store(static_cast<int>(State::Cancelled), std::memory_order_release);
        std::lock_guard<std::mutex> lock(stateMutex_);
        message_ = "Update cancelled";
        diagnostics::log("[StartupUpdate] cancelled during version check");
        return true;
    }

    if (check.state == UpdateChecker::State::UpToDate) {
        setProgress(
            State::UpToDate,
            1,
            1,
            0,
            "Application is up to date  ·  v" + check.localVersion
        );
        diagnostics::log(
            "[StartupUpdate] up to date local=" + check.localVersion +
            " remote=" + check.remoteVersion
        );
        return true;
    }

    if (check.state == UpdateChecker::State::Failed) {
        state_.store(static_cast<int>(State::Failed), std::memory_order_release);
        setError(check.error.empty() ? "Update check failed" : check.error);
        diagnostics::log(
            "[StartupUpdate] check failed: " +
            (check.error.empty() ? "unknown" : check.error)
        );
        return true;
    }

    // Update available — only the apply phase runs on a worker.
    pendingCheck_ = check;
    hasPendingCheck_ = true;
    setProgress(State::Downloading, 0, check.assetSize, 0, "Downloading update...");
    diagnostics::log(
        "[StartupUpdate] update available local=" + check.localVersion +
        " remote=" + check.remoteVersion + " — starting apply worker"
    );
    if (!startApplyWorker()) {
        return false;
    }
    return true;
}

bool StartupUpdateManager::isBusy() const {
    const State s = static_cast<State>(state_.load(std::memory_order_acquire));
    return s == State::Checking || s == State::Downloading || s == State::Installing;
}

StartupUpdateManager::Snapshot StartupUpdateManager::snapshot() const {
    Snapshot out;
    out.state = static_cast<State>(state_.load(std::memory_order_acquire));
    out.current = current_.load(std::memory_order_acquire);
    out.total = total_.load(std::memory_order_acquire);
    out.bytesPerSecond = bytesPerSecond_.load(std::memory_order_acquire);
    out.restartRequired = restartRequired_.load(std::memory_order_acquire);
    std::lock_guard<std::mutex> lock(stateMutex_);
    out.message = message_;
    out.error = error_;
    return out;
}

void StartupUpdateManager::requestCancel() {
    cancelRequested_.store(true, std::memory_order_release);
}

void StartupUpdateManager::wait() {
    if (thread_ < 0) return;
    sceKernelWaitThreadEnd(thread_, nullptr, nullptr);
    sceKernelDeleteThread(thread_);
    thread_ = -1;
}

int StartupUpdateManager::workerEntry(SceSize args, void* argp) {
    (void)args;
    StartupUpdateManager* self = *reinterpret_cast<StartupUpdateManager**>(argp);
    return self->workerMain();
}

void StartupUpdateManager::setProgress(
    State state,
    uint64_t current,
    uint64_t total,
    uint64_t bytesPerSecond,
    const std::string& message
) {
    state_.store(static_cast<int>(state), std::memory_order_release);
    current_.store(current, std::memory_order_release);
    total_.store(total, std::memory_order_release);
    bytesPerSecond_.store(bytesPerSecond, std::memory_order_release);
    std::lock_guard<std::mutex> lock(stateMutex_);
    message_ = message;
}

void StartupUpdateManager::setError(const std::string& error) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    error_ = error;
    if (message_.empty()) message_ = error;
}

int StartupUpdateManager::workerMain() {
    // Only download + install. Version check already ran on the main thread.
    if (!hasPendingCheck_) {
        state_.store(static_cast<int>(State::Failed), std::memory_order_release);
        setError("Internal update error: missing release metadata");
        diagnostics::log("[StartupUpdate] apply worker missing pendingCheck_");
        return 0;
    }

    const UpdateChecker::Result& check = pendingCheck_;
    if (cancelRequested_.load(std::memory_order_acquire)) {
        state_.store(static_cast<int>(State::Cancelled), std::memory_order_release);
        return 0;
    }

    setProgress(State::Downloading, 0, check.assetSize, 0, "Downloading update...");
    const bool applied = UpdateChecker::applyUpdate(
        check,
        [this](const UpdateChecker::ApplyProgress& progress) {
            State s = State::Downloading;
            if (progress.stage == UpdateChecker::ApplyStage::Extracting ||
                progress.stage == UpdateChecker::ApplyStage::Finalizing) {
                s = State::Installing;
            } else if (progress.stage == UpdateChecker::ApplyStage::Error) {
                s = State::Failed;
            }
            setProgress(
                s,
                progress.current,
                progress.total,
                progress.bytesPerSecond,
                progress.message.empty() ? "Working..." : progress.message
            );
        },
        [this]() { return cancelRequested_.load(std::memory_order_acquire); }
    );

    if (cancelRequested_.load(std::memory_order_acquire)) {
        state_.store(static_cast<int>(State::Cancelled), std::memory_order_release);
        std::lock_guard<std::mutex> lock(stateMutex_);
        message_ = "Update cancelled";
        return 0;
    }
    if (!applied) {
        state_.store(static_cast<int>(State::Failed), std::memory_order_release);
        setError("Update download/install failed");
        diagnostics::log("[StartupUpdate] update apply failed");
        return 0;
    }
    restartRequired_.store(true, std::memory_order_release);
    setProgress(State::Completed, 1, 1, 0, "Update installed — press START to restart");
    diagnostics::log("[StartupUpdate] update installed successfully; restart required");
    return 0;
}

} // namespace psvitaalive

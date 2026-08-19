#pragma once

#include "update/update_checker.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include <psp2/kernel/threadmgr.h>

namespace psvitaalive {

class StartupUpdateManager {
public:
    enum class State { Idle = 0, Checking, Downloading, Installing, UpToDate, Failed, Cancelled, Completed };

    struct Snapshot {
        State state = State::Idle;
        uint64_t current = 0;
        uint64_t total = 0;
        uint64_t bytesPerSecond = 0;
        std::string message;
        std::string error;
        bool restartRequired = false;
    };

    StartupUpdateManager();
    ~StartupUpdateManager();
    bool start(const std::string& currentVersion);
    bool isBusy() const;
    Snapshot snapshot() const;
    void requestCancel();
    void wait();

private:
    mutable std::mutex stateMutex_;
    SceUID thread_ = -1;
    std::atomic<bool> cancelRequested_{false};
    std::atomic<int> state_{static_cast<int>(State::Idle)};
    std::atomic<uint64_t> current_{0};
    std::atomic<uint64_t> total_{0};
    std::atomic<uint64_t> bytesPerSecond_{0};
    std::atomic<bool> restartRequired_{false};
    std::string message_;
    std::string error_;
    std::string version_;
    // Filled on the main thread by checkLatest; worker only runs applyUpdate.
    UpdateChecker::Result pendingCheck_{};
    bool hasPendingCheck_ = false;

    static int workerEntry(SceSize args, void* argp);
    int workerMain();
    bool startApplyWorker();
    void setProgress(State state, uint64_t current, uint64_t total, uint64_t bytesPerSecond, const std::string& message);
    void setError(const std::string& error);
};

} // namespace psvitaalive

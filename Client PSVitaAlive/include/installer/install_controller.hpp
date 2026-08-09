#pragma once

#include "network/download_manager.hpp"
#include "installer/install_dispatcher.hpp"

#include <psp2/kernel/threadmgr.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace psvitaalive {

struct InstallStatus {
    enum class State {
        Idle = 0,
        Downloading,
        Installing,
        Completed,
        Failed
    };

    State state = State::Idle;
    uint64_t current = 0;
    uint64_t total = 0;
    uint64_t bytesPerSecond = 0;
    std::string fileName;
    std::string stage;
    std::string message;
};

/**
 * Orchestrates download -> format-specific install/extract -> cleanup.
 * The UI reads status only; it never performs filesystem/network work.
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
        const std::string& zipDestination = std::string()
    );
    InstallStatus status() const;

    bool busy() const;

private:
    HttpClient http_;
    DownloadManager downloads_;
    InstallDispatcher dispatcher_;

    SceUID workerThread_ = -1;
    std::string activeJobId_;
    std::string activeZipDestination_;

    std::atomic<int> state_{static_cast<int>(InstallStatus::State::Idle)};
    std::atomic<uint64_t> current_{0};
    std::atomic<uint64_t> total_{0};
    std::atomic<uint64_t> speed_{0};
    std::atomic<bool> workerDone_{true};

    char message_[256] = {};
    char fileName_[256] = {};
    char stage_[64] = {};

    static int workerEntry(SceSize args, void* argp);
    int workerMain();

    void setMessage(const char* text);
    void setFileName(const char* text);
    void setStage(const char* text);
    void setState(InstallStatus::State state, const char* message);
};

} // namespace psvitaalive

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
    std::string message;
};

/**
 * Phase 10 orchestration layer.
 *
 * The UI only requests an installation and reads a lightweight status.
 * Networking, downloading and installation remain outside the renderer.
 */
class InstallController {
public:
    InstallController();
    ~InstallController();

    bool init();
    void shutdown();

    bool requestInstall(const std::string& url, const std::string& fileName);
    InstallStatus status() const;

    bool busy() const;

private:
    HttpClient http_;
    DownloadManager downloads_;
    InstallDispatcher dispatcher_;

    SceUID workerThread_ = -1;
    std::string activeJobId_;

    std::atomic<int> state_{static_cast<int>(InstallStatus::State::Idle)};
    std::atomic<uint64_t> current_{0};
    std::atomic<uint64_t> total_{0};
    std::atomic<bool> workerDone_{true};

    char message_[256] = {};

    static int workerEntry(SceSize args, void* argp);
    int workerMain();

    void setMessage(const char* text);
    void setState(InstallStatus::State state, const char* message);
};

} // namespace psvitaalive

#pragma once

#include <atomic>
#include <psp2/types.h>

namespace psvitaalive {

// Runs a bounded one-shot HEAD probe away from the UI thread.
// Unreachable means Archive.org did not respond from this connection,
// NOT proof of a global outage. An HTTP 4xx still proves the host responded.
class ArchiveServiceProbe {
public:
    enum class Result : int { Pending = 0, Available = 1, Unreachable = 2, Unknown = 3 };
    ArchiveServiceProbe() = default;
    ArchiveServiceProbe(const ArchiveServiceProbe&) = delete;
    ArchiveServiceProbe& operator=(const ArchiveServiceProbe&) = delete;
    ~ArchiveServiceProbe();
    bool start();
    Result result() const { return static_cast<Result>(result_.load(std::memory_order_acquire)); }
private:
    static int entry(SceSize size, void* arg);
    int run();
    std::atomic<int> result_{static_cast<int>(Result::Pending)};
    SceUID thread_ = -1;
};
} // namespace psvitaalive

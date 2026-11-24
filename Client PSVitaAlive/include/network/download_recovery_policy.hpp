#pragma once
#include <cstdint>

namespace psvitaalive {

// Runtime-only policy. No network or filesystem work here.
inline bool recoverableDownloadStatus(bool networkError, bool httpError, int status) {
    const bool transientHttp = status == 408 || status == 429 ||
        (status >= 500 && status <= 599);
    return (networkError && (status == 0 || (status >= 200 && status < 300) ||
        transientHttp)) || (httpError && transientHttp);
}

struct DownloadRecoveryPolicy {
    unsigned attempts = 0;
    uint64_t highWaterBytes = 0;
    uint64_t deadlineUs = 0;
    uint64_t offlineSinceUs = 0;
    bool offline = false;
    static constexpr unsigned maxAttempts = 4;
    static constexpr uint64_t progressResetBytes = 1024ULL * 1024ULL;
    static constexpr uint64_t delaysUs[maxAttempts] = {
        5000000ULL, 15000000ULL, 30000000ULL, 60000000ULL
    };

    bool exhausted() const { return attempts >= maxAttempts; }

    bool poll(uint64_t now, bool linkUsable, uint64_t savedBytes) {
        // Repeatedly downloading the same prefix cannot reset the budget.
        if (savedBytes >= highWaterBytes &&
            savedBytes - highWaterBytes >= progressResetBytes) {
            highWaterBytes = savedBytes;
            attempts = 0;
            deadlineUs = 0;
        }
        if (!linkUsable) {
            if (!offline) {
                offline = true;
                offlineSinceUs = now;
            }
            deadlineUs = 0;
            return false;
        }
        // A confirmed new loss/recovery of link gets a fresh budget even
        // after prior server retries were exhausted. Short flaps do not.
        if (offline) {
            if (now - offlineSinceUs >= 3000000ULL) attempts = 0;
            offline = false;
        }
        if (exhausted()) return false;
        if (deadlineUs == 0) {
            deadlineUs = now + delaysUs[attempts];
            return false;
        }
        if (now < deadlineUs) return false;
        ++attempts;
        deadlineUs = 0;
        return true;
    }
};

// A single transient NetCtl observation must not abort a live transfer.
struct DownloadLinkGrace {
    uint64_t disconnectedSinceUs = 0;
    bool disconnected = false;
    bool observe(uint64_t now, bool unavailable) {
        if (!unavailable) { disconnected = false; return false; }
        if (!disconnected) {
            disconnected = true;
            disconnectedSinceUs = now;
        }
        return now - disconnectedSinceUs >= 3000000ULL;
    }
};

} // namespace psvitaalive

#pragma once

#include <cstdint>

namespace psvitaalive {

// Byte-rate sampling on NEW transfer bytes only. Saved range-checkpoint bytes
// are intentionally excluded; a new HTTP session begins at zero transferred.
inline uint64_t transferRateBps(uint64_t bytes, uint64_t elapsedUs) {
    if (!elapsedUs) return 0;
    // Quotient/remainder avoids large bytes*1e6 overflow for multi-GB files.
    return (bytes / elapsedUs) * 1000000ULL +
           ((bytes % elapsedUs) * 1000000ULL) / elapsedUs;
}

struct TransferSpeedSampler {
    uint64_t sampleUs = 0;
    uint64_t sampleBytes = 0;
    uint64_t smoothedBps = 0;
    uint64_t observe(uint64_t nowUs, uint64_t newBytes) {
        if (!sampleUs || nowUs < sampleUs || newBytes < sampleBytes) {
            sampleUs = nowUs;
            sampleBytes = newBytes;
            smoothedBps = 0;
            return 0;
        }
        const uint64_t elapsedUs = nowUs - sampleUs;
        if (elapsedUs >= 750000ULL) {
            const uint64_t instant = transferRateBps(newBytes - sampleBytes, elapsedUs);
            smoothedBps = !smoothedBps ? instant :
                (smoothedBps * 3ULL / 4ULL + instant / 4ULL);
            sampleUs = nowUs;
            sampleBytes = newBytes;
        }
        return smoothedBps;
    }
};
} // namespace psvitaalive

#pragma once

#include <cstdint>

namespace psvitaalive {

// Cooperative I/O pacing for the install worker. Small files can create
// thousands of metadata operations without ever reaching a byte-only budget.
// This class is pure C++ so the policy can be tested without a Vita SDK.
class ZipWorkPacer {
public:
    static constexpr uint64_t kByteBudget = 1024ULL * 1024ULL;
    static constexpr uint32_t kEntryBudget = 8;

    // Return true when the worker should yield for one millisecond.
    bool afterBytes(uint64_t bytes) {
        bytes_ += bytes;
        if (bytes_ < kByteBudget) return false;
        reset();
        return true;
    }

    bool afterEntry() {
        if (++entries_ < kEntryBudget) return false;
        reset();
        return true;
    }

private:
    void reset() { bytes_ = 0; entries_ = 0; }
    uint64_t bytes_ = 0;
    uint32_t entries_ = 0;
};

} // namespace psvitaalive

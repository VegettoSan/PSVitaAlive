#pragma once

// Streaming comparison of a completed destination file with the size and CRC32
// in the ZIP central directory. Used ONLY by explicit saved-ZIP recovery.
// This is bounded in RAM and holds no persistent, power-loss-sensitive index.
#include <cstddef>
#include <cstdint>
#include <zlib.h>

namespace psvitaalive {

class ZipResumeVerifier {
public:
    ZipResumeVerifier(uint64_t expectedSize, uint32_t expectedCrc)
        : expectedSize_(expectedSize), expectedCrc_(expectedCrc) {}

    // Caller must supply both valid ZIP_STAT_SIZE and ZIP_STAT_CRC fields.
    // Feed only bytes actually returned by a successful read from the target.
    bool accept(const void* bytes, size_t length) {
        if (invalid_ || (!bytes && length) ||
            length > expectedSize_ - observedSize_ ||
            length > static_cast<size_t>(UINT32_MAX)) {
            invalid_ = true;
            return false;
        }
        observedCrc_ = static_cast<uint32_t>(crc32(observedCrc_,
            reinterpret_cast<const Bytef*>(bytes), static_cast<uInt>(length)));
        observedSize_ += static_cast<uint64_t>(length);
        return true;
    }

    bool matches(bool reachedEof, bool readHandleClosed) const {
        return !invalid_ && reachedEof && readHandleClosed &&
               observedSize_ == expectedSize_ && observedCrc_ == expectedCrc_;
    }

    uint64_t observedSize() const { return observedSize_; }

private:
    uint64_t expectedSize_ = 0;
    uint64_t observedSize_ = 0;
    uint32_t expectedCrc_ = 0;
    uint32_t observedCrc_ = 0;
    bool invalid_ = false;
};

} // namespace psvitaalive

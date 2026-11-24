#pragma once

#include <cstddef>
#include <cstdint>

namespace psvitaalive {

// PSVitaAlive publishes Game/Data ZIP payloads in STORE mode (method 0).
// Only large stored entries benefit from overlapping ZIP reads and Vita writes.
// Small entries and compressed/legacy formats keep the established serial path.
constexpr std::size_t kZipWritePipelineSlots = 3; // 3 x 256 KiB = 768 KiB
constexpr std::uint64_t kZipWritePipelineMinBytes = 8ULL * 1024ULL * 1024ULL;

inline bool shouldUseZipWritePipeline(unsigned method,
                                     bool hasUncompressedSize,
                                     std::uint64_t uncompressedBytes) {
    return method == 0 && hasUncompressedSize &&
           uncompressedBytes >= kZipWritePipelineMinBytes;
}

} // namespace psvitaalive

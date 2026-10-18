#pragma once

#include "network/http_client.hpp"

#include <cstdint>
#include <string>

namespace psvitaalive {

// Used only for fresh, large, range-capable Archive.org payloads.
// The normal resumable downloader remains the fallback.
enum class ParallelArchiveOutcome {
    Skipped,
    Complete,
    RetrySerial,
    Cancelled,
    IoError
};

ParallelArchiveOutcome downloadArchiveRanges(
    const std::string& url,
    const std::string& destination,
    uint64_t authoritativeSize,
    const HttpProgressFn& onProgress,
    const HttpCancelFn& shouldCancel
);

} // namespace psvitaalive

#pragma once

#include "network/http_client.hpp"

#include <cstdint>
#include <string>

namespace psvitaalive {

// Bounded adaptive range downloads for eligible HTTP(S) payloads.
// The normal resumable downloader remains the fallback for unsupported hosts.
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
    const HttpCancelFn& shouldCancel,
    const std::string& expectedSha1 = {}, // Required for direct Archive HTTP
    const std::string& expectedEtag = {}, // For generic HTTPS probes
    const std::string& expectedModified = {}
);

} // namespace psvitaalive

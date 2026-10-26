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
    Segmented, // Verified ZIP/VPK ranges, ready for virtual extraction.
    RetrySerial,
    Cancelled,
    Paused, // Safe range checkpoint retained for user testing.
    IoError
};

ParallelArchiveOutcome downloadArchiveRanges(
    const std::string& url,
    const std::string& destination,
    uint64_t authoritativeSize,
    const HttpProgressFn& onProgress,
    const HttpCancelFn& shouldCancel,
    const std::string& expectedEtag = {}, // For generic HTTPS probes
    const std::string& expectedModified = {},
    bool keepZipParts = false,
    const HttpCancelFn& shouldPause = {},
    const std::string& resourceIdentity = {},
    const std::string& probeEtag = {},
    const std::string& probeModified = {},
    unsigned retryIndex = 0
);

// Delete only the staging range files and their own checkpoint. Never delete a completed install payload.
void discardParallelSavedRanges(const std::string& destination);

// Returns saved validated-prefix bytes from a parallel checkpoint (0 if absent).
uint64_t parallelSavedBytes(const std::string& destination);

// Assemble saved ZIPs only for manual recovery; installs can read ranges.
bool materializeSegmentedZip(const std::string& base, uint64_t expectedSize);

} // namespace psvitaalive

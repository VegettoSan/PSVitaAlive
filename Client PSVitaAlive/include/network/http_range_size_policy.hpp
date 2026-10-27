#pragma once

#include <cstdint>

namespace psvitaalive {

// In a 206 response Content-Length counts only this response body. The
// authoritative full-file size comes exclusively from Content-Range.
// Redirect/error responses must not establish the payload's final size.
inline bool contentLengthIsFileTotal(long status, bool hasContentRange) {
    return status == 200 && !hasContentRange;
}

inline bool resumedContentRangeValid(uint64_t requestedOffset, uint64_t start,
                                     uint64_t end, uint64_t total) {
    return total > 0 && start == requestedOffset && start <= end && end < total;
}

// Arithmetic is subtraction-first, so malformed/large values cannot overflow.
inline bool rangeBodyFits(uint64_t start, uint64_t end, uint64_t received,
                          uint64_t incomingBytes) {
    if (end < start) return false;
    const uint64_t remainingRangeBytes = end - start + 1;
    return received <= remainingRangeBytes &&
           incomingBytes <= remainingRangeBytes - received;
}

} // namespace psvitaalive

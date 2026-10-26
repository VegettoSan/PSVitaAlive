#include "network/http_range_size_policy.hpp"

#include <cassert>
#include <cstdint>

using namespace psvitaalive;

int main() {
    constexpr uint64_t resume = 249561088ULL;
    constexpr uint64_t partial = 281149056ULL;
    constexpr uint64_t full = resume + partial;
    // HTTP 200 uses Content-Length; HTTP 206 MUST use Content-Range total.
    assert(contentLengthIsFileTotal(200, false));
    assert(!contentLengthIsFileTotal(206, false));
    assert(!contentLengthIsFileTotal(206, true));
    assert(!contentLengthIsFileTotal(302, false));
    assert(!contentLengthIsFileTotal(200, true));

    // Simulate both possible header orders from the same HTTP 206 response.
    uint64_t totalA = 0;
    if (contentLengthIsFileTotal(206, false)) totalA = partial;
    totalA = full; // Content-Range
    uint64_t totalB = full; // Content-Range
    if (contentLengthIsFileTotal(206, true)) totalB = partial;
    assert(totalA == full && totalB == full);

    // A resumed response must start at exactly the requested byte.
    assert(resumedContentRangeValid(resume, resume, full - 1, full));
    assert(!resumedContentRangeValid(resume, resume - 1, full - 1, full));
    assert(!resumedContentRangeValid(resume, resume, full, full));
    assert(!resumedContentRangeValid(resume, resume, resume - 1, full));
    assert(!resumedContentRangeValid(resume, resume, full - 1, 0));

    // Reject any extra byte (not merely the DownloadManager's 8 MiB guard).
    assert(rangeBodyFits(resume, full - 1, 0, partial));
    assert(rangeBodyFits(resume, full - 1, partial - 1, 1));
    assert(!rangeBodyFits(resume, full - 1, partial, 1));
    assert(!rangeBodyFits(resume, full - 1, 0, partial + 1));
    assert(!rangeBodyFits(20, 10, 0, 1));
    assert(!rangeBodyFits(0, UINT64_MAX, UINT64_MAX, 1));

    return 0;
}

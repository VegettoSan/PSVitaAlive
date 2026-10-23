#include "network/download_failure_policy.hpp"
#include <cassert>

using namespace psvitaalive;

int main() {
    UnreachableSourceDetector d;
    // A single timeout is not enough to declare an unreachable source.
    assert(!d.observe(true, true, true));
    assert(d.consecutiveFailures() == 1);
    assert(d.observe(true, true, true));
    assert(d.consecutiveFailures() == 2);

    // Any response resets the counter. Temporary HTTP 5xx and 429 are not
    // automatically interpreted as a dead network route.
    assert(!d.observe(true, false, true));
    assert(d.consecutiveFailures() == 0);
    assert(!d.observe(true, true, true));
    // A download that has already received bytes must be resumable.
    assert(!d.observe(true, true, false));
    assert(d.consecutiveFailures() == 0);
    assert(!d.observe(false, true, true));

    assert(isPermanentDownloadHttpFailure(400));
    assert(isPermanentDownloadHttpFailure(401));
    assert(isPermanentDownloadHttpFailure(403));
    assert(isPermanentDownloadHttpFailure(404));
    assert(isPermanentDownloadHttpFailure(405));
    assert(isPermanentDownloadHttpFailure(410));
    assert(isPermanentDownloadHttpFailure(451));
    assert(!isPermanentDownloadHttpFailure(200));
    assert(!isPermanentDownloadHttpFailure(206));
    assert(!isPermanentDownloadHttpFailure(408));
    assert(!isPermanentDownloadHttpFailure(416));
    assert(!isPermanentDownloadHttpFailure(429));
    assert(!isPermanentDownloadHttpFailure(500));
    assert(!isPermanentDownloadHttpFailure(503));
    return 0;
}

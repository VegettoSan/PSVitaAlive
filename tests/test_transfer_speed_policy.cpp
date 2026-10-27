#include "network/transfer_speed_policy.hpp"
#include <cassert>
#include <cstdint>

using namespace psvitaalive;
int main() {
    assert(transferRateBps(0, 1000000) == 0);
    assert(transferRateBps(1048576, 1000000) == 1048576);
    assert(transferRateBps(1048576, 2000000) == 524288);
    assert(transferRateBps(0, 0) == 0);

    // A resumed download starts with 200 MiB saved, but transfers zero new
    // bytes: UI speed must be zero, not savedBytes / timeSinceResume.
    TransferSpeedSampler speed;
    assert(speed.observe(1000000, 0) == 0);
    assert(speed.observe(1300000, 0) == 0);
    assert(speed.observe(2000000, 0) == 0);
    assert(speed.observe(3000000, 1048576) == 1048576);
    assert(speed.observe(3500000, 1048576) == 1048576);
    assert(speed.observe(4000000, 2097152) > 524288);
    // New HTTP transfer session uses a new sampler.
    TransferSpeedSampler next;
    assert(next.observe(10000000, 0) == 0);
    assert(next.observe(11000000, 1048576) == 1048576);
    return 0;
}

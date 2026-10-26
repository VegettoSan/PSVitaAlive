#include "network/parallel_resume_policy.hpp"
#include <cassert>
#include <cstdint>

using namespace psvitaalive;
int main() {
    // Two-way 6 MiB and four-way 1 GiB: continue at exactly saved prefix.
    constexpr uint64_t MiB = 1024ULL * 1024ULL;
    assert(parallelPartLength(0, 3*MiB-1) == 3*MiB);
    assert(parallelSavedPrefixValid(0, 3*MiB-1, 2*MiB));
    assert(parallelRangePending(0, 3*MiB-1, 2*MiB));
    assert(parallelNextByte(0, 2*MiB) == 2*MiB);
    assert(!parallelRangePending(0, 3*MiB-1, 3*MiB));
    assert(!parallelSavedPrefixValid(0, 3*MiB-1, 3*MiB+1));
    assert(!parallelSavedPrefixValid(17, 15, 0));
    constexpr uint64_t size = 1024*MiB;
    constexpr uint64_t chunk = size / 4;
    const uint64_t saved[] = {180*MiB, 110*MiB, 75*MiB, 50*MiB};
    uint64_t totalSaved = 0;
    for (uint64_t i = 0; i < 4; ++i) {
        const uint64_t start = i*chunk;
        const uint64_t end = i == 3 ? size-1 : (i+1)*chunk-1;
        assert(parallelSavedPrefixValid(start, end, saved[i]));
        assert(parallelNextByte(start, saved[i]) == start + saved[i]);
        totalSaved += saved[i];
    }
    assert(totalSaved == 415*MiB);
    assert(size-totalSaved == 609*MiB);
    // End is inclusive; 64-bit offsets must not wrap near 4 GiB.
    constexpr uint64_t bigStart = 5ULL*1024*MiB;
    assert(parallelNextByte(bigStart, 10*MiB) == bigStart+10*MiB);
    assert(parallelPartLength(bigStart, bigStart+12*MiB-1) == 12*MiB);
    return 0;
}

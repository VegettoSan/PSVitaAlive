#include "archive/segmented_zip_layout.hpp"

#include <cassert>
#include <cstdint>
#include <vector>
#include <limits>

using namespace psvitaalive;

int main() {
    std::vector<ZipRangeSpan> spans;
    uint64_t total = 0;
    assert(buildZipRangeLayout({256, 257}, spans, total));
    assert(total == 513 && spans.size() == 2);
    assert(findZipRange(spans, 0) == 0);
    assert(findZipRange(spans, 255) == 0);
    assert(findZipRange(spans, 256) == 1);
    assert(findZipRange(spans, 512) == 1);
    assert(findZipRange(spans, total) == spans.size());

    assert(buildZipRangeLayout({1024, 1024, 1024, 1027}, spans, total));
    assert(total == 4099);
    assert(findZipRange(spans, 2047) == 1);
    assert(findZipRange(spans, 2048) == 2);
    assert(findZipRange(spans, 3072) == 3);
    assert(findZipRange(spans, 4098) == 3);

    const uint64_t huge = 750ULL * 1024 * 1024;
    assert(buildZipRangeLayout({huge, huge, huge, huge + 1}, spans, total));
    assert(total > 0x80000000ULL); // ZIP64 / multi-gigabyte logical offsets
    assert(findZipRange(spans, huge * 3) == 3);
    assert(findZipRange(spans, total - 1) == 3);
    assert(findZipRange(spans, total) == 4);

    assert(!buildZipRangeLayout({}, spans, total));
    assert(!buildZipRangeLayout({100, 0}, spans, total));
    assert(!buildZipRangeLayout({100, 99}, spans, total));
    assert(!buildZipRangeLayout({100, 103}, spans, total));
    assert(!buildZipRangeLayout({100, 100, 99, 100}, spans, total));
    assert(!buildZipRangeLayout({100, 100, 100}, spans, total));
    assert(!buildZipRangeLayout({1, 1, 1, 7}, spans, total));
    const uint64_t max = std::numeric_limits<uint64_t>::max();
    assert(!buildZipRangeLayout({max, max}, spans, total));
    return 0;
}

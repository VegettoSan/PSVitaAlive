#include "archive/zip_work_pacer.hpp"

#include <cassert>

using psvitaalive::ZipWorkPacer;

int main() {
    // Eight tiny files must yield even when they write almost no bytes.
    ZipWorkPacer tiny;
    for (unsigned i = 0; i < 7; ++i) {
        assert(!tiny.afterBytes(256));
        assert(!tiny.afterEntry());
    }
    assert(!tiny.afterBytes(256));
    assert(tiny.afterEntry());
    assert(!tiny.afterEntry());

    // A single large file must still yield after each MiB.
    ZipWorkPacer large;
    for (unsigned i = 0; i < 3; ++i)
        assert(!large.afterBytes(256 * 1024ULL));
    assert(large.afterBytes(256 * 1024ULL));
    assert(!large.afterBytes(1));
    assert(large.afterBytes(ZipWorkPacer::kByteBudget));

    // Both budgets reset together so a single event cannot cause repeated
    // excessive delays on the following tiny file.
    ZipWorkPacer mixed;
    for (unsigned i = 0; i < 4; ++i) assert(!mixed.afterEntry());
    assert(mixed.afterBytes(ZipWorkPacer::kByteBudget));
    for (unsigned i = 0; i < 7; ++i) assert(!mixed.afterEntry());
    assert(mixed.afterEntry());
    assert(!mixed.afterBytes(ZipWorkPacer::kByteBudget / 2));
    assert(!mixed.afterEntry());
    return 0;
}

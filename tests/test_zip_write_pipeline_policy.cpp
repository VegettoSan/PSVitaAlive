#include "archive/zip_write_pipeline_policy.hpp"

#include <cassert>
#include <cstdint>

int main() {
    using namespace psvitaalive;
    static_assert(kZipWritePipelineSlots == 3, "Keep peak ZIP writer memory bounded");
    static_assert(kZipWritePipelineMinBytes == 8ULL * 1024 * 1024, "Pipeline threshold");
    const auto threshold = kZipWritePipelineMinBytes;

    assert(!shouldUseZipWritePipeline(0, false, threshold));
    assert(!shouldUseZipWritePipeline(0, true, 0));
    assert(!shouldUseZipWritePipeline(0, true, threshold - 1));
    assert(shouldUseZipWritePipeline(0, true, threshold));
    assert(shouldUseZipWritePipeline(0, true, 5905580032ULL)); // BT4 ZIP data size
    assert(shouldUseZipWritePipeline(0, true, UINT64_MAX));

    // Compressed and unfamiliar ZIPs must retain libzip's existing serial path.
    assert(!shouldUseZipWritePipeline(8, true, threshold * 8));
    assert(!shouldUseZipWritePipeline(12, true, threshold * 8));
    assert(!shouldUseZipWritePipeline(93, true, threshold * 8));
    assert(!shouldUseZipWritePipeline(99, false, 0));
    return 0;
}

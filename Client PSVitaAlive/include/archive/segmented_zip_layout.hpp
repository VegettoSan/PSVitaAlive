#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace psvitaalive {

// Logical ZIP layout over validated, independently stored HTTP ranges.
// Pure C++ so layout and >2 GiB offsets are regression-tested without VitaSDK.
struct ZipRangeSpan {
    uint64_t start = 0;
    uint64_t size = 0;
};

inline bool buildZipRangeLayout(const std::vector<uint64_t>& sizes,
                                std::vector<ZipRangeSpan>& spans,
                                uint64_t& total) {
    spans.clear();
    total = 0;
    if (sizes.size() != 2 && sizes.size() != 4) return false;
    const uint64_t first = sizes[0];
    if (!first) return false;
    for (size_t i = 0; i < sizes.size(); ++i) {
        if (!sizes[i] || sizes[i] > UINT64_MAX - total) {
            spans.clear();
            total = 0;
            return false;
        }
        if (i > 0 && i + 1 < sizes.size() && sizes[i] != first) {
            spans.clear();
            total = 0;
            return false;
        }
        if (i + 1 == sizes.size() &&
            (sizes[i] < first || sizes[i] - first > sizes.size() - 1)) {
            spans.clear();
            total = 0;
            return false;
        }
        spans.push_back({total, sizes[i]});
        total += sizes[i];
    }
    return true;
}

inline size_t findZipRange(const std::vector<ZipRangeSpan>& spans,
                           uint64_t position) {
    for (size_t i = 0; i < spans.size(); ++i) {
        if (position >= spans[i].start &&
            position - spans[i].start < spans[i].size) return i;
    }
    return spans.size(); // EOF or invalid seek
}

} // namespace psvitaalive

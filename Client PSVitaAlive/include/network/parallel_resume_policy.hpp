#pragma once
#include <cstdint>

namespace psvitaalive {

// One range owns [begin,end], inclusive. Only its contiguous prefix is reusable.
inline uint64_t parallelPartLength(uint64_t begin, uint64_t end) {
    return end >= begin ? end - begin + 1 : 0;
}
inline bool parallelSavedPrefixValid(uint64_t begin, uint64_t end, uint64_t saved) {
    return end >= begin && saved <= parallelPartLength(begin, end);
}
inline bool parallelRangePending(uint64_t begin, uint64_t end, uint64_t saved) {
    return parallelSavedPrefixValid(begin, end, saved) &&
           saved < parallelPartLength(begin, end);
}
inline uint64_t parallelNextByte(uint64_t begin, uint64_t saved) {
    return begin + saved; // caller first validates saved <= interval length
}
// A failed range that delivered no bytes cannot invalidate other ranges.
// Any newly received bytes still need an exact 206, valid offsets and identity.
inline bool parallelReceivedPrefixSafe(uint64_t received, bool ioError,
                                      bool rangeError, long status, bool rangeValid,
                                      bool sameValidator) {
    return !ioError && (received == 0 ||
        (!rangeError && status == 206 && rangeValid && sameValidator));
}
} // namespace psvitaalive

#pragma once

#include <cstdint>

namespace psvitaalive {
namespace update {

// Pure decision logic: catalog key dr=0 is legacy/unspecified.
// A missing local receipt is UNKNOWN, not evidence that data is outdated.
enum class DataRevisionState : unsigned char {
    Unspecified,
    Matches,
    Unverified,
    Different
};

inline DataRevisionState checkDataRevision(uint32_t required, bool hasDataLinks,
                                           bool hasLocalReceipt, uint32_t installed) {
    if (!hasDataLinks || required == 0) return DataRevisionState::Unspecified;
    if (!hasLocalReceipt || installed == 0) return DataRevisionState::Unverified;
    return installed == required ? DataRevisionState::Matches : DataRevisionState::Different;
}

inline bool shouldWarnDataRevision(DataRevisionState state) {
    return state == DataRevisionState::Unverified || state == DataRevisionState::Different;
}

} // namespace update
} // namespace psvitaalive

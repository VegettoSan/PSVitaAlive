#pragma once

#include <cstdint>

namespace psvitaalive {
namespace update {

// Public catalog dr=0 means no data revision tracking.
// A previously installed app with no recorded dr has revision 0: when the
// catalog first introduces dr=1, a full Install All update is offered.
// A newer local dr is never downgraded automatically.
inline bool needsDataRevisionInstall(uint32_t required, bool hasDataLinks,
                                     uint32_t installed) {
    return hasDataLinks && required > 0 && installed < required;
}

} // namespace update
} // namespace psvitaalive

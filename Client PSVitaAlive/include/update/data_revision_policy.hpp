#pragma once

#include <cstdint>

namespace psvitaalive {
namespace update {

// A Game/Data Files revision only chooses HOW to apply an already-detected
// VPK version update. Changing dr alone must never advertise a new update.
// With an older/missing local dr, use the existing Install All flow to choose
// one alternative per kind (VPK, Game Files, Data Files).
inline bool shouldUseInstallAllForUpdate(bool vpkUpdateAvailable,
                                         uint32_t catalogRevision,
                                         bool hasDataLinks,
                                         uint32_t installedRevision) {
    return vpkUpdateAvailable && hasDataLinks &&
           catalogRevision > 0 && installedRevision < catalogRevision;
}

} // namespace update
} // namespace psvitaalive

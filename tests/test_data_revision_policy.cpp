#include "update/data_revision_policy.hpp"
#include <cassert>

using psvitaalive::update::shouldUseInstallAllForUpdate;

int main() {
    // Version is unchanged: adding/increasing dr never advertises UPDATE.
    assert(!shouldUseInstallAllForUpdate(false, 1, true, 0));
    assert(!shouldUseInstallAllForUpdate(false, 2, true, 1));
    assert(!shouldUseInstallAllForUpdate(false, 2, true, 2));

    // A newer VPK version is available, and the data revision is missing/old.
    assert(shouldUseInstallAllForUpdate(true, 1, true, 0));
    assert(shouldUseInstallAllForUpdate(true, 2, true, 0));
    assert(shouldUseInstallAllForUpdate(true, 2, true, 1));

    // VPK-only update when there is no declared data revision or no new data.
    assert(!shouldUseInstallAllForUpdate(true, 0, true, 0));
    assert(!shouldUseInstallAllForUpdate(true, 2, false, 0));
    assert(!shouldUseInstallAllForUpdate(true, 2, true, 2));
    assert(!shouldUseInstallAllForUpdate(true, 2, true, 3)); // no data downgrade
}

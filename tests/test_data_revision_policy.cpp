#include "update/data_revision_policy.hpp"
#include <cassert>

using psvitaalive::update::needsDataRevisionInstall;

int main() {
    assert(!needsDataRevisionInstall(0, true, 0));       // no dr in catalog
    assert(!needsDataRevisionInstall(2, false, 0));      // no data links
    assert(needsDataRevisionInstall(1, true, 0));        // first dr on legacy app
    assert(needsDataRevisionInstall(2, true, 0));        // no previous receipt
    assert(needsDataRevisionInstall(2, true, 1));        // newer data in catalog
    assert(!needsDataRevisionInstall(2, true, 2));       // installed matches
    assert(!needsDataRevisionInstall(2, true, 3));       // never downgrade
}

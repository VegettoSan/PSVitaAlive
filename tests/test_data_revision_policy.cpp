#include "update/data_revision_policy.hpp"
#include <cassert>
using namespace psvitaalive::update;
int main() {
    assert(checkDataRevision(0, true, false, 0) == DataRevisionState::Unspecified);
    assert(checkDataRevision(2, false, true, 1) == DataRevisionState::Unspecified);
    assert(checkDataRevision(2, true, false, 0) == DataRevisionState::Unverified);
    assert(checkDataRevision(2, true, true, 2) == DataRevisionState::Matches);
    assert(checkDataRevision(2, true, true, 1) == DataRevisionState::Different);
    assert(checkDataRevision(2, true, true, 3) == DataRevisionState::Different);
    assert(!shouldWarnDataRevision(DataRevisionState::Unspecified));
    assert(!shouldWarnDataRevision(DataRevisionState::Matches));
    assert(shouldWarnDataRevision(DataRevisionState::Unverified));
    assert(shouldWarnDataRevision(DataRevisionState::Different));
}

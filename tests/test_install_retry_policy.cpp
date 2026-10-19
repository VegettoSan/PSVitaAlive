#include "installer/install_retry_policy.hpp"
#include <cassert>

int main() {
    using namespace psvitaalive;
    assert(isPermanentInstallResult(InstallDispatchResult::InvalidArchive));
    assert(isPermanentInstallResult(InstallDispatchResult::UnsupportedFormat));
    assert(isPermanentInstallResult(InstallDispatchResult::InvalidArgument));
    assert(isPermanentInstallResult(InstallDispatchResult::DetectFailed));
    assert(!isPermanentInstallResult(InstallDispatchResult::InstallFailed));
    assert(!isPermanentInstallResult(InstallDispatchResult::IoError));
    assert(!isPermanentInstallResult(InstallDispatchResult::Cancelled));
    assert(!isPermanentInstallResult(InstallDispatchResult::Ok));
    return 0;
}

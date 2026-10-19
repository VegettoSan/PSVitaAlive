#pragma once
#include "installer/install_dispatcher.hpp"

namespace psvitaalive {

// Stable error codes are reliable across languages and provider error strings.
// I/O and promoter failures remain retryable by the existing limited policy.
inline bool isPermanentInstallResult(InstallDispatchResult result) {
    return result == InstallDispatchResult::InvalidArgument ||
           result == InstallDispatchResult::UnsupportedFormat ||
           result == InstallDispatchResult::InvalidArchive ||
           result == InstallDispatchResult::DetectFailed;
}

} // namespace psvitaalive

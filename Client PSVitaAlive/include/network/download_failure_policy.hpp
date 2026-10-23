#pragma once

// Dependency-free retry policy shared by the Vita HTTP client and host tests.
// "Unavailable" means this transfer cannot reach its source right now;
// it does not assert that an entire website is permanently offline.
namespace psvitaalive {

class UnreachableSourceDetector {
public:
    // Count only failures before receiving an HTTP response or *any* file
    // bytes. Mid-transfer drops must retain the full resume/retry policy.
    bool observe(bool connectionFailure, bool noUsableHttpResponse, bool noBytes) {
        if (!connectionFailure || !noUsableHttpResponse || !noBytes) {
            failures_ = 0;
            return false;
        }
        if (failures_ < 2) ++failures_;
        return failures_ >= 2;
    }

    unsigned consecutiveFailures() const { return failures_; }

private:
    unsigned failures_ = 0;
};

// Terminal response codes apply to the requested resource, not to an entire
// hostname. Rate limiting (429), gateway errors (5xx), 408 and range repair
// (416) deliberately remain eligible for existing recovery paths.
inline bool isPermanentDownloadHttpFailure(long status) {
    return status == 400 || status == 401 || status == 403 ||
           status == 404 || status == 405 || status == 410 ||
           status == 451;
}

} // namespace psvitaalive

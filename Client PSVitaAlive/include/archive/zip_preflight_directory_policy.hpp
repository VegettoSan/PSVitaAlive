#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace psvitaalive {

// requiredDirs is sorted and uniqued only AFTER the exact destination directory
// preparation succeeds. A staged branch uses a different temporary path and
// must never trust the original destination directory's preflight.
inline bool zipParentReadyFromPreflight(const std::vector<std::string>& requiredDirs,
                                        const std::string& parent,
                                        bool staged) {
    return !staged &&
           std::binary_search(requiredDirs.begin(), requiredDirs.end(), parent);
}

} // namespace psvitaalive

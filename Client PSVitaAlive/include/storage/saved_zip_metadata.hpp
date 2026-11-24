#pragma once
#include <string>

namespace psvitaalive::storage {

// Read exact line keys from the existing, bounded manual-recovery sidecar.
// Display metadata never changes the inventory's deletion identity.
inline std::string savedZipField(const std::string& text, const std::string& key) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto end = text.find('\n', pos);
        const auto length = (end == std::string::npos ? text.size() : end) - pos;
        if (length >= key.size() && text.compare(pos, key.size(), key) == 0) {
            auto value = text.substr(pos + key.size(), length - key.size());
            if (!value.empty() && value.back() == '\r') value.pop_back();
            if (value == "-" || value.empty()) return {};
            return value;
        }
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return {};
}

// Never guess the owning app from the filename, or accept conflicting IDs.
inline bool savedZipMatchesApp(const std::string& appId, const std::string& titleId,
                               const std::string& candidateId,
                               const std::string& candidateTitleId) {
    return (!appId.empty() || !titleId.empty()) &&
        (appId.empty() || appId == candidateId) &&
        (titleId.empty() || titleId == candidateTitleId);
}

} // namespace psvitaalive::storage

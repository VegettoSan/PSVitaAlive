#pragma once

#include <cstdint>
#include <string>

namespace psvitaalive {

class UpdateChecker {
public:
    enum class State {
        UpToDate,
        UpdateAvailable,
        Failed
    };

    struct Result {
        State state = State::Failed;
        std::string localVersion;
        std::string remoteVersion;
        std::string releaseTag;
        std::string releaseName;
        std::string downloadUrl;
        std::string assetName;
        std::string digest;
        uint64_t assetSize = 0;
        std::string error;
    };

    static Result checkLatest(const std::string& currentVersion);
};

} // namespace psvitaalive

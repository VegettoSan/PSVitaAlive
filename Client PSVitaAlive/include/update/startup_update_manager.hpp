#pragma once

#include "update/update_checker.hpp"

#include <functional>
#include <string>

namespace psvitaalive {

/**
 * Owns the startup self-update phase independently from CatalogManager.
 * CatalogManager may mirror its progress into the existing loading/status UI
 * without owning any update policy or installation logic.
 */
class StartupUpdateManager {
public:
    enum class State {
        UpToDate,
        UpdateAvailableApplied,
        Failed
    };

    struct Result {
        State state = State::Failed;
        std::string localVersion;
        std::string remoteVersion;
        std::string error;
        bool restartRequired = false;
    };

    using ProgressFn = std::function<void(const UpdateChecker::ApplyProgress&)>;

    static Result run(
        const std::string& currentVersion,
        ProgressFn onProgress = nullptr
    );
};

} // namespace psvitaalive

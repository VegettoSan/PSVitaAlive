#include "update/startup_update_manager.hpp"

#include "diagnostic_logger.hpp"

namespace psvitaalive {

StartupUpdateManager::Result StartupUpdateManager::run(
    const std::string& currentVersion,
    ProgressFn onProgress
) {
    Result result;

    const UpdateChecker::Result check = UpdateChecker::checkLatest(currentVersion);
    result.localVersion = check.localVersion;
    result.remoteVersion = check.remoteVersion;

    if (check.state == UpdateChecker::State::UpToDate) {
        result.state = State::UpToDate;
        diagnostics::log("[StartupUpdate] client is up to date: local=" +
                         check.localVersion + " remote=" + check.remoteVersion);
        if (onProgress) {
            UpdateChecker::ApplyProgress progress;
            progress.stage = UpdateChecker::ApplyStage::Done;
            progress.current = 1;
            progress.total = 1;
            progress.message = "Application is up to date";
            onProgress(progress);
        }
        return result;
    }

    if (check.state == UpdateChecker::State::Failed) {
        result.state = State::Failed;
        result.error = check.error.empty() ? "Update check failed" : check.error;
        diagnostics::log("[StartupUpdate] update check failed: " + result.error);
        if (onProgress) {
            UpdateChecker::ApplyProgress progress;
            progress.stage = UpdateChecker::ApplyStage::Error;
            progress.message = result.error;
            onProgress(progress);
        }
        return result;
    }

    diagnostics::log("[StartupUpdate] update available: remote=" +
                     check.remoteVersion + " asset=" + check.assetName);

    const bool applied = UpdateChecker::applyUpdate(
        check,
        onProgress,
        nullptr
    );

    if (!applied) {
        result.state = State::Failed;
        result.error = "Update download/install failed";
        diagnostics::log("[StartupUpdate] update apply failed");
        return result;
    }

    result.state = State::UpdateAvailableApplied;
    result.restartRequired = true;
    diagnostics::log("[StartupUpdate] update installed successfully; restart required");
    return result;
}

} // namespace psvitaalive

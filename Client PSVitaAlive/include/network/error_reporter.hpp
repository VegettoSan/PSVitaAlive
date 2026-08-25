#pragma once

#include <string>

namespace psvitaalive {

/**
 * User-triggered Discord webhook report (errors + manual reports).
 * Does not touch download/install pipelines — only reads logs and POSTs JSON.
 */
struct ErrorReportResult {
    bool ok = false;
    std::string message; // short UI string
};

/**
 * Send a report embed to the configured Discord webhook.
 * @param title  Short error / report title (shown in Discord embed)
 * @param context Extra context (e.g. install message, file name)
 * Includes client version (PSVITAALIVE_VERSION), timestamp, and a tail of session.log.
 */
ErrorReportResult sendErrorReport(
    const std::string& title,
    const std::string& context = std::string()
);

/** Milliseconds until another report is allowed (rate limit). 0 = can send now. */
uint64_t errorReportCooldownRemainingMs();

} // namespace psvitaalive

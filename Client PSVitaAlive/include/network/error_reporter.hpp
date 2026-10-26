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
 * Report category used for Discord search filters (#tag) and embed color.
 * Search in Discord with e.g.  #install_failed  or  #manual
 */
enum class ErrorReportKind {
    Manual = 0,       // #manual
    InstallFailed,    // #install_failed
    DownloadFailed,   // #download_failed
    Catalog,          // #catalog
    SelfUpdate,       // #self_update
    Other             // #other
};

/** Optional app context when the report is about a catalog entry / install. */
struct ErrorReportAppInfo {
    std::string name;     // catalog display name
    std::string titleId;  // e.g. PCSG00000
    std::string version;  // catalog version if known
};

struct ErrorReportRequest {
    std::string title;                    // short headline
    std::string context;                  // error reason / extra notes
    ErrorReportKind kind = ErrorReportKind::Other;
    ErrorReportAppInfo app;               // empty if not app-related
    std::string fileName;                 // download/install file if any
};

/**
 * Send a report embed to the configured Discord webhook.
 * Message content includes searchable tags like #install_failed #app_PCSG00000
 * so you can filter in Discord search.
 */
ErrorReportResult sendErrorReport(const ErrorReportRequest& req);

/** Back-compat: title + context only (kind = Other). */
ErrorReportResult sendErrorReport(
    const std::string& title,
    const std::string& context = std::string()
);

/** User-written feedback from the main Report button: no diagnostics,
 * logs, file identifiers, selected app or device info are attached.
 */
struct ManualFeedbackRequest {
    std::string title;
    std::string body;
    bool suggestion = false;
};
ErrorReportResult sendManualFeedback(const ManualFeedbackRequest& req);

/** Milliseconds until another report is allowed (rate limit). 0 = can send now. */
uint64_t errorReportCooldownRemainingMs();

} // namespace psvitaalive

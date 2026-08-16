#pragma once

#include <string>

namespace psvitaalive {

/**
 * Minimal LiveArea / install verification helpers (guide phase 5).
 * Does not clone full VitaShell refresh.c — verifies trees and logs guidance.
 */
class RefreshManager {
public:
    /** True if ux0:app/<titleId>/ looks present (param.sfo or directory). */
    static bool appTreeExists(const std::string& titleId);

    /**
     * Post-install check. Returns true if tree is present or path is non-app
     * (ZIP/pspemu). Fills message for UI/logs.
     */
    static bool verifyAfterInstall(
        const std::string& titleId,
        const std::string& installPath,
        bool liveAreaHint,
        std::string& messageOut
    );
};

} // namespace psvitaalive

#pragma once

#include <string>

namespace psvitaalive {

/**
 * LiveArea helpers.
 * - appTreeExists / verifyAfterInstall: post-install checks and UI messages.
 * - refreshTitleLiveArea: VitaShell-style single-title refresh (move to
 *   ux0:temp/app + scePromoterUtilityPromotePkg). Used after fallback copy
 *   when Promote reported success but the system bubble was not registered.
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

    /**
     * Attempt a VitaShell-like LiveArea refresh for one title already present
     * under ux0:app/<titleId>. Returns true if the app tree is visible after
     * promote. On failure, best-effort restores the folder and fills messageOut.
     */
    static bool refreshTitleLiveArea(const std::string& titleId, std::string& messageOut);
};

} // namespace psvitaalive

#pragma once

#include <string>

namespace psvitaalive {

struct PluginStatus {
    bool nonpdrm = false;
    bool nopspemudrmKern = false;
    bool nopspemudrmUser = false;
    bool repatch = false;
    bool fdFix = false;
    /** Primary config path used by taiHEN-style resolution. */
    std::string configPathUsed;
    /** Human-readable scan summary for logs / UI. */
    std::string detail;
};

/**
 * Read-only taiHEN plugin detection (AutoPlugin2-style).
 *
 * - Only read ur0:tai/config.txt (community standard; do not use ux0:tai/config.txt).
 * - Parse sections (*KERNEL, *main, *ALL, title ids).
 * - Match plugins by basename (case-insensitive), ignore # comments.
 * - Verify file on the path listed in config (ur0: or ux0: allowed — some app-specific
 *   plugins must live on ux0 while the line stays in ur0 config.txt).
 * - Recognize RePatch variants: repatch.skprx, repatch_4.skprx, repatch_ex.skprx.
 * - Detect FdFix separately so callers can apply compatibility rules (RePatch can satisfy FdFix-dependent software).
 *
 * Does not modify config.txt or install plugins.
 */
class PluginDetector {
public:
    static PluginStatus scan();
};

} // namespace psvitaalive

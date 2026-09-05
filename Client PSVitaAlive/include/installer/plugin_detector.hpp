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
 * - Prefer ux0:tai/config.txt when present (taiHEN active config).
 * - Else ur0:tai/config.txt (standard AutoPlugin2 / SD2Vita path).
 * - Parse sections (*KERNEL, *main, *ALL, title ids).
 * - Match plugins by basename (case-insensitive), ignore # comments.
 * - Verify the .skprx/.suprx file exists on the path from config or common paths.
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

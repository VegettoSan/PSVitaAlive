#pragma once

#include <string>

namespace psvitaalive {

struct PluginStatus {
    bool nonpdrm = false;
    bool nopspemudrmKern = false;
    bool nopspemudrmUser = false;
    std::string configPathUsed;
    std::string detail;
};

/**
 * Scan taiHEN config for NoNpDrm / NoPspEmuDrm.
 * Does not install plugins — detection only (see implementation guide).
 */
class PluginDetector {
public:
    static PluginStatus scan();
};

} // namespace psvitaalive

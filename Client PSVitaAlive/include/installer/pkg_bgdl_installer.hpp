#pragma once

#include "installer/bgdl_client.hpp"

#include <string>

namespace psvitaalive {

struct PkgBgdlRequest {
    std::string title;
    std::string url;
    std::string zrif;       // NoPayStation zRIF (preferred)
    std::string rifPath;    // optional existing .rif on disk
    BgdlTaskType type = BgdlTaskType::Game;
};

struct PkgBgdlResult {
    bool ok = false;
    uint32_t bgdlId = 0;
    int errorCode = 0;
    std::string message;
};

/**
 * Licensed Vita PKG install via system BGDL (PKGj-style).
 * Completely separate from HomebrewInstaller / VPK promote path.
 *
 * Flow:
 *  1) Decode zRIF (or copy rif file) -> ux0:bgdl/temp.dat
 *  2) Queue ShellSvc background download with URL + license path
 *  3) System downloads PKG, applies license, promotes (LiveArea notification)
 *
 * Requires: taiHEN, NoNpDrm, real hardware (not Vita3K).
 */
class PkgBgdlInstaller {
public:
    static BgdlTaskType typeFromLinkType(const std::string& linkType);
    static PkgBgdlResult enqueue(const PkgBgdlRequest& req);
};

} // namespace psvitaalive

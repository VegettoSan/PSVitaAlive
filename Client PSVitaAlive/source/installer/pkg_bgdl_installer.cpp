#include "installer/pkg_bgdl_installer.hpp"
#include "installer/license_helper.hpp"
#include "diagnostic_logger.hpp"

#include <psp2/kernel/clib.h>

#include <cctype>
#include <string>

namespace psvitaalive {

BgdlTaskType PkgBgdlInstaller::typeFromLinkType(const std::string& linkType) {
    std::string t;
    t.reserve(linkType.size());
    for (char c : linkType) {
        t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (t.find("dlc") != std::string::npos || t == "addcont" || t == "add_cont") {
        return BgdlTaskType::AddCont;
    }
    if (t.find("update") != std::string::npos || t.find("patch") != std::string::npos) {
        return BgdlTaskType::GameUpdate;
    }
    if (t.find("theme") != std::string::npos) {
        return BgdlTaskType::Theme;
    }
    return BgdlTaskType::Game;
}

PkgBgdlResult PkgBgdlInstaller::enqueue(const PkgBgdlRequest& req) {
    PkgBgdlResult out;
    diagnostics::log("[PkgBgdl] enqueue begin");

    if (req.url.empty()) {
        out.message = "empty PKG url";
        out.errorCode = -2;
        diagnostics::log(std::string("[PkgBgdl] ") + out.message);
        return out;
    }

    if (!BgdlClient::instance().init()) {
        out.message = "BGDL unavailable (need real Vita + taiHEN/ShellSvc)";
        out.errorCode = -1;
        diagnostics::log(std::string("[PkgBgdl] ") + out.message);
        return out;
    }

    std::string rifPath;
    std::string err;
    const bool haveLicense = LicenseHelper::prepareBgdlLicense(req.zrif, req.rifPath, rifPath, err);
    if (!haveLicense) {
        // Still allow queue without license (will almost always fail for NPS content),
        // but surface a clear warning — PKGj refuses without zRIF for games.
        diagnostics::log(std::string("[PkgBgdl] license prepare failed: ") + err +
                         " — queuing without license is unsupported for NoPayStation PKGs");
        out.message = std::string("license required: ") + err;
        out.errorCode = -10;
        return out;
    }

    const std::string title = req.title.empty() ? "PSVitaAlive PKG" : req.title;
    const auto bg = BgdlClient::instance().enqueue(title, req.url, rifPath, req.type);
    out.ok = bg.ok;
    out.bgdlId = bg.bgdlId;
    out.errorCode = bg.errorCode;
    out.message = bg.message;

    if (bg.ok) {
        diagnostics::log(std::string("[PkgBgdl] queued ok id=") + std::to_string(bg.bgdlId) +
                         " type=" + std::to_string(static_cast<int>(req.type)) +
                         " rif=" + rifPath);
        out.message = "Queued in system download manager (LiveArea). id=" + std::to_string(bg.bgdlId);
    } else {
        diagnostics::log(std::string("[PkgBgdl] enqueue failed: ") + bg.message +
                         " code=" + std::to_string(bg.errorCode));
    }
    return out;
}

} // namespace psvitaalive

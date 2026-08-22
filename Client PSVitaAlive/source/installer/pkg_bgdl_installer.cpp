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
    if (t.find("psp") != std::string::npos || t.find("ps1") != std::string::npos ||
        t.find("psx") != std::string::npos) {
        return BgdlTaskType::Psp;
    }
    if (t.find("dlc") != std::string::npos || t == "addcont" || t == "add_cont") {
        return BgdlTaskType::AddCont;
    }
    if (t.find("update") != std::string::npos || t.find("patch") != std::string::npos) {
        return BgdlTaskType::Game;
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
    bool haveLicense = false;

    // Vita path: zRIF from request, or on-demand from content_id index (not kept in RAM).
    std::string zrif = req.zrif;
    if (zrif.empty() && req.rifPath.empty()) {
        std::string looked;
        const std::string& cid = req.contentId;
        if (!cid.empty() && LicenseHelper::lookupZrifForContentId(cid, looked)) {
            zrif = looked;
            diagnostics::log("[PkgBgdl] zRIF resolved from content_id index");
        }
        // Link often lacks content_id (only title_id / path PCSC#####_00). Fall back by title id.
        if (zrif.empty()) {
            std::string tid = req.contentId; // may already be empty
            // Prefer explicit title from content id: JA0003-PCSC80020_00-XXX -> PCSC80020
            auto extractTid = [](const std::string& s) -> std::string {
                // Match PCS[ABCEGH]##### or PSS##### style title ids
                for (size_t i = 0; i + 9 <= s.size(); ++i) {
                    if (s[i] == 'P' && s[i+1] == 'C' && (s[i+2] == 'S' || s[i+2] == 'C') &&
                        (s[i+3] >= 'A' && s[i+3] <= 'Z')) {
                        bool ok = true;
                        for (int k = 4; k < 9; ++k) {
                            if (s[i+k] < '0' || s[i+k] > '9') { ok = false; break; }
                        }
                        if (ok) return s.substr(i, 9);
                    }
                }
                return {};
            };
            if (tid.empty()) tid = extractTid(req.url);
            if (tid.empty()) tid = extractTid(req.title);
            if (tid.size() >= 9) {
                // Normalize to 9-char TITLEID if longer path fragment
                if (tid.size() > 9) tid = tid.substr(0, 9);
                if (LicenseHelper::lookupZrifForTitleId(tid, looked)) {
                    zrif = looked;
                    diagnostics::log(std::string("[PkgBgdl] zRIF resolved from title_id=") + tid);
                }
            }
        }
    }
    if (!zrif.empty() || !req.rifPath.empty()) {
        haveLicense = LicenseHelper::prepareBgdlLicense(zrif, req.rifPath, rifPath, err);
    }

    // PSP / PS1 path (PKGj): synthetic RIF from Content ID — does NOT use RAP from TSV.
    if (!haveLicense &&
        (req.type == BgdlTaskType::Psp || !req.contentId.empty()) &&
        !req.contentId.empty() &&
        req.zrif.empty()) {
        std::vector<uint8_t> pspRif;
        if (LicenseHelper::createPspRif(req.contentId, pspRif, err)) {
            if (LicenseHelper::writeRifBytes(pspRif, LicenseHelper::kBgdlTempRif, err)) {
                rifPath = LicenseHelper::kBgdlTempRif;
                haveLicense = true;
                diagnostics::log("[PkgBgdl] using synthetic PSP/PS1 RIF (PKGj-style)");
            }
        }
    }

    if (!haveLicense) {
        diagnostics::log(std::string("[PkgBgdl] license prepare failed: ") + err);
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

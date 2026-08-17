#include "installer/plugin_detector.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace psvitaalive {
namespace {

std::string toLower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool readWholeFile(const char* path, std::string& out) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return false;
    SceIoStat st{};
    if (sceIoGetstatByFd(fd, &st) < 0) {
        sceIoClose(fd);
        return false;
    }
    const size_t n = static_cast<size_t>(st.st_size);
    if (n == 0 || n > 512 * 1024) {
        sceIoClose(fd);
        return false;
    }
    out.resize(n);
    const int rd = sceIoRead(fd, &out[0], n);
    sceIoClose(fd);
    return rd > 0;
}

bool pathExists(const char* path) {
    SceIoStat st{};
    return sceIoGetstat(path, &st) >= 0;
}

bool containsAny(const std::string& hay, const char* const* needles) {
    for (int i = 0; needles[i] != nullptr; ++i) {
        if (hay.find(needles[i]) != std::string::npos) return true;
    }
    return false;
}

void scanTextForPlugins(const std::string& text, PluginStatus& st) {
    const std::string low = toLower(text);

    // NoNpDrm (kernel): official + common forks
    static const char* kNoNpDrm[] = {
        "nonpdrm.skprx",
        "nonpdrm_un.skprx",
        "nonpdrm",
        nullptr
    };
    if (containsAny(low, kNoNpDrm)) {
        st.nonpdrm = true;
    }

    // NoPspEmuDrm kernel half
    static const char* kNoPspKern[] = {
        "nopspemudrm_kern",
        "nopspemudrm.skprx",
        nullptr
    };
    if (containsAny(low, kNoPspKern)) {
        st.nopspemudrmKern = true;
    }

    // NoPspEmuDrm user half
    static const char* kNoPspUser[] = {
        "nopspemudrm_user",
        "nopspemudrm_user.suprx",
        nullptr
    };
    if (containsAny(low, kNoPspUser)) {
        st.nopspemudrmUser = true;
    }

    // Generic token without confusing nopsmdrm (PSM plugin)
    // If only "nopspemudrm" appears without _kern/_user, treat as kern hint only when
    // ".skprx" is on the same line-ish — already covered by nopspemudrm.skprx above.
}

} // namespace

PluginStatus PluginDetector::scan() {
    PluginStatus st;

    // Preferred practice: plugins live under ur0:tai (esp. SD2Vita).
    // Fall back to ux0 only if ur0 config is missing.
    // If BOTH exist, merge (OR flags) so a stale ux0 config does not hide ur0 plugins,
    // and report which paths were read.
    const char* ur0Config = "ur0:tai/config.txt";
    const char* ux0Config = "ux0:tai/config.txt";

    std::string ur0Text;
    std::string ux0Text;
    const bool hasUr0 = readWholeFile(ur0Config, ur0Text);
    const bool hasUx0 = readWholeFile(ux0Config, ux0Text);

    if (!hasUr0 && !hasUx0) {
        st.detail = "tai config.txt not found on ur0 or ux0";
        sceClibPrintf("[PluginDetector] %s\n", st.detail.c_str());
        return st;
    }

    if (hasUr0) {
        scanTextForPlugins(ur0Text, st);
        st.configPathUsed = ur0Config;
    }
    if (hasUx0) {
        scanTextForPlugins(ux0Text, st);
        if (st.configPathUsed.empty()) {
            st.configPathUsed = ux0Config;
        } else {
            st.configPathUsed = std::string(ur0Config) + "+" + ux0Config;
        }
    }

    // Optional: verify common plugin file locations when listed.
    if (st.nonpdrm) {
        const bool fileOk =
            pathExists("ur0:tai/nonpdrm.skprx") ||
            pathExists("ur0:tai/nonpdrm_un.skprx") ||
            pathExists("ux0:tai/nonpdrm.skprx") ||
            pathExists("ur0:tai/plugins/nonpdrm.skprx") ||
            pathExists("ux0:tai/plugins/nonpdrm.skprx") ||
            pathExists("ur0:/tai/nonpdrm.skprx");
        if (!fileOk) {
            st.detail += "nonpdrm listed but .skprx not found in common paths; ";
        }
    }
    if (st.nopspemudrmKern) {
        const bool fileOk =
            pathExists("ur0:tai/NoPspEmuDrm_kern.skprx") ||
            pathExists("ux0:tai/NoPspEmuDrm_kern.skprx") ||
            pathExists("ur0:/tai/NoPspEmuDrm_kern.skprx") ||
            pathExists("ur0:tai/nopspemudrm_kern.skprx") ||
            pathExists("ux0:tai/nopspemudrm_kern.skprx");
        if (!fileOk) {
            st.detail += "NoPspEmuDrm_kern listed but file missing; ";
        }
    }
    if (st.nopspemudrmUser) {
        const bool fileOk =
            pathExists("ur0:tai/NoPspEmuDrm_user.suprx") ||
            pathExists("ux0:tai/NoPspEmuDrm_user.suprx") ||
            pathExists("ur0:/tai/NoPspEmuDrm_user.suprx") ||
            pathExists("ur0:tai/nopspemudrm_user.suprx");
        if (!fileOk) {
            st.detail += "NoPspEmuDrm_user listed but file missing; ";
        }
    }

    char buf[256];
    sceClibSnprintf(
        buf, sizeof(buf),
        "config=%s nonpdrm=%d nopspemudrm_kern=%d user=%d",
        st.configPathUsed.c_str(),
        st.nonpdrm ? 1 : 0,
        st.nopspemudrmKern ? 1 : 0,
        st.nopspemudrmUser ? 1 : 0
    );
    if (st.detail.empty()) {
        st.detail = buf;
    } else {
        st.detail = std::string(buf) + " | " + st.detail;
    }

    // Note when both configs exist (taiHEN prefers ux0 if present).
    if (hasUr0 && hasUx0) {
        st.detail += " | note: both configs exist; taiHEN uses ux0 if present";
    }

    sceClibPrintf("[PluginDetector] %s\n", st.detail.c_str());
    return st;
}

} // namespace psvitaalive

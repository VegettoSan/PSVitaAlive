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
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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

bool pathExists(const std::string& path) {
    SceIoStat st{};
    return sceIoGetstat(path.c_str(), &st) >= 0;
}

} // namespace

PluginStatus PluginDetector::scan() {
    PluginStatus st;
    const char* candidates[] = {
        "ux0:tai/config.txt",
        "ur0:tai/config.txt",
    };

    std::string text;
    for (const char* c : candidates) {
        if (readWholeFile(c, text)) {
            st.configPathUsed = c;
            break;
        }
    }

    if (st.configPathUsed.empty()) {
        st.detail = "tai config.txt not found on ux0 or ur0";
        return st;
    }

    const std::string low = toLower(text);
    // Section-aware-ish: just search substrings (robust enough for detection).
    if (low.find("nonpdrm") != std::string::npos) {
        st.nonpdrm = true;
    }
    if (low.find("nopspemudrm_kern") != std::string::npos ||
        low.find("nopspemudrm.skprx") != std::string::npos) {
        st.nopspemudrmKern = true;
    }
    if (low.find("nopspemudrm_user") != std::string::npos) {
        st.nopspemudrmUser = true;
    }

    // Optional: verify common install paths exist when referenced.
    if (st.nonpdrm) {
        if (!pathExists("ux0:tai/nonpdrm.skprx") && !pathExists("ur0:tai/nonpdrm.skprx")) {
            st.detail += "nonpdrm listed but .skprx missing; ";
        }
    }

    char buf[192];
    sceClibSnprintf(
        buf, sizeof(buf),
        "config=%s nonpdrm=%d nopspemudrm_kern=%d user=%d",
        st.configPathUsed.c_str(),
        st.nonpdrm ? 1 : 0,
        st.nopspemudrmKern ? 1 : 0,
        st.nopspemudrmUser ? 1 : 0
    );
    if (st.detail.empty()) st.detail = buf;
    else st.detail = std::string(buf) + " | " + st.detail;

    sceClibPrintf("[PluginDetector] %s\n", st.detail.c_str());
    return st;
}

} // namespace psvitaalive

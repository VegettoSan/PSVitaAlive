#include "installer/plugin_detector.hpp"

#include "diagnostic_logger.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace psvitaalive {
namespace {

struct ConfigEntry {
    std::string section;   // without leading '*'
    std::string path;      // full path as written in config
    std::string basename;  // lower-case file name only
    int line = 0;
};

std::string toLowerCopy(const std::string& s) {
    std::string o = s;
    for (char& c : o) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return o;
}

void trimInPlace(std::string& s) {
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    size_t e = s.size();
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    s = s.substr(b, e - b);
}

std::string basenameOf(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    return toLowerCopy(name);
}

bool pathExists(const char* path) {
    if (!path || !path[0]) return false;
    size_t len = 0;
    while (path[len] != '\0') {
        if (++len > 255) return false;
    }
    SceIoStat st;
    std::memset(&st, 0, sizeof(st));
    return sceIoGetstat(path, &st) >= 0;
}

bool readWholeFile(const char* path, std::string& out) {
    out.clear();
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return false;
    SceIoStat st;
    std::memset(&st, 0, sizeof(st));
    if (sceIoGetstatByFd(fd, &st) < 0) {
        sceIoClose(fd);
        return false;
    }
    if (st.st_size <= 0) {
        sceIoClose(fd);
        return false;
    }
    const size_t n = static_cast<size_t>(st.st_size);
    if (n > 512 * 1024) {
        sceIoClose(fd);
        return false;
    }
    out.resize(n);
    const int rd = sceIoRead(fd, &out[0], static_cast<int>(n));
    sceIoClose(fd);
    if (rd <= 0) {
        out.clear();
        return false;
    }
    if (static_cast<size_t>(rd) != n) {
        out.resize(static_cast<size_t>(rd));
    }
    return true;
}

bool basenameMatches(const std::string& entryBase, const char* const* names) {
    if (!names) return false;
    for (int i = 0; names[i] != nullptr; ++i) {
        if (entryBase == names[i]) return true;
    }
    return false;
}

bool filePresentForEntry(const ConfigEntry& e) {
    if (e.basename.empty()) return false;
    if (!e.path.empty() && e.path.size() < 256 && pathExists(e.path.c_str())) return true;
    static const char* kRoots[] = {
        "ur0:tai/",
        "ux0:tai/",
        "ur0:/tai/",
        "ux0:/tai/",
        "ur0:tai/plugins/",
        "ux0:tai/plugins/",
        nullptr
    };
    for (int i = 0; kRoots[i] != nullptr; ++i) {
        const std::string candidate = std::string(kRoots[i]) + e.basename;
        if (pathExists(candidate.c_str())) return true;
        if (e.basename == "nopspemudrm_kern.skprx") {
            const std::string a = std::string(kRoots[i]) + "NoPspEmuDrm_kern.skprx";
            if (pathExists(a.c_str())) return true;
        }
        if (e.basename == "nopspemudrm_user.suprx") {
            const std::string a = std::string(kRoots[i]) + "NoPspEmuDrm_user.suprx";
            if (pathExists(a.c_str())) return true;
        }
        if (e.basename == "nonpdrm.skprx") {
            const std::string a = std::string(kRoots[i]) + "NoNpDrm.skprx";
            if (pathExists(a.c_str())) return true;
        }
    }
    return false;
}

void parseConfigText(const std::string& text, std::vector<ConfigEntry>& out) {
    std::string section = "KERNEL";
    int lineNo = 0;
    size_t i = 0;
    const size_t n = text.size();
    constexpr size_t kMaxEntries = 2048;
    while (i <= n && out.size() < kMaxEntries) {
        size_t j = i;
        while (j < n && text[j] != '\n' && text[j] != '\0') ++j;
        std::string line = text.substr(i, j - i);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        trimInPlace(line);
        ++lineNo;

        // UTF-8 BOM on first line
        if (lineNo == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
            trimInPlace(line);
        }

        if (line.empty()) {
            // skip
        } else if (line[0] == '#' || line[0] == ';' ||
                   (line.size() >= 2 && line[0] == '/' && line[1] == '/')) {
            // Commented / inactive — never counts as installed
        } else if (line[0] == '*') {
            section = line.substr(1);
            trimInPlace(section);
            const size_t hash = section.find('#');
            if (hash != std::string::npos) {
                section.resize(hash);
                trimInPlace(section);
            }
            if (section.size() > 64) section.resize(64);
            const std::string low = toLowerCopy(section);
            if (low == "kernel") section = "KERNEL";
            else if (low == "main") section = "main";
            else if (low == "all") section = "ALL";
        } else {
            std::string pathLine = line;
            const size_t hash = pathLine.find('#');
            if (hash != std::string::npos) {
                pathLine.resize(hash);
                trimInPlace(pathLine);
            }
            if (pathLine.empty() || pathLine[0] == '#' || pathLine[0] == ';') {
                // became comment
            } else if (pathLine.size() > 256) {
                // reject
            } else {
                ConfigEntry e;
                e.section = section;
                e.path = pathLine;
                e.basename = basenameOf(pathLine);
                e.line = lineNo;
                if (!e.basename.empty()) out.push_back(e);
            }
        }

        if (j >= n) break;
        i = j + 1;
    }
}

struct Hit {
    bool listed = false;
    bool fileOk = false;
    std::string section;
    std::string path;
    int line = 0;
};

Hit findPlugin(const std::vector<ConfigEntry>& entries, const char* const* names) {
    Hit hit;
    for (const ConfigEntry& e : entries) {
        if (!basenameMatches(e.basename, names)) continue;
        const bool ok = filePresentForEntry(e);
        if (!hit.listed || (ok && !hit.fileOk)) {
            hit.listed = true;
            hit.section = e.section;
            hit.path = e.path;
            hit.line = e.line;
            hit.fileOk = ok;
        }
        if (hit.fileOk) break;
    }
    return hit;
}

void safeCopyAscii(char* dst, size_t dstSz, const std::string& src) {
    if (!dst || dstSz == 0) return;
    const size_t maxCopy = dstSz > 1 ? dstSz - 1 : 0;
    size_t n = src.size() < maxCopy ? src.size() : maxCopy;
    for (size_t k = 0; k < n; ++k) {
        const unsigned char c = static_cast<unsigned char>(src[k]);
        dst[k] = (c >= 32 && c < 127) ? static_cast<char>(c) : '?';
    }
    dst[n] = '\0';
    if (src.size() > maxCopy && maxCopy >= 3) {
        dst[maxCopy - 3] = '.';
        dst[maxCopy - 2] = '.';
        dst[maxCopy - 1] = '.';
        dst[maxCopy] = '\0';
    }
}

void logHit(const char* label, const Hit& h) {
    char buf[320];
    if (!h.listed) {
        sceClibSnprintf(buf, sizeof(buf), "[PluginDetector] %s: not listed in config",
                        label ? label : "?");
    } else {
        char pathShort[96];
        char secShort[48];
        safeCopyAscii(pathShort, sizeof(pathShort), h.path);
        safeCopyAscii(secShort, sizeof(secShort), h.section);
        sceClibSnprintf(
            buf, sizeof(buf),
            "[PluginDetector] %s: listed section=%s line=%d file=%s path=%s",
            label ? label : "?",
            secShort,
            h.line,
            h.fileOk ? "OK" : "MISSING",
            pathShort
        );
    }
    diagnostics::log(buf);
    sceClibPrintf("%s\n", buf);
}

} // namespace

PluginStatus PluginDetector::scan() {
    PluginStatus st;
    diagnostics::log("[PluginDetector] scan begin (AutoPlugin2-style parser)");

    static const char* kUx0 = "ux0:tai/config.txt";
    static const char* kUr0 = "ur0:tai/config.txt";
    static const char* kUx0Alt = "ux0:/tai/config.txt";
    static const char* kUr0Alt = "ur0:/tai/config.txt";

    const bool hasUx0 = pathExists(kUx0) || pathExists(kUx0Alt);
    const bool hasUr0 = pathExists(kUr0) || pathExists(kUr0Alt);

    const char* primary = nullptr;
    const char* secondary = nullptr;
    if (hasUx0) {
        primary = pathExists(kUx0) ? kUx0 : kUx0Alt;
        if (hasUr0) secondary = pathExists(kUr0) ? kUr0 : kUr0Alt;
    } else if (hasUr0) {
        primary = pathExists(kUr0) ? kUr0 : kUr0Alt;
    }

    {
        char buf[192];
        sceClibSnprintf(
            buf, sizeof(buf),
            "[PluginDetector] configs ux0=%d ur0=%d primary=%s",
            hasUx0 ? 1 : 0,
            hasUr0 ? 1 : 0,
            primary ? primary : "(none)"
        );
        diagnostics::log(buf);
    }

    if (!primary) {
        st.detail = "tai config.txt not found on ux0 or ur0";
        diagnostics::log(std::string("[PluginDetector] ") + st.detail);
        sceClibPrintf("[PluginDetector] %s\n", st.detail.c_str());
        // Missing config is not fatal — all plugin flags stay false.
        diagnostics::log("[PluginDetector] scan end (no config)");
        return st;
    }

    std::string text;
    if (!readWholeFile(primary, text)) {
        st.detail = std::string("failed to read ") + primary;
        diagnostics::log(std::string("[PluginDetector] ") + st.detail);
        diagnostics::log("[PluginDetector] scan end (read fail)");
        return st;
    }
    st.configPathUsed = primary;

    std::vector<ConfigEntry> entries;
    parseConfigText(text, entries);
    {
        char buf[160];
        sceClibSnprintf(
            buf, sizeof(buf),
            "[PluginDetector] parsed %s bytes=%u entries=%u",
            primary,
            (unsigned)text.size(),
            (unsigned)entries.size()
        );
        diagnostics::log(buf);
    }

    if (secondary) {
        std::string text2;
        if (readWholeFile(secondary, text2)) {
            const size_t before = entries.size();
            parseConfigText(text2, entries);
            char buf[160];
            sceClibSnprintf(
                buf, sizeof(buf),
                "[PluginDetector] merged secondary %s extra_entries=%u",
                secondary,
                (unsigned)(entries.size() - before)
            );
            diagnostics::log(buf);
            st.configPathUsed = std::string(primary) + "+" + secondary;
        }
    }

    static const char* kNoNpDrmNames[] = {
        "nonpdrm.skprx",
        "nonpdrm_un.skprx",
        "nonpdrm_en.skprx",
        nullptr
    };
    static const char* kNoPspKernNames[] = {
        "nopspemudrm_kern.skprx",
        "nopspemudrm.skprx",
        nullptr
    };
    static const char* kNoPspUserNames[] = {
        "nopspemudrm_user.suprx",
        nullptr
    };
    static const char* kRepatchNames[] = {
        "repatch.skprx",
        "repatch_4.skprx",
        "repatch_ex.skprx",
        nullptr
    };
    static const char* kFdFixNames[] = {
        "fd_fix.skprx",
        nullptr
    };

    const Hit nonpdrm = findPlugin(entries, kNoNpDrmNames);
    const Hit nopspK = findPlugin(entries, kNoPspKernNames);
    const Hit nopspU = findPlugin(entries, kNoPspUserNames);
    const Hit repatch = findPlugin(entries, kRepatchNames);
    const Hit fdFix = findPlugin(entries, kFdFixNames);

    logHit("NoNpDrm", nonpdrm);
    logHit("NoPspEmuDrm_kern", nopspK);
    logHit("NoPspEmuDrm_user", nopspU);
    logHit("RePatch", repatch);
    logHit("FdFix", fdFix);

    // Present for install warnings: listed in active (non-comment) config AND file on disk.
    st.nonpdrm = nonpdrm.listed && nonpdrm.fileOk;
    st.nopspemudrmKern = nopspK.listed && nopspK.fileOk;
    st.nopspemudrmUser = nopspU.listed && nopspU.fileOk;
    st.repatch = repatch.listed && repatch.fileOk;
    st.fdFix = fdFix.listed && fdFix.fileOk;

    char summary[512];
    sceClibSnprintf(
        summary, sizeof(summary),
        "config=%s nonpdrm=%d (listed=%d file=%d) nopsp_kern=%d (listed=%d file=%d) nopsp_user=%d (listed=%d file=%d) repatch=%d (listed=%d file=%d) fd_fix=%d (listed=%d file=%d)",
        st.configPathUsed.c_str(),
        st.nonpdrm ? 1 : 0, nonpdrm.listed ? 1 : 0, nonpdrm.fileOk ? 1 : 0,
        st.nopspemudrmKern ? 1 : 0, nopspK.listed ? 1 : 0, nopspK.fileOk ? 1 : 0,
        st.nopspemudrmUser ? 1 : 0, nopspU.listed ? 1 : 0, nopspU.fileOk ? 1 : 0,
        st.repatch ? 1 : 0, repatch.listed ? 1 : 0, repatch.fileOk ? 1 : 0,
        st.fdFix ? 1 : 0, fdFix.listed ? 1 : 0, fdFix.fileOk ? 1 : 0
    );
    st.detail = summary;

    if (hasUx0 && hasUr0) {
        st.detail += " | both ux0+ur0 configs present; primary is ux0 (taiHEN)";
    }
    if (nonpdrm.listed && !nonpdrm.fileOk) {
        st.detail += " | NoNpDrm listed but .skprx missing";
    }
    if (nopspK.listed && !nopspK.fileOk) {
        st.detail += " | NoPspEmuDrm_kern listed but file missing";
    }
    if (nopspU.listed && !nopspU.fileOk) {
        st.detail += " | NoPspEmuDrm_user listed but file missing";
    }
    if (repatch.listed && !repatch.fileOk) {
        st.detail += " | RePatch listed but plugin file missing";
    }
    if (fdFix.listed && !fdFix.fileOk) {
        st.detail += " | FdFix listed but plugin file missing";
    }
    if (st.repatch && st.fdFix) {
        st.detail += " | RePatch+FdFix conflict detected";
    }
    if (!nopspK.listed && !nopspU.listed) {
        st.detail += " | NoPspEmuDrm not in config (commented or absent) — OK, LiveArea PSP/PS1 blocked";
    }

    diagnostics::log(std::string("[PluginDetector] ") + st.detail);
    sceClibPrintf("[PluginDetector] %s\n", st.detail.c_str());
    diagnostics::log("[PluginDetector] scan end");
    return st;
}

} // namespace psvitaalive

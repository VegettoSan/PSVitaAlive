#include "installer/tai_config_editor.hpp"
#include "diagnostic_logger.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>

#include <string>
#include <vector>
#include <cctype>

namespace psvitaalive {
namespace {

bool pathExists(const char* path) {
    SceIoStat st;
    return path && sceIoGetstat(path, &st) >= 0;
}

std::string trimCopy(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

bool readAll(const std::string& path, std::string& out) {
    out.clear();
    const SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) return false;
    char buf[1024];
    for (;;) {
        const int n = sceIoRead(fd, buf, sizeof(buf));
        if (n < 0) { sceIoClose(fd); return false; }
        if (n == 0) break;
        out.append(buf, static_cast<size_t>(n));
        if (out.size() > 512 * 1024) { sceIoClose(fd); return false; } // safety
    }
    sceIoClose(fd);
    return true;
}

bool writeAllAtomic(const std::string& path, const std::string& data) {
    const std::string tmp = path + ".psva_tmp";
    sceIoRemove(tmp.c_str());
    const SceUID fd = sceIoOpen(tmp.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return false;
    size_t off = 0;
    while (off < data.size()) {
        const int n = sceIoWrite(fd, data.data() + off, static_cast<SceSize>(data.size() - off));
        if (n <= 0) { sceIoClose(fd); sceIoRemove(tmp.c_str()); return false; }
        off += static_cast<size_t>(n);
    }
    sceIoClose(fd);
    sceIoRemove(path.c_str());
    if (sceIoRename(tmp.c_str(), path.c_str()) < 0) {
        // fallback: try direct write
        sceIoRemove(tmp.c_str());
        const SceUID fd2 = sceIoOpen(path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
        if (fd2 < 0) return false;
        off = 0;
        while (off < data.size()) {
            const int n = sceIoWrite(fd2, data.data() + off, static_cast<SceSize>(data.size() - off));
            if (n <= 0) { sceIoClose(fd2); return false; }
            off += static_cast<size_t>(n);
        }
        sceIoClose(fd2);
    }
    return true;
}

bool lineExistsExact(const std::string& content, const std::string& line) {
    const std::string want = trimCopy(line);
    if (want.empty()) return true;
    size_t pos = 0;
    while (pos < content.size()) {
        size_t end = content.find('\n', pos);
        if (end == std::string::npos) end = content.size();
        std::string row = trimCopy(content.substr(pos, end - pos));
        if (!row.empty() && row[0] != '#' && row == want) return true;
        pos = (end < content.size()) ? end + 1 : end;
    }
    return false;
}

std::string normalizeSection(std::string s) {
    s = trimCopy(s);
    if (s.empty() || s == "none" || s == "None" || s == "NONE") return {};
    if (s[0] != '*') s.insert(s.begin(), '*');
    return s;
}

} // namespace

std::string TaiConfigEditor::resolveActiveConfigPath() {
    static const char* kUx0 = "ux0:tai/config.txt";
    static const char* kUr0 = "ur0:tai/config.txt";
    static const char* kUx0Alt = "ux0:/tai/config.txt";
    static const char* kUr0Alt = "ur0:/tai/config.txt";
    if (pathExists(kUx0)) return kUx0;
    if (pathExists(kUx0Alt)) return kUx0Alt;
    if (pathExists(kUr0)) return kUr0;
    if (pathExists(kUr0Alt)) return kUr0Alt;
    return {};
}

bool TaiConfigEditor::configContainsLine(const std::string& lineIn) {
    const std::string line = trimCopy(lineIn);
    if (line.empty()) return false;
    const std::string path = resolveActiveConfigPath();
    if (path.empty()) return false;
    std::string content;
    if (!readAll(path, content)) return false;
    return lineExistsExact(content, line);
}

bool TaiConfigEditor::appendLineToSection(
    const std::string& sectionIn,
    const std::string& lineIn,
    std::string* errorOut
) {
    const std::string section = normalizeSection(sectionIn);
    const std::string line = trimCopy(lineIn);
    if (section.empty()) {
        diagnostics::log("[TaiConfig] section=none — skip config edit");
        return true;
    }
    if (line.empty()) {
        if (errorOut) *errorOut = "empty plugin line";
        return false;
    }

    const std::string path = resolveActiveConfigPath();
    if (path.empty()) {
        if (errorOut) *errorOut = "tai config.txt not found on ux0 or ur0";
        diagnostics::log("[TaiConfig] no config.txt found");
        return false;
    }

    std::string content;
    if (!readAll(path, content)) {
        if (errorOut) *errorOut = "cannot read " + path;
        diagnostics::log(std::string("[TaiConfig] read failed: ") + path);
        return false;
    }

    if (lineExistsExact(content, line)) {
        diagnostics::log(std::string("[TaiConfig] line already present, skip: ") + line);
        return true;
    }

    // Find section range: from header line to next *header or EOF
    size_t sectionStart = std::string::npos;
    size_t insertAt = std::string::npos;
    {
        size_t pos = 0;
        while (pos <= content.size()) {
            size_t end = content.find('\n', pos);
            if (end == std::string::npos) end = content.size();
            const std::string row = trimCopy(content.substr(pos, end - pos));
            if (!row.empty() && row[0] == '*') {
                if (sectionStart != std::string::npos) {
                    // next section begins at pos — insert before it
                    insertAt = pos;
                    break;
                }
                if (row == section) {
                    sectionStart = pos;
                }
            }
            if (end >= content.size()) break;
            pos = end + 1;
        }
        if (sectionStart != std::string::npos && insertAt == std::string::npos) {
            insertAt = content.size();
        }
    }

    std::string out;
    if (sectionStart == std::string::npos) {
        // Create section at end
        out = content;
        if (!out.empty() && out.back() != '\n') out.push_back('\n');
        out += section;
        out.push_back('\n');
        out += line;
        out.push_back('\n');
        diagnostics::log(std::string("[TaiConfig] created section ") + section + " + line in " + path);
    } else {
        // Insert line before insertAt (end of section)
        // Prefer placing after last non-empty line of the section
        out.reserve(content.size() + line.size() + 2);
        out.append(content, 0, insertAt);
        if (!out.empty() && out.back() != '\n') out.push_back('\n');
        out += line;
        out.push_back('\n');
        if (insertAt < content.size()) out.append(content, insertAt, std::string::npos);
        diagnostics::log(std::string("[TaiConfig] appended to ") + section + " in " + path + ": " + line);
    }

    if (!writeAllAtomic(path, out)) {
        if (errorOut) *errorOut = "cannot write " + path;
        diagnostics::log(std::string("[TaiConfig] write failed: ") + path);
        return false;
    }
    return true;
}

} // namespace psvitaalive

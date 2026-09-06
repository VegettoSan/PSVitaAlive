#include "ui/ui_font.hpp"

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace psvitaalive {
namespace ui {
namespace {

bool fileReadable(const char* path) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return false;
    sceIoClose(fd);
    return true;
}

vita2d_pgf* tryLoad(const char* path) {
    if (!path || !path[0]) return nullptr;
    if (!fileReadable(path)) return nullptr;
    vita2d_pgf* f = vita2d_load_custom_pgf(path);
    if (f)
        sceClibPrintf("[UiFont] loaded %s\n", path);
    return f;
}

void scanDirForPgf(const char* dir, std::vector<std::string>& out) {
    SceUID dfd = sceIoDopen(dir);
    if (dfd < 0) return;
    SceIoDirent de{};
    while (sceIoDread(dfd, &de) > 0) {
        if (SCE_S_ISDIR(de.d_stat.st_mode)) continue;
        const char* name = de.d_name;
        if (!name || !name[0] || name[0] == '.') continue;
        const size_t n = std::strlen(name);
        if (n < 5) continue;
        // case-insensitive .pgf
        if ((name[n - 4] == '.' || name[n - 4] == '.') &&
            (name[n - 3] == 'p' || name[n - 3] == 'P') &&
            (name[n - 2] == 'g' || name[n - 2] == 'G') &&
            (name[n - 1] == 'f' || name[n - 1] == 'F')) {
            out.emplace_back(name);
        }
    }
    sceIoDclose(dfd);
}

const char* stylePreferredFile(UiFontStyle style) {
    switch (style) {
        case UiFontStyle::Serif: return "serif.pgf";
        case UiFontStyle::Sans: return "sans.pgf";
        case UiFontStyle::SerifBold: return "serif_bold.pgf";
        case UiFontStyle::SansBold: return "sans_bold.pgf";
        default: return nullptr;
    }
}

vita2d_pgf* loadByBasename(const char* file) {
    if (!file || !file[0]) return nullptr;
    char path[256];
    sceClibSnprintf(path, sizeof(path), "ux0:data/psvitaalive/fonts/%s", file);
    if (vita2d_pgf* f = tryLoad(path)) return f;
    sceClibSnprintf(path, sizeof(path), "app0:font/%s", file);
    if (vita2d_pgf* f = tryLoad(path)) return f;
    return nullptr;
}

} // namespace

const char* uiFontStyleKey(UiFontStyle style) {
    switch (style) {
        case UiFontStyle::Serif: return "FONT_SERIF";
        case UiFontStyle::Sans: return "FONT_SANS";
        case UiFontStyle::SerifBold: return "FONT_SERIF_BOLD";
        case UiFontStyle::SansBold: return "FONT_SANS_BOLD";
        default: return "FONT_DEFAULT";
    }
}

std::vector<std::string> listAvailableUiFonts() {
    std::vector<std::string> files;
    scanDirForPgf("ux0:data/psvitaalive/fonts", files);
    scanDirForPgf("app0:font", files);
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}

vita2d_pgf* loadUiFont(UiFontStyle style, const std::string& customFile) {
    // 1) Explicit custom basename from Settings (any .pgf you added)
    if (!customFile.empty()) {
        if (vita2d_pgf* f = loadByBasename(customFile.c_str())) return f;
        sceClibPrintf("[UiFont] custom file missing %s — try style / default\n", customFile.c_str());
    }

    // 2) Preferred name for legacy style slots (serif.pgf, sans.pgf, …)
    if (const char* pref = stylePreferredFile(style)) {
        if (vita2d_pgf* f = loadByBasename(pref)) return f;
        sceClibPrintf("[UiFont] preferred %s missing\n", pref);
    }

    // 3) Default
    vita2d_pgf* def = vita2d_load_default_pgf();
    if (!def)
        sceClibPrintf("[UiFont] default PGF load failed\n");
    return def;
}

vita2d_pgf* loadUiFont(UiFontStyle style) {
    return loadUiFont(style, std::string());
}

} // namespace ui
} // namespace psvitaalive

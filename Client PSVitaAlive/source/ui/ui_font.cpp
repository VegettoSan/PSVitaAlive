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

bool endsWithIgnoreCase(const char* name, size_t n, const char* ext4) {
    // ext4 like ".pgf" / ".ttf" / ".otf" (4 chars)
    if (n < 4) return false;
    for (int i = 0; i < 4; ++i) {
        char a = name[n - 4 + i];
        char b = ext4[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

bool isFontFileName(const char* name) {
    if (!name || !name[0] || name[0] == '.') return false;
    const size_t n = std::strlen(name);
    return endsWithIgnoreCase(name, n, ".pgf")
        || endsWithIgnoreCase(name, n, ".ttf")
        || endsWithIgnoreCase(name, n, ".otf");
}

void scanDirForFonts(const char* dir, std::vector<std::string>& out) {
    SceUID dfd = sceIoDopen(dir);
    if (dfd < 0) return;
    SceIoDirent de{};
    while (sceIoDread(dfd, &de) > 0) {
        if (SCE_S_ISDIR(de.d_stat.st_mode)) continue;
        if (isFontFileName(de.d_name))
            out.emplace_back(de.d_name);
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

bool isFreeTypeName(const char* file) {
    if (!file) return false;
    const size_t n = std::strlen(file);
    return endsWithIgnoreCase(file, n, ".ttf") || endsWithIgnoreCase(file, n, ".otf");
}

UiFont tryLoadPath(const char* path, bool preferFt) {
    UiFont out;
    if (!path || !path[0] || !fileReadable(path)) return out;

    if (preferFt) {
        vita2d_font* ft = vita2d_load_font_file(path);
        if (ft) {
            out.kind = UiFont::Kind::FreeType;
            out.ft = ft;
            sceClibPrintf("[UiFont] FreeType loaded %s\n", path);
            return out;
        }
        sceClibPrintf("[UiFont] FreeType load failed %s\n", path);
        return out;
    }

    vita2d_pgf* pgf = vita2d_load_custom_pgf(path);
    if (pgf) {
        out.kind = UiFont::Kind::Pgf;
        out.pgf = pgf;
        sceClibPrintf("[UiFont] PGF loaded %s\n", path);
        return out;
    }
    sceClibPrintf("[UiFont] PGF load failed %s\n", path);
    return out;
}

UiFont loadByBasename(const char* file) {
    UiFont out;
    if (!file || !file[0]) return out;
    const bool ft = isFreeTypeName(file);
    char path[256];
    sceClibSnprintf(path, sizeof(path), "ux0:data/psvitaalive/fonts/%s", file);
    out = tryLoadPath(path, ft);
    if (out) return out;
    sceClibSnprintf(path, sizeof(path), "app0:font/%s", file);
    return tryLoadPath(path, ft);
}

/**
 * Map legacy PGF float scale → FreeType pixel size.
 *
 * Vita UI uses scales roughly 0.50–1.20. FreeType "size" is pixel height;
 * a linear *N factor overshoots spacing vs ScePgf. Use a gentler curve and
 * prefer even sizes (cleaner FT grayscale raster on Vita).
 */
unsigned scaleToPx(float scale) {
    // 0.50 → ~15px, 0.74 → ~18px, 1.00 → ~21px, 1.12 → ~23px
    float pxF = 8.0f + scale * 13.0f;
    int px = static_cast<int>(pxF + 0.5f);
    if (px < 12) px = 12;
    if (px > 36) px = 36;
    if (px & 1) ++px; // even size
    return static_cast<unsigned>(px);
}

} // namespace

void UiFont::reset() {
    if (kind == Kind::Pgf && pgf) {
        vita2d_free_pgf(pgf);
        pgf = nullptr;
    } else if (kind == Kind::FreeType && ft) {
        vita2d_free_font(ft);
        ft = nullptr;
    }
    kind = Kind::None;
}

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
    scanDirForFonts("ux0:data/psvitaalive/fonts", files);
    scanDirForFonts("app0:font", files);
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}

UiFont loadDefaultUiFont() {
    UiFont out;
    vita2d_pgf* def = vita2d_load_default_pgf();
    if (def) {
        out.kind = UiFont::Kind::Pgf;
        out.pgf = def;
    } else {
        sceClibPrintf("[UiFont] default PGF load failed\n");
    }
    return out;
}

UiFont loadUiFont(UiFontStyle style, const std::string& customFile) {
    if (!customFile.empty()) {
        UiFont f = loadByBasename(customFile.c_str());
        if (f) return f;
        sceClibPrintf("[UiFont] custom missing %s — fallback\n", customFile.c_str());
    }
    if (const char* pref = stylePreferredFile(style)) {
        UiFont f = loadByBasename(pref);
        if (f) return f;
        sceClibPrintf("[UiFont] preferred %s missing\n", pref);
    }
    return loadDefaultUiFont();
}

UiFont loadUiFont(UiFontStyle style) {
    return loadUiFont(style, std::string());
}

void uiDrawText(const UiFont* font, int x, int y, unsigned color, float scale, const char* text) {
    if (!font || !text) return;
    if (font->kind == UiFont::Kind::Pgf && font->pgf) {
        vita2d_pgf_draw_text(font->pgf, x, y, color, scale, text);
        return;
    }
    if (font->kind == UiFont::Kind::FreeType && font->ft) {
        // Match PGF baseline used across layouts (y is baseline for both APIs).
        const unsigned px = scaleToPx(scale);
        // Half-pixel optical align: smaller sizes need +1, large titles +2.
        const int yFt = y + (px >= 24 ? 2 : 1);
        vita2d_font_draw_text(font->ft, x, yFt, color, px, text);
        return;
    }
}

int uiTextWidth(const UiFont* font, float scale, const char* text) {
    if (!font || !text) return 0;
    if (font->kind == UiFont::Kind::Pgf && font->pgf) {
        return vita2d_pgf_text_width(font->pgf, scale, text);
    }
    if (font->kind == UiFont::Kind::FreeType && font->ft) {
        int w = 0, h = 0;
        vita2d_font_text_dimensions(font->ft, scaleToPx(scale), text, &w, &h);
        return w;
    }
    return 0;
}

} // namespace ui
} // namespace psvitaalive

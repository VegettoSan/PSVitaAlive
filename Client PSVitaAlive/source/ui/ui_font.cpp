#include "ui/ui_font.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>

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

/** Style → file base name under app0:font/ or ux0:data/psvitaalive/fonts/ */
const char* styleFileName(UiFontStyle style) {
    switch (style) {
        case UiFontStyle::Serif:     return "serif.pgf";
        case UiFontStyle::Sans:      return "sans.pgf";
        case UiFontStyle::SerifBold: return "serif_bold.pgf";
        case UiFontStyle::SansBold:  return "sans_bold.pgf";
        default:                     return nullptr;
    }
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

vita2d_pgf* loadUiFont(UiFontStyle style) {
    // Priority:
    //   1) User override on memory card (no rebuild needed)
    //   2) Fonts bundled in the VPK under app0:font/
    //   3) vita2d default PGF
    //
    // System sa0: fonts are intentionally NOT used: they are missing or
    // unreliable on many real units and on Vita3K, so the old Sans/Serif
    // selector appeared to do nothing.
    const char* file = styleFileName(style);
    if (file) {
        char path[256];

        sceClibSnprintf(path, sizeof(path), "ux0:data/psvitaalive/fonts/%s", file);
        if (vita2d_pgf* f = tryLoad(path)) return f;

        sceClibSnprintf(path, sizeof(path), "app0:font/%s", file);
        if (vita2d_pgf* f = tryLoad(path)) return f;

        sceClibPrintf("[UiFont] missing custom font %s (checked ux0 + app0) — fallback default\n", file);
    }

    vita2d_pgf* def = vita2d_load_default_pgf();
    if (!def)
        sceClibPrintf("[UiFont] default PGF load failed\n");
    return def;
}

} // namespace ui
} // namespace psvitaalive

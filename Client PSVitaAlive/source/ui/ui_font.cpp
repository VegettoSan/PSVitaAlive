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
    // System Latin PGF set (same family as classic PSP/Vita Latin faces).
    // Paths verified on retail firmware + Vita3K when fonts are present.
    const char* primary = nullptr;
    const char* secondary = nullptr;
    switch (style) {
        case UiFontStyle::Serif:
            primary = "sa0:data/font/ltn0.pgf";
            secondary = "sa0:/data/font/ltn0.pgf";
            break;
        case UiFontStyle::Sans:
            primary = "sa0:data/font/ltn2.pgf";
            secondary = "sa0:/data/font/ltn2.pgf";
            break;
        case UiFontStyle::SerifBold:
            primary = "sa0:data/font/ltn4.pgf";
            secondary = "sa0:/data/font/ltn4.pgf";
            break;
        case UiFontStyle::SansBold:
            primary = "sa0:data/font/ltn6.pgf";
            secondary = "sa0:/data/font/ltn6.pgf";
            break;
        default:
            break;
    }

    if (primary) {
        if (vita2d_pgf* f = tryLoad(primary)) return f;
        if (secondary) {
            if (vita2d_pgf* f = tryLoad(secondary)) return f;
        }
        sceClibPrintf("[UiFont] missing %s — fallback default\n", primary);
    }

    vita2d_pgf* def = vita2d_load_default_pgf();
    if (!def)
        sceClibPrintf("[UiFont] default PGF load failed\n");
    return def;
}

} // namespace ui
} // namespace psvitaalive

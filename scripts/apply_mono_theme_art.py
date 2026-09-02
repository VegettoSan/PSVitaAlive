#!/usr/bin/env python3
"""Generate mono logo/splash assets and wire theme-tint drawing."""
from pathlib import Path
import re

try:
    from PIL import Image
    import numpy as np
except ImportError:
    import subprocess, sys
    subprocess.check_call([sys.executable, "-m", "pip", "install", "pillow", "numpy", "-q"])
    from PIL import Image
    import numpy as np

ROOT = Path("Client PSVitaAlive")
UI = ROOT / "assets" / "ui"
CPP = ROOT / "source" / "ui" / "full_catalog_screen.cpp"
HPP = ROOT / "include" / "ui" / "full_catalog_screen.hpp"


def to_mono(src: Path, dst: Path):
    im = Image.open(src).convert("RGBA")
    arr = np.array(im)
    r = arr[:, :, 0].astype(np.float32)
    g = arr[:, :, 1].astype(np.float32)
    b = arr[:, :, 2].astype(np.float32)
    a = arr[:, :, 3]
    L = (0.299 * r + 0.587 * g + 0.114 * b).clip(0, 255).astype(np.uint8)
    out = np.stack([L, L, L, a], axis=2)
    Image.fromarray(out, "RGBA").save(dst, optimize=True)
    print(f"mono {dst.name}: {dst.stat().st_size} bytes")


to_mono(UI / "PSVitaAlive_Store_logo_text.png", UI / "PSVitaAlive_Store_logo_text_mono.png")
to_mono(UI / "catalog_loading.png", UI / "catalog_loading_mono.png")

# --- Header: mono texture pointers ---
hpp = HPP.read_text(encoding="utf-8")
if "headerLogoMonoTex_" not in hpp:
    hpp = hpp.replace(
        "    /** Full-screen splash while catalogs download/load at startup (app0:ui/catalog_loading.png). */\n"
        "    vita2d_texture* catalogLoadingTex_ = nullptr;\n"
        "    /** Header brand image (app0:ui/PSVitaAlive_Store_logo_text.png). */\n"
        "    vita2d_texture* headerLogoTex_ = nullptr;\n",
        "    /** Full-screen splash while catalogs download/load at startup (app0:ui/catalog_loading.png). */\n"
        "    vita2d_texture* catalogLoadingTex_ = nullptr;\n"
        "    /** Monochrome splash for non-brand theme tinting. */\n"
        "    vita2d_texture* catalogLoadingMonoTex_ = nullptr;\n"
        "    /** Header brand image (app0:ui/PSVitaAlive_Store_logo_text.png). */\n"
        "    vita2d_texture* headerLogoTex_ = nullptr;\n"
        "    /** Monochrome header logo for non-brand theme tinting. */\n"
        "    vita2d_texture* headerLogoMonoTex_ = nullptr;\n",
    )
    HPP.write_text(hpp, encoding="utf-8")
    print("hpp updated")
else:
    print("hpp already has mono tex")

cpp = CPP.read_text(encoding="utf-8")

# Helper near colorThemeDisplayName
if "bool isBrandColorTheme" not in cpp:
    helper = '''
/** Brand art (full-color logo/splash) only for the original PSVitaAlive theme. */
bool isBrandColorTheme(::psvitaalive::ColorTheme t) {
    return t == ::psvitaalive::ColorTheme::NeonLime;
}

'''
    cpp = cpp.replace(
        "const char* colorThemeDisplayName(::psvitaalive::ColorTheme t) {",
        helper + "const char* colorThemeDisplayName(::psvitaalive::ColorTheme t) {",
        1,
    )

# init: load mono textures
old_init = '''    catalogLoadingTex_ = vita2d_load_PNG_file("app0:ui/catalog_loading.png");
    if (!catalogLoadingTex_) {
        catalogLoadingTex_ = vita2d_load_PNG_file("app0:ui/catalog_loading.PNG");
    }
    if (catalogLoadingTex_) {
        diagnostics::log("[UI] catalog_loading.png loaded");
    } else {
        diagnostics::log("[UI] catalog_loading.png not found (fallback overlay)");
    }
    headerLogoTex_ = vita2d_load_PNG_file("app0:ui/PSVitaAlive_Store_logo_text.png");
    if (!headerLogoTex_) {
        headerLogoTex_ = vita2d_load_PNG_file("app0:ui/logo.png");
    }
    if (headerLogoTex_) {
        diagnostics::log("[UI] header logo loaded");
    } else {
        diagnostics::log("[UI] header logo not found (text fallback)");
    }
'''

new_init = '''    catalogLoadingTex_ = vita2d_load_PNG_file("app0:ui/catalog_loading.png");
    if (!catalogLoadingTex_) {
        catalogLoadingTex_ = vita2d_load_PNG_file("app0:ui/catalog_loading.PNG");
    }
    if (catalogLoadingTex_) {
        diagnostics::log("[UI] catalog_loading.png loaded");
    } else {
        diagnostics::log("[UI] catalog_loading.png not found (fallback overlay)");
    }
    catalogLoadingMonoTex_ = vita2d_load_PNG_file("app0:ui/catalog_loading_mono.png");
    if (catalogLoadingMonoTex_) {
        diagnostics::log("[UI] catalog_loading_mono.png loaded");
    } else {
        diagnostics::log("[UI] catalog_loading_mono.png not found (tint fallback to color art)");
    }
    headerLogoTex_ = vita2d_load_PNG_file("app0:ui/PSVitaAlive_Store_logo_text.png");
    if (!headerLogoTex_) {
        headerLogoTex_ = vita2d_load_PNG_file("app0:ui/logo.png");
    }
    if (headerLogoTex_) {
        diagnostics::log("[UI] header logo loaded");
    } else {
        diagnostics::log("[UI] header logo not found (text fallback)");
    }
    headerLogoMonoTex_ = vita2d_load_PNG_file("app0:ui/PSVitaAlive_Store_logo_text_mono.png");
    if (headerLogoMonoTex_) {
        diagnostics::log("[UI] header logo mono loaded");
    } else {
        diagnostics::log("[UI] header logo mono not found (tint fallback to color art)");
    }
'''

if old_init not in cpp:
    raise SystemExit("init texture load block not found")
cpp = cpp.replace(old_init, new_init, 1)

# shutdown free mono
old_free = '''    if (catalogLoadingTex_) {
        vita2d_free_texture(catalogLoadingTex_);
        catalogLoadingTex_ = nullptr;
    }
    if (headerLogoTex_) {
        vita2d_free_texture(headerLogoTex_);
        headerLogoTex_ = nullptr;
    }
'''
new_free = '''    if (catalogLoadingTex_) {
        vita2d_free_texture(catalogLoadingTex_);
        catalogLoadingTex_ = nullptr;
    }
    if (catalogLoadingMonoTex_) {
        vita2d_free_texture(catalogLoadingMonoTex_);
        catalogLoadingMonoTex_ = nullptr;
    }
    if (headerLogoTex_) {
        vita2d_free_texture(headerLogoTex_);
        headerLogoTex_ = nullptr;
    }
    if (headerLogoMonoTex_) {
        vita2d_free_texture(headerLogoMonoTex_);
        headerLogoMonoTex_ = nullptr;
    }
'''
if old_free not in cpp:
    raise SystemExit("shutdown free block not found")
cpp = cpp.replace(old_free, new_free, 1)

# drawHeader logo (main)
old_logo = '''    if (headerLogoTex_) {
        const float lw = (float)vita2d_texture_get_width(headerLogoTex_);
        const float lh = (float)vita2d_texture_get_height(headerLogoTex_);
        const float maxH = (float)(HEADER_H - 10);
        const float maxW = 190.f;
        float sc = maxH / (lh > 1.f ? lh : 1.f);
        if (lw * sc > maxW) sc = maxW / (lw > 1.f ? lw : 1.f);
        const float dw = lw * sc;
        const float dh = lh * sc;
        const float dx = 10.f;
        const float dy = ((float)HEADER_H - dh) * 0.5f;
        vita2d_draw_texture_scale(headerLogoTex_, dx, dy, sc, sc);
        searchLeft = (int)(dx + dw + 12.f);
        if (searchLeft < 160) searchLeft = 160;
    } else {
'''

new_logo = '''    {
        const bool brand = isBrandColorTheme(settingsEdit_.colorTheme);
        vita2d_texture* logoTex = (brand || !headerLogoMonoTex_) ? headerLogoTex_ : headerLogoMonoTex_;
        if (logoTex) {
        const float lw = (float)vita2d_texture_get_width(logoTex);
        const float lh = (float)vita2d_texture_get_height(logoTex);
        const float maxH = (float)(HEADER_H - 10);
        const float maxW = 190.f;
        float sc = maxH / (lh > 1.f ? lh : 1.f);
        if (lw * sc > maxW) sc = maxW / (lw > 1.f ? lw : 1.f);
        const float dw = lw * sc;
        const float dh = lh * sc;
        const float dx = 10.f;
        const float dy = ((float)HEADER_H - dh) * 0.5f;
        if (brand || !headerLogoMonoTex_)
            vita2d_draw_texture_scale(logoTex, dx, dy, sc, sc);
        else
            vita2d_draw_texture_tint_scale(logoTex, dx, dy, sc, sc, ACCENT);
        searchLeft = (int)(dx + dw + 12.f);
        if (searchLeft < 160) searchLeft = 160;
        } else {
'''

if old_logo not in cpp:
    raise SystemExit("drawHeader logo block not found")
# Need to close extra brace - the else branch ends with searchLeft = 200; }
# Original: if (headerLogoTex_) { ... } else { text; searchLeft=200; }
# New: { if (logoTex) { ... } else { text; searchLeft=200; } }
cpp = cpp.replace(old_logo, new_logo, 1)

# Fix closing of the else after text fallback - find first occurrence after our change
# Original structure after else:
#        vita2d_pgf_draw_text(...);
#        searchLeft = 200;
#    }
# We need an extra closing } for the outer block.
# Only fix the drawHeader one carefully.
marker = '''        vita2d_pgf_draw_text(font_, 14, 30, ACCENT, 0.98f, "PSVitaAlive");
        searchLeft = 200;
    }
    // Search field + optional G/D Files filter chip (Homebrew only) + clock'''
if marker not in cpp:
    raise SystemExit("logo text fallback close marker not found")
cpp = cpp.replace(
    marker,
    '''        vita2d_pgf_draw_text(font_, 14, 30, ACCENT, 0.98f, "PSVitaAlive");
        searchLeft = 200;
        }
    }
    // Search field + optional G/D Files filter chip (Homebrew only) + clock''',
    1,
)

# drawLoadingOverlay splash
old_splash = '''    const unsigned tint = RGBA8(255, 255, 255, a > 255 ? 255 : a);
    if (catalogLoadingTex_) {
        const float tw = (float)vita2d_texture_get_width(catalogLoadingTex_);
        const float th = (float)vita2d_texture_get_height(catalogLoadingTex_);
        const float sx = (tw > 1.f) ? (SCREEN_W / tw) : 1.f;
        const float sy = (th > 1.f) ? (SCREEN_H / th) : 1.f;
        vita2d_draw_texture_tint_scale(catalogLoadingTex_, 0.f, 0.f, sx, sy, tint);
    } else {
        vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0x0A, 0x0A, 0x0A, a > 255 ? 255 : a));
    }
'''

new_splash = '''    {
        const bool brand = isBrandColorTheme(settingsEdit_.colorTheme);
        vita2d_texture* splash = (brand || !catalogLoadingMonoTex_) ? catalogLoadingTex_ : catalogLoadingMonoTex_;
        const unsigned aa = a > 255 ? 255u : a;
        const unsigned tint = (brand || !catalogLoadingMonoTex_)
            ? RGBA8(255, 255, 255, aa)
            : withAlpha(ACCENT, aa);
        if (splash) {
            const float tw = (float)vita2d_texture_get_width(splash);
            const float th = (float)vita2d_texture_get_height(splash);
            const float sx = (tw > 1.f) ? (SCREEN_W / tw) : 1.f;
            const float sy = (th > 1.f) ? (SCREEN_H / th) : 1.f;
            vita2d_draw_texture_tint_scale(splash, 0.f, 0.f, sx, sy, tint);
        } else {
            vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0x0A, 0x0A, 0x0A, aa));
        }
    }
'''

if old_splash not in cpp:
    raise SystemExit("loading splash draw block not found")
cpp = cpp.replace(old_splash, new_splash, 1)

# Touch path uses headerLogoTex_ for layout only - OK to keep measuring color tex
# (same dimensions as mono)

CPP.write_text(cpp, encoding="utf-8")
print("OK mono theme art wired")

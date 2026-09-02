#!/usr/bin/env python3
"""Expand theme switches + open theme picker from Settings."""
from pathlib import Path
import re

CPP = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
cpp = CPP.read_text(encoding="utf-8")

# ---- Replace colorThemeDisplayName ----
old_dn = re.search(r"const char\* colorThemeDisplayName\(::psvitaalive::ColorTheme t\) \{[\s\S]*?\n\}", cpp)
if not old_dn:
    raise SystemExit("colorThemeDisplayName not found")
new_dn = '''const char* colorThemeDisplayName(::psvitaalive::ColorTheme t) {
    switch (t) {
        case ::psvitaalive::ColorTheme::Cyan: return "Cyan";
        case ::psvitaalive::ColorTheme::Rose: return "Rose";
        case ::psvitaalive::ColorTheme::Amber: return "Amber";
        case ::psvitaalive::ColorTheme::Violet: return "Violet";
        case ::psvitaalive::ColorTheme::Mono: return "Mono";
        case ::psvitaalive::ColorTheme::Oled: return "OLED";
        case ::psvitaalive::ColorTheme::PsVita: return "PS Vita";
        case ::psvitaalive::ColorTheme::Crimson: return "Crimson";
        case ::psvitaalive::ColorTheme::Coffee: return "Coffee";
        case ::psvitaalive::ColorTheme::Gold: return "Gold";
        case ::psvitaalive::ColorTheme::Emerald: return "Emerald";
        case ::psvitaalive::ColorTheme::Coral: return "Coral";
        case ::psvitaalive::ColorTheme::Teal: return "Teal";
        case ::psvitaalive::ColorTheme::Indigo: return "Indigo";
        case ::psvitaalive::ColorTheme::Sky: return "Sky";
        case ::psvitaalive::ColorTheme::Magenta: return "Magenta";
        case ::psvitaalive::ColorTheme::Mint: return "Mint";
        case ::psvitaalive::ColorTheme::Sunset: return "Sunset";
        case ::psvitaalive::ColorTheme::Ocean: return "Ocean";
        case ::psvitaalive::ColorTheme::Lavender: return "Lavender";
        case ::psvitaalive::ColorTheme::Cherry: return "Cherry";
        case ::psvitaalive::ColorTheme::Sand: return "Sand";
        case ::psvitaalive::ColorTheme::Forest: return "Forest";
        case ::psvitaalive::ColorTheme::Ice: return "Ice";
        case ::psvitaalive::ColorTheme::Grape: return "Grape";
        case ::psvitaalive::ColorTheme::Peach: return "Peach";
        case ::psvitaalive::ColorTheme::Azure: return "Azure";
        case ::psvitaalive::ColorTheme::Steel: return "Steel";
        case ::psvitaalive::ColorTheme::Honey: return "Honey";
        case ::psvitaalive::ColorTheme::Midnight: return "Midnight";
        case ::psvitaalive::ColorTheme::Sakura: return "Sakura";
        case ::psvitaalive::ColorTheme::Matrix: return "Matrix";
        case ::psvitaalive::ColorTheme::NeonLime:
        default: return "Neon Lime";
    }
}'''
cpp = cpp[:old_dn.start()] + new_dn + cpp[old_dn.end():]

# ---- Replace colorThemeAccentRgb ----
old_rgb = re.search(r"void colorThemeAccentRgb\(::psvitaalive::ColorTheme t, unsigned& ar, unsigned& ag, unsigned& ab\) \{[\s\S]*?\n\}", cpp)
if not old_rgb:
    raise SystemExit("colorThemeAccentRgb not found")
new_rgb = '''void colorThemeAccentRgb(::psvitaalive::ColorTheme t, unsigned& ar, unsigned& ag, unsigned& ab) {
    ar = 0x3B; ag = 0xFF; ab = 0x00;
    switch (t) {
        case ::psvitaalive::ColorTheme::Cyan: ar=0x00; ag=0xE5; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Rose: ar=0xFF; ag=0x5C; ab=0xA8; break;
        case ::psvitaalive::ColorTheme::Amber: ar=0xFF; ag=0xB0; ab=0x20; break;
        case ::psvitaalive::ColorTheme::Violet: ar=0xB2; ag=0x4D; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Mono: ar=0xC8; ag=0xC8; ab=0xCC; break;
        case ::psvitaalive::ColorTheme::Oled: ar=0x5C; ag=0xFF; ab=0x9A; break;
        case ::psvitaalive::ColorTheme::PsVita: ar=0x00; ag=0x9A; ab=0xDE; break;
        case ::psvitaalive::ColorTheme::Crimson: ar=0xFF; ag=0x2D; ab=0x4A; break;
        case ::psvitaalive::ColorTheme::Coffee: ar=0xD4; ag=0xA5; ab=0x5E; break;
        case ::psvitaalive::ColorTheme::Gold: ar=0xFF; ag=0xC8; ab=0x2E; break;
        case ::psvitaalive::ColorTheme::Emerald: ar=0x00; ag=0xD4; ab=0x7A; break;
        case ::psvitaalive::ColorTheme::Coral: ar=0xFF; ag=0x7A; ab=0x66; break;
        case ::psvitaalive::ColorTheme::Teal: ar=0x2E; ag=0xD4; ab=0xC0; break;
        case ::psvitaalive::ColorTheme::Indigo: ar=0x7A; ag=0x6C; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Sky: ar=0x6C; ag=0xC9; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Magenta: ar=0xFF; ag=0x2E; ab=0xD4; break;
        case ::psvitaalive::ColorTheme::Mint: ar=0x7E; ag=0xFF; ab=0xC4; break;
        case ::psvitaalive::ColorTheme::Sunset: ar=0xFF; ag=0x6B; ab=0x35; break;
        case ::psvitaalive::ColorTheme::Ocean: ar=0x00; ag=0x7A; ab=0xCC; break;
        case ::psvitaalive::ColorTheme::Lavender: ar=0xC4; ag=0x9E; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Cherry: ar=0xE0; ag=0x1B; ab=0x4C; break;
        case ::psvitaalive::ColorTheme::Sand: ar=0xE0; ag=0xC28; ab=0x7A; break;
        case ::psvitaalive::ColorTheme::Forest: ar=0x2E; ag=0x8B; ab=0x57; break;
        case ::psvitaalive::ColorTheme::Ice: ar=0xB8; ag=0xE0; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Grape: ar=0x8E; ag=0x2D; ab=0xE2; break;
        case ::psvitaalive::ColorTheme::Peach: ar=0xFF; ag=0xB3; ab=0x8A; break;
        case ::psvitaalive::ColorTheme::Azure: ar=0x1E; ag=0x90; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Steel: ar=0x8A; ag=0x9B; ab=0xB0; break;
        case ::psvitaalive::ColorTheme::Honey: ar=0xFF; ag=0xB3; ab=0x00; break;
        case ::psvitaalive::ColorTheme::Midnight: ar=0x4A; ag=0x6C; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Sakura: ar=0xFF; ag=0x8A; ab=0xC4; break;
        case ::psvitaalive::ColorTheme::Matrix: ar=0x00; ag=0xFF; ab=0x41; break;
        case ::psvitaalive::ColorTheme::NeonLime:
        default: ar=0x3B; ag=0xFF; ab=0x00; break;
    }
}'''
# fix typo 0xC28 -> 0xC2
new_rgb = new_rgb.replace("ag=0xC28", "ag=0xC2")
cpp = cpp[:old_rgb.start()] + new_rgb + cpp[old_rgb.end():]

# ---- Replace applyColorTheme switch body (from unsigned ar= to ACCENT =) ----
old_apply = re.search(
    r"void applyColorTheme\(::psvitaalive::ColorTheme t\) \{[\s\S]*?ACCENT = RGBA8\(ar, ag, ab, 255\);",
    cpp,
)
if not old_apply:
    raise SystemExit("applyColorTheme not found")

new_apply = '''void applyColorTheme(::psvitaalive::ColorTheme t) {
    BG = RGBA8(0x0A,0x0A,0x0A,255);
    SURFACE = RGBA8(0x1A,0x1A,0x1A,255);
    SURFACE2 = RGBA8(0x12,0x12,0x14,255);
    PANEL = RGBA8(0x0E,0x0E,0x10,255);
    BORDER = RGBA8(0x2A,0x2A,0x2E,255);
    TEXT = RGBA8(0xAA,0xAA,0xAA,255);
    DIM = RGBA8(0x66,0x66,0x6A,255);
    WHITE = RGBA8(0xF0,0xF0,0xF0,255);
    SILVER = RGBA8(0xC8,0xC8,0xCC,255);

    unsigned ar=0x3B, ag=0xFF, ab=0x00;
    colorThemeAccentRgb(t, ar, ag, ab);

    switch (t) {
        case ::psvitaalive::ColorTheme::Oled:
        case ::psvitaalive::ColorTheme::Matrix:
            BG = RGBA8(0x00,0x00,0x00,255);
            SURFACE = RGBA8(0x0C,0x0C,0x0C,255);
            SURFACE2 = RGBA8(0x08,0x08,0x08,255);
            PANEL = RGBA8(0x05,0x05,0x05,255);
            break;
        case ::psvitaalive::ColorTheme::PsVita:
        case ::psvitaalive::ColorTheme::Ocean:
        case ::psvitaalive::ColorTheme::Azure:
        case ::psvitaalive::ColorTheme::Sky:
        case ::psvitaalive::ColorTheme::Ice:
            BG = RGBA8(0x0A,0x0C,0x12,255);
            SURFACE = RGBA8(0x14,0x18,0x22,255);
            SURFACE2 = RGBA8(0x10,0x14,0x1C,255);
            PANEL = RGBA8(0x0C,0x10,0x16,255);
            BORDER = RGBA8(0x2A,0x34,0x44,255);
            break;
        case ::psvitaalive::ColorTheme::Crimson:
        case ::psvitaalive::ColorTheme::Cherry:
            BG = RGBA8(0x0E,0x08,0x0A,255);
            SURFACE = RGBA8(0x1C,0x10,0x14,255);
            SURFACE2 = RGBA8(0x16,0x0C,0x10,255);
            PANEL = RGBA8(0x12,0x0A,0x0C,255);
            break;
        case ::psvitaalive::ColorTheme::Coffee:
        case ::psvitaalive::ColorTheme::Sand:
        case ::psvitaalive::ColorTheme::Honey:
            BG = RGBA8(0x0E,0x0B,0x08,255);
            SURFACE = RGBA8(0x1C,0x16,0x10,255);
            SURFACE2 = RGBA8(0x16,0x12,0x0C,255);
            PANEL = RGBA8(0x12,0x0E,0x0A,255);
            BORDER = RGBA8(0x3A,0x2E,0x22,255);
            TEXT = RGBA8(0xC0,0xB0,0x9A,255);
            DIM = RGBA8(0x7A,0x6A,0x58,255);
            break;
        case ::psvitaalive::ColorTheme::Gold:
        case ::psvitaalive::ColorTheme::Sunset:
        case ::psvitaalive::ColorTheme::Peach:
            BG = RGBA8(0x0C,0x0A,0x06,255);
            SURFACE = RGBA8(0x1A,0x16,0x0C,255);
            SURFACE2 = RGBA8(0x14,0x12,0x0A,255);
            PANEL = RGBA8(0x10,0x0E,0x08,255);
            break;
        case ::psvitaalive::ColorTheme::Emerald:
        case ::psvitaalive::ColorTheme::Forest:
        case ::psvitaalive::ColorTheme::Mint:
            BG = RGBA8(0x06,0x0C,0x0A,255);
            SURFACE = RGBA8(0x0E,0x18,0x14,255);
            SURFACE2 = RGBA8(0x0A,0x14,0x10,255);
            PANEL = RGBA8(0x08,0x10,0x0C,255);
            break;
        case ::psvitaalive::ColorTheme::Coral:
        case ::psvitaalive::ColorTheme::Sakura:
        case ::psvitaalive::ColorTheme::Rose:
        case ::psvitaalive::ColorTheme::Magenta:
            BG = RGBA8(0x0E,0x0A,0x0C,255);
            SURFACE = RGBA8(0x1C,0x12,0x16,255);
            SURFACE2 = RGBA8(0x16,0x0E,0x12,255);
            PANEL = RGBA8(0x12,0x0C,0x10,255);
            break;
        case ::psvitaalive::ColorTheme::Teal:
            BG = RGBA8(0x06,0x0C,0x0C,255);
            SURFACE = RGBA8(0x0E,0x18,0x18,255);
            SURFACE2 = RGBA8(0x0A,0x14,0x14,255);
            PANEL = RGBA8(0x08,0x10,0x10,255);
            break;
        case ::psvitaalive::ColorTheme::Indigo:
        case ::psvitaalive::ColorTheme::Violet:
        case ::psvitaalive::ColorTheme::Lavender:
        case ::psvitaalive::ColorTheme::Grape:
        case ::psvitaalive::ColorTheme::Midnight:
            BG = RGBA8(0x0A,0x0A,0x12,255);
            SURFACE = RGBA8(0x14,0x14,0x22,255);
            SURFACE2 = RGBA8(0x10,0x10,0x1C,255);
            PANEL = RGBA8(0x0C,0x0C,0x16,255);
            BORDER = RGBA8(0x2E,0x2E,0x44,255);
            break;
        case ::psvitaalive::ColorTheme::Steel:
            BG = RGBA8(0x0A,0x0B,0x0C,255);
            SURFACE = RGBA8(0x16,0x18,0x1A,255);
            SURFACE2 = RGBA8(0x12,0x14,0x16,255);
            PANEL = RGBA8(0x0E,0x10,0x12,255);
            BORDER = RGBA8(0x34,0x38,0x3E,255);
            break;
        default:
            break;
    }
    ACCENT = RGBA8(ar, ag, ab, 255);'''

cpp = cpp[:old_apply.start()] + new_apply + cpp[old_apply.end():]

# ---- openThemeSetupIfNeeded: allow reopen via openThemePicker ----
old_open = re.search(r"void FullCatalogScreen::openThemeSetupIfNeeded\(\) \{[\s\S]*?\n\}", cpp)
if not old_open:
    raise SystemExit("openThemeSetupIfNeeded not found")
new_open = '''void FullCatalogScreen::openThemePicker() {
    themeSetupVisible_ = true;
    const int n = static_cast<int>(::psvitaalive::ColorTheme::Count);
    themeSetupFocus_ = static_cast<int>(settingsEdit_.colorTheme);
    if (themeSetupFocus_ < 0 || themeSetupFocus_ >= n) themeSetupFocus_ = 0;
    themeSetupScrollRow_ = 0;
    visualThemeSetupScroll_ = 0.f;
    // Keep focused row roughly in view
    const int cols = 3;
    themeSetupScrollRow_ = std::max(0, themeSetupFocus_ / cols - 1);
    applyColorTheme(settingsEdit_.colorTheme);
    diagnostics::log("[UI] theme picker opened");
}

void FullCatalogScreen::openThemeSetupIfNeeded() {
    if (themeSetupChecked_) return;
    themeSetupChecked_ = true;
    if (settingsEdit_.themeSetupDone) {
        diagnostics::log("[UI] theme setup skipped (already done)");
        return;
    }
    openThemePicker();
    diagnostics::log("[UI] theme setup modal shown (first run)");
}'''
cpp = cpp[:old_open.start()] + new_open + cpp[old_open.end():]

# ---- cycleSettingsOption row 2 opens picker ----
old_cycle = '''    } else if (row == 2) {
        const int n = static_cast<int>(::psvitaalive::ColorTheme::Count);
        int v = static_cast<int>(settingsEdit_.colorTheme);
        v = (v + delta) % n;
        if (v < 0) v += n;
        settingsEdit_.colorTheme = static_cast<::psvitaalive::ColorTheme>(v);
        applyColorTheme(settingsEdit_.colorTheme); // live preview
    }'''
new_cycle = '''    } else if (row == 2) {
        (void)delta;
        openThemePicker(); // same palette window as first-run setup
    }'''
if old_cycle not in cpp:
    raise SystemExit("cycleSettingsOption theme branch not found")
cpp = cpp.replace(old_cycle, new_cycle, 1)

# ---- themeLabel uses colorThemeDisplayName ----
old_label = re.search(r"auto themeLabel = \[&\]\(\) -> std::string \{[\s\S]*?\n    \};", cpp)
if not old_label:
    raise SystemExit("themeLabel not found")
new_label = '''auto themeLabel = [&]() -> std::string {
        return std::string(colorThemeDisplayName(settingsEdit_.colorTheme));
    };'''
cpp = cpp[:old_label.start()] + new_label + cpp[old_label.end():]

# Settings row hint
cpp = cpp.replace(
    '{"INTERFACE", "Color theme", themeLabel(), "Accent color - changes live", true}',
    '{"INTERFACE", "Color theme", themeLabel() + "  >", "X / tap: open color palette picker", true}',
    1,
)

# ---- handleInput: theme setup before SETTINGS early return ----
old_hi = "if(state_.mode==UiMode::SETTINGS){handleSettingsInput(pressed,nav);return;}"
new_hi = "if(themeSetupVisible_){const int themeCount=static_cast<int>(::psvitaalive::ColorTheme::Count);const int cols=3;if(nav&SCE_CTRL_LEFT){if(themeSetupFocus_<themeCount){int c=themeSetupFocus_%cols;if(c>0)--themeSetupFocus_;}return;}if(nav&SCE_CTRL_RIGHT){if(themeSetupFocus_<themeCount){int c=themeSetupFocus_%cols;if(c<cols-1&&themeSetupFocus_+1<themeCount)++themeSetupFocus_;}return;}if(nav&SCE_CTRL_UP||(pressed&SCE_CTRL_UP)){if(themeSetupFocus_==themeCount){themeSetupFocus_=std::max(0,themeCount-cols);}else if(themeSetupFocus_>=cols)themeSetupFocus_-=cols;return;}if(nav&SCE_CTRL_DOWN||(pressed&SCE_CTRL_DOWN)){if(themeSetupFocus_<themeCount){int n=themeSetupFocus_+cols;if(n<themeCount)themeSetupFocus_=n;else themeSetupFocus_=themeCount;}return;}if(pressed&SCE_CTRL_CROSS){if(themeSetupFocus_==themeCount)closeThemeSetup(true);else applyThemeSetupFocus();return;}return;}if(state_.mode==UiMode::SETTINGS){handleSettingsInput(pressed,nav);return;}"
if old_hi not in cpp:
    raise SystemExit("SETTINGS early return not found in handleInput")
cpp = cpp.replace(old_hi, new_hi, 1)

# ---- drawSettings: overlay theme picker at end ----
if "drawThemeSetupOverlay();\n    vita2d_end_drawing();\n    vita2d_swap_buffers();\n}\n\nvoid FullCatalogScreen::handleInput" not in cpp:
    # Find end of drawSettings
    ds = cpp.find("void FullCatalogScreen::drawSettings()")
    if ds < 0:
        raise SystemExit("drawSettings not found")
    end_draw = cpp.find("vita2d_end_drawing();", ds)
    if end_draw < 0:
        raise SystemExit("drawSettings end_drawing not found")
    # only first end_drawing in drawSettings - check next lines
    snippet_end = cpp.find("void FullCatalogScreen::handleInput", ds)
    # find last vita2d_end_drawing before handleInput after drawSettings
    region = cpp[ds:snippet_end]
    last = region.rfind("vita2d_end_drawing();")
    if last < 0:
        raise SystemExit("no end_drawing in drawSettings")
    abs_pos = ds + last
    insert = "if (themeSetupVisible_) drawThemeSetupOverlay();\n    "
    if "themeSetupVisible_) drawThemeSetupOverlay" not in region:
        cpp = cpp[:abs_pos] + insert + cpp[abs_pos:]

# ---- handleTouch SETTINGS: if theme visible, don't steal (theme block is already above) ----
# Theme touch is before settings - OK

# Header declaration for openThemePicker - patch hpp via note: already need method
# We'll add to cpp only; need hpp declaration

CPP.write_text(cpp, encoding="utf-8")
print("cpp themes+settings picker OK")

HPP = Path("Client PSVitaAlive/include/ui/full_catalog_screen.hpp")
hpp = HPP.read_text(encoding="utf-8")
if "void openThemePicker();" not in hpp:
    hpp = hpp.replace(
        "void openThemeSetupIfNeeded();",
        "void openThemeSetupIfNeeded();\n    void openThemePicker();",
        1,
    )
    HPP.write_text(hpp, encoding="utf-8")
    print("hpp openThemePicker declared")
else:
    print("hpp already has openThemePicker")

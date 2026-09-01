#!/usr/bin/env python3
"""Apply color-theme palettes to PSVitaAlive (settings + runtime theme). Safe surgical edits."""
from pathlib import Path
import re

ROOT = Path(".")
HPP = ROOT / "Client PSVitaAlive/include/installer/app_settings.hpp"
CPP_SET = ROOT / "Client PSVitaAlive/source/installer/app_settings.cpp"
UI = ROOT / "Client PSVitaAlive/source/ui/full_catalog_screen.cpp"


def must(path: Path) -> str:
    if not path.exists():
        raise SystemExit(f"missing {path}")
    return path.read_text(encoding="utf-8")


def write(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")
    print(f"updated {path}")


def patch_hpp(text: str) -> str:
    if "ColorTheme" in text:
        print("hpp: ColorTheme already present")
        return text
    old = """enum class PspTarget {
    Adrenaline = 0, // ux0:pspemu/ISO or GAME
    LiveArea        // requires NoPspEmuDrm (same paths; bubble depends on plugin)
};

struct AppSettingsData {
    InstallMethod installMethod = InstallMethod::Auto;
    PspTarget pspTarget = PspTarget::Adrenaline;
    bool warnMissingPlugins = true;
    /** If false, skip the startup "download all images?" dialog (on-demand only). */
    bool promptImageWarmup = false;
    // Internal startup diagnostics; intentionally hidden from the UI.
    bool startupPluginDetection = true;
    bool startupUpdateCheck = true;
};"""
    new = """enum class PspTarget {
    Adrenaline = 0, // ux0:pspemu/ISO or GAME
    LiveArea        // requires NoPspEmuDrm (same paths; bubble depends on plugin)
};

/** UI accent / surface palette (Settings -> Color theme). Default keeps current Neon Lime. */
enum class ColorTheme {
    NeonLime = 0, // brand default #3BFF00
    Cyan,         // blue / cyan
    Rose,         // pink / rose
    Amber,        // warm orange
    Violet,       // purple
    Mono,         // silver / grayscale
    Oled,         // pure black + soft mint
    Count
};

struct AppSettingsData {
    InstallMethod installMethod = InstallMethod::Auto;
    PspTarget pspTarget = PspTarget::Adrenaline;
    ColorTheme colorTheme = ColorTheme::NeonLime;
    bool warnMissingPlugins = true;
    /** If false, skip the startup "download all images?" dialog (on-demand only). */
    bool promptImageWarmup = false;
    // Internal startup diagnostics; intentionally hidden from the UI.
    bool startupPluginDetection = true;
    bool startupUpdateCheck = true;
};"""
    if old not in text:
        raise SystemExit("hpp: AppSettingsData block not found")
    text = text.replace(old, new, 1)

    old2 = """    static const char* toString(InstallMethod m);
    static const char* toString(PspTarget t);
    static InstallMethod parseInstallMethod(const std::string& s);
    static PspTarget parsePspTarget(const std::string& s);
};"""
    new2 = """    static const char* toString(InstallMethod m);
    static const char* toString(PspTarget t);
    static const char* toString(ColorTheme t);
    static InstallMethod parseInstallMethod(const std::string& s);
    static PspTarget parsePspTarget(const std::string& s);
    static ColorTheme parseColorTheme(const std::string& s);
};"""
    if old2 not in text:
        raise SystemExit("hpp: AppSettings methods block not found")
    return text.replace(old2, new2, 1)


def patch_settings_cpp(text: str) -> str:
    if "parseColorTheme" in text and "color_theme" in text:
        print("settings.cpp: color theme already present")
        return text

    needle = """PspTarget AppSettings::parsePspTarget(const std::string& s) {
    if (s == "livearea") return PspTarget::LiveArea;
    return PspTarget::Adrenaline;
}"""
    insert = needle + """

const char* AppSettings::toString(ColorTheme t) {
    switch (t) {
        case ColorTheme::Cyan: return "cyan";
        case ColorTheme::Rose: return "rose";
        case ColorTheme::Amber: return "amber";
        case ColorTheme::Violet: return "violet";
        case ColorTheme::Mono: return "mono";
        case ColorTheme::Oled: return "oled";
        case ColorTheme::NeonLime:
        default: return "neon";
    }
}

ColorTheme AppSettings::parseColorTheme(const std::string& s) {
    if (s == "cyan" || s == "blue") return ColorTheme::Cyan;
    if (s == "rose" || s == "pink" || s == "rosal") return ColorTheme::Rose;
    if (s == "amber" || s == "orange") return ColorTheme::Amber;
    if (s == "violet" || s == "purple") return ColorTheme::Violet;
    if (s == "mono" || s == "gray" || s == "grey") return ColorTheme::Mono;
    if (s == "oled" || s == "black") return ColorTheme::Oled;
    return ColorTheme::NeonLime;
}"""
    if needle not in text:
        raise SystemExit("settings.cpp: parsePspTarget block not found")
    text = text.replace(needle, insert, 1)

    load_add = """    if (containsKey(json, "psp_target", v)) data.pspTarget = parsePspTarget(v);
    if (containsKey(json, "color_theme", v)) data.colorTheme = parseColorTheme(v);
"""
    if 'if (containsKey(json, "psp_target", v)) data.pspTarget = parsePspTarget(v);' not in text:
        raise SystemExit("settings.cpp: load psp_target line missing")
    if "color_theme" not in text:
        text = text.replace(
            '    if (containsKey(json, "psp_target", v)) data.pspTarget = parsePspTarget(v);\n',
            load_add,
            1,
        )

    old_log = """    sceClibPrintf("[AppSettings] loaded method=%s psp=%s warn=%d imagesPrompt=%d pluginDetect=%d updateCheck=%d\\n",
                  toString(data.installMethod), toString(data.pspTarget),
                  data.warnMissingPlugins ? 1 : 0, data.promptImageWarmup ? 1 : 0,
                  data.startupPluginDetection ? 1 : 0, data.startupUpdateCheck ? 1 : 0);"""
    new_log = """    sceClibPrintf("[AppSettings] loaded method=%s psp=%s theme=%s warn=%d imagesPrompt=%d pluginDetect=%d updateCheck=%d\\n",
                  toString(data.installMethod), toString(data.pspTarget), toString(data.colorTheme),
                  data.warnMissingPlugins ? 1 : 0, data.promptImageWarmup ? 1 : 0,
                  data.startupPluginDetection ? 1 : 0, data.startupUpdateCheck ? 1 : 0);"""
    if old_log in text:
        text = text.replace(old_log, new_log, 1)

    old_save = """    char json[512];
    sceClibSnprintf(
        json, sizeof(json),
        "{\\n"
        "  \\"install_method\\": \\"%s\\",\\n"
        "  \\"psp_target\\": \\"%s\\",\\n"
        "  \\"warn_missing_plugins\\": %s,\\n"
        "  \\"prompt_image_warmup\\": %s,\\n"
        "  \\"startup_plugin_detection\\": %s,\\n"
        "  \\"startup_update_check\\": %s\\n"
        "}\\n",
        toString(data.installMethod),
        toString(data.pspTarget),
        data.warnMissingPlugins ? "true" : "false",
        data.promptImageWarmup ? "true" : "false",
        data.startupPluginDetection ? "true" : "false",
        data.startupUpdateCheck ? "true" : "false"
    );"""
    new_save = """    char json[640];
    sceClibSnprintf(
        json, sizeof(json),
        "{\\n"
        "  \\"install_method\\": \\"%s\\",\\n"
        "  \\"psp_target\\": \\"%s\\",\\n"
        "  \\"color_theme\\": \\"%s\\",\\n"
        "  \\"warn_missing_plugins\\": %s,\\n"
        "  \\"prompt_image_warmup\\": %s,\\n"
        "  \\"startup_plugin_detection\\": %s,\\n"
        "  \\"startup_update_check\\": %s\\n"
        "}\\n",
        toString(data.installMethod),
        toString(data.pspTarget),
        toString(data.colorTheme),
        data.warnMissingPlugins ? "true" : "false",
        data.promptImageWarmup ? "true" : "false",
        data.startupPluginDetection ? "true" : "false",
        data.startupUpdateCheck ? "true" : "false"
    );"""
    if old_save not in text:
        raise SystemExit("settings.cpp: save block not found")
    text = text.replace(old_save, new_save, 1)
    return text


THEME_BLOCK = r"""
// LiveArea brand palette (mutable - ColorTheme swaps accent/surfaces at runtime)
unsigned BG=RGBA8(0x0A,0x0A,0x0A,255);
unsigned SURFACE=RGBA8(0x1A,0x1A,0x1A,255);
unsigned SURFACE2=RGBA8(0x12,0x12,0x14,255);
unsigned PANEL=RGBA8(0x0E,0x0E,0x10,255);
unsigned BORDER=RGBA8(0x2A,0x2A,0x2E,255);
unsigned TEXT=RGBA8(0xAA,0xAA,0xAA,255);
unsigned DIM=RGBA8(0x66,0x66,0x6A,255);
unsigned ACCENT=RGBA8(0x3B,0xFF,0x00,255);       // #3BFF00 default
unsigned ACCENT_DIM=RGBA8(0x3B,0xFF,0x00,90);
unsigned ACCENT_SOFT=RGBA8(0x3B,0xFF,0x00,40);
unsigned WHITE=RGBA8(0xF0,0xF0,0xF0,255);
unsigned SILVER=RGBA8(0xC8,0xC8,0xCC,255);

/** Rebuild RGBA with a new alpha; keeps RGB from c (vita2d RGBA8 = A<<24 | B<<16 | G<<8 | R). */
unsigned withAlpha(unsigned c, unsigned a) {
    return (c & 0x00FFFFFFu) | ((a & 0xFFu) << 24);
}

void applyColorTheme(::psvitaalive::ColorTheme t) {
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
    switch (t) {
        case ::psvitaalive::ColorTheme::Cyan:
            ar=0x00; ag=0xE5; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Rose:
            ar=0xFF; ag=0x5C; ab=0xA8; break;
        case ::psvitaalive::ColorTheme::Amber:
            ar=0xFF; ag=0xB0; ab=0x20; break;
        case ::psvitaalive::ColorTheme::Violet:
            ar=0xB2; ag=0x4D; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Mono:
            ar=0xC8; ag=0xC8; ab=0xCC; break;
        case ::psvitaalive::ColorTheme::Oled:
            ar=0x5C; ag=0xFF; ab=0x9A;
            BG = RGBA8(0x00,0x00,0x00,255);
            SURFACE = RGBA8(0x0C,0x0C,0x0C,255);
            SURFACE2 = RGBA8(0x08,0x08,0x08,255);
            PANEL = RGBA8(0x05,0x05,0x05,255);
            break;
        case ::psvitaalive::ColorTheme::NeonLime:
        default:
            ar=0x3B; ag=0xFF; ab=0x00; break;
    }
    ACCENT = RGBA8(ar, ag, ab, 255);
    ACCENT_DIM = RGBA8(ar, ag, ab, 90);
    ACCENT_SOFT = RGBA8(ar, ag, ab, 40);
}

/** Thin neon frame used across cards, panels, and modals (uses current ACCENT). */
void drawNeonFrame(int x, int y, int w, int h, unsigned alphaOuter = 70, unsigned alphaInner = 180) {
    const unsigned outer = withAlpha(ACCENT, alphaOuter);
    const unsigned inner = withAlpha(ACCENT, alphaInner);
    vita2d_draw_rectangle(x - 1, y - 1, w + 2, 1, outer);
    vita2d_draw_rectangle(x - 1, y + h, w + 2, 1, outer);
    vita2d_draw_rectangle(x - 1, y - 1, 1, h + 2, outer);
    vita2d_draw_rectangle(x + w, y - 1, 1, h + 2, outer);
    vita2d_draw_rectangle(x, y, w, 1, inner);
    vita2d_draw_rectangle(x, y + h - 1, w, 1, inner);
    vita2d_draw_rectangle(x, y, 1, h, inner);
    vita2d_draw_rectangle(x + w - 1, y, 1, h, inner);
}
"""


def patch_ui(text: str) -> str:
    if "applyColorTheme" not in text or "ColorTheme::Cyan" not in text:
        start = text.find("// LiveArea brand palette")
        if start < 0:
            start = text.find("constexpr unsigned BG=")
        if start < 0:
            start = text.find("unsigned BG=RGBA8")
        if start < 0:
            raise SystemExit("ui: color palette start not found")
        dn = text.find("void drawNeonFrame(", start)
        if dn < 0:
            raise SystemExit("ui: drawNeonFrame not found")
        end = text.find("\n}\n", dn)
        if end < 0:
            raise SystemExit("ui: drawNeonFrame end not found")
        end = end + 2
        text = text[:start] + THEME_BLOCK.lstrip("\n") + text[end:]
        text = text.replace(
            "const unsigned borderCol = RGBA8(0x3B, 0xFF, 0x00, borderA > 255 ? 255 : borderA);",
            "const unsigned borderCol = withAlpha(ACCENT, borderA > 255 ? 255 : borderA);",
        )

    old_set = """void FullCatalogScreen::setAppSettings(const ::psvitaalive::AppSettingsData& settings) {
    settingsEdit_ = settings;
}"""
    new_set = """void FullCatalogScreen::setAppSettings(const ::psvitaalive::AppSettingsData& settings) {
    settingsEdit_ = settings;
    applyColorTheme(settingsEdit_.colorTheme);
}"""
    if old_set in text:
        text = text.replace(old_set, new_set, 1)
    elif "applyColorTheme(settingsEdit_.colorTheme)" not in text:
        raise SystemExit("ui: setAppSettings block not found")

    old_cycle = """void FullCatalogScreen::cycleSettingsOption(int row, int delta) {
    if (delta == 0) return;
    if (row == 0) {
        int v = static_cast<int>(settingsEdit_.installMethod);
        v = (v + delta) % 3;
        if (v < 0) v += 3;
        settingsEdit_.installMethod = static_cast<::psvitaalive::InstallMethod>(v);
    } else if (row == 1) {
        settingsEdit_.pspTarget = (settingsEdit_.pspTarget == ::psvitaalive::PspTarget::Adrenaline)
            ? ::psvitaalive::PspTarget::LiveArea : ::psvitaalive::PspTarget::Adrenaline;
    } else if (row == 2) {
        settingsEdit_.warnMissingPlugins = !settingsEdit_.warnMissingPlugins;
    } else if (row == 3) {
        settingsEdit_.promptImageWarmup = !settingsEdit_.promptImageWarmup;
    } else if (row == 4) {
        triggerSelfUpdateAction();
    }
}"""
    new_cycle = """void FullCatalogScreen::cycleSettingsOption(int row, int delta) {
    if (delta == 0) return;
    if (row == 0) {
        int v = static_cast<int>(settingsEdit_.installMethod);
        v = (v + delta) % 3;
        if (v < 0) v += 3;
        settingsEdit_.installMethod = static_cast<::psvitaalive::InstallMethod>(v);
    } else if (row == 1) {
        settingsEdit_.pspTarget = (settingsEdit_.pspTarget == ::psvitaalive::PspTarget::Adrenaline)
            ? ::psvitaalive::PspTarget::LiveArea : ::psvitaalive::PspTarget::Adrenaline;
    } else if (row == 2) {
        const int n = static_cast<int>(::psvitaalive::ColorTheme::Count);
        int v = static_cast<int>(settingsEdit_.colorTheme);
        v = (v + delta) % n;
        if (v < 0) v += n;
        settingsEdit_.colorTheme = static_cast<::psvitaalive::ColorTheme>(v);
        applyColorTheme(settingsEdit_.colorTheme); // live preview
    } else if (row == 3) {
        settingsEdit_.warnMissingPlugins = !settingsEdit_.warnMissingPlugins;
    } else if (row == 4) {
        settingsEdit_.promptImageWarmup = !settingsEdit_.promptImageWarmup;
    } else if (row == 5) {
        triggerSelfUpdateAction();
    }
}"""
    if old_cycle in text:
        text = text.replace(old_cycle, new_cycle, 1)
    elif "ColorTheme::Count" not in text:
        raise SystemExit("ui: cycleSettingsOption block not found")

    text = text.replace("constexpr int kRows = 5;", "constexpr int kRows = 6;")

    if "Color theme" not in text:
        new_opts = """    auto themeLabel = [&]() -> std::string {
        switch (settingsEdit_.colorTheme) {
            case ::psvitaalive::ColorTheme::Cyan: return "Cyan";
            case ::psvitaalive::ColorTheme::Rose: return "Rose";
            case ::psvitaalive::ColorTheme::Amber: return "Amber";
            case ::psvitaalive::ColorTheme::Violet: return "Violet";
            case ::psvitaalive::ColorTheme::Mono: return "Mono";
            case ::psvitaalive::ColorTheme::Oled: return "OLED";
            case ::psvitaalive::ColorTheme::NeonLime:
            default: return "Neon Lime";
        }
    };
    Opt opts[6] = {
        {"INSTALL", "Install method", methodLabel(), "Auto: BGDL for PKG when available", true},
        {"", "PSP / PS1 target", pspLabel(), "ISO/CSO/PBP under ux0:pspemu", false},
        {"INTERFACE", "Color theme", themeLabel(), "Accent color - changes live", true},
        {"", "Warn missing plugins", settingsEdit_.warnMissingPlugins ? "Yes" : "No", "Startup toast if NoNpDrm is missing", false},
        {"CATALOG", "Prompt image download", settingsEdit_.promptImageWarmup ? "Yes" : "No", "If you choose No once, it will not ask again", true},
        {"UPDATES", "App updates", updateLabel(), "GitHub Releases - X to check / install", true},
    };"""
        m = re.search(r"Opt opts\[5\] = \{.*?\};", text, re.S)
        if not m:
            raise SystemExit("ui: opts[5] block not found")
        text = text[: m.start()] + new_opts + text[m.end() :]

        text = text.replace(
            "for (int i = 0; i < 5; ++i) {\n        if (opts[i].sectionStart",
            "for (int i = 0; i < 6; ++i) {\n        if (opts[i].sectionStart",
        )
        text = text.replace(
            "for (int i = 0; i <= settingsFocus_ && i < 5; ++i)",
            "for (int i = 0; i <= settingsFocus_ && i < 6; ++i)",
        )

        old_help = """        switch (settingsFocus_) {
        case 0:
            body1 = "How packages are installed.";
            body2 = "Auto uses BGDL for PKG when";
            body3 = "Shell supports it, else Direct.";
            break;
        case 1:
            body1 = "Where PSP/PS1 content goes.";
            body2 = "Adrenaline: ISO/CSO under";
            body3 = "ux0:pspemu. LiveArea needs plugins.";
            break;
        case 2:
            body1 = "Show a toast at startup when";
            body2 = "NoNpDrm / NoPspEmuDrm are";
            body3 = "missing from taiHEN config.";
            break;
        case 3:
            body1 = "Ask once whether to download";
            body2 = "all catalog images at startup.";
            body3 = "Off = load images on demand.";
            break;
        case 4:
            body1 = "Checks GitHub Releases for a";
            body2 = "newer PSVitaAlive.vpk and can";
            body3 = "install it in-place (VitaDB style).";
            break;
        default: break;
        }"""
        new_help = """        switch (settingsFocus_) {
        case 0:
            body1 = "How packages are installed.";
            body2 = "Auto uses BGDL for PKG when";
            body3 = "Shell supports it, else Direct.";
            break;
        case 1:
            body1 = "Where PSP/PS1 content goes.";
            body2 = "Adrenaline: ISO/CSO under";
            body3 = "ux0:pspemu. LiveArea needs plugins.";
            break;
        case 2:
            body1 = "UI accent palette. Neon is the";
            body2 = "brand default; Cyan / Rose are";
            body3 = "popular. Report stays red.";
            break;
        case 3:
            body1 = "Show a toast at startup when";
            body2 = "NoNpDrm / NoPspEmuDrm are";
            body3 = "missing from taiHEN config.";
            break;
        case 4:
            body1 = "Ask once whether to download";
            body2 = "all catalog images at startup.";
            body3 = "Off = load images on demand.";
            break;
        case 5:
            body1 = "Checks GitHub Releases for a";
            body2 = "newer PSVitaAlive.vpk and can";
            body3 = "install it in-place (VitaDB style).";
            break;
        default: break;
        }"""
        if old_help not in text:
            raise SystemExit("ui: help switch not found")
        text = text.replace(old_help, new_help, 1)
        text = text.replace("if (settingsFocus_ == 4) {", "if (settingsFocus_ == 5) {")

    old_meta = """        Meta meta[5] = {
            {true, "INSTALL"}, {false, ""}, {true, "INTERFACE"}, {true, "CATALOG"}, {true, "UPDATES"}
        };
        int rowY[5];
        int y = contentTop - static_cast<int>(settingsScrollY_);
        for (int i = 0; i < 5; ++i) {
            if (meta[i].sectionStart && meta[i].section[0]) y += 22;
            rowY[i] = y;
            y += 52 + 8;
        }
        const int rowH = 52;
        const int measured = 5 * (52 + 8) + 4 * 22;"""
    new_meta = """        Meta meta[6] = {
            {true, "INSTALL"}, {false, ""}, {true, "INTERFACE"}, {false, ""}, {true, "CATALOG"}, {true, "UPDATES"}
        };
        int rowY[6];
        int y = contentTop - static_cast<int>(settingsScrollY_);
        for (int i = 0; i < 6; ++i) {
            if (meta[i].sectionStart && meta[i].section[0]) y += 22;
            rowY[i] = y;
            y += 52 + 8;
        }
        const int rowH = 52;
        const int measured = 6 * (52 + 8) + 4 * 22;"""
    if old_meta in text:
        text = text.replace(old_meta, new_meta, 1)
    else:
        print("warn: touch meta block not exact - check manually")

    text = re.sub(
        r"(for \(int i = 0; i < )5(; \+\+i\) \{\s*if \(x >= listX && x < listX \+ listW && yy >= rowY\[i\] && yy < rowY\[i\] \+ rowH\))",
        r"\g<1>6\2",
        text,
        count=2,
    )

    if "Opt opts[6]" in text:

        def fix_loop(m):
            block = m.group(0)
            if "settingsFocus_" in block or "opts[i]" in block:
                return block.replace("i < 5", "i < 6")
            return block

        text = re.sub(
            r"for \(int i = 0; i < 5; \+\+i\) \{.{0,400}?settingsFocus_.{0,200}?\}",
            fix_loop,
            text,
            flags=re.S,
        )

    return text


def main() -> None:
    hpp = patch_hpp(must(HPP))
    write(HPP, hpp)
    sc = patch_settings_cpp(must(CPP_SET))
    write(CPP_SET, sc)
    ui = patch_ui(must(UI))
    write(UI, ui)
    print("OK color theme patch applied")


if __name__ == "__main__":
    main()

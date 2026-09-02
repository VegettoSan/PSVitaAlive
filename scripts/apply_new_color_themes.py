#!/usr/bin/env python3
from pathlib import Path

CPP = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
cpp = CPP.read_text(encoding="utf-8")

old_switch = """    unsigned ar=0x3B, ag=0xFF, ab=0x00;
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
"""

new_switch = """    unsigned ar=0x3B, ag=0xFF, ab=0x00;
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
        case ::psvitaalive::ColorTheme::PsVita:
            // Classic PS Vita LiveArea / system blue
            ar=0x00; ag=0x9A; ab=0xDE;
            BG = RGBA8(0x0A,0x0C,0x12,255);
            SURFACE = RGBA8(0x14,0x18,0x22,255);
            SURFACE2 = RGBA8(0x10,0x14,0x1C,255);
            PANEL = RGBA8(0x0C,0x10,0x16,255);
            BORDER = RGBA8(0x2A,0x34,0x44,255);
            break;
        case ::psvitaalive::ColorTheme::Crimson:
            ar=0xFF; ag=0x2D; ab=0x4A;
            BG = RGBA8(0x0E,0x08,0x0A,255);
            SURFACE = RGBA8(0x1C,0x10,0x14,255);
            SURFACE2 = RGBA8(0x16,0x0C,0x10,255);
            PANEL = RGBA8(0x12,0x0A,0x0C,255);
            break;
        case ::psvitaalive::ColorTheme::Coffee:
            ar=0xD4; ag=0xA dig; ab=0x5E;
            BG = RGBA8(0x0E,0x0B,0x08,255);
            SURFACE = RGBA8(0x1C,0x16,0x10,255);
            SURFACE2 = RGBA8(0x16,0x12,0x0C,255);
            PANEL = RGBA8(0x12,0x0E,0x0A,255);
            BORDER = RGBA8(0x3A,0x2E,0x22,255);
            TEXT = RGBA8(0xC0,0xB0,0x9A,255);
            DIM = RGBA8(0x7A,0x6A,0x58,255);
            break;
        case ::psvitaalive::ColorTheme::Gold:
            ar=0xFF; ag=0xC8; ab=0x2E;
            BG = RGBA8(0x0C,0x0A,0x06,255);
            SURFACE = RGBA8(0x1A,0x16,0x0C,255);
            SURFACE2 = RGBA8(0x14,0x12,0x0A,255);
            PANEL = RGBA8(0x10,0x0E,0x08,255);
            break;
        case ::psvitaalive::ColorTheme::Emerald:
            ar=0x00; ag=0xD4; ab=0x7A;
            BG = RGBA8(0x06,0x0C,0x0A,255);
            SURFACE = RGBA8(0x0E,0x18,0x14,255);
            SURFACE2 = RGBA8(0x0A,0x14,0x10,255);
            PANEL = RGBA8(0x08,0x10,0x0C,255);
            break;
        case ::psvitaalive::ColorTheme::Coral:
            ar=0xFF; ag=0x7A; ab=0x66;
            BG = RGBA8(0x0E,0x0A,0x0A,255);
            SURFACE = RGBA8(0x1C,0x14,0x12,255);
            SURFACE2 = RGBA8(0x16,0x10,0x0E,255);
            PANEL = RGBA8(0x12,0x0C,0x0C,255);
            break;
        case ::psvitaalive::ColorTheme::Teal:
            ar=0x2E; ag=0xD4; ab=0xC0;
            BG = RGBA8(0x06,0x0C,0x0C,255);
            SURFACE = RGBA8(0x0E,0x18,0x18,255);
            SURFACE2 = RGBA8(0x0A,0x14,0x14,255);
            PANEL = RGBA8(0x08,0x10,0x10,255);
            break;
        case ::psvitaalive::ColorTheme::Indigo:
            ar=0x7A; ag=0x6C; ab=0xFF;
            BG = RGBA8(0x0A,0x0A,0x12,255);
            SURFACE = RGBA8(0x14,0x14,0x22,255);
            SURFACE2 = RGBA8(0x10,0x10,0x1C,255);
            PANEL = RGBA8(0x0C,0x0C,0x16,255);
            BORDER = RGBA8(0x2E,0x2E,0x44,255);
            break;
        case ::psvitaalive::ColorTheme::NeonLime:
        default:
            ar=0x3B; ag=0xFF; ab=0x00; break;
    }
"""

# Fix typo in coffee accent green channel
new_switch = new_switch.replace("ar=0xD4; ag=0xA dig; ab=0x5E;", "ar=0xD4; ag=0xA5; ab=0x5E;")

if old_switch not in cpp:
    raise SystemExit("applyColorTheme switch not found")
cpp = cpp.replace(old_switch, new_switch, 1)

old_label = """        auto themeLabel = [&]() -> std::string {
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
"""

new_label = """        auto themeLabel = [&]() -> std::string {
        switch (settingsEdit_.colorTheme) {
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
            case ::psvitaalive::ColorTheme::NeonLime:
            default: return "Neon Lime";
        }
    };
"""

if old_label not in cpp:
    raise SystemExit("themeLabel switch not found")
cpp = cpp.replace(old_label, new_label, 1)

CPP.write_text(cpp, encoding="utf-8")
print("OK new color themes wired")

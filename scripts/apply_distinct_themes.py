#!/usr/bin/env python3
"""Replace theme name/RGB/applyColorTheme with a fully distinct expanded set."""
from pathlib import Path
import re

CPP = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
cpp = CPP.read_text(encoding="utf-8")

new_dn = r'''const char* colorThemeDisplayName(::psvitaalive::ColorTheme t) {
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
        case ::psvitaalive::ColorTheme::Scarlet: return "Scarlet";
        case ::psvitaalive::ColorTheme::Orange: return "Orange";
        case ::psvitaalive::ColorTheme::White: return "White";
        case ::psvitaalive::ColorTheme::Snow: return "Snow";
        case ::psvitaalive::ColorTheme::Ivory: return "Ivory";
        case ::psvitaalive::ColorTheme::Khaki: return "Khaki";
        case ::psvitaalive::ColorTheme::Terracotta: return "Terracotta";
        case ::psvitaalive::ColorTheme::Ruby: return "Ruby";
        case ::psvitaalive::ColorTheme::Copper: return "Copper";
        case ::psvitaalive::ColorTheme::Olive: return "Olive";
        case ::psvitaalive::ColorTheme::Maroon: return "Maroon";
        case ::psvitaalive::ColorTheme::Turquoise: return "Turquoise";
        case ::psvitaalive::ColorTheme::Lemon: return "Lemon";
        case ::psvitaalive::ColorTheme::Plum: return "Plum";
        case ::psvitaalive::ColorTheme::Navy: return "Navy";
        case ::psvitaalive::ColorTheme::Rust: return "Rust";
        case ::psvitaalive::ColorTheme::Champagne: return "Champagne";
        case ::psvitaalive::ColorTheme::Graphite: return "Graphite";
        case ::psvitaalive::ColorTheme::NeonLime:
        default: return "Neon Lime";
    }
}'''

new_rgb = r'''void colorThemeAccentRgb(::psvitaalive::ColorTheme t, unsigned& ar, unsigned& ag, unsigned& ab) {
    ar = 0x3B; ag = 0xFF; ab = 0x00;
    switch (t) {
        case ::psvitaalive::ColorTheme::Cyan:       ar=0x00; ag=0xE5; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Rose:       ar=0xFF; ag=0x5C; ab=0xA8; break;
        case ::psvitaalive::ColorTheme::Amber:      ar=0xFF; ag=0xB0; ab=0x20; break;
        case ::psvitaalive::ColorTheme::Violet:     ar=0xB2; ag=0x4D; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Mono:       ar=0xC8; ag=0xC8; ab=0xCC; break;
        case ::psvitaalive::ColorTheme::Oled:       ar=0x5C; ag=0xFF; ab=0x9A; break;
        case ::psvitaalive::ColorTheme::PsVita:     ar=0x00; ag=0x9A; ab=0xDE; break;
        case ::psvitaalive::ColorTheme::Crimson:    ar=0xE0; ag=0x20; ab=0x40; break;
        case ::psvitaalive::ColorTheme::Coffee:     ar=0xA0; ag=0x72; ab=0x3C; break;
        case ::psvitaalive::ColorTheme::Gold:       ar=0xFF; ag=0xD0; ab=0x00; break;
        case ::psvitaalive::ColorTheme::Emerald:    ar=0x00; ag=0xD4; ab=0x7A; break;
        case ::psvitaalive::ColorTheme::Coral:      ar=0xFF; ag=0x7A; ab=0x66; break;
        case ::psvitaalive::ColorTheme::Teal:       ar=0x00; ag=0xC4; ab=0xB0; break;
        case ::psvitaalive::ColorTheme::Indigo:     ar=0x5A; ag=0x4C; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Sky:        ar=0x7E; ag=0xD0; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Magenta:    ar=0xFF; ag=0x00; ab=0xCC; break;
        case ::psvitaalive::ColorTheme::Mint:       ar=0x98; ag=0xFF; ab=0xCC; break;
        case ::psvitaalive::ColorTheme::Sunset:     ar=0xFF; ag=0x55; ab=0x20; break;
        case ::psvitaalive::ColorTheme::Ocean:      ar=0x00; ag=0x6A; ab=0xB8; break;
        case ::psvitaalive::ColorTheme::Lavender:   ar=0xC8; ag=0xA8; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Cherry:     ar=0xC4; ag=0x10; ab=0x38; break;
        case ::psvitaalive::ColorTheme::Sand:       ar=0xE0; ag=0xC2; ab=0x7A; break;
        case ::psvitaalive::ColorTheme::Forest:     ar=0x1E; ag=0x6B; ab=0x3A; break;
        case ::psvitaalive::ColorTheme::Ice:        ar=0xD0; ag=0xF0; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Grape:      ar=0x8E; ag=0x2D; ab=0xE2; break;
        case ::psvitaalive::ColorTheme::Peach:      ar=0xFF; ag=0xB3; ab=0x8A; break;
        case ::psvitaalive::ColorTheme::Azure:      ar=0x1E; ag=0x90; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Steel:      ar=0x8A; ag=0x9B; ab=0xB0; break;
        case ::psvitaalive::ColorTheme::Honey:      ar=0xFF; ag=0xB3; ab=0x00; break;
        case ::psvitaalive::ColorTheme::Midnight:   ar=0x3A; ag=0x5C; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Sakura:     ar=0xFF; ag=0x8A; ab=0xC4; break;
        case ::psvitaalive::ColorTheme::Matrix:     ar=0x00; ag=0xFF; ab=0x41; break;
        case ::psvitaalive::ColorTheme::Scarlet:    ar=0xFF; ag=0x14; ab=0x14; break;
        case ::psvitaalive::ColorTheme::Orange:     ar=0xFF; ag=0x7A; ab=0x00; break;
        case ::psvitaalive::ColorTheme::White:      ar=0xF5; ag=0xF5; ab=0xF5; break;
        case ::psvitaalive::ColorTheme::Snow:       ar=0xFF; ag=0xFF; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Ivory:      ar=0xFF; ag=0xF0; ab=0xD8; break;
        case ::psvitaalive::ColorTheme::Khaki:      ar=0xC3; ag=0xB0; ab=0x91; break;
        case ::psvitaalive::ColorTheme::Terracotta: ar=0xE2; ag=0x72; ab=0x5B; break;
        case ::psvitaalive::ColorTheme::Ruby:       ar=0x9B; ag=0x11; ab=0x1E; break;
        case ::psvitaalive::ColorTheme::Copper:     ar=0xB8; ag=0x73; ab=0x33; break;
        case ::psvitaalive::ColorTheme::Olive:      ar=0x6B; ag=0x8E; ab=0x23; break;
        case ::psvitaalive::ColorTheme::Maroon:     ar=0x80; ag=0x00; ab=0x20; break;
        case ::psvitaalive::ColorTheme::Turquoise:  ar=0x40; ag=0xE0; ab=0xD0; break;
        case ::psvitaalive::ColorTheme::Lemon:      ar=0xF7; ag=0xE7; ab=0x33; break;
        case ::psvitaalive::ColorTheme::Plum:       ar=0x8E; ag=0x45; ab=0x85; break;
        case ::psvitaalive::ColorTheme::Navy:       ar=0x2A; ag=0x5C; ab=0x9E; break;
        case ::psvitaalive::ColorTheme::Rust:       ar=0xB7; ag=0x41; ab=0x0E; break;
        case ::psvitaalive::ColorTheme::Champagne:  ar=0xF7; ag=0xE7; ab=0xCE; break;
        case ::psvitaalive::ColorTheme::Graphite:   ar=0x90; ag=0x94; ab=0x98; break;
        case ::psvitaalive::ColorTheme::NeonLime:
        default: ar=0x3B; ag=0xFF; ab=0x00; break;
    }
}'''

new_apply = r'''void applyColorTheme(::psvitaalive::ColorTheme t) {
    // Neutral baseline (overridden per theme for stronger identity).
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
            SURFACE = RGBA8(0x0A,0x0A,0x0A,255);
            SURFACE2 = RGBA8(0x06,0x06,0x06,255);
            PANEL = RGBA8(0x03,0x03,0x03,255);
            BORDER = RGBA8(0x22,0x22,0x22,255);
            break;
        case ::psvitaalive::ColorTheme::PsVita:
            BG = RGBA8(0x08,0x0C,0x14,255);
            SURFACE = RGBA8(0x12,0x18,0x24,255);
            SURFACE2 = RGBA8(0x0E,0x14,0x1E,255);
            PANEL = RGBA8(0x0A,0x10,0x18,255);
            BORDER = RGBA8(0x28,0x38,0x4C,255);
            break;
        case ::psvitaalive::ColorTheme::Ocean:
        case ::psvitaalive::ColorTheme::Navy:
            BG = RGBA8(0x06,0x0A,0x14,255);
            SURFACE = RGBA8(0x0E,0x16,0x28,255);
            SURFACE2 = RGBA8(0x0A,0x12,0x20,255);
            PANEL = RGBA8(0x08,0x0E,0x1A,255);
            BORDER = RGBA8(0x24,0x34,0x50,255);
            break;
        case ::psvitaalive::ColorTheme::Azure:
        case ::psvitaalive::ColorTheme::Sky:
            BG = RGBA8(0x0A,0x10,0x16,255);
            SURFACE = RGBA8(0x14,0x1C,0x28,255);
            SURFACE2 = RGBA8(0x10,0x18,0x22,255);
            PANEL = RGBA8(0x0C,0x14,0x1C,255);
            BORDER = RGBA8(0x30,0x40,0x52,255);
            break;
        case ::psvitaalive::ColorTheme::Ice:
        case ::psvitaalive::ColorTheme::Snow:
            BG = RGBA8(0x0C,0x10,0x14,255);
            SURFACE = RGBA8(0x18,0x1E,0x24,255);
            SURFACE2 = RGBA8(0x12,0x18,0x1E,255);
            PANEL = RGBA8(0x0E,0x14,0x18,255);
            BORDER = RGBA8(0x3A,0x48,0x54,255);
            TEXT = RGBA8(0xC8,0xD4,0xE0,255);
            break;
        case ::psvitaalive::ColorTheme::White:
            BG = RGBA8(0x12,0x12,0x12,255);
            SURFACE = RGBA8(0x22,0x22,0x22,255);
            SURFACE2 = RGBA8(0x1A,0x1A,0x1A,255);
            PANEL = RGBA8(0x16,0x16,0x16,255);
            BORDER = RGBA8(0x48,0x48,0x48,255);
            TEXT = RGBA8(0xD8,0xD8,0xD8,255);
            DIM = RGBA8(0x88,0x88,0x88,255);
            break;
        case ::psvitaalive::ColorTheme::Ivory:
        case ::psvitaalive::ColorTheme::Champagne:
            BG = RGBA8(0x10,0x0E,0x0A,255);
            SURFACE = RGBA8(0x1E,0x1A,0x14,255);
            SURFACE2 = RGBA8(0x18,0x14,0x10,255);
            PANEL = RGBA8(0x14,0x12,0x0C,255);
            BORDER = RGBA8(0x44,0x3C,0x30,255);
            TEXT = RGBA8(0xD8,0xCC,0xB4,255);
            break;
        case ::psvitaalive::ColorTheme::Scarlet:
            BG = RGBA8(0x12,0x04,0x04,255);
            SURFACE = RGBA8(0x22,0x0A,0x0A,255);
            SURFACE2 = RGBA8(0x1A,0x08,0x08,255);
            PANEL = RGBA8(0x14,0x06,0x06,255);
            BORDER = RGBA8(0x50,0x18,0x18,255);
            break;
        case ::psvitaalive::ColorTheme::Crimson:
        case ::psvitaalive::ColorTheme::Cherry:
            BG = RGBA8(0x0E,0x06,0x0A,255);
            SURFACE = RGBA8(0x1C,0x0C,0x12,255);
            SURFACE2 = RGBA8(0x16,0x0A,0x0E,255);
            PANEL = RGBA8(0x12,0x08,0x0C,255);
            BORDER = RGBA8(0x44,0x1C,0x28,255);
            break;
        case ::psvitaalive::ColorTheme::Ruby:
        case ::psvitaalive::ColorTheme::Maroon:
            BG = RGBA8(0x0C,0x04,0x08,255);
            SURFACE = RGBA8(0x1A,0x08,0x10,255);
            SURFACE2 = RGBA8(0x14,0x06,0x0C,255);
            PANEL = RGBA8(0x10,0x05,0x0A,255);
            BORDER = RGBA8(0x40,0x14,0x22,255);
            break;
        case ::psvitaalive::ColorTheme::Orange:
        case ::psvitaalive::ColorTheme::Sunset:
            BG = RGBA8(0x12,0x08,0x04,255);
            SURFACE = RGBA8(0x22,0x12,0x08,255);
            SURFACE2 = RGBA8(0x1A,0x0E,0x06,255);
            PANEL = RGBA8(0x14,0x0A,0x04,255);
            BORDER = RGBA8(0x50,0x2C,0x14,255);
            break;
        case ::psvitaalive::ColorTheme::Amber:
        case ::psvitaalive::ColorTheme::Honey:
        case ::psvitaalive::ColorTheme::Gold:
        case ::psvitaalive::ColorTheme::Lemon:
            BG = RGBA8(0x0E,0x0C,0x04,255);
            SURFACE = RGBA8(0x1C,0x18,0x08,255);
            SURFACE2 = RGBA8(0x14,0x12,0x06,255);
            PANEL = RGBA8(0x10,0x0E,0x04,255);
            BORDER = RGBA8(0x48,0x3C,0x14,255);
            break;
        case ::psvitaalive::ColorTheme::Coffee:
        case ::psvitaalive::ColorTheme::Copper:
        case ::psvitaalive::ColorTheme::Rust:
            BG = RGBA8(0x0E,0x0A,0x06,255);
            SURFACE = RGBA8(0x1C,0x14,0x0C,255);
            SURFACE2 = RGBA8(0x16,0x10,0x0A,255);
            PANEL = RGBA8(0x12,0x0C,0x08,255);
            BORDER = RGBA8(0x42,0x30,0x1C,255);
            TEXT = RGBA8(0xC4,0xA8,0x88,255);
            break;
        case ::psvitaalive::ColorTheme::Terracotta:
        case ::psvitaalive::ColorTheme::Sand:
        case ::psvitaalive::ColorTheme::Khaki:
            BG = RGBA8(0x10,0x0C,0x08,255);
            SURFACE = RGBA8(0x1E,0x18,0x12,255);
            SURFACE2 = RGBA8(0x18,0x14,0x0E,255);
            PANEL = RGBA8(0x14,0x10,0x0C,255);
            BORDER = RGBA8(0x46,0x38,0x28,255);
            TEXT = RGBA8(0xC8,0xB8,0xA0,255);
            DIM = RGBA8(0x80,0x70,0x58,255);
            break;
        case ::psvitaalive::ColorTheme::Peach:
        case ::psvitaalive::ColorTheme::Coral:
            BG = RGBA8(0x12,0x0A,0x0A,255);
            SURFACE = RGBA8(0x22,0x14,0x12,255);
            SURFACE2 = RGBA8(0x1A,0x10,0x0E,255);
            PANEL = RGBA8(0x14,0x0C,0x0C,255);
            BORDER = RGBA8(0x4C,0x30,0x28,255);
            break;
        case ::psvitaalive::ColorTheme::Rose:
        case ::psvitaalive::ColorTheme::Sakura:
        case ::psvitaalive::ColorTheme::Magenta:
            BG = RGBA8(0x10,0x08,0x0E,255);
            SURFACE = RGBA8(0x20,0x10,0x1A,255);
            SURFACE2 = RGBA8(0x18,0x0C,0x14,255);
            PANEL = RGBA8(0x12,0x0A,0x10,255);
            BORDER = RGBA8(0x48,0x28,0x3C,255);
            break;
        case ::psvitaalive::ColorTheme::Emerald:
        case ::psvitaalive::ColorTheme::Mint:
            BG = RGBA8(0x06,0x0E,0x0A,255);
            SURFACE = RGBA8(0x0C,0x1A,0x14,255);
            SURFACE2 = RGBA8(0x0A,0x14,0x10,255);
            PANEL = RGBA8(0x08,0x10,0x0C,255);
            BORDER = RGBA8(0x24,0x40,0x32,255);
            break;
        case ::psvitaalive::ColorTheme::Forest:
        case ::psvitaalive::ColorTheme::Olive:
            BG = RGBA8(0x06,0x0A,0x06,255);
            SURFACE = RGBA8(0x0E,0x16,0x0E,255);
            SURFACE2 = RGBA8(0x0A,0x12,0x0A,255);
            PANEL = RGBA8(0x08,0x0E,0x08,255);
            BORDER = RGBA8(0x2A,0x3A,0x22,255);
            break;
        case ::psvitaalive::ColorTheme::Teal:
        case ::psvitaalive::ColorTheme::Turquoise:
            BG = RGBA8(0x04,0x0C,0x0C,255);
            SURFACE = RGBA8(0x0C,0x1A,0x1A,255);
            SURFACE2 = RGBA8(0x08,0x14,0x14,255);
            PANEL = RGBA8(0x06,0x10,0x10,255);
            BORDER = RGBA8(0x24,0x40,0x40,255);
            break;
        case ::psvitaalive::ColorTheme::Violet:
        case ::psvitaalive::ColorTheme::Grape:
        case ::psvitaalive::ColorTheme::Plum:
            BG = RGBA8(0x0C,0x08,0x12,255);
            SURFACE = RGBA8(0x18,0x10,0x22,255);
            SURFACE2 = RGBA8(0x12,0x0C,0x1A,255);
            PANEL = RGBA8(0x0E,0x0A,0x14,255);
            BORDER = RGBA8(0x3A,0x28,0x4C,255);
            break;
        case ::psvitaalive::ColorTheme::Indigo:
        case ::psvitaalive::ColorTheme::Midnight:
            BG = RGBA8(0x08,0x08,0x14,255);
            SURFACE = RGBA8(0x12,0x12,0x24,255);
            SURFACE2 = RGBA8(0x0E,0x0E,0x1C,255);
            PANEL = RGBA8(0x0A,0x0A,0x16,255);
            BORDER = RGBA8(0x2C,0x2C,0x4C,255);
            break;
        case ::psvitaalive::ColorTheme::Lavender:
            BG = RGBA8(0x0E,0x0C,0x14,255);
            SURFACE = RGBA8(0x1A,0x16,0x24,255);
            SURFACE2 = RGBA8(0x14,0x12,0x1C,255);
            PANEL = RGBA8(0x10,0x0E,0x16,255);
            BORDER = RGBA8(0x3C,0x34,0x50,255);
            break;
        case ::psvitaalive::ColorTheme::Steel:
        case ::psvitaalive::ColorTheme::Graphite:
        case ::psvitaalive::ColorTheme::Mono:
            BG = RGBA8(0x0C,0x0C,0x0E,255);
            SURFACE = RGBA8(0x1A,0x1A,0x1E,255);
            SURFACE2 = RGBA8(0x14,0x14,0x16,255);
            PANEL = RGBA8(0x10,0x10,0x12,255);
            BORDER = RGBA8(0x3A,0x3A,0x40,255);
            break;
        case ::psvitaalive::ColorTheme::Cyan:
            BG = RGBA8(0x06,0x0C,0x10,255);
            SURFACE = RGBA8(0x0C,0x18,0x1E,255);
            SURFACE2 = RGBA8(0x0A,0x14,0x18,255);
            PANEL = RGBA8(0x08,0x10,0x14,255);
            BORDER = RGBA8(0x28,0x40,0x48,255);
            break;
        default:
            break;
    }

    ACCENT = RGBA8(ar, ag, ab, 255);'''

def replace_func(src, pattern, replacement):
    m = re.search(pattern, src)
    if not m:
        raise SystemExit(f"pattern not found: {pattern[:60]}")
    return src[:m.start()] + replacement + src[m.end():]

cpp = replace_func(cpp, r"const char\* colorThemeDisplayName\(::psvitaalive::ColorTheme t\) \{[\s\S]*?\n\}", new_dn)
cpp = replace_func(cpp, r"void colorThemeAccentRgb\(::psvitaalive::ColorTheme t, unsigned& ar, unsigned& ag, unsigned& ab\) \{[\s\S]*?\n\}", new_rgb)
cpp = replace_func(cpp, r"void applyColorTheme\(::psvitaalive::ColorTheme t\) \{[\s\S]*?ACCENT = RGBA8\(ar, ag, ab, 255\);", new_apply)

CPP.write_text(cpp, encoding="utf-8")
print("OK distinct themes applied")

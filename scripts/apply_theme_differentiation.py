#!/usr/bin/env python3
"""Make near-identical themes clearly distinct; rename Neon Lime -> PSVitaAlive."""
from pathlib import Path
import re

CPP = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
cpp = CPP.read_text(encoding="utf-8")

# 1) Display name: Neon Lime -> PSVitaAlive
cpp = cpp.replace('default: return "Neon Lime";', 'default: return "PSVitaAlive";')
cpp = cpp.replace('return "Neon Lime";', 'return "PSVitaAlive";')

# 2) Accent RGB: force distinct hues for the confused groups
def set_rgb(src: str, theme: str, r: int, g: int, b: int) -> str:
    pat = rf'(case ::psvitaalive::ColorTheme::{theme}:\s*ar=)0x[0-9A-Fa-f]+;\s*ag=0x[0-9A-Fa-f]+;\s*ab=0x[0-9A-Fa-f]+;'
    repl = rf'\g<1>0x{r:02X}; ag=0x{g:02X}; ab=0x{b:02X};'
    out, n = re.subn(pat, repl, src, count=1)
    if n != 1:
        raise SystemExit(f"RGB for {theme} not updated ({n})")
    return out

# Reds — spread across pink / pure / jewel / brown-red
cpp = set_rgb(cpp, "Crimson", 0xDC, 0x14, 0x3C)   # classic crimson
cpp = set_rgb(cpp, "Cherry",  0xFF, 0x2D, 0x6A)   # pink-cherry
cpp = set_rgb(cpp, "Ruby",    0x9B, 0x00, 0x2E)   # deep jewel (blue undertone)
cpp = set_rgb(cpp, "Maroon",  0x6B, 0x1E, 0x2A)   # brownish maroon
cpp = set_rgb(cpp, "Scarlet", 0xFF, 0x00, 0x00)   # pure fire red

# Greens — brand lime vs terminal matrix
cpp = set_rgb(cpp, "Matrix",  0x00, 0xFF, 0x66)   # phosphor / terminal green
# NeonLime stays 3BFF00 via default in switch

# Warm whites
cpp = set_rgb(cpp, "Ivory",     0xFF, 0xF5, 0xE0)  # cream
cpp = set_rgb(cpp, "Champagne", 0xE8, 0xC8, 0x8A)  # golden champagne (less pink-white)

# 3) applyColorTheme surfaces: split grouped cases so each identity is unique
# Replace the shared red groups and ivory/champagne and matrix/oled

old_oled = '''        case ::psvitaalive::ColorTheme::Oled:
        case ::psvitaalive::ColorTheme::Matrix:
            BG = RGBA8(0x00,0x00,0x00,255);
            SURFACE = RGBA8(0x0A,0x0A,0x0A,255);
            SURFACE2 = RGBA8(0x06,0x06,0x06,255);
            PANEL = RGBA8(0x03,0x03,0x03,255);
            BORDER = RGBA8(0x22,0x22,0x22,255);
            break;'''

new_oled = '''        case ::psvitaalive::ColorTheme::Oled:
            BG = RGBA8(0x00,0x00,0x00,255);
            SURFACE = RGBA8(0x0A,0x0A,0x0A,255);
            SURFACE2 = RGBA8(0x06,0x06,0x06,255);
            PANEL = RGBA8(0x03,0x03,0x03,255);
            BORDER = RGBA8(0x22,0x22,0x22,255);
            break;
        case ::psvitaalive::ColorTheme::Matrix:
            // Terminal phosphor: pure black + green-tinted chrome (not brand lime).
            BG = RGBA8(0x00,0x05,0x00,255);
            SURFACE = RGBA8(0x05,0x10,0x08,255);
            SURFACE2 = RGBA8(0x03,0x0C,0x06,255);
            PANEL = RGBA8(0x02,0x08,0x04,255);
            BORDER = RGBA8(0x10,0x38,0x1C,255);
            TEXT = RGBA8(0x8A,0xC8,0x9A,255);
            DIM = RGBA8(0x3A,0x6A,0x48,255);
            break;'''

if old_oled not in cpp:
    raise SystemExit("Oled/Matrix surface block not found")
cpp = cpp.replace(old_oled, new_oled, 1)

old_ivory = '''        case ::psvitaalive::ColorTheme::Ivory:
        case ::psvitaalive::ColorTheme::Champagne:
            BG = RGBA8(0x10,0x0E,0x0A,255);
            SURFACE = RGBA8(0x1E,0x1A,0x14,255);
            SURFACE2 = RGBA8(0x18,0x14,0x10,255);
            PANEL = RGBA8(0x14,0x12,0x0C,255);
            BORDER = RGBA8(0x44,0x3C,0x30,255);
            TEXT = RGBA8(0xD8,0xCC,0xB4,255);
            break;'''

new_ivory = '''        case ::psvitaalive::ColorTheme::Ivory:
            // Soft cream / paper — light warm gray surfaces
            BG = RGBA8(0x12,0x10,0x0E,255);
            SURFACE = RGBA8(0x22,0x1E,0x1A,255);
            SURFACE2 = RGBA8(0x1A,0x18,0x14,255);
            PANEL = RGBA8(0x16,0x14,0x12,255);
            BORDER = RGBA8(0x4A,0x42,0x38,255);
            TEXT = RGBA8(0xE0,0xD8,0xC8,255);
            DIM = RGBA8(0x90,0x86,0x78,255);
            break;
        case ::psvitaalive::ColorTheme::Champagne:
            // Golden toast — stronger gold/amber wash (not cream)
            BG = RGBA8(0x10,0x0C,0x06,255);
            SURFACE = RGBA8(0x20,0x18,0x0C,255);
            SURFACE2 = RGBA8(0x18,0x12,0x08,255);
            PANEL = RGBA8(0x14,0x0E,0x06,255);
            BORDER = RGBA8(0x50,0x3C,0x1C,255);
            TEXT = RGBA8(0xE0,0xC8,0x90,255);
            DIM = RGBA8(0x8A,0x72,0x48,255);
            break;'''

if old_ivory not in cpp:
    raise SystemExit("Ivory/Champagne surface block not found")
cpp = cpp.replace(old_ivory, new_ivory, 1)

old_crim = '''        case ::psvitaalive::ColorTheme::Crimson:
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
            break;'''

new_crim = '''        case ::psvitaalive::ColorTheme::Crimson:
            // Classic crimson — cool red-purple base
            BG = RGBA8(0x10,0x04,0x08,255);
            SURFACE = RGBA8(0x20,0x08,0x12,255);
            SURFACE2 = RGBA8(0x18,0x06,0x0E,255);
            PANEL = RGBA8(0x12,0x05,0x0A,255);
            BORDER = RGBA8(0x4C,0x14,0x28,255);
            break;
        case ::psvitaalive::ColorTheme::Cherry:
            // Candy cherry — warmer pink-red surfaces
            BG = RGBA8(0x12,0x06,0x0C,255);
            SURFACE = RGBA8(0x24,0x0E,0x18,255);
            SURFACE2 = RGBA8(0x1C,0x0A,0x12,255);
            PANEL = RGBA8(0x16,0x08,0x10,255);
            BORDER = RGBA8(0x58,0x20,0x38,255);
            TEXT = RGBA8(0xE0,0xA0,0xB4,255);
            break;
        case ::psvitaalive::ColorTheme::Ruby:
            // Jewel ruby — very dark, slight blue-red
            BG = RGBA8(0x08,0x02,0x06,255);
            SURFACE = RGBA8(0x14,0x04,0x0C,255);
            SURFACE2 = RGBA8(0x0E,0x03,0x08,255);
            PANEL = RGBA8(0x0A,0x02,0x06,255);
            BORDER = RGBA8(0x38,0x08,0x18,255);
            break;
        case ::psvitaalive::ColorTheme::Maroon:
            // Brown-maroon — earthy, less saturated
            BG = RGBA8(0x0C,0x06,0x06,255);
            SURFACE = RGBA8(0x18,0x0C,0x0C,255);
            SURFACE2 = RGBA8(0x12,0x0A,0x0A,255);
            PANEL = RGBA8(0x0E,0x08,0x08,255);
            BORDER = RGBA8(0x3A,0x1E,0x1E,255);
            TEXT = RGBA8(0xC0,0x98,0x98,255);
            DIM = RGBA8(0x70,0x50,0x50,255);
            break;'''

if old_crim not in cpp:
    raise SystemExit("Crimson/Cherry/Ruby/Maroon surface block not found")
cpp = cpp.replace(old_crim, new_crim, 1)

# Explicit NeonLime surface: keep neutral dark (brand) so Matrix never matches
# Ensure default path for NeonLime is neutral — already default: break;
# Add explicit case before default if missing
if "case ::psvitaalive::ColorTheme::NeonLime:" not in cpp.split("void applyColorTheme")[1].split("ACCENT = RGBA8")[0]:
    cpp = cpp.replace(
        "        default:\n            break;\n    }\n\n    ACCENT = RGBA8(ar, ag, ab, 255);",
        """        case ::psvitaalive::ColorTheme::NeonLime:
            // Brand PSVitaAlive — neutral near-black (not green-tinted like Matrix)
            BG = RGBA8(0x0A,0x0A,0x0A,255);
            SURFACE = RGBA8(0x1A,0x1A,0x1A,255);
            SURFACE2 = RGBA8(0x12,0x12,0x14,255);
            PANEL = RGBA8(0x0E,0x0E,0x10,255);
            BORDER = RGBA8(0x2A,0x2A,0x2E,255);
            TEXT = RGBA8(0xAA,0xAA,0xAA,255);
            DIM = RGBA8(0x66,0x66,0x6A,255);
            break;
        default:
            break;
    }

    ACCENT = RGBA8(ar, ag, ab, 255);""",
        1,
    )

CPP.write_text(cpp, encoding="utf-8")
print("OK theme differentiation applied")

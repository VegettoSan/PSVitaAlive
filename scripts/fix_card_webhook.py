from pathlib import Path

p = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
text = p.read_text(encoding="utf-8")
changed = 0

def rep(old, new, label):
    global text, changed
    if old in text:
        text = text.replace(old, new)
        changed += 1
        print(label + ": replaced")
        return True
    return False

old_wh = (
    'constexpr const char* kDataRequestWebhookUrl =\n'
    '    "https://discord.com/api/webhooks/1540832184774959268/"\n'
    '    "XPinil0HHmwzje7MOMXjXi0iQEHf7lHQtmZZILre3AbXMTxRLnObpYwX5yGhqzrdROWr";'
)
new_wh = (
    'constexpr const char* kDataRequestWebhookUrl =\n'
    '    "https://discord.com/api/webhooks/1544111419895840772/"\n'
    '    "hoqpAh5rNz_lt6-T7UroKCwPBRTRZ1RtXlGlsruyO3T--7yIk3jgx_ml0y2OuC9Bgqef";'
)
if not rep(old_wh, new_wh, "webhook"):
    if "1544111419895840772" in text:
        print("webhook: already new")
    else:
        raise SystemExit("webhook: FAIL")

old_marq = (
    "void drawMarqueeText(vita2d_pgf* font, int x, int y, int maxW, unsigned color, float scale,\n"
    "                     const std::string& text, bool animate) {\n"
    "    if (!font || text.empty() || maxW <= 8) return;\n"
    "    const int tw = vita2d_pgf_text_width(font, scale, text.c_str());\n"
    "    vita2d_enable_clipping();\n"
    "    vita2d_set_clip_rectangle(x, y - 22, x + maxW, y + 8);\n"
    "    if (!animate || tw <= maxW) {\n"
    "        vita2d_pgf_draw_text(font, x, y, color, scale, text.c_str());\n"
    "        vita2d_disable_clipping();\n"
    "        return;\n"
    "    }"
)
new_marq = (
    "void drawMarqueeText(vita2d_pgf* font, int x, int y, int maxW, unsigned color, float scale,\n"
    "                     const std::string& text, bool animate) {\n"
    "    if (!font || text.empty() || maxW <= 8) return;\n"
    "    const int tw = vita2d_pgf_text_width(font, scale, text.c_str());\n"
    "    // Always clip so glyphs never bleed past the card/panel edge.\n"
    "    vita2d_enable_clipping();\n"
    "    vita2d_set_clip_rectangle(x, y - 22, x + maxW, y + 8);\n"
    "    if (tw <= maxW) {\n"
    "        vita2d_pgf_draw_text(font, x, y, color, scale, text.c_str());\n"
    "        vita2d_disable_clipping();\n"
    "        return;\n"
    "    }\n"
    "    if (!animate) {\n"
    "        // Static: shrink with \"...\" so long names never overflow the card.\n"
    "        std::string s = text;\n"
    "        const char* dots = \"...\";\n"
    "        const int dotsW = vita2d_pgf_text_width(font, scale, dots);\n"
    "        while (s.size() > 1 && vita2d_pgf_text_width(font, scale, s.c_str()) + dotsW > maxW)\n"
    "            s.pop_back();\n"
    "        while (!s.empty() && (unsigned char)s.back() < 0x80 && (s.back() == ' ' || s.back() == '.'))\n"
    "            s.pop_back();\n"
    "        s += dots;\n"
    "        vita2d_pgf_draw_text(font, x, y, color, scale, s.c_str());\n"
    "        vita2d_disable_clipping();\n"
    "        return;\n"
    "    }"
)
if not rep(old_marq, new_marq, "marquee"):
    if "Static: shrink" in text:
        print("marquee: already fixed")
    else:
        raise SystemExit("marquee: FAIL")

old_name = (
    "    int tx = x + is + 20 + ox;\n"
    "    {\n"
    "        const float nameSc = focus ? 0.90f : 0.84f;\n"
    "        const int nameMaxW = std::max(40, (x + ox + ww) - tx - 10);\n"
    "        drawMarqueeText(font_, tx, y + 25 + oy, nameMaxW, WHITE, nameSc, it.name, focus);\n"
    "    }"
)
new_name = (
    "    int tx = x + is + 20 + ox;\n"
    "    {\n"
    "        const float nameSc = focus ? 0.90f : 0.84f;\n"
    "        // Leave room on the right for Game Files / Data Files chips so the title never overlaps them.\n"
    "        int rightPad = 12;\n"
    "        if (itemHasLinkType(it, \"game files\") || itemHasLinkType(it, \"data files\")\n"
    "            || itemHasLinkType(it, \"game file\") || itemHasLinkType(it, \"data file\")) {\n"
    "            rightPad = 118; // ~\"Game Files\" chip width + margin\n"
    "        }\n"
    "        const int nameMaxW = std::max(40, (x + ox + ww) - tx - rightPad);\n"
    "        drawMarqueeText(font_, tx, y + 25 + oy, nameMaxW, WHITE, nameSc, it.name, focus);\n"
    "    }"
)
if not rep(old_name, new_name, "nameMaxW"):
    if "rightPad = 118" in text:
        print("nameMaxW: already fixed")
    else:
        raise SystemExit("nameMaxW: FAIL")

p.write_text(text, encoding="utf-8")
print("changed", changed, "size", p.stat().st_size)

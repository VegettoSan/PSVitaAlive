#!/usr/bin/env python3
"""Split cards, detail panel, G/D Files chip, UX0 footer — larger type + room."""
from pathlib import Path

CPP = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
TYPES = Path("Client PSVitaAlive/include/ui/ui_types.hpp")
cpp = CPP.read_text(encoding="utf-8")
types = TYPES.read_text(encoding="utf-8")
orig_cpp, orig_types = cpp, types

# --- ui_types: slightly taller footer for UX0 panel ---
if "constexpr int FOOTER_H = 48;" in types:
    types = types.replace("constexpr int FOOTER_H = 48;", "constexpr int FOOTER_H = 54;", 1)
elif "constexpr int FOOTER_H = 54;" not in types:
    raise SystemExit("FOOTER_H not found")

# --- Split card taller so text is not cramped ---
old_const = (
    "constexpr int FULL_CARD_H=136,SPLIT_CARD_H=94,DETAIL_HEADER_H=100,"
    "LINE_H=21,DETAIL_SECTION_H=28,DETAIL_META_H=24,DETAIL_SECTION_GAP=16,"
    "TRANSITION_MS=340,LINK_ROW_H=42,LINK_GAP=6,SCREENSHOT_ROW_H=250;"
)
new_const = (
    "constexpr int FULL_CARD_H=136,SPLIT_CARD_H=118,DETAIL_HEADER_H=108,"
    "LINE_H=24,DETAIL_SECTION_H=30,DETAIL_META_H=28,DETAIL_SECTION_GAP=16,"
    "TRANSITION_MS=340,LINK_ROW_H=46,LINK_GAP=6,SCREENSHOT_ROW_H=250;"
)
if old_const not in cpp:
    # try current values flexibly
    import re
    m = re.search(r"constexpr int FULL_CARD_H=\d+,SPLIT_CARD_H=\d+,DETAIL_HEADER_H=\d+,LINE_H=\d+,DETAIL_SECTION_H=\d+,DETAIL_META_H=\d+,DETAIL_SECTION_GAP=\d+,TRANSITION_MS=340,LINK_ROW_H=\d+,LINK_GAP=6,SCREENSHOT_ROW_H=250;", cpp)
    if not m:
        raise SystemExit("layout constants not found")
    cpp = cpp[:m.start()] + new_const + cpp[m.end():]
else:
    cpp = cpp.replace(old_const, new_const, 1)

# LINK_SECTION_H + INSTALL_ALL_BLOCK_H
cpp = cpp.replace("static const int LINK_SECTION_H = 20;", "static const int LINK_SECTION_H = 26;", 1)
cpp = cpp.replace("static const int INSTALL_ALL_BLOCK_H = 58;", "static const int INSTALL_ALL_BLOCK_H = 68;", 1)

# --- G/D Files filter chip in header ---
old_gd = """    const int gdW = 92;
    const int clockReserve = 92;
"""
new_gd = """    const int gdW = 118;
    const int clockReserve = 92;
"""
if old_gd not in cpp:
    raise SystemExit("gdW not found")
cpp = cpp.replace(old_gd, new_gd, 1)

old_gd_lab = """        const char* lab = "G/D Files";
        const float sc = 0.52f;
        const int tw = vita2d_pgf_text_width(font_, sc, lab);
        vita2d_pgf_draw_text(font_, gdX + (gdW - tw) / 2, barY + 21, folderText, sc, lab);
"""
new_gd_lab = """        const char* lab = "G/D Files";
        const float sc = 0.70f;
        const int tw = vita2d_pgf_text_width(font_, sc, lab);
        vita2d_pgf_draw_text(font_, gdX + (gdW - tw) / 2, barY + 22, folderText, sc, lab);
"""
if old_gd_lab not in cpp:
    raise SystemExit("G/D Files label not found")
cpp = cpp.replace(old_gd_lab, new_gd_lab, 1)

# --- Split/full card: adaptive vertical layout for compact height ---
old_card = """    int is = h >= 110 ? 80 : 58;
    drawImage(!it.icon.empty() ? it.icon : it.cover, "app", x + 10 + ox, y + 10 + oy, is, is);
    int tx = x + is + 18 + ox;
    {
        const float nameSc = focus ? 0.98f : 0.92f;
        // Reserve right side for Game/Data Files chips + size so title never underlaps.
        int rightPad = 16;
        if (itemHasLinkType(it, "game files") || itemHasLinkType(it, "data files")
            || itemHasLinkType(it, "game file") || itemHasLinkType(it, "data file")) {
            rightPad = 110;
        }
        const int nameMaxW = std::max(40, (x + ox + ww) - tx - rightPad);
        drawMarqueeText(font_, tx, y + 28 + oy, nameMaxW, WHITE, nameSc, it.name, focus);
    }
    vita2d_pgf_draw_text(font_, tx, y + 50 + oy, TEXT, 0.82f, ellipsize(it.author.empty() ? "Unknown author" : it.author, 18).c_str());
    vita2d_pgf_draw_text(font_, tx, y + 72 + oy, colorForStatus(it.status), 0.80f, ellipsize(it.status, 14).c_str());
    // Version / date bottom-left; size always bottom-right when known (all catalogs).
    std::string meta = (it.version.empty() ? "" : "v" + it.version) + (it.versionDate.empty() ? "" : "  " + it.versionDate);
    if (!meta.empty())
        vita2d_pgf_draw_text(font_, x + 10 + ox, y + h - 12 + oy, DIM, 0.72f, ellipsize(meta, 18).c_str());
"""
new_card = """    const bool compact = h < 125;
    int is = compact ? 64 : 80;
    drawImage(!it.icon.empty() ? it.icon : it.cover, "app", x + 10 + ox, y + (compact ? 8 : 10) + oy, is, is);
    int tx = x + is + (compact ? 14 : 18) + ox;
    {
        const float nameSc = compact ? (focus ? 0.90f : 0.84f) : (focus ? 0.98f : 0.92f);
        // Reserve right side for Game/Data Files chips + size so title never underlaps.
        int rightPad = 16;
        if (itemHasLinkType(it, "game files") || itemHasLinkType(it, "data files")
            || itemHasLinkType(it, "game file") || itemHasLinkType(it, "data file")) {
            rightPad = compact ? 100 : 110;
        }
        const int nameMaxW = std::max(40, (x + ox + ww) - tx - rightPad);
        drawMarqueeText(font_, tx, y + (compact ? 24 : 28) + oy, nameMaxW, WHITE, nameSc, it.name, focus);
    }
    vita2d_pgf_draw_text(font_, tx, y + (compact ? 44 : 50) + oy, TEXT, compact ? 0.74f : 0.82f,
        ellipsize(it.author.empty() ? "Unknown author" : it.author, compact ? 16 : 18).c_str());
    vita2d_pgf_draw_text(font_, tx, y + (compact ? 64 : 72) + oy, colorForStatus(it.status), compact ? 0.72f : 0.80f,
        ellipsize(it.status, 14).c_str());
    // Version / date bottom-left; size always bottom-right when known (all catalogs).
    std::string meta = (it.version.empty() ? "" : "v" + it.version) + (it.versionDate.empty() ? "" : "  " + it.versionDate);
    if (!meta.empty())
        vita2d_pgf_draw_text(font_, x + 10 + ox, y + h - (compact ? 10 : 12) + oy, DIM, compact ? 0.66f : 0.72f, ellipsize(meta, compact ? 16 : 18).c_str());
"""
if old_card not in cpp:
    raise SystemExit("card layout block not found")
cpp = cpp.replace(old_card, new_card, 1)

# Folder chips a bit larger still on cards
cpp = cpp.replace(
    "                const float sc = 0.70f;\n                const int tw = vita2d_pgf_text_width(font_, sc, label.c_str());\n                const int padX = 7;\n                const int cw = tw + padX * 2;\n                const int ch = 22;",
    "                const float sc = 0.76f;\n                const int tw = vita2d_pgf_text_width(font_, sc, label.c_str());\n                const int padX = 8;\n                const int cw = tw + padX * 2;\n                const int ch = 24;",
    1,
)

# --- Detail links: section + INSTALL ALL + rows ---
cpp = cpp.replace(
    '        vita2d_pgf_draw_text(font_, x + 4, y + 14, ACCENT, 0.64f, "INSTALL ALL");',
    '        vita2d_pgf_draw_text(font_, x + 4, y + 18, ACCENT, 0.76f, "INSTALL ALL");',
    1,
)
cpp = cpp.replace(
    '        vita2d_pgf_draw_text(font_, x + 12, by + 20, tc, 0.70f, "INSTALL ALL");\n        vita2d_pgf_draw_text(font_, x + 12, by + 42, sub, 0.56f,\n            "Install app + Game/Data Files from scratch");',
    '        vita2d_pgf_draw_text(font_, x + 12, by + 24, tc, 0.84f, "INSTALL ALL");\n        vita2d_pgf_draw_text(font_, x + 12, by + 48, sub, 0.66f,\n            "Install app + Game/Data Files from scratch");',
    1,
)
cpp = cpp.replace(
    "            vita2d_pgf_draw_text(font_, x + 4, ry + 14, ACCENT, 0.64f, linkSectionTitle(row.section));",
    "            vita2d_pgf_draw_text(font_, x + 4, ry + 18, ACCENT, 0.76f, linkSectionTitle(row.section));",
    1,
)
cpp = cpp.replace(
    "{ const int titleMaxW = std::max(40, w - 20 - badgeW - 8); drawMarqueeText(font_, x + 10, ry + 15, titleMaxW, mc, 0.72f, title, f); }\n        std::string meta = linkSectionMetaLabel(row.section);\n        if (!sizeLabel.empty()) meta += \"  •  \" + sizeLabel;\n        if (can) meta += f ? \"  •  X: install\" : \"  •  X\";\n        vita2d_pgf_draw_text(font_, x + 10, ry + 31, f ? BG : DIM, 0.64f, ellipsize(meta, badgeW ? 28 : 42).c_str());\n        if (l.recommended) {\n            const int bx = x + w - badgeW - 8, by = ry + 8;\n            vita2d_pgf_draw_text(font_, bx, ry + 16, f ? BG : ACCENT, 0.58f, \"Recommended\");",
    "{ const int titleMaxW = std::max(40, w - 20 - badgeW - 8); drawMarqueeText(font_, x + 10, ry + 17, titleMaxW, mc, 0.80f, title, f); }\n        std::string meta = linkSectionMetaLabel(row.section);\n        if (!sizeLabel.empty()) meta += \"  •  \" + sizeLabel;\n        if (can) meta += f ? \"  •  X: install\" : \"  •  X\";\n        vita2d_pgf_draw_text(font_, x + 10, ry + 35, f ? BG : DIM, 0.70f, ellipsize(meta, badgeW ? 26 : 40).c_str());\n        if (l.recommended) {\n            const int bx = x + w - badgeW - 8, by = ry + 8;\n            vita2d_pgf_draw_text(font_, bx, ry + 18, f ? BG : ACCENT, 0.66f, \"Recommended\");",
    1,
)

# --- Detail content: section headers, body, meta rows ---
old_sec = """    auto drawSectionHeader = [&](int sx, int sy, const char* title) {
        if (sy + DETAIL_SECTION_H < top || sy > bottom) return;
        vita2d_pgf_draw_text(font_, sx, sy + 16, ACCENT, 0.56f, title);
        vita2d_draw_rectangle(sx, sy + DETAIL_SECTION_H - 4, cw, 1, BORDER);
        vita2d_draw_rectangle(sx, sy + DETAIL_SECTION_H - 4, 48, 1, ACCENT);
    };

    auto drawBody = [&](int sx, int sy, const std::vector<std::string>& lines) {
        int dy = sy;
        for (const auto& line : lines) {
            if (dy >= top - LINE_H && dy <= bottom + LINE_H) {
                vita2d_pgf_draw_text(font_, sx, dy + 14, TEXT, 0.58f, line.c_str());
            }
            dy += LINE_H;
        }
        return dy;
    };

    auto drawMetaRow = [&](int sx, int sy, int rowW, const char* label, const std::string& value) {
        if (value.empty()) return sy;
        if (sy >= top - DETAIL_META_H && sy <= bottom + DETAIL_META_H) {
            vita2d_pgf_draw_text(font_, sx + 10, sy + 15, DIM, 0.50f, label);
            const int lw = vita2d_pgf_text_width(font_, 0.50f, label);
            const int vx = sx + std::max(108, lw + 18);
            const int maxChars = std::max(8, (rowW - (vx - sx) - 12) / 7);
            vita2d_pgf_draw_text(font_, vx, sy + 15, WHITE, 0.54f, ellipsize(value, maxChars).c_str());
        }
        return sy + DETAIL_META_H;
    };
"""
new_sec = """    auto drawSectionHeader = [&](int sx, int sy, const char* title) {
        if (sy + DETAIL_SECTION_H < top || sy > bottom) return;
        vita2d_pgf_draw_text(font_, sx, sy + 18, ACCENT, 0.72f, title);
        vita2d_draw_rectangle(sx, sy + DETAIL_SECTION_H - 4, cw, 1, BORDER);
        vita2d_draw_rectangle(sx, sy + DETAIL_SECTION_H - 4, 56, 1, ACCENT);
    };

    auto drawBody = [&](int sx, int sy, const std::vector<std::string>& lines) {
        int dy = sy;
        for (const auto& line : lines) {
            if (dy >= top - LINE_H && dy <= bottom + LINE_H) {
                vita2d_pgf_draw_text(font_, sx, dy + 16, TEXT, 0.68f, line.c_str());
            }
            dy += LINE_H;
        }
        return dy;
    };

    auto drawMetaRow = [&](int sx, int sy, int rowW, const char* label, const std::string& value) {
        if (value.empty()) return sy;
        if (sy >= top - DETAIL_META_H && sy <= bottom + DETAIL_META_H) {
            vita2d_pgf_draw_text(font_, sx + 10, sy + 18, DIM, 0.64f, label);
            const int lw = vita2d_pgf_text_width(font_, 0.64f, label);
            const int vx = sx + std::max(120, lw + 20);
            const int maxChars = std::max(8, (rowW - (vx - sx) - 12) / 8);
            vita2d_pgf_draw_text(font_, vx, sy + 18, WHITE, 0.68f, ellipsize(value, maxChars).c_str());
        }
        return sy + DETAIL_META_H;
    };
"""
if old_sec not in cpp:
    raise SystemExit("detail section/body/meta not found")
cpp = cpp.replace(old_sec, new_sec, 1)

# Detail header title/author/meta
old_dh = """    drawImage(!it.icon.empty() ? it.icon : it.cover, "app", x + 12, y + 12, 68, 68);
    // Leave room for active-panel label chip on the left when focused
    const int titleX = active ? x + 100 : x + 92;
    {
        // Leave room for Select-links / Request-data buttons on the right.
        const int titleMaxW = std::max(60, (x + w - 150) - titleX);
        drawMarqueeText(font_, titleX, y + 29, titleMaxW, WHITE, 0.92f, it.name, active);
    }
    vita2d_pgf_draw_text(font_, titleX, y + 50, TEXT, 0.74f, ellipsize(it.author, 20).c_str());
    std::string meta = (it.version.empty() ? "" : "v" + it.version) + (it.versionDate.empty() ? "" : "  " + it.versionDate);
    vita2d_pgf_draw_text(font_, titleX, y + 70, colorForStatus(it.status), 0.60f, ellipsize(meta.empty() ? it.status : meta, 22).c_str());
"""
new_dh = """    drawImage(!it.icon.empty() ? it.icon : it.cover, "app", x + 12, y + 14, 72, 72);
    // Leave room for active-panel label chip on the left when focused
    const int titleX = active ? x + 100 : x + 96;
    {
        // Leave room for Select-links / Request-data buttons on the right.
        const int titleMaxW = std::max(60, (x + w - 160) - titleX);
        drawMarqueeText(font_, titleX, y + 32, titleMaxW, WHITE, 1.00f, it.name, active);
    }
    vita2d_pgf_draw_text(font_, titleX, y + 56, TEXT, 0.80f, ellipsize(it.author, 18).c_str());
    std::string meta = (it.version.empty() ? "" : "v" + it.version) + (it.versionDate.empty() ? "" : "  " + it.versionDate);
    vita2d_pgf_draw_text(font_, titleX, y + 78, colorForStatus(it.status), 0.70f, ellipsize(meta.empty() ? it.status : meta, 20).c_str());
"""
if old_dh not in cpp:
    raise SystemExit("detail header not found")
cpp = cpp.replace(old_dh, new_dh, 1)

# Select links / Request data buttons larger
old_btn = """        const int bx = x + w - 142, by = y + 12, bw = 128, bh = 28;
"""
new_btn = """        const int bx = x + w - 156, by = y + 12, bw = 142, bh = 32;
"""
if old_btn not in cpp:
    raise SystemExit("select links btn not found")
cpp = cpp.replace(old_btn, new_btn, 1)

cpp = cpp.replace(
    '            vita2d_pgf_draw_text(font_, bx + 10, by + 19, linkOn ? BG : ACCENT, 0.64f, linkOn ? "△ Exit link mode" : "△ Select links");',
    '            vita2d_pgf_draw_text(font_, bx + 8, by + 22, linkOn ? BG : ACCENT, 0.72f, linkOn ? "△ Exit link mode" : "△ Select links");',
    1,
)
cpp = cpp.replace(
    "            const int rby = !it.linkDetails.empty() ? (by + bh + 6) : by;\n            const int rbx = bx, rbw = bw, rbh = 30;",
    "            const int rby = !it.linkDetails.empty() ? (by + bh + 6) : by;\n            const int rbx = bx, rbw = bw, rbh = 32;",
    1,
)
cpp = cpp.replace(
    '            vita2d_pgf_draw_text(font_, rbx + 6, rby + 20, REQ, 0.64f, "□ Request data");',
    '            vita2d_pgf_draw_text(font_, rbx + 8, rby + 22, REQ, 0.72f, "□ Request data");',
    1,
)

# --- UX0 footer panel: larger type + clearer layout ---
old_ux0 = """    const Ux0SpaceInfo sp = queryUx0Space();
    // Wider panel + larger type for readability on real Vita screens.
    const int panelW = 220;
    const int panelH = FOOTER_H - 6;
    const int panelX = SCREEN_W - panelW - 6;
    const int panelY = SCREEN_H - FOOTER_H + 3;
    vita2d_draw_rectangle(panelX, panelY, panelW, panelH, SURFACE);
    vita2d_draw_rectangle(panelX, panelY, 3, panelH, ACCENT);

    if (!sp.ok) {
        vita2d_pgf_draw_text(font, panelX + 12, panelY + 20, DIM, 0.58f, "ux0 n/a");
        return;
    }
    vita2d_pgf_draw_text(font, panelX + 10, panelY + 15, ACCENT, 0.56f, "UX0");
    char line[48];
    sceClibSnprintf(line, sizeof(line), "%s free", formatBytesShort(sp.freeBytes).c_str());
    vita2d_pgf_draw_text(font, panelX + 48, panelY + 15, WHITE, 0.56f, line);
    sceClibSnprintf(line, sizeof(line), "of %s total", formatBytesShort(sp.totalBytes).c_str());
    vita2d_pgf_draw_text(font, panelX + 10, panelY + 30, TEXT, 0.52f, line);

    const int barX = panelX + 10, barY = panelY + panelH - 8, barW = panelW - 20, barH = 5;
    vita2d_draw_rectangle(barX, barY, barW, barH, BORDER);
    float used = 0.f;
    if (sp.totalBytes > 0)
        used = 1.f - (float)((double)sp.freeBytes / (double)sp.totalBytes);
    if (used < 0.f) used = 0.f;
    if (used > 1.f) used = 1.f;
    const unsigned fill = used > 0.90f ? RGBA8(0xE0, 0x32, 0x32, 255)
                        : (used > 0.75f ? RGBA8(0xFF, 0xB0, 0x20, 255) : ACCENT);
    vita2d_draw_rectangle(barX, barY, std::max(1, (int)(barW * used)), barH, fill);
}
"""
new_ux0 = """    const Ux0SpaceInfo sp = queryUx0Space();
    // Wider panel + larger type for readability on real Vita screens.
    const int panelW = 250;
    const int panelH = FOOTER_H - 4;
    const int panelX = SCREEN_W - panelW - 4;
    const int panelY = SCREEN_H - FOOTER_H + 2;
    vita2d_draw_rectangle(panelX, panelY, panelW, panelH, SURFACE);
    vita2d_draw_rectangle(panelX, panelY, 3, panelH, ACCENT);

    if (!sp.ok) {
        vita2d_pgf_draw_text(font, panelX + 12, panelY + 22, DIM, 0.68f, "ux0 n/a");
        return;
    }
    // Line 1: UX0 + free space (primary info)
    vita2d_pgf_draw_text(font, panelX + 10, panelY + 18, ACCENT, 0.70f, "UX0");
    char line[48];
    sceClibSnprintf(line, sizeof(line), "%s free", formatBytesShort(sp.freeBytes).c_str());
    vita2d_pgf_draw_text(font, panelX + 52, panelY + 18, WHITE, 0.70f, line);
    // Line 2: total capacity
    sceClibSnprintf(line, sizeof(line), "of %s total", formatBytesShort(sp.totalBytes).c_str());
    vita2d_pgf_draw_text(font, panelX + 10, panelY + 34, TEXT, 0.62f, line);

    const int barX = panelX + 10, barY = panelY + panelH - 10, barW = panelW - 20, barH = 6;
    vita2d_draw_rectangle(barX, barY, barW, barH, BORDER);
    float used = 0.f;
    if (sp.totalBytes > 0)
        used = 1.f - (float)((double)sp.freeBytes / (double)sp.totalBytes);
    if (used < 0.f) used = 0.f;
    if (used > 1.f) used = 1.f;
    const unsigned fill = used > 0.90f ? RGBA8(0xE0, 0x32, 0x32, 255)
                        : (used > 0.75f ? RGBA8(0xFF, 0xB0, 0x20, 255) : ACCENT);
    vita2d_draw_rectangle(barX, barY, std::max(1, (int)(barW * used)), barH, fill);
}
"""
if old_ux0 not in cpp:
    raise SystemExit("UX0 panel not found")
cpp = cpp.replace(old_ux0, new_ux0, 1)

if cpp == orig_cpp and types == orig_types:
    raise SystemExit("no changes")
CPP.write_text(cpp, encoding="utf-8")
TYPES.write_text(types, encoding="utf-8")
print("OK detail/split/UX0/G-D readability applied")

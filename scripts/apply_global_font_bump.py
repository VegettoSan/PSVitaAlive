#!/usr/bin/env python3
"""Global readability: larger type + roomier cards/modals without overlap."""
from pathlib import Path

CPP = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
cpp = CPP.read_text(encoding="utf-8")
orig = cpp

# --- Layout constants: taller cards so larger text does not collide ---
old_const = (
    "constexpr int FULL_CARD_H=120,SPLIT_CARD_H=82,DETAIL_HEADER_H=92,"
    "LINE_H=18,DETAIL_SECTION_H=26,DETAIL_META_H=22,DETAIL_SECTION_GAP=14,"
    "TRANSITION_MS=340,LINK_ROW_H=38,LINK_GAP=6,SCREENSHOT_ROW_H=250;"
)
new_const = (
    "constexpr int FULL_CARD_H=136,SPLIT_CARD_H=94,DETAIL_HEADER_H=100,"
    "LINE_H=21,DETAIL_SECTION_H=28,DETAIL_META_H=24,DETAIL_SECTION_GAP=16,"
    "TRANSITION_MS=340,LINK_ROW_H=42,LINK_GAP=6,SCREENSHOT_ROW_H=250;"
)
if old_const not in cpp:
    raise SystemExit("layout constants not found")
cpp = cpp.replace(old_const, new_const, 1)

# --- Catalog card: larger type + more vertical room + wider right pad for chips ---
old_card_text = """    int is = h >= 100 ? 76 : 54;
    drawImage(!it.icon.empty() ? it.icon : it.cover, "app", x + 10 + ox, y + 9 + oy, is, is);
    int tx = x + is + 20 + ox;
    {
        const float nameSc = focus ? 0.90f : 0.84f;
        // Leave a modest right margin so long titles stay readable but do not spill
        // under the Game Files / Data Files chips (chips sit mid/lower-right).
        int rightPad = 12;
        if (itemHasLinkType(it, "game files") || itemHasLinkType(it, "data files")
            || itemHasLinkType(it, "game file") || itemHasLinkType(it, "data file")) {
            rightPad = 64; // enough clearance; title keeps most of the row
        }
        const int nameMaxW = std::max(40, (x + ox + ww) - tx - rightPad);
        drawMarqueeText(font_, tx, y + 25 + oy, nameMaxW, WHITE, nameSc, it.name, focus);
    }
    vita2d_pgf_draw_text(font_, tx, y + 45 + oy, TEXT, 0.74f, ellipsize(it.author.empty() ? "Unknown author" : it.author, 20).c_str());
    vita2d_pgf_draw_text(font_, tx, y + 64 + oy, colorForStatus(it.status), 0.72f, ellipsize(it.status, 16).c_str());
"""
new_card_text = """    int is = h >= 110 ? 80 : 58;
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
"""
if old_card_text not in cpp:
    raise SystemExit("card text block not found")
cpp = cpp.replace(old_card_text, new_card_text, 1)

# Card meta (version/date) scale
cpp = cpp.replace(
    "vita2d_pgf_draw_text(font_, x + 10 + ox, y + h - 10 + oy, DIM, 0.66f, ellipsize(meta, 20).c_str());",
    "vita2d_pgf_draw_text(font_, x + 10 + ox, y + h - 12 + oy, DIM, 0.72f, ellipsize(meta, 18).c_str());",
    1,
)

# Size chip + folder chip slightly larger
old_size_chip = """            auto drawSizeChip = [&](const std::string& label) {
                if (label.empty()) return;
                const float sc = 0.66f;
                const int tw = vita2d_pgf_text_width(font_, sc, label.c_str());
                const int padX = 5;
                const int cw = tw + padX * 2;
                const int ch = 17;
                const int sx = right - cw - 6;
                const int cy = sy - ch + 3;
                vita2d_draw_rectangle(sx, cy, cw, ch, SURFACE2);
                vita2d_pgf_draw_text(font_, sx + padX, sy, TEXT, sc, label.c_str());
                sy -= 19;
            };
"""
new_size_chip = """            auto drawSizeChip = [&](const std::string& label) {
                if (label.empty()) return;
                const float sc = 0.70f;
                const int tw = vita2d_pgf_text_width(font_, sc, label.c_str());
                const int padX = 6;
                const int cw = tw + padX * 2;
                const int ch = 19;
                const int sx = right - cw - 6;
                const int cy = sy - ch + 3;
                vita2d_draw_rectangle(sx, cy, cw, ch, SURFACE2);
                vita2d_pgf_draw_text(font_, sx + padX, sy, TEXT, sc, label.c_str());
                sy -= 21;
            };
"""
if old_size_chip not in cpp:
    raise SystemExit("size chip not found")
cpp = cpp.replace(old_size_chip, new_size_chip, 1)

old_folder = """            auto drawFolderChip = [&](const std::string& label) {
                if (label.empty()) return;
                const float sc = 0.66f;
                const int tw = vita2d_pgf_text_width(font_, sc, label.c_str());
                const int padX = 7;
                const int cw = tw + padX * 2;
                const int ch = 20;
                const int sx = right - cw - 6;
                const int cy = sy - ch + 4;
                const unsigned folderBg = RGBA8(0x3A, 0x2C, 0x10, 255);
                const unsigned folderEdge = RGBA8(0xE8, 0xB4, 0x3A, 255);
                const unsigned folderText = RGBA8(0xFF, 0xD2, 0x6A, 255);
                vita2d_draw_rectangle(sx, cy, cw, ch, folderBg);
                vita2d_draw_rectangle(sx, cy, cw, 2, folderEdge); // top tab highlight
                vita2d_draw_rectangle(sx, cy, 2, ch, folderEdge);
                vita2d_pgf_draw_text(font_, sx + padX, sy + 1, folderText, sc, label.c_str());
                sy -= 22;
            };
"""
new_folder = """            auto drawFolderChip = [&](const std::string& label) {
                if (label.empty()) return;
                const float sc = 0.70f;
                const int tw = vita2d_pgf_text_width(font_, sc, label.c_str());
                const int padX = 7;
                const int cw = tw + padX * 2;
                const int ch = 22;
                const int sx = right - cw - 6;
                const int cy = sy - ch + 4;
                const unsigned folderBg = RGBA8(0x3A, 0x2C, 0x10, 255);
                const unsigned folderEdge = RGBA8(0xE8, 0xB4, 0x3A, 255);
                const unsigned folderText = RGBA8(0xFF, 0xD2, 0x6A, 255);
                vita2d_draw_rectangle(sx, cy, cw, ch, folderBg);
                vita2d_draw_rectangle(sx, cy, cw, 2, folderEdge); // top tab highlight
                vita2d_draw_rectangle(sx, cy, 2, ch, folderEdge);
                vita2d_pgf_draw_text(font_, sx + padX, sy + 1, folderText, sc, label.c_str());
                sy -= 24;
            };
"""
if old_folder not in cpp:
    raise SystemExit("folder chip not found")
cpp = cpp.replace(old_folder, new_folder, 1)

# Badge label slightly larger
cpp = cpp.replace(
    "            const float sc = 0.48f;\n            const int tw = vita2d_pgf_text_width(font_, sc, lab);",
    "            const float sc = 0.56f;\n            const int tw = vita2d_pgf_text_width(font_, sc, lab);",
    1,
)

# --- Header title ---
cpp = cpp.replace(
    'vita2d_pgf_draw_text(font_, 14, 28, ACCENT, 0.88f, "PSVitaAlive");',
    'vita2d_pgf_draw_text(font_, 14, 30, ACCENT, 0.98f, "PSVitaAlive");',
    1,
)

# Search placeholder slightly larger
cpp = cpp.replace(
    'vita2d_pgf_draw_text(font_, barX + 12, barY + 21, DIM, 0.58f, "Search...  (△)");',
    'vita2d_pgf_draw_text(font_, barX + 12, barY + 22, DIM, 0.66f, "Search...  (△)");',
    1,
)
cpp = cpp.replace(
    'vita2d_pgf_draw_text(font_, barX + 12, barY + 21, ACCENT, 0.56f, "FILTER");',
    'vita2d_pgf_draw_text(font_, barX + 12, barY + 22, ACCENT, 0.64f, "FILTER");',
    1,
)
cpp = cpp.replace(
    'vita2d_pgf_draw_text(font_, barX + 70, barY + 21, WHITE, 0.58f, ellipsize(searchQuery_, 22).c_str());',
    'vita2d_pgf_draw_text(font_, barX + 78, barY + 22, WHITE, 0.66f, ellipsize(searchQuery_, 20).c_str());',
    1,
)

# --- News overlay ---
old_news_hdr = """    vita2d_pgf_draw_text(font_, x + 24, y + 34, ACCENT, 0.62f, "NEWS");
    vita2d_pgf_draw_text(font_, x + 24, y + 64, WHITE, 0.78f,
                         ellipsize(newsTitle_, 64).c_str());

    const int textTop = y + 88;
    const int textBottom = y + h - 56;
    const int lineH = 22;
"""
new_news_hdr = """    vita2d_pgf_draw_text(font_, x + 24, y + 36, ACCENT, 0.74f, "NEWS");
    vita2d_pgf_draw_text(font_, x + 24, y + 68, WHITE, 0.92f,
                         ellipsize(newsTitle_, 52).c_str());

    const int textTop = y + 96;
    const int textBottom = y + h - 60;
    const int lineH = 26;
"""
if old_news_hdr not in cpp:
    raise SystemExit("news header not found")
cpp = cpp.replace(old_news_hdr, new_news_hdr, 1)

cpp = cpp.replace(
    'vita2d_pgf_draw_text(font_, x + w - 90, y + 34, DIM, 0.48f, scr);',
    'vita2d_pgf_draw_text(font_, x + w - 96, y + 36, DIM, 0.58f, scr);',
    1,
)
cpp = cpp.replace(
    '    vita2d_pgf_draw_text(font_, x + 24, y + h - 14, DIM, 0.60f, "D-Pad: scroll   Circle: close");\n}\n\nvoid FullCatalogScreen::drawReportChip()',
    '    vita2d_pgf_draw_text(font_, x + 24, y + h - 14, DIM, 0.66f, "D-Pad: scroll   Circle: close");\n}\n\nvoid FullCatalogScreen::drawReportChip()',
    1,
)

# News markdown base scales
old_md = """            float scale = 0.55f;
            int height = 22;
            int indent = 0;
            bool emphasize = false;
            if (pl.kind == news_md::Kind::H1) {
                scale = 0.88f; height = 32; emphasize = true;
            } else if (pl.kind == news_md::Kind::H2) {
                scale = 0.74f; height = 28; emphasize = true;
            } else if (pl.kind == news_md::Kind::H3) {
                scale = 0.64f; height = 24; emphasize = true;
            } else if (pl.kind == news_md::Kind::List) {
                scale = 0.55f; height = 22; indent = 12; emphasize = true;
            }
"""
new_md = """            float scale = 0.64f;
            int height = 26;
            int indent = 0;
            bool emphasize = false;
            if (pl.kind == news_md::Kind::H1) {
                scale = 0.98f; height = 36; emphasize = true;
            } else if (pl.kind == news_md::Kind::H2) {
                scale = 0.84f; height = 32; emphasize = true;
            } else if (pl.kind == news_md::Kind::H3) {
                scale = 0.74f; height = 28; emphasize = true;
            } else if (pl.kind == news_md::Kind::List) {
                scale = 0.64f; height = 26; indent = 12; emphasize = true;
            }
"""
if old_md not in cpp:
    raise SystemExit("news md scales not found")
cpp = cpp.replace(old_md, new_md, 1)

# News emphasize thresholds (matched to new scales)
cpp = cpp.replace(
    "                if (scale >= 0.80f) baseCol = ACCENT;\n                else if (scale >= 0.68f) baseCol = WHITE;",
    "                if (scale >= 0.90f) baseCol = ACCENT;\n                else if (scale >= 0.78f) baseCol = WHITE;",
    1,
)

# --- Install All confirm ---
old_ia = """    const int ow = 620, oh = 360;
    const int ox = (SCREEN_W - ow) / 2, oy = (SCREEN_H - oh) / 2;
    vita2d_draw_rectangle(ox, oy, ow, oh, SURFACE2);
    vita2d_draw_rectangle(ox, oy, ow, 3, ACCENT);
    vita2d_draw_rectangle(ox, oy + oh - 1, ow, 1, BORDER);
    vita2d_draw_rectangle(ox, oy, 1, oh, BORDER);
    vita2d_draw_rectangle(ox + ow - 1, oy, 1, oh, BORDER);

    if (installAllPhase_ == InstallAllPhase::Confirm) {
        vita2d_pgf_draw_text(font_, ox + 22, oy + 36, ACCENT, 0.78f, "Install All");
        vita2d_pgf_draw_text(font_, ox + 22, oy + 68, WHITE, 0.58f, ellipsize(item.name, 48).c_str());
        const char* lines[] = {
            "This installs the homebrew from scratch:",
            "1) App (VPK)  2) Game Files  3) Data Files",
            "You will pick one download source per step when needed.",
            "If you only want to update the app, use the VPK button instead.",
        };
        int ty = oy + 100;
        for (const char* ln : lines) {
            vita2d_pgf_draw_text(font_, ox + 22, ty, TEXT, 0.54f, ln);
            ty += 24;
        }
        const int bw = 200, bh = 40;
        const int by = oy + oh - 56;
        const int bxOk = ox + 28;
        const int bxCancel = ox + ow - 28 - bw;
        const bool fOk = installAllFocus_ == 0;
        const bool fCancel = installAllFocus_ == 1;
        vita2d_draw_rectangle(bxOk, by, bw, bh, fOk ? ACCENT : SURFACE2);
        vita2d_pgf_draw_text(font_, bxOk + 36, by + 26, fOk ? BG : WHITE, 0.62f, "Continue");
        vita2d_draw_rectangle(bxCancel, by, bw, bh, fCancel ? ACCENT : SURFACE2);
        vita2d_pgf_draw_text(font_, bxCancel + 48, by + 26, fCancel ? BG : WHITE, 0.62f, "Cancel");
        vita2d_pgf_draw_text(font_, ox + 22, oy + oh - 78, DIM, 0.48f, "D-Pad: move   X: select   O: cancel");
        return;
    }

    const char* title = "Choose download";
    if (installAllPhase_ == InstallAllPhase::PickGameFiles) title = "Choose Game Files";
    else if (installAllPhase_ == InstallAllPhase::PickDataFiles) title = "Choose Data Files";
    vita2d_pgf_draw_text(font_, ox + 22, oy + 36, ACCENT, 0.78f, title);
    vita2d_pgf_draw_text(font_, ox + 22, oy + 62, DIM, 0.50f, "Same content — pick one mirror / source");
"""
new_ia = """    const int ow = 680, oh = 400;
    const int ox = (SCREEN_W - ow) / 2, oy = (SCREEN_H - oh) / 2;
    vita2d_draw_rectangle(ox, oy, ow, oh, SURFACE2);
    vita2d_draw_rectangle(ox, oy, ow, 3, ACCENT);
    vita2d_draw_rectangle(ox, oy + oh - 1, ow, 1, BORDER);
    vita2d_draw_rectangle(ox, oy, 1, oh, BORDER);
    vita2d_draw_rectangle(ox + ow - 1, oy, 1, oh, BORDER);

    if (installAllPhase_ == InstallAllPhase::Confirm) {
        vita2d_pgf_draw_text(font_, ox + 24, oy + 40, ACCENT, 0.96f, "Install All");
        vita2d_pgf_draw_text(font_, ox + 24, oy + 76, WHITE, 0.72f, ellipsize(item.name, 42).c_str());
        const char* lines[] = {
            "This installs the homebrew from scratch:",
            "1) App (VPK)  2) Game Files  3) Data Files",
            "You will pick one download source per step when needed.",
            "If you only want to update the app, use the VPK button instead.",
        };
        int ty = oy + 112;
        for (const char* ln : lines) {
            vita2d_pgf_draw_text(font_, ox + 24, ty, TEXT, 0.66f, ln);
            ty += 28;
        }
        const int bw = 220, bh = 44;
        const int by = oy + oh - 60;
        const int bxOk = ox + 28;
        const int bxCancel = ox + ow - 28 - bw;
        const bool fOk = installAllFocus_ == 0;
        const bool fCancel = installAllFocus_ == 1;
        vita2d_draw_rectangle(bxOk, by, bw, bh, fOk ? ACCENT : SURFACE2);
        {
            const char* lab = "Continue";
            const float sc = 0.72f;
            const int tw = vita2d_pgf_text_width(font_, sc, lab);
            vita2d_pgf_draw_text(font_, bxOk + (bw - tw) / 2, by + 30, fOk ? BG : WHITE, sc, lab);
        }
        vita2d_draw_rectangle(bxCancel, by, bw, bh, fCancel ? ACCENT : SURFACE2);
        {
            const char* lab = "Cancel";
            const float sc = 0.72f;
            const int tw = vita2d_pgf_text_width(font_, sc, lab);
            vita2d_pgf_draw_text(font_, bxCancel + (bw - tw) / 2, by + 30, fCancel ? BG : WHITE, sc, lab);
        }
        vita2d_pgf_draw_text(font_, ox + 24, oy + oh - 84, DIM, 0.58f, "D-Pad: move   X: select   O: cancel");
        return;
    }

    const char* title = "Choose download";
    if (installAllPhase_ == InstallAllPhase::PickGameFiles) title = "Choose Game Files";
    else if (installAllPhase_ == InstallAllPhase::PickDataFiles) title = "Choose Data Files";
    vita2d_pgf_draw_text(font_, ox + 24, oy + 40, ACCENT, 0.96f, title);
    vita2d_pgf_draw_text(font_, ox + 24, oy + 70, DIM, 0.60f, "Same content — pick one mirror / source");
"""
if old_ia not in cpp:
    raise SystemExit("install all confirm not found")
cpp = cpp.replace(old_ia, new_ia, 1)

# Install All pick rows text
cpp = cpp.replace(
    "{ const int titleMaxW = std::max(40, rw - 24 - badgeW - 8); drawMarqueeText(font_, rx + 12, ry + 16, titleMaxW, mc, 0.72f, name, f); }",
    "{ const int titleMaxW = std::max(40, rw - 24 - badgeW - 8); drawMarqueeText(font_, rx + 12, ry + 18, titleMaxW, mc, 0.80f, name, f); }",
    1,
)
cpp = cpp.replace(
    "        vita2d_pgf_draw_text(font_, rx + 12, ry + 32, DIM, 0.58f, ellipsize(meta, badgeW ? 30 : 48).c_str());",
    "        vita2d_pgf_draw_text(font_, rx + 12, ry + 36, DIM, 0.66f, ellipsize(meta, badgeW ? 28 : 44).c_str());",
    1,
)
cpp = cpp.replace(
    '    vita2d_pgf_draw_text(font_, ox + 22, oy + oh - 28, DIM, 0.48f, "D-Pad: move   X: select   O: cancel");\n}\n\nvoid FullCatalogScreen::drawLoadingOverlay()',
    '    vita2d_pgf_draw_text(font_, ox + 24, oy + oh - 28, DIM, 0.58f, "D-Pad: move   X: select   O: cancel");\n}\n\nvoid FullCatalogScreen::drawLoadingOverlay()',
    1,
)

# --- Request Data modal ---
cpp = cpp.replace(
    '    vita2d_pgf_draw_text(font_, x + 24, y + 36, ACC, 0.62f, "REQUEST DATA / GAME FILES");\n    vita2d_pgf_draw_text(font_, x + 24, y + 72, WHITE, 0.60f, "This app has no Data/Game Files links.");\n    vita2d_pgf_draw_text(font_, x + 24, y + 100, TEXT, 0.54f, "Send a request so we can look for them.");\n    vita2d_pgf_draw_text(font_, x + 24, y + 122, TEXT, 0.54f, "It may take several days — we will add them");\n    vita2d_pgf_draw_text(font_, x + 24, y + 144, TEXT, 0.54f, "when available. Thank you for your patience.");',
    '    vita2d_pgf_draw_text(font_, x + 24, y + 40, ACC, 0.78f, "REQUEST DATA / GAME FILES");\n    vita2d_pgf_draw_text(font_, x + 24, y + 78, WHITE, 0.72f, "This app has no Data/Game Files links.");\n    vita2d_pgf_draw_text(font_, x + 24, y + 110, TEXT, 0.66f, "Send a request so we can look for them.");\n    vita2d_pgf_draw_text(font_, x + 24, y + 136, TEXT, 0.66f, "It may take several days — we will add them");\n    vita2d_pgf_draw_text(font_, x + 24, y + 162, TEXT, 0.66f, "when available. Thank you for your patience.");',
    1,
)

# Report modal
cpp = cpp.replace(
    '    vita2d_pgf_draw_text(font_, x + 24, y + 36, RED, 0.62f, "REPORT AN ISSUE");\n    vita2d_pgf_draw_text(font_, x + 24, y + 72, WHITE, 0.64f, "Did something go wrong?");\n    vita2d_pgf_draw_text(font_, x + 24, y + 100, TEXT, 0.56f, "Send a report with the recent logs so we can");\n    vita2d_pgf_draw_text(font_, x + 24, y + 122, TEXT, 0.56f, "review it and fix the problem as soon as possible.");',
    '    vita2d_pgf_draw_text(font_, x + 24, y + 40, RED, 0.78f, "REPORT AN ISSUE");\n    vita2d_pgf_draw_text(font_, x + 24, y + 78, WHITE, 0.76f, "Did something go wrong?");\n    vita2d_pgf_draw_text(font_, x + 24, y + 112, TEXT, 0.66f, "Send a report with the recent logs so we can");\n    vita2d_pgf_draw_text(font_, x + 24, y + 138, TEXT, 0.66f, "review it and fix the problem as soon as possible.");',
    1,
)

# Toast text already 0.70 — bump a bit more + taller toast
cpp = cpp.replace(
    "    const int th = 40;\n    const int x = (SCREEN_W - tw) / 2;\n    const int y = SCREEN_H - FOOTER_H - th - 16;",
    "    const int th = 46;\n    const int x = (SCREEN_W - tw) / 2;\n    const int y = SCREEN_H - FOOTER_H - th - 16;",
    1,
)
cpp = cpp.replace(
    "        vita2d_pgf_draw_text(font_, x + 16, y + 26, RGBA8(255, 255, 255, alpha), 0.70f, toastMessage_.c_str());",
    "        vita2d_pgf_draw_text(font_, x + 16, y + 30, RGBA8(255, 255, 255, alpha), 0.76f, toastMessage_.c_str());",
    1,
)

# Settings list labels
cpp = cpp.replace(
    "            vita2d_pgf_draw_text(font_, listX + 14, y + 20, focus ? WHITE : TEXT, 0.68f, opts[i].label.c_str());",
    "            vita2d_pgf_draw_text(font_, listX + 14, y + 22, focus ? WHITE : TEXT, 0.76f, opts[i].label.c_str());",
    1,
)
# label might be opts[i].label without .c_str in some forms - check
if "opts[i].label.c_str()" not in cpp and "opts[i].label" in cpp:
    cpp = cpp.replace(
        "            vita2d_pgf_draw_text(font_, listX + 14, y + 20, focus ? WHITE : TEXT, 0.68f, opts[i].label);",
        "            vita2d_pgf_draw_text(font_, listX + 14, y + 22, focus ? WHITE : TEXT, 0.76f, opts[i].label);",
        1,
    )

# Footer bar left hints
cpp = cpp.replace(
    "        vita2d_pgf_draw_text(font, 12, SCREEN_H - 14, TEXT, 0.58f, leftHints);",
    "        vita2d_pgf_draw_text(font, 12, SCREEN_H - 14, TEXT, 0.64f, leftHints);",
    1,
)

if cpp == orig:
    raise SystemExit("no changes applied")
CPP.write_text(cpp, encoding="utf-8")
print("OK global font bump applied")

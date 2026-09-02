#!/usr/bin/env python3
"""Fix UX0 width, fit 3 split cards, marquee/panel clip spill, touch hitboxes."""
from pathlib import Path
import re

CPP = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
cpp = CPP.read_text(encoding="utf-8")
orig = cpp

# 1) UX0 panel back to original width (keep larger fonts)
cpp = cpp.replace(
    "    const int panelW = 250;\n    const int panelH = FOOTER_H - 4;\n    const int panelX = SCREEN_W - panelW - 4;\n    const int panelY = SCREEN_H - FOOTER_H + 2;",
    "    const int panelW = 220;\n    const int panelH = FOOTER_H - 4;\n    const int panelX = SCREEN_W - panelW - 6;\n    const int panelY = SCREEN_H - FOOTER_H + 2;",
    1,
)

# 2) SPLIT_CARD_H 118 -> 108 so 3 rows fit with FOOTER_H=54
# contentH ~= 544-52-36-54-24 = 378; (108+10)*3 = 354 <= 378
cpp = cpp.replace(
    "constexpr int FULL_CARD_H=136,SPLIT_CARD_H=118,DETAIL_HEADER_H=108,",
    "constexpr int FULL_CARD_H=136,SPLIT_CARD_H=108,DETAIL_HEADER_H=108,",
    1,
)

# 3) Marquee: do not leave clipping fully disabled — expand to screen;
#    parent re-asserts panel clip after each card.
old_mq = """    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x, clipTop, x + maxW, clipBottom);
    vita2d_pgf_draw_text(font, x - offset, y, color, scale, text.c_str());
    vita2d_pgf_draw_text(font, x - offset + cycle, y, color, scale, text.c_str());
    vita2d_disable_clipping();
}
"""
new_mq = """    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x, clipTop, x + maxW, clipBottom);
    vita2d_pgf_draw_text(font, x - offset, y, color, scale, text.c_str());
    vita2d_pgf_draw_text(font, x - offset + cycle, y, color, scale, text.c_str());
    // Do not leave scissor disabled: rest of the card (badges, chips) would spill
    // outside the panel while scrolling. Expand to full screen; caller re-asserts
    // the panel clip right after drawCatalogCard returns.
    vita2d_set_clip_rectangle(0, 0, SCREEN_W, SCREEN_H);
}
"""
if old_mq not in cpp:
    raise SystemExit("marquee clip block not found")
cpp = cpp.replace(old_mq, new_mq, 1)

# 4) After marquee in drawCatalogCard, clip to card bounds so badges stay inside card
# Find the drawMarqueeText call for card name and add card clip after it.
old_after_name = """        drawMarqueeText(font_, tx, y + (compact ? 24 : 28) + oy, nameMaxW, WHITE, nameSc, it.name, focus);
    }
    vita2d_pgf_draw_text(font_, tx, y + (compact ? 44 : 50) + oy, TEXT, compact ? 0.74f : 0.82f,
"""
new_after_name = """        drawMarqueeText(font_, tx, y + (compact ? 24 : 28) + oy, nameMaxW, WHITE, nameSc, it.name, focus);
    }
    // Keep remaining card chrome inside the card box (marquee temporarily tightens scissor).
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x + ox, y + oy, x + ox + ww, y + oy + hh);
    vita2d_pgf_draw_text(font_, tx, y + (compact ? 44 : 50) + oy, TEXT, compact ? 0.74f : 0.82f,
"""
if old_after_name not in cpp:
    raise SystemExit("card after-name block not found")
cpp = cpp.replace(old_after_name, new_after_name, 1)

# Reduce focus lift so selected card does not poke out of panel as much while scrolling
cpp = cpp.replace(
    """    int ox = focus ? -1 : 0;
    int oy = focus ? -1 : 0;
    int ww = focus ? w + 2 : w;
    int hh = focus ? h + 2 : h;
""",
    """    // Keep focus chrome inside the card/panel (no outward expand while scrolling).
    int ox = 0;
    int oy = 0;
    int ww = w;
    int hh = h;
""",
    1,
)

# Split/full loops: draw one extra row below for smoother scroll coverage
cpp = cpp.replace(
    "        for (int r = -1; r <= vis; ++r) {\n            for (int c = 0; c < 3; ++c) {",
    "        for (int r = -1; r <= vis + 1; ++r) {\n            for (int c = 0; c < 3; ++c) {",
    1,
)
cpp = cpp.replace(
    "        for (int r = -1; r <= vis; ++r) {\n            const int i = baseRow + r;",
    "        for (int r = -1; r <= vis + 1; ++r) {\n            const int i = baseRow + r;",
    1,
)

# 5) Touch hitboxes match drawn sizes
# G/D Files filter
old_touch_gd = """        const int barY = 10, barH = 32;
        const int gdW = 92;
        const int clockReserve = 92;
        // Approximate logo width used in drawHeader (searchLeft often ~200)
        const int barX = 200;
        const bool showGd = (state_.catalog == CatalogType::Homebrew);
        const int barW = std::max(120, SCREEN_W - barX - clockReserve - (showGd ? (gdW + 10) : 0));
        const int gdX = barX + barW + 6;
"""
new_touch_gd = """        const int barY = 10, barH = 32;
        const int gdW = 118;
        const int clockReserve = 92;
        // Approximate logo width used in drawHeader (searchLeft often ~200)
        const int barX = 200;
        const bool showGd = (state_.catalog == CatalogType::Homebrew);
        const int barW = std::max(120, SCREEN_W - barX - clockReserve - (showGd ? (gdW + 10) : 0));
        const int gdX = barX + barW + 6;
"""
if old_touch_gd not in cpp:
    raise SystemExit("touch G/D block not found")
cpp = cpp.replace(old_touch_gd, new_touch_gd, 1)

# Select links / Request data touch (match drawDetailPanel)
old_touch_links = """            const int bx = dx + dw - 142, by = dy + 12, bw = 128, bh = 28;
            if (!tapItem.linkDetails.empty() && hit(x, y, bx, by, bw, bh)) {
                if (state_.linkNavigation) exitLinkNavigation();
                else enterLinkNavigation();
                return;
            }
            if (itemEligibleForDataRequest(tapItem)) {
                const int rby = !tapItem.linkDetails.empty() ? (by + bh + 6) : by;
                if (hit(x, y, bx, rby, bw, 26)) {
                    openDataRequestConfirm();
                    return;
                }
            }
"""
new_touch_links = """            const int bx = dx + dw - 156, by = dy + 12, bw = 142, bh = 32;
            if (!tapItem.linkDetails.empty() && hit(x, y, bx, by, bw, bh)) {
                if (state_.linkNavigation) exitLinkNavigation();
                else enterLinkNavigation();
                return;
            }
            if (itemEligibleForDataRequest(tapItem)) {
                const int rby = !tapItem.linkDetails.empty() ? (by + bh + 6) : by;
                if (hit(x, y, bx, rby, bw, 32)) {
                    openDataRequestConfirm();
                    return;
                }
            }
"""
if old_touch_links not in cpp:
    raise SystemExit("touch select-links block not found")
cpp = cpp.replace(old_touch_links, new_touch_links, 1)

# News/Report footer chips — panelW for layout still 220; ensure chip hit uses FOOTER_H
# Already uses FOOTER_H - 8; with FOOTER 54 chips are taller. Widen news/report slightly for touch.
old_footer_touch = """        const int panelW = 220;
        const int panelX = SCREEN_W - panelW - 6;
        const int reportW = 100;
        const int newsW = 92;
        const int chipH = FOOTER_H - 8;
        const int reportX = panelX - reportW - 8;
        const int newsX = reportX - newsW - 8;
        const int chipY = SCREEN_H - FOOTER_H + 4;
"""
new_footer_touch = """        const int panelW = 220;
        const int panelX = SCREEN_W - panelW - 6;
        const int reportW = 108;
        const int newsW = 100;
        const int chipH = FOOTER_H - 6;
        const int reportX = panelX - reportW - 8;
        const int newsX = reportX - newsW - 8;
        const int chipY = SCREEN_H - FOOTER_H + 3;
"""
if old_footer_touch not in cpp:
    raise SystemExit("footer touch chips not found")
cpp = cpp.replace(old_footer_touch, new_footer_touch, 1)

# Match News/Report chip draw sizes if they still use old widths
# drawNewsChip / drawReportChip
for label, old_w, new_w in (("News", 92, 100),):
    pass

# Update drawReportChip / drawNewsChip panel positioning if hardcoded
# Read-only check - often share panelW 220
cpp2 = cpp
# drawNewsChip chip width
if "const int chipW = 92;" in cpp:
    cpp = cpp.replace("const int chipW = 92;", "const int chipW = 100;", 1)
# might be computed from text - leave if not present

if cpp == orig:
    raise SystemExit("no changes applied")
CPP.write_text(cpp, encoding="utf-8")
print("OK ui regression fixes applied")

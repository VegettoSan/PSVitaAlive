#!/usr/bin/env python3
from pathlib import Path
import re

CPP = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
s = CPP.read_text(encoding="utf-8")
orig = s

# 1) Restore the split-detail card height that existed before the last UI squeeze.
old = "SPLIT_CARD_H=108"
new = "SPLIT_CARD_H=118"
if old not in s:
    raise SystemExit("SPLIT_CARD_H=108 not found")
s = s.replace(old, new, 1)

# 2) Clip every catalog card to its own rectangle. The marquee helper temporarily
#    changes the scissor; the existing caller re-asserts the panel clip afterwards.
needle = "void FullCatalogScreen::drawCatalogCard(const CatalogItem&it,int idx,int x,int y,int w,int h,bool focus){\n    const float pulse = focus ? focusPulse() : 0.f;"
replacement = "void FullCatalogScreen::drawCatalogCard(const CatalogItem&it,int idx,int x,int y,int w,int h,bool focus){\n    vita2d_enable_clipping();\n    vita2d_set_clip_rectangle(x, y, x + w, y + h);\n    const float pulse = focus ? focusPulse() : 0.f;"
if needle not in s:
    raise SystemExit("drawCatalogCard anchor not found")
s = s.replace(needle, replacement, 1)

# 3) Keep the whole detail panel inside its own rectangle. This is deliberately
#    local to Detail and does not alter navigation/rendering outside the panel.
needle = "void FullCatalogScreen::drawDetailPanel(int x,int y,int w,int h){\n    vita2d_draw_rectangle(x, y, w, h, PANEL);"
replacement = "void FullCatalogScreen::drawDetailPanel(int x,int y,int w,int h){\n    vita2d_enable_clipping();\n    vita2d_set_clip_rectangle(x, y, x + w, y + h);\n    vita2d_draw_rectangle(x, y, w, h, PANEL);"
if needle not in s:
    raise SystemExit("drawDetailPanel anchor not found")
s = s.replace(needle, replacement, 1)

# 4) Make modal typography actually readable while retaining the current layout.
repls = {
    '0.78f, "REQUEST DATA / GAME FILES"': '0.86f, "REQUEST DATA / GAME FILES"',
    '0.72f, "This app has no Data/Game Files links."': '0.78f, "This app has no Data/Game Files links."',
    'TEXT, 0.66f, "Send a request so we can look for them."': 'TEXT, 0.72f, "Send a request so we can look for them."',
    'TEXT, 0.66f, "It may take several days — we will add them"': 'TEXT, 0.72f, "It may take several days — we will add them"',
    'TEXT, 0.66f, "when available. Thank you for your patience."': 'TEXT, 0.72f, "when available. Thank you for your patience."',
    'const float sc = 0.62f;\n        const int tw = vita2d_pgf_text_width(font_, sc, lab);': 'const float sc = 0.74f;\n        const int tw = vita2d_pgf_text_width(font_, sc, lab);',
    '0.78f, "REPORT AN ISSUE"': '0.86f, "REPORT AN ISSUE"',
    '0.76f, "Did something go wrong?"': '0.80f, "Did something go wrong?"',
    'TEXT, 0.66f, "Send a report with the recent logs so we can"': 'TEXT, 0.72f, "Send a report with the recent logs so we can"',
    'TEXT, 0.66f, "review it and fix the problem as soon as possible."': 'TEXT, 0.72f, "review it and fix the problem as soon as possible."',
    '0.74f, "NEWS"': '0.82f, "NEWS"',
    '0.92f,\n                         ellipsize(newsTitle_': '1.00f,\n                         ellipsize(newsTitle_',
    'scale = 0.64f;\n            int height = 26;': 'scale = 0.70f;\n            int height = 28;',
    'scale = 0.98f; height = 36;': 'scale = 1.04f; height = 38;',
    'scale = 0.84f; height = 32;': 'scale = 0.90f; height = 34;',
    'scale = 0.74f; height = 28;': 'scale = 0.80f; height = 30;',
    'scale = 0.64f; height = 26; indent = 12;': 'scale = 0.70f; height = 28; indent = 12;',
    'DIM, 0.66f, "D-Pad: scroll   Circle: close"': 'DIM, 0.72f, "D-Pad: scroll   Circle: close"',
}
for a, b in repls.items():
    if a not in s:
        continue
    s = s.replace(a, b, 1)

# Restore the larger vertical spacing required by the larger split cards only where
# the readability pass had compressed it too aggressively.
s = s.replace('LINE_H=24,DETAIL_SECTION_H=30,DETAIL_META_H=28,DETAIL_SECTION_GAP=16',
              'LINE_H=24,DETAIL_SECTION_H=30,DETAIL_META_H=28,DETAIL_SECTION_GAP=16', 1)

# 5) Modal touch: expand hit areas modestly around the actual drawn buttons. This
#    fixes finger placement tolerance without making the catalog cards themselves
#    globally easier to hit.

def pad_modal_hits(block: str) -> str:
    return re.sub(
        r'if \(hit\(x, y, (bx[A-Za-z0-9_]+), (by), (bw), (bh)\)\)',
        r'if (hit(x, y, \\1 - 12, \\2 - 12, \\3 + 24, \\4 + 24))',
        block,
    )

# The generated source keeps the modal touch handlers in stable comment-delimited blocks.
for marker_start, marker_end in [
    ('// Data/Game Files request confirmation modal touch', '// Report confirmation modal touch'),
    ('// Report confirmation modal touch', '// Footer News + Report chips'),
]:
    start = s.find(marker_start)
    end = s.find(marker_end, start + len(marker_start))
    if start >= 0 and end > start:
        block = s[start:end]
        block = pad_modal_hits(block)
        s = s[:start] + block + s[end:]

# News close button uses different variable names, so patch that one explicitly.
s = s.replace(
    'if (hit(x, y, bx, by, bw, bh))\n                    closeNewsModal(newsMarkSeenOnClose_);',
    'if (hit(x, y, bx - 12, by - 12, bw + 24, bh + 24))\n                    closeNewsModal(newsMarkSeenOnClose_);',
    1,
)

# Install-All modal: pad each button hitbox in its dedicated touch block.
start = s.find('// --- Install All wizard overlay ---')
end = s.find('// --- Install overlay:', start + 1)
if start >= 0 and end > start:
    block = s[start:end]
    block = pad_modal_hits(block)
    s = s[:start] + block + s[end:]

# Install outcome overlay: pad its explicit close/report/cancel button hits only.
start = s.find('// --- Install overlay: only the explicit button is tappable ---')
end = s.find('// Data/Game Files request confirmation modal touch', start + 1)
if start >= 0 and end > start:
    block = s[start:end]
    block = pad_modal_hits(block)
    s = s[:start] + block + s[end:]

if s == orig:
    raise SystemExit('no changes applied')
CPP.write_text(s, encoding='utf-8')
print('patched', CPP)

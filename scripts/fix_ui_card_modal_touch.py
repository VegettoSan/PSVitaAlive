#!/usr/bin/env python3
from pathlib import Path

CPP = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
s = CPP.read_text(encoding="utf-8")
orig = s

# Split cards: accept either the original 108 value or an already-restored 118 value.
if "SPLIT_CARD_H=108" in s:
    s = s.replace("SPLIT_CARD_H=108", "SPLIT_CARD_H=118", 1)
elif "SPLIT_CARD_H=118" not in s:
    raise SystemExit("unexpected SPLIT_CARD_H value")

# Clip card content to its own rectangle if not already patched.
card_fn = s.find("void FullCatalogScreen::drawCatalogCard")
card_head = s[card_fn:card_fn + 700] if card_fn >= 0 else ""
if "vita2d_set_clip_rectangle(x, y, x + w, y + h);" not in card_head:
    old = "void FullCatalogScreen::drawCatalogCard(const CatalogItem&it,int idx,int x,int y,int w,int h,bool focus){\n    const float pulse = focus ? focusPulse() : 0.f;"
    new = "void FullCatalogScreen::drawCatalogCard(const CatalogItem&it,int idx,int x,int y,int w,int h,bool focus){\n    vita2d_enable_clipping();\n    vita2d_set_clip_rectangle(x, y, x + w, y + h);\n    const float pulse = focus ? focusPulse() : 0.f;"
    if old not in s:
        raise SystemExit("drawCatalogCard anchor not found")
    s = s.replace(old, new, 1)

# Clip Detail to its visible panel if not already patched.
detail_fn = s.find("void FullCatalogScreen::drawDetailPanel")
detail_head = s[detail_fn:detail_fn + 500] if detail_fn >= 0 else ""
if "vita2d_set_clip_rectangle(x, y, x + w, y + h);" not in detail_head:
    old = "void FullCatalogScreen::drawDetailPanel(int x,int y,int w,int h){\n    vita2d_draw_rectangle(x, y, w, h, PANEL);"
    new = "void FullCatalogScreen::drawDetailPanel(int x,int y,int w,int h){\n    vita2d_enable_clipping();\n    vita2d_set_clip_rectangle(x, y, x + w, y + h);\n    vita2d_draw_rectangle(x, y, w, h, PANEL);"
    if old not in s:
        raise SystemExit("drawDetailPanel anchor not found")
    s = s.replace(old, new, 1)

# Modal text readability.
for old, new in [
    ('0.78f, "REQUEST DATA / GAME FILES"', '0.86f, "REQUEST DATA / GAME FILES"'),
    ('WHITE, 0.72f, "This app has no Data/Game Files links."', 'WHITE, 0.78f, "This app has no Data/Game Files links."'),
    ('TEXT, 0.66f, "Send a request so we can look for them."', 'TEXT, 0.72f, "Send a request so we can look for them."'),
    ('TEXT, 0.66f, "It may take several days — we will add them"', 'TEXT, 0.72f, "It may take several days — we will add them"'),
    ('TEXT, 0.66f, "when available. Thank you for your patience."', 'TEXT, 0.72f, "when available. Thank you for your patience."'),
    ('0.78f, "REPORT AN ISSUE"', '0.86f, "REPORT AN ISSUE"'),
    ('WHITE, 0.76f, "Did something go wrong?"', 'WHITE, 0.80f, "Did something go wrong?"'),
    ('TEXT, 0.66f, "Send a report with the recent logs so we can"', 'TEXT, 0.72f, "Send a report with the recent logs so we can"'),
    ('TEXT, 0.66f, "review it and fix the problem as soon as possible."', 'TEXT, 0.72f, "review it and fix the problem as soon as possible."'),
    ('ACCENT, 0.74f, "NEWS"', 'ACCENT, 0.82f, "NEWS"'),
    ('WHITE, 0.92f,\n                         ellipsize(newsTitle_', 'WHITE, 1.00f,\n                         ellipsize(newsTitle_'),
    ('DIM, 0.66f, "D-Pad: scroll   Circle: close"', 'DIM, 0.72f, "D-Pad: scroll   Circle: close"'),
]:
    if old in s:
        s = s.replace(old, new, 1)

for old, new in [
    ('scale = 0.64f;\n            int height = 26;', 'scale = 0.70f;\n            int height = 28;'),
    ('scale = 0.98f; height = 36;', 'scale = 1.04f; height = 38;'),
    ('scale = 0.84f; height = 32;', 'scale = 0.90f; height = 34;'),
    ('scale = 0.74f; height = 28;', 'scale = 0.80f; height = 30;'),
    ('scale = 0.64f; height = 26; indent = 12;', 'scale = 0.70f; height = 28; indent = 12;'),
]:
    if old in s:
        s = s.replace(old, new, 1)

# Make buttons in confirmation modals readable. Scope by label, so unrelated controls stay untouched.
for label in ['O  Cancel', 'X  Send', 'X  Report']:
    needle = f'const char* lab = "{label}";\n        const float sc = 0.62f;'
    repl = f'const char* lab = "{label}";\n        const float sc = 0.74f;'
    if needle in s:
        s = s.replace(needle, repl, 1)

# Expand touch targets of modal buttons only; keep their visible geometry unchanged.
for old, new in [
    ('if (hit(x, y, bxCancel, by, bw, bh)) {\n            closeDataRequestConfirm();',
     'if (hit(x, y, bxCancel - 12, by - 12, bw + 24, bh + 24)) {\n            closeDataRequestConfirm();'),
    ('if (hit(x, y, bxSend, by, bw, bh)) {\n            closeDataRequestConfirm();',
     'if (hit(x, y, bxSend - 12, by - 12, bw + 24, bh + 24)) {\n            closeDataRequestConfirm();'),
    ('if (hit(x, y, bxCancel, by, bw, bh)) {\n            closeReportConfirm();',
     'if (hit(x, y, bxCancel - 12, by - 12, bw + 24, bh + 24)) {\n            closeReportConfirm();'),
    ('if (hit(x, y, bxReport, by, bw, bh)) {\n            closeReportConfirm();',
     'if (hit(x, y, bxReport - 12, by - 12, bw + 24, bh + 24)) {\n            closeReportConfirm();'),
    ('if (hit(x, y, bx, by, bw, bh))\n                    closeNewsModal(newsMarkSeenOnClose_);',
     'if (hit(x, y, bx - 12, by - 12, bw + 24, bh + 24))\n                    closeNewsModal(newsMarkSeenOnClose_);'),
    ('if (hit(x, y, bxOk, by, bw, bh)) { installAllFocus_ = 0;',
     'if (hit(x, y, bxOk - 12, by - 12, bw + 24, bh + 24)) { installAllFocus_ = 0;'),
    ('if (hit(x, y, bxCancel, by, bw, bh)) { closeInstallAllWizard(true); return; }',
     'if (hit(x, y, bxCancel - 12, by - 12, bw + 24, bh + 24)) { closeInstallAllWizard(true); return; }'),
]:
    if old in s:
        s = s.replace(old, new, 1)

if s == orig:
    raise SystemExit("no changes applied")
CPP.write_text(s, encoding="utf-8")
print("UI patch complete")

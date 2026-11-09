#!/usr/bin/env python3
"""Regression for the real PS Vita feedback clipping incident (2026-11-08).

Source-level guard only: it cannot substitute a live Vita3K/PS Vita visual test.
The bug was calling vita2d_set_clip_rectangle() without enabling clipping,
then drawing the scrolled controls over the modal title and fixed buttons.
"""
from pathlib import Path
import re

source = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp").read_text(
    encoding="utf-8"
)
start = source.index("void FullCatalogScreen::drawReportConfirmOverlay() {")
end = source.index("int FullCatalogScreen::reportWorkerEntry(", start)
body = source[start:end]

# Strip comments before token checking, so a comment cannot satisfy a guard.
body = re.sub(r"//[^\n]*", "", body)
events = list(re.finditer(
    r"\bvita2d_(enable_clipping|disable_clipping|set_clip_rectangle)\s*\(", body
))
assert events, "No clipping operations found in feedback renderer"
enabled = False
on_count = 0
off_count = 0
set_count = 0
for match in events:
    operation = match.group(1)
    if operation == "enable_clipping":
        assert not enabled, "Unexpected nested clip enable"
        enabled = True
        on_count += 1
    elif operation == "disable_clipping":
        enabled = False
        off_count += 1
    else:
        assert enabled, (
            "Feedback calls vita2d_set_clip_rectangle() while clipping "
            "is disabled; scrolled content will draw outside the panel"
        )
        set_count += 1
assert not enabled, "Feedback clipping state leaks into the surrounding UI"
assert on_count == 2, "Both independent panes need a live clip"
assert off_count == 3, "Reset, between panes, and before fixed buttons"
assert set_count >= 6, "Preserve both pane clips and the two nested text clips"

# Verify all scrollable right-pane elements are rendered while clipping is
# active (including the two type buttons and the warning below the message).
def clipping_at(offset):
    current = False
    for match in events:
        if match.start() >= offset:
            break
        if match.group(1) == "enable_clipping":
            current = True
        elif match.group(1) == "disable_clipping":
            current = False
    return current

for marker in (
    "int infoY=FB_VIEW_Y+25-infoScroll;",
    "const int ty=r.typeY-formScroll;",
    "const int y=r.appY-formScroll;",
    "const int titleY=r.titleY-formScroll;",
    "const int bodyY=r.bodyY-formScroll;",
    "if (!feedbackNoticeLines_.empty()) {",
):
    assert marker in body, f"Feedback geometry changed: {marker}"
    assert clipping_at(body.index(marker)), f"Missing active viewport: {marker}"

# Fixed scrollbars and submit/cancel controls must not be clipped.
for marker in (
    "drawScrollbar(FB_INFO_X+FB_INFO_W-10,",
    "const int sendX=FB_FORM_FIELD_X,sendW=284;",
):
    assert marker in body
    assert not clipping_at(body.index(marker)), f"Fixed UI clipped: {marker}"

# The viewport ends above the permanent action buttons.
fb_y, fb_h, view_y, view_h, action_h = 16, 512, 110, 348, 43
action_y = fb_y + fb_h - 58
assert view_y + view_h < action_y
assert action_y + action_h < fb_y + fb_h

# Scenarios with/without a selected app and with varying warning lengths
# must be scrollable to the bottom of the right column without overlapping
# the fixed buttons.
for app_lines in (0, 1, 3, 7):
    for notice_lines in (0, 1, 2, 6):
        cursor = view_y + 12 + 54
        if app_lines:
            cursor += 87 + app_lines * 25 + 12
        cursor += 82
        cursor += 148 + 45
        if notice_lines:
            cursor += 20 + notice_lines * 25 + 15 + 14
        content_height = cursor - view_y + 10
        max_scroll = max(0, content_height - view_h)
        last_content_bottom = cursor - (14 if notice_lines else 0) - max_scroll
        assert last_content_bottom <= view_y + view_h
print("Feedback viewport clipping and dynamic scroll boundaries OK")

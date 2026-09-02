#!/usr/bin/env python3
"""Fix theme picker scroll: match catalog card navigation feel."""
from pathlib import Path
import re

CPP = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
cpp = CPP.read_text(encoding="utf-8")

# --- Fix updateAnimations: theme scroll lerp was trapped inside news else-branch ---
old_anim = '''    // News modal scroll (line units)
    if (newsVisible_) {
        const float targetNews = static_cast<float>(newsScrollLine_);
        visualNewsScroll_ += (targetNews - visualNewsScroll_) * 0.22f;
        if (std::fabs(targetNews - visualNewsScroll_) < 0.02f)
            visualNewsScroll_ = targetNews;
    } else {
        visualNewsScroll_ = static_cast<float>(newsScrollLine_);
    visualThemeSetupScroll_ += (static_cast<float>(themeSetupScrollRow_) - visualThemeSetupScroll_) * 0.28f;
    }
'''

new_anim = '''    // News modal scroll (line units)
    if (newsVisible_) {
        const float targetNews = static_cast<float>(newsScrollLine_);
        visualNewsScroll_ += (targetNews - visualNewsScroll_) * 0.22f;
        if (std::fabs(targetNews - visualNewsScroll_) < 0.02f)
            visualNewsScroll_ = targetNews;
    } else {
        visualNewsScroll_ = static_cast<float>(newsScrollLine_);
    }

    // Theme picker grid scroll (row units) — same smoothing as catalog cards
    if (themeSetupVisible_) {
        const float targetTheme = static_cast<float>(themeSetupScrollRow_);
        visualThemeSetupScroll_ += (targetTheme - visualThemeSetupScroll_) * 0.18f;
        if (std::fabs(targetTheme - visualThemeSetupScroll_) < 0.008f)
            visualThemeSetupScroll_ = targetTheme;
    }
'''

if old_anim not in cpp:
    # try looser match
    m = re.search(
        r"// News modal scroll.*\n(?:.*\n){0,12}?\s*visualThemeSetupScroll_[^\n]*\n\s*\}",
        cpp,
    )
    if not m:
        raise SystemExit("updateAnimations news/theme block not found")
    cpp = cpp[:m.start()] + new_anim + cpp[m.end():]
else:
    cpp = cpp.replace(old_anim, new_anim, 1)

# --- Helper: clamp theme scroll to keep focus visible (catalog-style) ---
helper = r'''
/** Keep theme picker focus row inside the visible window (like clampCatalogScroll). */
static void clampThemePickerScroll(int focus, int& scrollRow, int themeCount, int cols, int visibleRows) {
    if (themeCount <= 0) { scrollRow = 0; return; }
    const int rows = (themeCount + cols - 1) / cols;
    const int maxScroll = std::max(0, rows - visibleRows);
    if (focus >= 0 && focus < themeCount) {
        const int fr = focus / cols;
        if (fr < scrollRow) scrollRow = fr;
        if (fr >= scrollRow + visibleRows) scrollRow = fr - visibleRows + 1;
    }
    if (scrollRow < 0) scrollRow = 0;
    if (scrollRow > maxScroll) scrollRow = maxScroll;
}

'''

if "clampThemePickerScroll" not in cpp:
    # insert before openThemePicker
    anchor = "void FullCatalogScreen::openThemePicker()"
    if anchor not in cpp:
        raise SystemExit("openThemePicker not found")
    cpp = cpp.replace(anchor, helper + anchor, 1)

# --- Replace handleInput theme block with scroll-aware navigation ---
old_hi = (
    "if(themeSetupVisible_){const int themeCount=static_cast<int>(::psvitaalive::ColorTheme::Count);"
    "const int cols=3;"
    "if(nav&SCE_CTRL_LEFT){if(themeSetupFocus_<themeCount){int c=themeSetupFocus_%cols;if(c>0)--themeSetupFocus_;}return;}"
    "if(nav&SCE_CTRL_RIGHT){if(themeSetupFocus_<themeCount){int c=themeSetupFocus_%cols;if(c<cols-1&&themeSetupFocus_+1<themeCount)++themeSetupFocus_;}return;}"
    "if(nav&SCE_CTRL_UP||(pressed&SCE_CTRL_UP)){if(themeSetupFocus_==themeCount){themeSetupFocus_=std::max(0,themeCount-cols);}"
    "else if(themeSetupFocus_>=cols)themeSetupFocus_-=cols;return;}"
    "if(nav&SCE_CTRL_DOWN||(pressed&SCE_CTRL_DOWN)){if(themeSetupFocus_<themeCount){int n=themeSetupFocus_+cols;if(n<themeCount)themeSetupFocus_=n;else themeSetupFocus_=themeCount;}return;}"
    "if(pressed&SCE_CTRL_CROSS){if(themeSetupFocus_==themeCount)closeThemeSetup(true);else applyThemeSetupFocus();return;}return;}"
)

# Match current (may have slight spacing differences from earlier patch)
m_hi = re.search(
    r"if\(themeSetupVisible_\)\{const int themeCount=static_cast<int>\(::psvitaalive::ColorTheme::Count\);.*?return;\}if\(state_\.mode==UiMode::SETTINGS\)",
    cpp,
    re.S,
)
if not m_hi:
    raise SystemExit("themeSetupVisible_ handleInput block not found")

new_hi = (
    "if(themeSetupVisible_){"
    "const int themeCount=static_cast<int>(::psvitaalive::ColorTheme::Count);"
    "const int cols=3;"
    "const int visibleRows=5;"  # matches drawThemeSetupOverlay grid
    "auto afterMove=[&](){clampThemePickerScroll(themeSetupFocus_,themeSetupScrollRow_,themeCount,cols,visibleRows);};"
    "if(nav&SCE_CTRL_LEFT){if(themeSetupFocus_<themeCount){int c=themeSetupFocus_%cols;if(c>0){--themeSetupFocus_;afterMove();}}return;}"
    "if(nav&SCE_CTRL_RIGHT){if(themeSetupFocus_<themeCount){int c=themeSetupFocus_%cols;if(c<cols-1&&themeSetupFocus_+1<themeCount){++themeSetupFocus_;afterMove();}}return;}"
    "if(nav&SCE_CTRL_UP){"
    "  if(themeSetupFocus_==themeCount){themeSetupFocus_=std::max(0,themeCount-1);}"
    "  else if(themeSetupFocus_>=cols)themeSetupFocus_-=cols;"
    "  else if(themeSetupScrollRow_>0)--themeSetupScrollRow_;"  # scroll window even at top row of focus
    "  afterMove();return;}"
    "if(nav&SCE_CTRL_DOWN){"
    "  if(themeSetupFocus_<themeCount){int n=themeSetupFocus_+cols;if(n<themeCount)themeSetupFocus_=n;else themeSetupFocus_=themeCount;}"
    "  afterMove();return;}"
    "if(pressed&SCE_CTRL_CROSS){if(themeSetupFocus_==themeCount)closeThemeSetup(true);else applyThemeSetupFocus();return;}"
    "return;}"
    "if(state_.mode==UiMode::SETTINGS)"
)
cpp = cpp[:m_hi.start()] + new_hi + cpp[m_hi.end():]

# --- Improve touch scroll: pixel-based like catalog (use half row step) ---
old_touch_step = '''                touchAccumY_ += static_cast<float>(-dy);
                const float step = 22.f;
                while (touchAccumY_ >= step) {
                    if (themeSetupScrollRow_ < maxScroll) ++themeSetupScrollRow_;
                    touchAccumY_ -= step;
                }
                while (touchAccumY_ <= -step) {
                    if (themeSetupScrollRow_ > 0) --themeSetupScrollRow_;
                    touchAccumY_ += step;
                }
                if (themeSetupScrollRow_ < 0) themeSetupScrollRow_ = 0;
                if (themeSetupScrollRow_ > maxScroll) themeSetupScrollRow_ = maxScroll;
'''

new_touch_step = '''                // Catalog-like drag: ~half a row of finger travel per list step
                touchAccumY_ += static_cast<float>(-dy);
                const float step = std::max(12.f, static_cast<float>(rowH) * 0.45f);
                while (touchAccumY_ >= step) {
                    if (themeSetupScrollRow_ < maxScroll) ++themeSetupScrollRow_;
                    touchAccumY_ -= step;
                }
                while (touchAccumY_ <= -step) {
                    if (themeSetupScrollRow_ > 0) --themeSetupScrollRow_;
                    touchAccumY_ += step;
                }
                if (themeSetupScrollRow_ < 0) themeSetupScrollRow_ = 0;
                if (themeSetupScrollRow_ > maxScroll) themeSetupScrollRow_ = maxScroll;
'''

if old_touch_step not in cpp:
    raise SystemExit("theme touch scroll step block not found")
cpp = cpp.replace(old_touch_step, new_touch_step, 1)

# Use clamp helper in draw instead of inline (keep draw logic, just ensure consistency)
# Update footer hint
cpp = cpp.replace(
    '"D-Pad: move   X: apply theme / Save   Touch: tap theme or Save"',
    '"D-Pad: move / scroll   X: apply / Save   Touch: drag scroll, tap theme"',
    1,
)

CPP.write_text(cpp, encoding="utf-8")
print("OK theme picker scroll fixed")

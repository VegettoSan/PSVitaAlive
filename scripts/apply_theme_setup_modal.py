#!/usr/bin/env python3
"""Add first-run theme setup modal (before News) to FullCatalogScreen + main."""
from pathlib import Path

CPP = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
MAIN = Path("Client PSVitaAlive/source/main.cpp")
cpp = CPP.read_text(encoding="utf-8")
main = MAIN.read_text(encoding="utf-8")

# --- Helpers near applyColorTheme: theme name + accent RGB without mutating globals ---
anchor = "void applyColorTheme(::psvitaalive::ColorTheme t) {"
if anchor not in cpp:
    raise SystemExit("applyColorTheme not found")

helpers = r'''
const char* colorThemeDisplayName(::psvitaalive::ColorTheme t) {
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
        case ::psvitaalive::ColorTheme::NeonLime:
        default: return "Neon Lime";
    }
}

/** Accent RGB for a theme (does not mutate global palette). */
void colorThemeAccentRgb(::psvitaalive::ColorTheme t, unsigned& ar, unsigned& ag, unsigned& ab) {
    ar = 0x3B; ag = 0xFF; ab = 0x00;
    switch (t) {
        case ::psvitaalive::ColorTheme::Cyan: ar=0x00; ag=0xE5; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Rose: ar=0xFF; ag=0x5C; ab=0xA8; break;
        case ::psvitaalive::ColorTheme::Amber: ar=0xFF; ag=0xB0; ab=0x20; break;
        case ::psvitaalive::ColorTheme::Violet: ar=0xB2; ag=0x4D; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::Mono: ar=0xC8; ag=0xC8; ab=0xCC; break;
        case ::psvitaalive::ColorTheme::Oled: ar=0x5C; ag=0xFF; ab=0x9A; break;
        case ::psvitaalive::ColorTheme::PsVita: ar=0x00; ag=0x9A; ab=0xDE; break;
        case ::psvitaalive::ColorTheme::Crimson: ar=0xFF; ag=0x2D; ab=0x4A; break;
        case ::psvitaalive::ColorTheme::Coffee: ar=0xD4; ag=0xA5; ab=0x5E; break;
        case ::psvitaalive::ColorTheme::Gold: ar=0xFF; ag=0xC8; ab=0x2E; break;
        case ::psvitaalive::ColorTheme::Emerald: ar=0x00; ag=0xD4; ab=0x7A; break;
        case ::psvitaalive::ColorTheme::Coral: ar=0xFF; ag=0x7A; ab=0x66; break;
        case ::psvitaalive::ColorTheme::Teal: ar=0x2E; ag=0xD4; ab=0xC0; break;
        case ::psvitaalive::ColorTheme::Indigo: ar=0x7A; ag=0x6C; ab=0xFF; break;
        case ::psvitaalive::ColorTheme::NeonLime:
        default: ar=0x3B; ag=0xFF; ab=0x00; break;
    }
}

'''

if "colorThemeDisplayName" not in cpp:
    cpp = cpp.replace(anchor, helpers + anchor, 1)

# --- Theme setup methods after closeNewsModal ---
if "void FullCatalogScreen::openThemeSetupIfNeeded" not in cpp:
    news_close = "void FullCatalogScreen::closeNewsModal(bool markSeen) {"
    # Find end of closeNewsModal function - insert after runNewsCheck is better
    # Insert after closeNewsModal body ends, before runNewsCheck
    marker = "void FullCatalogScreen::runNewsCheck(bool forceShow) {"
    if marker not in cpp:
        raise SystemExit("runNewsCheck not found")

    methods = r'''
void FullCatalogScreen::openThemeSetupIfNeeded() {
    if (themeSetupChecked_) return;
    themeSetupChecked_ = true;
    if (settingsEdit_.themeSetupDone) {
        diagnostics::log("[UI] theme setup skipped (already done)");
        return;
    }
    themeSetupVisible_ = true;
    const int n = static_cast<int>(::psvitaalive::ColorTheme::Count);
    themeSetupFocus_ = static_cast<int>(settingsEdit_.colorTheme);
    if (themeSetupFocus_ < 0 || themeSetupFocus_ >= n) themeSetupFocus_ = 0;
    themeSetupScrollRow_ = 0;
    visualThemeSetupScroll_ = 0.f;
    applyColorTheme(settingsEdit_.colorTheme);
    diagnostics::log("[UI] theme setup modal shown (first run)");
}

void FullCatalogScreen::applyThemeSetupFocus() {
    const int n = static_cast<int>(::psvitaalive::ColorTheme::Count);
    if (themeSetupFocus_ < 0 || themeSetupFocus_ >= n) return;
    const auto t = static_cast<::psvitaalive::ColorTheme>(themeSetupFocus_);
    settingsEdit_.colorTheme = t;
    applyColorTheme(t);
    showToast(std::string("Theme: ") + colorThemeDisplayName(t), 1200);
}

void FullCatalogScreen::closeThemeSetup(bool save) {
    if (!themeSetupVisible_) return;
    if (save) {
        settingsEdit_.themeSetupDone = true;
        applyColorTheme(settingsEdit_.colorTheme);
        if (settingsSave_) settingsSave_(settingsEdit_);
        diagnostics::log(std::string("[UI] theme setup saved theme=") +
                         ::psvitaalive::AppSettings::toString(settingsEdit_.colorTheme));
        showToast("Theme saved — you can change it later in Settings", 2200);
    }
    themeSetupVisible_ = false;
}

void FullCatalogScreen::drawThemeSetupOverlay() {
    if (!themeSetupVisible_ || !font_) return;

    const int themeCount = static_cast<int>(::psvitaalive::ColorTheme::Count);
    const int cols = 3;
    const int rows = (themeCount + cols - 1) / cols;
    const int btnW = 220;
    const int btnH = 44;
    const int gapX = 12;
    const int gapY = 10;
    const int gridW = cols * btnW + (cols - 1) * gapX;

    const int w = 760, h = 460;
    const int x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;

    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, 160));
    vita2d_draw_rectangle(x, y, w, h, PANEL);
    vita2d_draw_rectangle(x, y, w, 3, ACCENT);
    vita2d_draw_rectangle(x, y + 3, 3, h - 6, ACCENT);
    vita2d_draw_rectangle(x + w - 3, y + 3, 3, h - 6, BORDER);
    vita2d_draw_rectangle(x, y + h - 3, w, 3, BORDER);

    vita2d_pgf_draw_text(font_, x + 22, y + 34, ACCENT, 0.90f, "Welcome — choose your color theme");
    vita2d_pgf_draw_text(font_, x + 22, y + 58, TEXT, 0.62f,
        "Pick the look you prefer. Each button shows that theme's accent color.");
    vita2d_pgf_draw_text(font_, x + 22, y + 78, DIM, 0.58f,
        "Press X on a theme to apply it. Then press Save. You can change this later in Settings.");

    const int gridTop = y + 100;
    const int gridBottom = y + h - 78;
    const int gridH = gridBottom - gridTop;
    const int rowH = btnH + gapY;
    const int visibleRows = std::max(1, gridH / rowH);
    const int maxScroll = std::max(0, rows - visibleRows);
    if (themeSetupScrollRow_ < 0) themeSetupScrollRow_ = 0;
    if (themeSetupScrollRow_ > maxScroll) themeSetupScrollRow_ = maxScroll;
    if (visualThemeSetupScroll_ < 0.f) visualThemeSetupScroll_ = 0.f;
    if (visualThemeSetupScroll_ > (float)maxScroll) visualThemeSetupScroll_ = (float)maxScroll;

    // Keep focused theme row in view
    if (themeSetupFocus_ < themeCount) {
        const int fr = themeSetupFocus_ / cols;
        if (fr < themeSetupScrollRow_) themeSetupScrollRow_ = fr;
        if (fr >= themeSetupScrollRow_ + visibleRows)
            themeSetupScrollRow_ = fr - visibleRows + 1;
    }

    const float vs = visualThemeSetupScroll_;
    const int startRow = (int)std::floor(vs);
    const float frac = vs - (float)startRow;
    const int gridX = x + (w - gridW) / 2;

    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x + 12, gridTop, x + w - 12, gridBottom);

    for (int r = startRow - 1; r <= startRow + visibleRows + 1; ++r) {
        if (r < 0 || r >= rows) continue;
        const int ry = gridTop + (int)(((float)r - vs) * (float)rowH);
        for (int c = 0; c < cols; ++c) {
            const int idx = r * cols + c;
            if (idx < 0 || idx >= themeCount) continue;
            const auto th = static_cast<::psvitaalive::ColorTheme>(idx);
            unsigned ar, ag, ab;
            colorThemeAccentRgb(th, ar, ag, ab);
            const unsigned accent = RGBA8(ar, ag, ab, 255);
            const unsigned soft = RGBA8(ar, ag, ab, 55);
            const int bx = gridX + c * (btnW + gapX);
            const int by = ry;
            const bool focused = (themeSetupFocus_ == idx);
            const bool selected = (settingsEdit_.colorTheme == th);

            vita2d_draw_rectangle(bx, by, btnW, btnH, SURFACE);
            vita2d_draw_rectangle(bx, by, 5, btnH, accent);
            if (focused) {
                vita2d_draw_rectangle(bx, by, btnW, 2, accent);
                vita2d_draw_rectangle(bx, by + btnH - 2, btnW, 2, accent);
                vita2d_draw_rectangle(bx, by, 2, btnH, accent);
                vita2d_draw_rectangle(bx + btnW - 2, by, 2, btnH, accent);
            } else {
                vita2d_draw_rectangle(bx, by, btnW, 1, BORDER);
            }
            if (selected) {
                vita2d_draw_rectangle(bx + 8, by + 6, btnW - 16, btnH - 12, soft);
            }
            const char* name = colorThemeDisplayName(th);
            const float sc = 0.72f;
            const int tw = vita2d_pgf_text_width(font_, sc, name);
            vita2d_pgf_draw_text(font_, bx + (btnW - tw) / 2 + 4, by + 30, focused ? WHITE : TEXT, sc, name);
        }
    }
    vita2d_disable_clipping();

    if (maxScroll > 0) {
        const int tx = x + w - 14, ty = gridTop, th = gridH;
        vita2d_draw_rectangle(tx, ty, 4, th, BORDER);
        const int thumb = std::max(20, th * visibleRows / std::max(1, rows));
        const float scrollT = std::min(1.f, visualThemeSetupScroll_ / (float)std::max(1, maxScroll));
        const int yy = ty + (int)((th - thumb) * scrollT);
        vita2d_draw_rectangle(tx, yy, 4, thumb, ACCENT);
    }

    // Save button
    const int saveW = 200, saveH = 40;
    const int saveX = x + (w - saveW) / 2;
    const int saveY = y + h - 58;
    const bool saveFocus = (themeSetupFocus_ == themeCount);
    vita2d_draw_rectangle(saveX, saveY, saveW, saveH, saveFocus ? ACCENT : SURFACE2);
    if (saveFocus) {
        vita2d_draw_rectangle(saveX, saveY, saveW, 2, WHITE);
        vita2d_draw_rectangle(saveX, saveY + saveH - 2, saveW, 2, WHITE);
    }
    const char* saveLab = "Save";
    const float ssc = 0.80f;
    const int stw = vita2d_pgf_text_width(font_, ssc, saveLab);
    vita2d_pgf_draw_text(font_, saveX + (saveW - stw) / 2, saveY + 28,
                         saveFocus ? RGBA8(0,0,0,255) : WHITE, ssc, saveLab);

    vita2d_pgf_draw_text(font_, x + 22, y + h - 14, DIM, 0.52f,
        "D-Pad: move   X: apply theme / Save   Touch: tap theme or Save");
}

'''
    cpp = cpp.replace(marker, methods + marker, 1)

# --- Input: block & handle theme setup before news ---
old_news_input = "if(newsVisible_){if(pressed&SCE_CTRL_CIRCLE){closeNewsModal(newsMarkSeenOnClose_);return;}"
if old_news_input not in cpp:
    raise SystemExit("news input block not found")

theme_input = (
    "if(themeSetupVisible_){"
    "const int themeCount=static_cast<int>(::psvitaalive::ColorTheme::Count);"
    "const int cols=3;"
    "const int totalFocus=themeCount+1;"  # themes + Save
    "if(nav&SCE_CTRL_LEFT){if(themeSetupFocus_<themeCount){int c=themeSetupFocus_%cols;if(c>0)--themeSetupFocus_;}return;}"
    "if(nav&SCE_CTRL_RIGHT){if(themeSetupFocus_<themeCount){int c=themeSetupFocus_%cols;if(c<cols-1&&themeSetupFocus_+1<themeCount)++themeSetupFocus_;}return;}"
    "if(nav&SCE_CTRL_UP||(pressed&SCE_CTRL_UP)){"
    "  if(themeSetupFocus_==themeCount){themeSetupFocus_=std::max(0,themeCount-cols);}"
    "  else if(themeSetupFocus_>=cols)themeSetupFocus_-=cols;"
    "  return;}"
    "if(nav&SCE_CTRL_DOWN||(pressed&SCE_CTRL_DOWN)){"
    "  if(themeSetupFocus_<themeCount){int n=themeSetupFocus_+cols;if(n<themeCount)themeSetupFocus_=n;else themeSetupFocus_=themeCount;}"
    "  return;}"
    "if(pressed&SCE_CTRL_CROSS){"
    "  if(themeSetupFocus_==themeCount)closeThemeSetup(true);"
    "  else applyThemeSetupFocus();"
    "  return;}"
    "return;}"
    + old_news_input
)
cpp = cpp.replace(old_news_input, theme_input, 1)

# --- Touch: theme setup before news ---
old_news_touch = "    // --- News modal: drag to scroll + Close tap ---\n    if (newsVisible_) {"
if old_news_touch not in cpp:
    raise SystemExit("news touch block not found")

theme_touch = r'''    // --- First-run theme setup: scroll grid + tap theme / Save ---
    if (themeSetupVisible_) {
        const int themeCount = static_cast<int>(::psvitaalive::ColorTheme::Count);
        const int cols = 3;
        const int rows = (themeCount + cols - 1) / cols;
        const int btnW = 220, btnH = 44, gapX = 12, gapY = 10;
        const int gridW = cols * btnW + (cols - 1) * gapX;
        const int ow = 760, oh = 460;
        const int ox = (SCREEN_W - ow) / 2, oy = (SCREEN_H - oh) / 2;
        const int gridTop = oy + 100;
        const int gridBottom = oy + oh - 78;
        const int gridH = gridBottom - gridTop;
        const int rowH = btnH + gapY;
        const int visibleRows = std::max(1, gridH / rowH);
        const int maxScroll = std::max(0, rows - visibleRows);
        const int gridX = ox + (ow - gridW) / 2;
        const int saveW = 200, saveH = 40;
        const int saveX = ox + (ow - saveW) / 2;
        const int saveY = oy + oh - 58;

        if (td.reportNum > 0) {
            const int px = mapX(td.report[0].x);
            const int py = mapY(td.report[0].y);
            if (!touchDown_) {
                touchDown_ = true;
                touchStartX_ = px;
                touchStartY_ = py;
                touchLastY_ = py;
                touchMoved_ = false;
                touchAccumY_ = 0.f;
            } else {
                const int dy = py - touchLastY_;
                touchLastY_ = py;
                if (std::abs(px - touchStartX_) > 14 || std::abs(py - touchStartY_) > 14)
                    touchMoved_ = true;
                touchAccumY_ += static_cast<float>(-dy);
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
            }
        } else if (touchDown_) {
            const int px = touchStartX_, py = touchStartY_;
            const bool wasMoved = touchMoved_;
            touchDown_ = false;
            touchAccumY_ = 0.f;
            if (!wasMoved) {
                if (hit(px, py, saveX - 8, saveY - 8, saveW + 16, saveH + 16)) {
                    closeThemeSetup(true);
                } else {
                    for (int r = 0; r < rows; ++r) {
                        for (int c = 0; c < cols; ++c) {
                            const int idx = r * cols + c;
                            if (idx >= themeCount) continue;
                            const int bx = gridX + c * (btnW + gapX);
                            const int by = gridTop + (int)(((float)r - visualThemeSetupScroll_) * (float)rowH);
                            if (hit(px, py, bx, by, btnW, btnH)) {
                                themeSetupFocus_ = idx;
                                applyThemeSetupFocus();
                                break;
                            }
                        }
                    }
                }
            }
        }
        return;
    }

''' + old_news_touch
cpp = cpp.replace(old_news_touch, theme_touch, 1)

# --- Draw overlays in full catalog + split ---
for needle in [
    "if(newsVisible_)drawNewsOverlay();",
    "if(newsVisible_)drawNewsOverlay();",
]:
    pass

# replace all news overlay draws to also draw theme setup (theme on top)
old_draw_news = "if(newsVisible_)drawNewsOverlay();"
new_draw_news = "if(newsVisible_)drawNewsOverlay();if(themeSetupVisible_)drawThemeSetupOverlay();"
if old_draw_news not in cpp:
    raise SystemExit("draw news overlay not found")
cpp = cpp.replace(old_draw_news, new_draw_news)  # all occurrences

# --- Smooth scroll in updateAnimations if present ---
if "visualNewsScroll_" in cpp and "visualThemeSetupScroll_" not in cpp.split("void FullCatalogScreen::updateAnimations")[1][:2000] if "void FullCatalogScreen::updateAnimations" in cpp else True:
    # try to add lerp next to news scroll
    if "visualNewsScroll_" in cpp:
        # common pattern: visualNewsScroll_ += (newsScrollLine_ - visualNewsScroll_) * ...
        import re
        m = re.search(r"visualNewsScroll_\s*[\+\-]=[^;]+;", cpp)
        if m:
            snippet = m.group(0)
            # Find a larger context line
            idx = cpp.find(snippet)
            # look for newsScrollLine_ lerp line(s)
            pass

# Simpler: in updateAnimations, after visualNewsScroll handling, add theme setup
if "void FullCatalogScreen::updateAnimations" in cpp:
    ua = cpp.find("void FullCatalogScreen::updateAnimations")
    # find visualNewsScroll_ assignment
    news_vs = cpp.find("visualNewsScroll_", ua)
    if news_vs > 0 and "visualThemeSetupScroll_" not in cpp[ua:ua+2500]:
        # find end of that statement block - inject after first visualNewsScroll_ line containing newsScrollLine_
        chunk = cpp[ua:ua+2500]
        import re
        mm = re.search(r"[^\n]*visualNewsScroll_[^\n]*newsScrollLine_[^\n]*\n", chunk)
        if not mm:
            mm = re.search(r"[^\n]*visualNewsScroll_[^\n]*\n", chunk)
        if mm:
            insert = mm.group(0) + "    visualThemeSetupScroll_ += (static_cast<float>(themeSetupScrollRow_) - visualThemeSetupScroll_) * 0.28f;\n"
            cpp = cpp[:ua] + chunk.replace(mm.group(0), insert, 1) + cpp[ua+2500:]

# Guard openReportConfirm and similar when theme setup visible
cpp = cpp.replace(
    "if (newsVisible_) return;\n    reportConfirmVisible_ = true;",
    "if (newsVisible_ || themeSetupVisible_) return;\n    reportConfirmVisible_ = true;",
    1,
)

# runNewsCheck should not open while theme setup visible
cpp = cpp.replace(
    "void FullCatalogScreen::runNewsCheck(bool forceShow) {\n    if (newsVisible_) return;",
    "void FullCatalogScreen::runNewsCheck(bool forceShow) {\n    if (newsVisible_ || themeSetupVisible_) return;",
    1,
)

CPP.write_text(cpp, encoding="utf-8")
print("cpp patched")

# --- main.cpp: theme setup before news ---
if "startupThemePending" not in main:
    main = main.replace(
        "bool startupImageChoicePending=false;bool startupNewsPending=false;",
        "bool startupImageChoicePending=false;bool startupThemePending=false;bool startupNewsPending=false;",
        1,
    )
    # replace all startupNewsPending=true with theme pending first
    main = main.replace("startupNewsPending=true", "startupThemePending=true")

    old_news_block = '''        if(startupNewsPending && !active && !screen.isNewsVisible()){
            screen.runNewsCheck(false);
            if(screen.isNewsCheckDone() || screen.isNewsVisible()){
                startupNewsPending=false;
            }
        }'''
    new_block = '''        if(startupThemePending && !active && !screen.isThemeSetupVisible()){
            screen.openThemeSetupIfNeeded();
            if(!screen.isThemeSetupVisible()){
                // Already done or just closed without showing
                startupThemePending=false;
                startupNewsPending=true;
            }
        }
        // While theme modal is open, wait (do not open News yet).
        if(startupThemePending && screen.isThemeSetupVisible()){
            // stay pending until user saves
        } else if(startupThemePending && !screen.isThemeSetupVisible()){
            startupThemePending=false;
            startupNewsPending=true;
        }
        if(startupNewsPending && !active && !screen.isNewsVisible() && !screen.isThemeSetupVisible()){
            screen.runNewsCheck(false);
            if(screen.isNewsCheckDone() || screen.isNewsVisible()){
                startupNewsPending=false;
            }
        }'''
    # Try flexible match
    if old_news_block not in main:
        # whitespace flexible
        import re
        pat = r"if\s*\(\s*startupNewsPending\s*&&\s*!active\s*&&\s*!screen\.isNewsVisible\s*\(\s*\)\s*\)\s*\{[^}]*runNewsCheck[^}]*\}"
        m = re.search(pat, main, re.S)
        if not m:
            raise SystemExit("startup news block not found in main")
        main = main[:m.start()] + new_block + main[m.end():]
    else:
        main = main.replace(old_news_block, new_block, 1)

    # When theme modal closes, the next frame should set news pending.
    # Fix logic: while visible keep theme pending; when was pending and not visible -> news
    # Simplify the block further after write

MAIN.write_text(main, encoding="utf-8")
print("main patched")
print("OK theme setup modal")

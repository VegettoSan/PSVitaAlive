#!/usr/bin/env python3
from pathlib import Path
import sys

p = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
cpp = p.read_text(encoding="utf-8")

old_elig = """bool FullCatalogScreen::itemEligibleForDataRequest(const CatalogItem& item) const {
    if (itemHasDataOrGameFiles(item)) return false;
    if (state_.catalog == CatalogType::VitaGames || state_.catalog == CatalogType::PspGames
        || state_.catalog == CatalogType::Ps1Games) return true;
    return categoryWantsDataFiles(item.category);
}"""
new_elig = """bool FullCatalogScreen::itemEligibleForDataRequest(const CatalogItem& item) const {
    // Data/Game Files requests only make sense for homebrew ports/games/emulators.
    if (state_.catalog != CatalogType::Homebrew) return false;
    if (itemHasDataOrGameFiles(item)) return false;
    return categoryWantsDataFiles(item.category);
}"""
if old_elig not in cpp:
    sys.exit("elig block not found")
cpp = cpp.replace(old_elig, new_elig, 1)

old_sq = 'if(pressed&SCE_CTRL_SQUARE){if(!searchQuery_.empty()||dataFilesFilter_){dataFilesFilter_=false;applySearch("");showToast("Filters cleared",1200);return;}if(state_.mode==UiMode::SPLIT_DETAIL&&state_.activePanel==UiPanel::Detail&&!state_.linkNavigation){const int si=selectedIndex();if(si>=0&&itemEligibleForDataRequest(catalogView()[si])){openDataRequestConfirm();return;}}return;}'
new_sq = 'if(pressed&SCE_CTRL_SQUARE){if(state_.mode==UiMode::FULL_CATALOG){if(!searchQuery_.empty()||dataFilesFilter_){dataFilesFilter_=false;applySearch("");showToast("Filters cleared",1200);}return;}if(state_.mode==UiMode::SPLIT_DETAIL&&state_.activePanel==UiPanel::Detail&&!state_.linkNavigation){const int si=selectedIndex();if(si>=0&&itemEligibleForDataRequest(catalogView()[si])){openDataRequestConfirm();return;}}return;}'
if old_sq not in cpp:
    sys.exit("square block not found")
cpp = cpp.replace(old_sq, new_sq, 1)

old_links = """    if (!it.linkDetails.empty()) {
        int bx = x + w - 142, by = y + 12, bw = 128, bh = 28;
        const bool linkOn = state_.linkNavigation;
        const float pulse = linkOn ? focusPulse() : 0.f;
        vita2d_draw_rectangle(bx, by, bw, bh, linkOn ? ACCENT : SURFACE2);
        if (linkOn) {
            const unsigned glow = RGBA8(0x3B, 0xFF, 0x00, static_cast<unsigned>(40 + pulse * 80));
            vita2d_draw_rectangle(bx - 2, by - 2, bw + 4, bh + 4, glow);
            vita2d_draw_rectangle(bx, by, bw, bh, ACCENT);
        }
        vita2d_draw_rectangle(bx, by, bw, 1, ACCENT);
        vita2d_draw_rectangle(bx, by, 1, bh, ACCENT);
        vita2d_draw_rectangle(bx, by + bh - 1, bw, 1, ACCENT);
        vita2d_draw_rectangle(bx + bw - 1, by, 1, bh, ACCENT);
        vita2d_pgf_draw_text(font_, bx + 10, by + 19, linkOn ? BG : ACCENT, 0.56f, linkOn ? "△ Exit link mode" : "△ Select links");
        if (itemEligibleForDataRequest(it)) {
            const int rbx = bx, rby = by + bh + 6, rbw = bw, rbh = 26;
            vita2d_draw_rectangle(rbx, rby, rbw, rbh, SURFACE2);
            vita2d_draw_rectangle(rbx, rby, rbw, 1, ACCENT);
            vita2d_draw_rectangle(rbx, rby, 1, rbh, ACCENT);
            vita2d_draw_rectangle(rbx, rby + rbh - 1, rbw, 1, ACCENT);
            vita2d_draw_rectangle(rbx + rbw - 1, rby, 1, rbh, ACCENT);
            vita2d_pgf_draw_text(font_, rbx + 6, rby + 18, ACCENT, 0.48f, "□ Request data");
        }
    }"""
new_links = """    {
        const int bx = x + w - 142, by = y + 12, bw = 128, bh = 28;
        if (!it.linkDetails.empty()) {
            const bool linkOn = state_.linkNavigation;
            const float pulse = linkOn ? focusPulse() : 0.f;
            vita2d_draw_rectangle(bx, by, bw, bh, linkOn ? ACCENT : SURFACE2);
            if (linkOn) {
                const unsigned glow = RGBA8(0x3B, 0xFF, 0x00, static_cast<unsigned>(40 + pulse * 80));
                vita2d_draw_rectangle(bx - 2, by - 2, bw + 4, bh + 4, glow);
                vita2d_draw_rectangle(bx, by, bw, bh, ACCENT);
            }
            vita2d_draw_rectangle(bx, by, bw, 1, ACCENT);
            vita2d_draw_rectangle(bx, by, 1, bh, ACCENT);
            vita2d_draw_rectangle(bx, by + bh - 1, bw, 1, ACCENT);
            vita2d_draw_rectangle(bx + bw - 1, by, 1, bh, ACCENT);
            vita2d_pgf_draw_text(font_, bx + 10, by + 19, linkOn ? BG : ACCENT, 0.56f, linkOn ? "△ Exit link mode" : "△ Select links");
        }
        if (itemEligibleForDataRequest(it)) {
            // Below Select links when present; same header column when no links.
            const int rby = !it.linkDetails.empty() ? (by + bh + 6) : by;
            const int rbx = bx, rbw = bw, rbh = 26;
            vita2d_draw_rectangle(rbx, rby, rbw, rbh, SURFACE2);
            vita2d_draw_rectangle(rbx, rby, rbw, 1, ACCENT);
            vita2d_draw_rectangle(rbx, rby, 1, rbh, ACCENT);
            vita2d_draw_rectangle(rbx, rby + rbh - 1, rbw, 1, ACCENT);
            vita2d_draw_rectangle(rbx + rbw - 1, rby, 1, rbh, ACCENT);
            vita2d_pgf_draw_text(font_, rbx + 6, rby + 18, ACCENT, 0.48f, "□ Request data");
        }
    }"""
if old_links not in cpp:
    sys.exit("links draw not found")
cpp = cpp.replace(old_links, new_links, 1)

old_rt = """    // Report confirmation modal touch
    if (reportConfirmVisible_) {"""
new_rt = """    // Data/Game Files request confirmation modal touch
    if (dataRequestConfirmVisible_) {
        const int mw = 560, mh = 280;
        const int mx = (SCREEN_W - mw) / 2, my = (SCREEN_H - mh) / 2;
        const int by = my + mh - 56, bh = 40, bw = 180, gap = 24;
        const int bxCancel = mx + (mw - (bw * 2 + gap)) / 2;
        const int bxSend = bxCancel + bw + gap;
        if (hit(x, y, bxCancel, by, bw, bh)) {
            closeDataRequestConfirm();
            return;
        }
        if (hit(x, y, bxSend, by, bw, bh)) {
            closeDataRequestConfirm();
            trySendDataRequest();
            return;
        }
        return; // consume other touches while modal is open
    }

    // Report confirmation modal touch
    if (reportConfirmVisible_) {"""
if old_rt not in cpp:
    sys.exit("report touch not found")
cpp = cpp.replace(old_rt, new_rt, 1)

old_lt = """    // Links button (same coords as drawDetailPanel)
    if (!catalogView().empty()) {
        const int i = selectedIndex();
        if (i >= 0 && !catalogView()[i].linkDetails.empty()) {
            const int bx = dx + dw - 142, by = dy + 12, bw = 128, bh = 28;
            if (hit(x, y, bx, by, bw, bh)) {
                if (state_.linkNavigation) exitLinkNavigation();
                else enterLinkNavigation();
                return;
            }
        }
    }"""
new_lt = """    // Links + Request data buttons (same coords as drawDetailPanel)
    if (!catalogView().empty()) {
        const int i = selectedIndex();
        if (i >= 0) {
            const CatalogItem& tapItem = catalogView()[i];
            const int bx = dx + dw - 142, by = dy + 12, bw = 128, bh = 28;
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
        }
    }"""
if old_lt not in cpp:
    sys.exit("links touch not found")
cpp = cpp.replace(old_lt, new_lt, 1)

p.write_text(cpp, encoding="utf-8")
print("OK", len(cpp))

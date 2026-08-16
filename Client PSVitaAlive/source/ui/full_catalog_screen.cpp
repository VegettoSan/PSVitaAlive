#include "ui/full_catalog_screen.hpp"
#include "diagnostic_logger.hpp"
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/rtc.h>
#include <psp2/io/devctl.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <set>
#include <unordered_set>
#include <utility>
namespace psvitaalive::ui { namespace {

std::string currentTimeLabel() {
    SceDateTime dt{};
    if (sceRtcGetCurrentClockLocalTime(&dt) < 0) return "--:--";
    char buf[16];
    sceClibSnprintf(buf, sizeof(buf), "%02d:%02d", (int)dt.hour, (int)dt.minute);
    return buf;
}

std::string formatBytesShort(uint64_t bytes) {
    char buf[32];
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        sceClibSnprintf(buf, sizeof(buf), "%.2fG", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024ULL * 1024ULL)
        sceClibSnprintf(buf, sizeof(buf), "%.1fM", (double)bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024ULL)
        sceClibSnprintf(buf, sizeof(buf), "%lluK", (unsigned long long)(bytes / 1024ULL));
    else
        sceClibSnprintf(buf, sizeof(buf), "%lluB", (unsigned long long)bytes);
    return buf;
}

struct Ux0SpaceInfo {
    bool ok = false;
    uint64_t freeBytes = 0;
    uint64_t totalBytes = 0;
};

Ux0SpaceInfo queryUx0Space() {
    static Ux0SpaceInfo cached{};
    static uint64_t lastMs = 0;
    const uint64_t nowMs = sceKernelGetProcessTimeWide() / 1000ULL;
    if (lastMs != 0 && nowMs >= lastMs && (nowMs - lastMs) < 3000ULL) return cached;

    struct {
        uint64_t max_size;
        uint64_t free_size;
        uint32_t cluster_size;
        void* unk;
    } info{};
    const int ret = sceIoDevctl("ux0:", 0x3001, nullptr, 0, &info, sizeof(info));
    lastMs = nowMs;
    cached = {};
    if (ret < 0) return cached;
    cached.ok = true;
    cached.freeBytes = info.free_size;
    cached.totalBytes = info.max_size > 0 ? info.max_size : info.free_size;
    return cached;
}

std::string ux0FreeSpaceLabel() {
    const Ux0SpaceInfo s = queryUx0Space();
    if (!s.ok) return "ux0 --";
    char buf[64];
    sceClibSnprintf(buf, sizeof(buf), "%s/%s",
                    formatBytesShort(s.freeBytes).c_str(),
                    formatBytesShort(s.totalBytes).c_str());
    return buf;
}


void drawFooterBar(vita2d_pgf* font, const char* leftHints) {
    vita2d_draw_rectangle(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, SURFACE2);
    if (leftHints && font)
        vita2d_pgf_draw_text(font, 12, SCREEN_H - 14, TEXT, 0.50f, leftHints);
    if (!font) return;

    const Ux0SpaceInfo sp = queryUx0Space();
    const int panelW = 172;
    const int panelH = FOOTER_H - 6;
    const int panelX = SCREEN_W - panelW - 6;
    const int panelY = SCREEN_H - FOOTER_H + 3;
    vita2d_draw_rectangle(panelX, panelY, panelW, panelH, SURFACE);
    vita2d_draw_rectangle(panelX, panelY, 2, panelH, ACCENT);

    if (!sp.ok) {
        vita2d_pgf_draw_text(font, panelX + 10, panelY + 18, DIM, 0.50f, "ux0 n/d");
        return;
    }
    vita2d_pgf_draw_text(font, panelX + 8, panelY + 13, ACCENT, 0.46f, "UX0");
    char line[48];
    sceClibSnprintf(line, sizeof(line), "%s libres", formatBytesShort(sp.freeBytes).c_str());
    vita2d_pgf_draw_text(font, panelX + 34, panelY + 13, WHITE, 0.48f, line);
    sceClibSnprintf(line, sizeof(line), "de %s total", formatBytesShort(sp.totalBytes).c_str());
    vita2d_pgf_draw_text(font, panelX + 8, panelY + 26, TEXT, 0.44f, line);

    const int barX = panelX + 8, barY = panelY + panelH - 7, barW = panelW - 16, barH = 4;
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


void FullCatalogScreen::drawFullCatalog(){vita2d_start_drawing();vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);drawCatalogPanel(0,HEADER_H+TABS_H,SCREEN_W,SCREEN_H-HEADER_H-TABS_H-FOOTER_H,false);drawFooterBar(font_, "D-Pad: Navigate   X: Detail   △: Search   □: Clear   L/R: Catalog   START: Exit");if(catalogLoading_||installProgressActive_)drawLoadingOverlay();if(!catalogError_.empty())vita2d_pgf_draw_text(font_,18,HEADER_H+TABS_H+26,ACCENT,.66f,catalogError_.c_str());drawToast();vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::drawSplitDetail(){vita2d_start_drawing();vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);int top=HEADER_H+TABS_H,hh=SCREEN_H-HEADER_H-TABS_H-FOOTER_H,lw=SCREEN_W/2;drawCatalogPanel(0,top,lw,hh,true);drawDetailPanel(lw,top,SCREEN_W-lw,hh);vita2d_draw_rectangle(lw-1,top,2,hh,BORDER);drawFooterBar(font_, state_.activePanel==UiPanel::Catalog?"PANEL: LISTA  |  → Detail   D-Pad: Navigate   O: Back   L/R: Catalog":"PANEL: DETALLE  |  ← Lista   D-Pad: Scroll   △: Links   X: Action   O: Back");if(catalogLoading_||installProgressActive_)drawLoadingOverlay();drawToast();vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::drawOpeningDetail(){float p=transitionProgress();int lw=SCREEN_W-(int)(SCREEN_W/2*p),rw=SCREEN_W-lw;vita2d_start_drawing();vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);int top=HEADER_H+TABS_H,hh=SCREEN_H-HEADER_H-TABS_H-FOOTER_H;drawCatalogPanel(0,top,lw,hh,true);if(rw>0)drawDetailPanel(lw,top,rw,hh);vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::drawClosingDetail(){float p=1.0f-transitionProgress();int lw=SCREEN_W-(int)(SCREEN_W/2*p),rw=SCREEN_W-lw;vita2d_start_drawing();vita2d_clear_screen();drawHeader(SCREEN_W);drawTabs(SCREEN_W);int top=HEADER_H+TABS_H,hh=SCREEN_H-HEADER_H-TABS_H-FOOTER_H;drawCatalogPanel(0,top,lw,hh,true);if(rw>0)drawDetailPanel(lw,top,SCREEN_W-lw,hh);vita2d_end_drawing();vita2d_swap_buffers();}void FullCatalogScreen::draw(){switch(state_.mode){case UiMode::FULL_CATALOG:drawFullCatalog();break;case UiMode::OPENING_DETAIL:drawOpeningDetail();break;case UiMode::SPLIT_DETAIL:drawSplitDetail();break;case UiMode::CLOSING_DETAIL:drawClosingDetail();break;}}bool FullCatalogScreen::updateAndDraw(){
    if(!ready_)return false;
    flushDeferredTextureFrees();
    if(catalogSwitchCooldownFrames_>0)--catalogSwitchCooldownFrames_;
    // Expire toast
    if(toastExpiresMs_!=0){
        const uint64_t now=sceKernelGetProcessTimeWide()/1000ULL;
        if(now>=toastExpiresMs_){toastMessage_.clear();toastExpiresMs_=0;}
    }
    handleInput();
    handleTouch();
    updateTransition();
    updateAnimations();
    if(catalogSwitchCooldownFrames_==0)prepareVisibleTextures();
    draw();
    return !state_.requestExit;
}
} // namespace psvitaalive::ui

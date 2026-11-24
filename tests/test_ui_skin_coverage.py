"""Regression guard for the second skin coverage pass; not a hardware test."""
from pathlib import Path
import re

root = Path("Client PSVitaAlive")
source = (root / "source/ui/full_catalog_screen.cpp").read_text(encoding="utf-8")
main = (root / "source/main.cpp").read_text(encoding="utf-8")
def body(start, end):
    return source.split(start,1)[1].split(end,1)[0]

checks = [
    ("battery", "void drawBatteryIndicator(", "// OLED protection mode", ("icon_battery", "icon_charging", "state.percent")),
    ("feedback", "void FullCatalogScreen::drawReportConfirmOverlay()", "int FullCatalogScreen::reportWorkerEntry(", ("ui_panel", "ui_input", "ui_button_secondary", "ui_scroll_thumb")),
    ("QR", "void FullCatalogScreen::drawWebQrOverlay()", "void FullCatalogScreen::drawDetailLinks(", ("ui_dialog", "ui_panel_inset", "ui_button_primary", "ui_scroll_thumb", "vita2d_draw_texture_scale")),
    ("detail list", "void FullCatalogScreen::drawCatalogPanel(", "void FullCatalogScreen::wrapText(", ("ui_panel", "ui_scroll_thumb")),
    ("detail links", "void FullCatalogScreen::drawDetailLinks(", "void FullCatalogScreen::drawDetailContent(", ("ui_button_primary", "ui_button_secondary")),
    ("detail header", "void FullCatalogScreen::drawDetailPanel(", "FullCatalogScreen::PrimaryAction FullCatalogScreen::primaryActionFor", ("ui_panel", "ui_button_secondary", "ui_badge")),
    ("startup", "void FullCatalogScreen::drawLoadingOverlay()", "// Burn-in protection", ("ui_dialog", "ui_progress_track", "ui_progress_fill", "catalogLoadingTex_")),
    ("file badges", "void FullCatalogScreen::drawCatalogCard(", "void FullCatalogScreen::drawCatalogPanel(", ("icon_data", "icon_gamepad", "icon_dlc", "0x3A,0x2C,0x10", "0xE8,0xB4,0x3A", "0xFF,0xD2,0x6A")),
]
for label,start,end,markers in checks:
    fragment=body(start,end)
    for marker in markers:
        assert marker in fragment, (label,marker)
for start,end in [
    ("void FullCatalogScreen::drawActivePanelFrame(", "namespace {\nbool readSfoStringKey"),
    ("void FullCatalogScreen::drawThemeSetupOverlay()", "void FullCatalogScreen::"),
    ("void FullCatalogScreen::drawReportConfirmOverlay()", "int FullCatalogScreen::reportWorkerEntry("),
]:
    assert not re.search(r"vita2d_draw_rectangle\([^;]*,\s*WHITE\s*\);",body(start,end))
assert "skin::ui_atlas::ui_dialog" in main and "skin::hint(&font,boxX+28" in main
print("Second-pass UI skin coverage OK")

# Third pass: installed data operations, explicit controller shortcuts and
# footer layout. These are source guards; actual readability is tested on Vita.
installed = (root / "source/ui/installed_data_screen.inc").read_text(encoding="utf-8")
hints = (root / "include/ui/skin_hints.hpp").read_text(encoding="utf-8")
inspector = installed.split("void FullCatalogScreen::drawInstalledDataOverlay()",1)[1]
assert not re.search(r"vita2d_draw_rectangle\([^;]*,\s*WHITE\s*\);",inspector)
assert "ui_progress_track,barX,barY,barW,23" in inspector
assert "ui_progress_track,barX,barY,barW,barH" in inspector
assert re.search(r'skin::hint\([^;]*"ID_RESULT_CONTINUE"',inspector)
assert re.search(r'skin::hint\([^;]*"ID_CONTROLS"',inspector)
summary = body("void FullCatalogScreen::drawInstallAllSummary()", "void FullCatalogScreen::drawInstallAllOverlay()")
assert re.search(r'skin::hint\([^;]*"ALL_REPORT_SELECT_REPORT_HINT"',summary)
assert "ctrl_cross_classic,action,action" in summary
assert "ctrl_select,lab,lab" in summary
download = body("const int cancelH = DOWNLOAD_TOUCH_BUTTON_H;", "void drawFooterBar(")
assert "ctrl_square_classic,label,plab" in download
assert "ctrl_touch_front" not in download
assert "skin::footer(font,12,SCREEN_H-FOOTER_H" in source
assert "constexpr int glyphSize=22" in hints
assert "ctrl_triangle_classic,barX+barW-30" in source
assert "sx+padX+iconW,cy+17" in source
print("Third-pass control and storage skin coverage OK")

# Batched GPU pool: preserve allocations until proven idle, never reset every frame.
renderer = (root / "include/ui/skin_renderer.hpp").read_text(encoding="utf-8")
assert "framePoolBytes = 2u * 1024u * 1024u" in renderer
assert "frameReserveBytes = 1024u * 1024u" in renderer
begin = renderer.split("inline bool beginFrame()",1)[1].split("inline bool ready()",1)[0]
assert "if (vita2d_pool_free_space() < frameReserveBytes)" in begin
assert begin.index("vita2d_wait_rendering_done();") < begin.index("vita2d_pool_reset();") < begin.index("vita2d_start_drawing_advanced(nullptr, 0);")
assert "vita2d_start_drawing();" not in source and "vita2d_start_drawing();" not in main
assert "vita2d_init_advanced(skin::framePoolBytes);" in source
assert "skin::beginFrame()" in source and "skin::beginFrame()" in main
assert "vita2d_pool_memalign(bytes, 16u)" in renderer
assert "std::isfinite" in renderer
frame = body("inline void startSkinFrame()", "constexpr uint32_t NAV_DIRECTION_MASK")
assert "clip_leaks=%u" in frame and "low_pool_frames=%u" in frame
assert "pool_reset_waits=%u" in frame and "pool_reset_idle=%u" in frame
assert "pool_min_scene=%s" in frame and "5000000ULL" in frame
assert "if (i < 0) { vita2d_disable_clipping(); return; }" in source
assert "if(w<=36)" in source
flush = body("void FullCatalogScreen::flushDeferredTextureFrees()", "void FullCatalogScreen::")
assert flush.index("vita2d_wait_rendering_done();") < flush.index("skin::recycleAfterIdle();")
print("Batched GPU arena and safe recycling source checks OK")

# Perf pass: offscreen links keep their original layout and navigation indices.
links = body("void FullCatalogScreen::drawDetailLinks(", "void FullCatalogScreen::drawDetailContent(")
assert "if(!visible(ry,row.isSection?LINK_SECTION_H:LINK_ROW_H)) continue;" in links
assert "if(!visible(btnY,LINK_ROW_H)) continue;" in links
assert "heightOut = linkLayoutTotalHeight(rows) + yOff;" in links
assert "peak_image_us=%llu" in source and "peak_draw_us=%llu" in source
assert "saved ZIP probe slow us=%llu" in source
print("Offscreen link culling and frame phase diagnostics checks OK")

# Image performance pass: source guards for queue-only UI requests and the
# actual decode-attempt budget (an LRU replacement leaves map size unchanged).
image = (root / "source/ui/image_cache.cpp").read_text(encoding="utf-8")
request = image.split("std::string ImageCache::request(",1)[1].split("void ImageCache::preload(",1)[0]
for forbidden in ("sceIoGetstat", "cachedNormalizedPngLooksValid", "pruneSupersededVersions", "sceIoRemove"):
    assert forbidden not in request, forbidden
worker = image.split("int ImageCache::workerMain()",1)[1]
assert "return !candidate.diskChecked;" in worker
assert "cachedNormalizedPngLooksValid(job.path,maxDim)" in worker
assert "job.diskChecked=true;" in worker
assert worker.index("cachedNormalizedPngLooksValid(job.path,maxDim)") < worker.index("http.downloadToFile")
assert "if(cancelledAfterNormalize){if(valid){markReady(job.path);}else{sceIoRemove(job.path.c_str());}" in worker
assert image.count("PNG_IMAGE_FLAG_FAST") == 2
load = body("bool FullCatalogScreen::prepareImageTexture(", "int FullCatalogScreen::detailScreenshotContentOffset(")
assert load.index("textures_.find(path)") < load.index("imageCache_->request(url,ns)")
assert load.index("vita2d_load_PNG_file") < load.index("evictTextureIfNeeded(ns)")
assert "return true;" in load
prepare = body("void FullCatalogScreen::prepareVisibleTextures()", "void FullCatalogScreen::drawImageLoadingPlaceholder(")
assert "textures_.size()>before" not in prepare
assert prepare.count("++loads;") == 4
assert "||isTransitioning()" in prepare
assert "submit_peak_us=%llu swap_peak_us=%llu" in source
print("Image worker disk probe, decode budget and present diagnostics checks OK")

# Only the topmost storage modal is drawn, without a catalog or a hidden
# storage dialog underneath. The normal frame path still owns other modals.
modal = body("void FullCatalogScreen::draw(){", "bool FullCatalogScreen::updateAndDraw()")
assert 'skinFrameScene=installedDataVisible_?"installed_data":"storage";' in modal
assert "if (installedDataVisible_) drawInstalledDataOverlay();" in modal
assert "else drawStorageOverlay();" in modal
assert modal.index("else drawStorageOverlay();") < modal.index("return;",modal.index("else drawStorageOverlay();")) < modal.index("drawFullCatalog();")
assert "storage_draw_peak_us=%llu installed_draw_peak_us=%llu" in modal
finish = body("void FullCatalogScreen::finishUiFrame(){", "void FullCatalogScreen::draw(){")
assert "drawStorageOverlay()" not in finish and "drawInstalledDataOverlay()" not in finish
catalog = body("void FullCatalogScreen::drawCatalogPanel(", "void FullCatalogScreen::wrapText(")
assert catalog.count("catalogCardFocus && i == state_.focusIndex") == 2
assert "navigationFocus_.catalogCardFocused(" in catalog
print("Exclusive navigation focus and topmost storage rendering checks OK")

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

# Safety is independent of instrumentation: keep scissor and arena guards.
assert frame.count("if (vita2d_get_clipping_enabled()) vita2d_disable_clipping();") == 2
assert frame.index("skin::beginFrame()") < frame.index("vita2d_end_drawing()")
assert frame.index("vita2d_end_drawing()") < frame.index("vita2d_swap_buffers()")
assert "diagnostics::log(" not in frame and "sceKernelGetProcessTimeWide()" not in frame
for obsolete in ("clip_leaks", "low_pool_frames", "pool_reset_waits", "pool_reset_idle", "[GPU]", "[Perf]", "skinFrameStartedUs", "skinFrameScene", "perfBeforeFreeUs", "perfBeforeImageUs", "perfWindowStartUs"):
    assert obsolete not in source, obsolete
for obsolete in ("resetFrameCounters", "poolFallbacks", "invalidGeometry", "poolResetWaits", "poolResetIdle", "++calls", "vertices +="):
    assert obsolete not in renderer, obsolete
assert "if (!v) { return false; }" in renderer
assert "vita2d_pool_free_space() < bytes + 64" in renderer
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
assert "savedZipProbeResult_ = savedZipList_(item);" in source
assert "savedZipProbeAtUs_ = sceKernelGetProcessTimeWide();" in source
print("Offscreen link culling and saved ZIP cache checks OK")

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
# Both texture paths must allocate successfully before evicting a resident icon.
queue_upload = load.split('if(queueProtected_ && ns=="app") {',1)[1].split("// Only completed, validated cache files",1)[0]
disk_load = load.split("// Only completed, validated cache files",1)[1]
assert queue_upload.index("takeDecodedIcon") < queue_upload.index("vita2d_create_empty_texture") < queue_upload.index("evictTextureIfNeeded(ns)")
assert queue_upload.index("if(!texture)") < queue_upload.index("evictTextureIfNeeded(ns)")
assert "vita2d_load_PNG_file" not in queue_upload and "sceIoGetstat" not in queue_upload
assert disk_load.index("vita2d_load_PNG_file") < disk_load.index("evictTextureIfNeeded(ns)")
decode = image.split("bool ImageCache::serviceDecodedIcon()",1)[1].split("bool ImageCache::isReady",1)[0]
assert decode.index("sceKernelUnlockMutex") < decode.index("png_image_begin_read_from_file")
assert "png_image_finish_read" in decode and "serviceDecodedIcon()" in worker
assert "image.width<=APP_IMAGE_MAX_DIM" in decode and "image.height<=APP_IMAGE_MAX_DIM" in decode
assert "return true;" in load
prepare = body("void FullCatalogScreen::prepareVisibleTextures()", "void FullCatalogScreen::drawImageLoadingPlaceholder(")
assert "textures_.size()>before" not in prepare
assert prepare.count("++loads;") == 4
assert "||isTransitioning()" in prepare
assert "[Perf]" not in image and "normStart" not in image and "probeUs" not in image
assert "[UI] texture load failed" in load and "[ImageCache] failed" in image
print("Image worker disk probe, decode budget and production error logging checks OK")

# Only the topmost storage modal is drawn, without a catalog or a hidden
# storage dialog underneath. The normal frame path still owns other modals.
modal = body("void FullCatalogScreen::draw(){", "bool FullCatalogScreen::updateAndDraw()")
assert "if (installedDataVisible_) drawInstalledDataOverlay();" in modal
assert "else drawStorageOverlay();" in modal
assert modal.index("else drawStorageOverlay();") < modal.index("return;",modal.index("else drawStorageOverlay();")) < modal.index("drawFullCatalog();")
finish = body("void FullCatalogScreen::finishUiFrame(){", "void FullCatalogScreen::draw(){")
assert "drawStorageOverlay()" not in finish and "drawInstalledDataOverlay()" not in finish
catalog = body("void FullCatalogScreen::drawCatalogPanel(", "void FullCatalogScreen::wrapText(")
assert catalog.count("catalogCardFocus && i == state_.focusIndex") == 2
assert "navigationFocus_.catalogCardFocused(" in catalog
print("Exclusive navigation focus and topmost storage rendering checks OK")

# Storage scan loading UI uses existing assets and real worker phase labels,
# with bounded indeterminate animation, not a simulated percentage.
storage_loading = body("void FullCatalogScreen::drawStorageOverlay()", "const auto& snap=storageSnapshot_;")
assert "ui_progress_track" in storage_loading and "ui_progress_fill" in storage_loading
assert "Layout::loadingSegmentOffset" in storage_loading
assert "storageWorkPhase_.load" in storage_loading
assert "STORAGE_WAIT" in storage_loading
worker_storage = body("int FullCatalogScreen::storageWorkerEntry(", "void FullCatalogScreen::rebuildStorageRows()")
for phase in ("ScanStorage", "ResolveZips", "ScanInstalled"):
    assert "storageWorkPhase_.store(StorageWorkPhase::"+phase in worker_storage
print("Storage loading activity indicator and worker phases checks OK")

# Split-view focus outline must not obscure the content with LIST/DETAIL chips.
panel_frame = body("void FullCatalogScreen::drawActivePanelFrame(", "namespace {\nbool readSfoStringKey")
assert "skin::focus(" in panel_frame
assert "ui_badge" not in panel_frame and "uiDrawText" not in panel_frame
assert source.count("drawActivePanelFrame(x + 2, y + 2, w - 4, h - 4);") == 2
assert "((nav&SCE_CTRL_RIGHT)||(pressed&SCE_CTRL_CROSS))&&selectedIndex()>=0" in source
split_tap = body("// --- Left list ---", "// --- Right detail panel ---")
assert "state_.activePanel = UiPanel::Detail;" in split_tap
assert "releaseScreenshotTextures();" in split_tap and "state_.linkNavigation=false;" in split_tap
assert source.count("if (touchStartX_ >= SCREEN_W / 2)") == 2
scan_loading = inspector.split("if (installedDataWorker_ >= 0 && installedDataAction_ != -3)",1)[1].split("if (installedDataResultVisible_)",1)[0]
for marker in ("ui_panel_inset","icon_storage","ui_progress_track","ui_progress_fill","StorageLayout::loadingSegmentOffset","knownTotal","ID_APPS_PROGRESS","ID_FOLDERS_READ","ID_FILES_READ","ID_CANCEL_SCAN"):
    assert marker in scan_loading, marker
assert "phase>=2 && total>0" in scan_loading
print("Split panel focus and installed-data loading style checks OK")

# Theme picker uses shared touch/render geometry; each tile exposes its accent.
# Focus uses the active theme accent with a thick frame and dark interior.
# Closing persists the last explicitly applied theme, never the highlighted card.
picker = body("void FullCatalogScreen::drawThemeSetupOverlay()", "void FullCatalogScreen::runNewsCheck(")
assert "ThemePickerLayout" in picker and "skin::ui_atlas::ui_badge,bx+10" in picker
assert "colorThemeAccentRgb(th" in picker and "ctrl_cross_classic" in picker
assert source.count("using P=ThemePickerLayout;") == 2
assert "if(pressed&SCE_CTRL_CIRCLE){closeThemeSetup(true);return;}" in source
close_theme = body("void FullCatalogScreen::closeThemeSetup(", "void FullCatalogScreen::drawThemeSetupOverlay()")
assert "themeSetupFocus_" not in close_theme
assert "settingsEdit_.colorTheme =" not in close_theme
assert "settingsEdit_.colorTheme=" not in close_theme
assert close_theme.index("applyColorTheme(settingsEdit_.colorTheme, false)") < close_theme.index("settingsSave_(settingsEdit_)")
apply_theme = body("void FullCatalogScreen::applyThemeSetupFocus()", "void FullCatalogScreen::closeThemeSetup(")
assert "settingsEdit_.colorTheme = t;" in apply_theme
assert "themeSetupAppliedFocus_ = themeSetupFocus_;" in apply_theme
assert "applyColorTheme(t);" in apply_theme
assert source.count("themeSetupAppliedFocus_ = themeSetupFocus_;") == 2  # open + explicit apply
assert "if(themeSetupFocus_==themeCount||themeSetupAppliedFocus_==themeSetupFocus_)closeThemeSetup(true);" in source
assert "else{applyThemeSetupFocus();}" in source
assert re.search(r"if \(themeSetupAppliedFocus_ == themeSetupFocus_\) \{\s*closeThemeSetup\(true\);\s*\} else \{\s*applyThemeSetupFocus\(\);", source)
assert "if (hit(px, py, saveX, saveY, saveW, saveH)) {\n                    closeThemeSetup(true);" in source
assert "constexpr int focusBorder = 4;" in picker
assert "const unsigned focusAccent = ACCENT;" in picker
assert "ui_button_primary,bx,by,bw,bh,focusAccent" in picker
assert "bx+focusBorder,by+focusBorder,bw-2*focusBorder,bh-2*focusBorder" in picker
assert "lerpColorU(tile,focusAccent,.10f)" in picker
assert "drawPickerButton(bx,by,btnW,btnH,tile,withAlpha(accent,100),focused)" in picker
assert "drawPickerButton(saveX,saveY,saveW,saveH,SURFACE2,BORDER,saveFocus)" in picker
assert "drawMarqueeText(&font_,bx+55,by+34,btnW-69,RGBA8(248,248,248,255)" in picker
assert "focusFill" not in picker and "focusInk" not in picker
assert "const bool selected = (settingsEdit_.colorTheme == th);" in picker
settings = body("void FullCatalogScreen::drawSettings()", "bool FullCatalogScreen::chromeNavigationAvailable()")
assert "icon_settings" in settings and "ui_toggle_on" in settings
assert "drawMarqueeText(&font_,tx,y+21" in settings and "drawMarqueeText(&font_,tx,y+42" in settings
assert "if(y+rowH>contentTop && y<listClipBottom)" in settings
assert "settingsInfoScrollY_-dy" in source and "if(touchMoved_){touchMoved_=false;touchAccumY_=0.f;return;}" in source
print("Theme picker geometry, save semantics and settings readability checks OK")

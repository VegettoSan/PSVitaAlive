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

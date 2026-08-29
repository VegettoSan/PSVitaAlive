#!/usr/bin/env python3
"""News chip = Install All style (pulse border); coherent install/extract wait hints."""
from __future__ import annotations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FCS = ROOT / "Client PSVitaAlive/source/ui/full_catalog_screen.cpp"


def main() -> int:
    text = FCS.read_text(encoding="utf-8")
    changed = False

    old_news = '''void FullCatalogScreen::drawNewsChip() {
    // Left of Report chip (which is left of ux0 panel)
    const int panelW = 220;
    const int panelX = SCREEN_W - panelW - 6;
    const int chipW = 92;
    const int chipH = FOOTER_H - 8;
    const int reportW = 100;
    const int chipX = panelX - reportW - 8 - chipW - 8;
    const int chipY = SCREEN_H - FOOTER_H + 4;
    if (!font_) return;
    const unsigned BLACK = RGBA8(0, 0, 0, 255);
    // Primary solid green button style (same as other accent CTAs)
    const unsigned fill = ACCENT;
    const unsigned textCol = BLACK;
    vita2d_draw_rectangle(chipX, chipY, chipW, chipH, fill);
    const char* lab = "News";
    const float scale = 0.70f;
    const int tw = vita2d_pgf_text_width(font_, scale, lab);
    const int th = 20;
    vita2d_pgf_draw_text(font_, chipX + (chipW - tw) / 2, chipY + (chipH + th) / 2 - 2, textCol, scale, lab);
}'''

    new_news = '''void FullCatalogScreen::drawNewsChip() {
    // Left of Report chip (which is left of ux0 panel)
    const int panelW = 220;
    const int panelX = SCREEN_W - panelW - 6;
    const int chipW = 92;
    const int chipH = FOOTER_H - 8;
    const int reportW = 100;
    const int chipX = panelX - reportW - 8 - chipW - 8;
    const int chipY = SCREEN_H - FOOTER_H + 4;
    if (!font_) return;
    // Match Install All CTA: SURFACE2 fill + soft green border pulse
    const float pulse = 0.40f + 0.60f * focusPulse();
    const unsigned borderA = (unsigned)(120.f + 135.f * pulse);
    const unsigned borderCol = RGBA8(0x3B, 0xFF, 0x00, borderA > 255 ? 255 : borderA);
    const unsigned fill = SURFACE2;
    const int bwPulse = 2 + (int)(1.5f * pulse);
    vita2d_draw_rectangle(chipX, chipY, chipW, chipH, borderCol);
    vita2d_draw_rectangle(chipX + bwPulse, chipY + bwPulse,
                          chipW - bwPulse * 2, chipH - bwPulse * 2, fill);
    const char* lab = "News";
    const float scale = 0.70f;
    const int tw = vita2d_pgf_text_width(font_, scale, lab);
    const int th = 20;
    vita2d_pgf_draw_text(font_, chipX + (chipW - tw) / 2, chipY + (chipH + th) / 2 - 2, ACCENT, scale, lab);
}'''

    if "Match Install All CTA" in text:
        print("news chip: already styled")
    elif old_news not in text:
        raise SystemExit("drawNewsChip block not found")
    else:
        text = text.replace(old_news, new_news, 1)
        changed = True
        print("news chip: Install All pulse style")

    old_hint = '''const bool msgRetry =
    installProgressMessage_.find("retrying") != std::string::npos ||
    installProgressMessage_.find("Retry") != std::string::npos ||
    installProgressMessage_.find("retry") != std::string::npos;
const bool stageExtract =
    installProgressStage_.find("Extract") != std::string::npos ||
    installProgressStage_.find("extract") != std::string::npos ||
    installProgressStage_.find("ZIP") != std::string::npos;
const bool stageInstall = (installProgressStage_ == "Installing");
const bool stageDownload =
    installProgressStage_ == "Downloading" ||
    installProgressStage_ == "BGDL" ||
    installProgressStage_ == "Cancelling" ||
    installProgressStage_.empty();
// Sliding bar while connecting, retrying, or when we still have no size/bytes yet.
const bool indeterminate =
    msgRetry ||
    total == 0 ||
    (current == 0 && installProgressSpeed_ == 0 && (stageDownload || stageInstall || stageExtract));
'''

    old_footer = '''// Context hint so users don't think the app froze while connecting/retrying.
const char* waitHint = nullptr;
if (indeterminate) {
  if (msgRetry)
    waitHint = "Retrying connection — please wait. This is not an error.";
  else if (stageExtract)
    waitHint = "Extracting — large files can take a while. This is not an error.";
  else if (stageInstall)
    waitHint = "Installing — please wait. This is not frozen.";
  else
    waitHint = "Connecting to the server — may take a moment. This is not an error.";
}
if (waitHint)
  vita2d_pgf_draw_text(font_,x+28,y+242,ACCENT,.52f,waitHint);
else
  vita2d_pgf_draw_text(font_,x+28,y+242,DIM,.50f,"Speed depends on your internet connection — please be patient.");
'''

    new_stage = '''const bool msgRetry =
    installProgressMessage_.find("retrying") != std::string::npos ||
    installProgressMessage_.find("Retry") != std::string::npos ||
    installProgressMessage_.find("retry") != std::string::npos;
const bool stageExtract =
    installProgressStage_.find("Extract") != std::string::npos ||
    installProgressStage_.find("extract") != std::string::npos ||
    installProgressStage_.find("ZIP") != std::string::npos ||
    installProgressStage_.find("Unzip") != std::string::npos;
const bool stageInstall =
    installProgressStage_ == "Installing" ||
    installProgressStage_.find("Install") != std::string::npos ||
    installProgressStage_.find("Promote") != std::string::npos ||
    installProgressStage_.find("promote") != std::string::npos;
const bool stageDownload =
    !stageExtract && !stageInstall && (
    installProgressStage_ == "Downloading" ||
    installProgressStage_ == "BGDL" ||
    installProgressStage_ == "Cancelling" ||
    installProgressStage_.empty());
// Sliding bar while connecting/retrying (download) or waiting with no bytes yet.
// Install/extract use the bar too when progress is unknown, but never the "server" copy.
const bool indeterminate =
    msgRetry ||
    total == 0 ||
    (current == 0 && installProgressSpeed_ == 0 && (stageDownload || stageInstall || stageExtract));
'''

    new_footer = '''// Footer must match the real phase — never "Connecting..." during install/extract.
const char* waitHint = nullptr;
if (msgRetry && stageDownload)
  waitHint = "Retrying connection — please wait. This is not an error.";
else if (msgRetry && (stageInstall || stageExtract))
  waitHint = "Retrying this step — please wait. This is not an error.";
else if (stageExtract)
  waitHint = "Extracting files — large archives can take a while. This is not an error.";
else if (stageInstall)
  waitHint = "Installing on the console — please wait. This is not frozen.";
else if (indeterminate && stageDownload)
  waitHint = "Connecting to the server — may take a moment. This is not an error.";
else if (stageDownload)
  waitHint = "Speed depends on your internet connection — please be patient.";
else
  waitHint = "Please wait — this step can take a moment.";
vita2d_pgf_draw_text(font_,x+28,y+242,
    (stageDownload && !indeterminate && !msgRetry) ? DIM : ACCENT,
    .52f, waitHint);
'''

    if "never the \"server\" copy" in text:
        print("hints: already coherent")
    else:
        if old_hint not in text:
            raise SystemExit("stage detection block not found")
        if old_footer not in text:
            raise SystemExit("waitHint footer not found")
        text = text.replace(old_hint, new_stage, 1)
        text = text.replace(old_footer, new_footer, 1)
        changed = True
        print("hints: install/extract vs download fixed")

    if not changed:
        print("no changes")
        return 0
    FCS.write_text(text, encoding="utf-8")
    print("OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

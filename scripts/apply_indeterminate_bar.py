#!/usr/bin/env python3
"""Indeterminate sliding progress bar + wait hints when connecting/retrying."""
from __future__ import annotations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FCS = ROOT / "Client PSVitaAlive/source/ui/full_catalog_screen.cpp"


def main() -> int:
    text = FCS.read_text(encoding="utf-8")
    if "Connecting to the server — may take a moment" in text:
        print("already patched")
        return 0

    old = r'''std::string file=installProgressFile_.empty()?"Preparing...":ellipsize(installProgressFile_,72);
vita2d_pgf_draw_text(font_,x+28,y+108,TEXT,.62f,file.c_str());
const uint64_t total=installProgressTotal_,current=std::min<uint64_t>(installProgressCurrent_,total?total:installProgressCurrent_);
const uint64_t pct=total?std::min<uint64_t>(100,(current*100)/total):0;
int bx=x+28,by=y+140,bw=w-56,bh=12;
vita2d_draw_rectangle(bx,by,bw,bh,BORDER);
if (total == 0 && installProgressStage_ == "BGDL") {
  // Indeterminate pulse while license/queue runs on the worker thread
  const float t = (float)(sceKernelGetProcessTimeWide() / 1000ULL % 1000) / 1000.f;
  const int pulse = (int)(bw * (0.25f + 0.5f * (t < 0.5f ? t * 2.f : (2.f - t * 2.f))));
  const int off = (int)((bw - pulse) * t);
  if (pulse > 0) vita2d_draw_rectangle(bx + off, by, pulse, bh, ACCENT);
} else {
  vita2d_draw_rectangle(bx,by,bw*(int)pct/100,bh,ACCENT);
}
char stats[220];
sceClibSnprintf(stats,sizeof(stats),"%llu%%  %s / %s  •  %s/s",(unsigned long long)pct,formatBytes(current).c_str(),total?formatBytes(total).c_str():"?",formatBytes(installProgressSpeed_).c_str());
vita2d_pgf_draw_text(font_,x+28,y+168,TEXT,.58f,stats);
uint64_t eta=0;if(installProgressSpeed_>0&&total>current)eta=(total-current)/installProgressSpeed_;
char info[180];
sceClibSnprintf(info,sizeof(info),"File: 1 / 1   ETA: %s",formatEta(eta).c_str());
vita2d_pgf_draw_text(font_,x+28,y+194,ACCENT,.62f,info);
if(!installProgressMessage_.empty())vita2d_pgf_draw_text(font_,x+28,y+218,DIM,.54f,ellipsize(installProgressMessage_,82).c_str());
// Soft reminder — speed depends on the user's connection
vita2d_pgf_draw_text(font_,x+28,y+242,DIM,.50f,"Speed depends on your internet connection — please be patient.");
'''

    new = r'''std::string file=installProgressFile_.empty()?"Preparing...":ellipsize(installProgressFile_,72);
vita2d_pgf_draw_text(font_,x+28,y+108,TEXT,.62f,file.c_str());
const uint64_t total=installProgressTotal_,current=std::min<uint64_t>(installProgressCurrent_,total?total:installProgressCurrent_);
const uint64_t pct=total?std::min<uint64_t>(100,(current*100)/total):0;
int bx=x+28,by=y+140,bw=w-56,bh=12;
vita2d_draw_rectangle(bx,by,bw,bh,BORDER);
const bool msgRetry =
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
if (indeterminate) {
  // Bounce a segment left↔right (same idea as catalog loading strip).
  const float t = (float)(sceKernelGetProcessTimeWide() / 1000ULL % 1400) / 1400.f;
  const float u = (t < 0.5f) ? (t * 2.f) : (2.f - t * 2.f);
  const int pulse = std::max(28, (int)(bw * 0.28f));
  const int off = (int)((bw - pulse) * u);
  if (pulse > 0) vita2d_draw_rectangle(bx + off, by, pulse, bh, ACCENT);
} else {
  vita2d_draw_rectangle(bx,by,bw*(int)pct/100,bh,ACCENT);
}
char stats[220];
if (indeterminate) {
  sceClibSnprintf(stats,sizeof(stats),"…  %s / %s  •  %s/s",
      formatBytes(current).c_str(),
      total?formatBytes(total).c_str():"?",
      formatBytes(installProgressSpeed_).c_str());
} else {
  sceClibSnprintf(stats,sizeof(stats),"%llu%%  %s / %s  •  %s/s",(unsigned long long)pct,formatBytes(current).c_str(),total?formatBytes(total).c_str():"?",formatBytes(installProgressSpeed_).c_str());
}
vita2d_pgf_draw_text(font_,x+28,y+168,TEXT,.58f,stats);
uint64_t eta=0;if(installProgressSpeed_>0&&total>current)eta=(total-current)/installProgressSpeed_;
char info[180];
if (indeterminate)
  sceClibSnprintf(info,sizeof(info),"File: 1 / 1   ETA: —");
else
  sceClibSnprintf(info,sizeof(info),"File: 1 / 1   ETA: %s",formatEta(eta).c_str());
vita2d_pgf_draw_text(font_,x+28,y+194,ACCENT,.62f,info);
if(!installProgressMessage_.empty())vita2d_pgf_draw_text(font_,x+28,y+218,DIM,.54f,ellipsize(installProgressMessage_,82).c_str());
// Context hint so users don't think the app froze while connecting/retrying.
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

    if old not in text:
        raise SystemExit("progress UI block not found")
    FCS.write_text(text.replace(old, new, 1), encoding="utf-8")
    print("OK: indeterminate bar + wait hints")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Theme-aware hardcoded greens + larger install overlay text."""
from pathlib import Path

CPP = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
cpp = CPP.read_text(encoding="utf-8")
orig = cpp

# --- 1) Search bar borders: hardcoded neon -> ACCENT variants ---
old_search = """    vita2d_draw_rectangle(barX, barY, barW, barH, SURFACE);
    vita2d_draw_rectangle(barX - 1, barY - 1, barW + 2, 1, RGBA8(0x3B, 0xFF, 0x00, 50));
    vita2d_draw_rectangle(barX - 1, barY + barH, barW + 2, 1, RGBA8(0x3B, 0xFF, 0x00, 50));
    vita2d_draw_rectangle(barX - 1, barY - 1, 1, barH + 2, RGBA8(0x3B, 0xFF, 0x00, 50));
    vita2d_draw_rectangle(barX + barW, barY - 1, 1, barH + 2, RGBA8(0x3B, 0xFF, 0x00, 50));
    vita2d_draw_rectangle(barX, barY, barW, 1, RGBA8(0x3B, 0xFF, 0x00, 140));
    vita2d_draw_rectangle(barX, barY + barH - 1, barW, 1, RGBA8(0x3B, 0xFF, 0x00, 140));
    vita2d_draw_rectangle(barX, barY, 1, barH, RGBA8(0x3B, 0xFF, 0x00, 140));
    vita2d_draw_rectangle(barX + barW - 1, barY, 1, barH, RGBA8(0x3B, 0xFF, 0x00, 140));
"""
new_search = """    vita2d_draw_rectangle(barX, barY, barW, barH, SURFACE);
    vita2d_draw_rectangle(barX - 1, barY - 1, barW + 2, 1, withAlpha(ACCENT, 50));
    vita2d_draw_rectangle(barX - 1, barY + barH, barW + 2, 1, withAlpha(ACCENT, 50));
    vita2d_draw_rectangle(barX - 1, barY - 1, 1, barH + 2, withAlpha(ACCENT, 50));
    vita2d_draw_rectangle(barX + barW, barY - 1, 1, barH + 2, withAlpha(ACCENT, 50));
    vita2d_draw_rectangle(barX, barY, barW, 1, withAlpha(ACCENT, 140));
    vita2d_draw_rectangle(barX, barY + barH - 1, barW, 1, withAlpha(ACCENT, 140));
    vita2d_draw_rectangle(barX, barY, 1, barH, withAlpha(ACCENT, 140));
    vita2d_draw_rectangle(barX + barW - 1, barY, 1, barH, withAlpha(ACCENT, 140));
"""
if old_search not in cpp:
    raise SystemExit("search bar block not found")
cpp = cpp.replace(old_search, new_search, 1)

# --- 2) Toast borders ---
old_toast = """    vita2d_draw_rectangle(x, y, tw, th, RGBA8(0x18, 0x18, 0x18, alpha));
    vita2d_draw_rectangle(x, y, tw, 2, RGBA8(0x3B, 0xFF, 0x00, alpha));
    vita2d_draw_rectangle(x, y + th - 1, tw, 1, RGBA8(0x3B, 0xFF, 0x00, static_cast<unsigned>(alpha * 0.5f)));
    if (font_)
        vita2d_pgf_draw_text(font_, x + 16, y + 26, RGBA8(255, 255, 255, alpha), 0.64f, toastMessage_.c_str());
"""
new_toast = """    vita2d_draw_rectangle(x, y, tw, th, RGBA8(0x18, 0x18, 0x18, alpha));
    vita2d_draw_rectangle(x, y, tw, 2, withAlpha(ACCENT, alpha));
    vita2d_draw_rectangle(x, y + th - 1, tw, 1, withAlpha(ACCENT, static_cast<unsigned>(alpha * 0.5f)));
    if (font_)
        vita2d_pgf_draw_text(font_, x + 16, y + 26, RGBA8(255, 255, 255, alpha), 0.70f, toastMessage_.c_str());
"""
if old_toast not in cpp:
    raise SystemExit("toast block not found")
cpp = cpp.replace(old_toast, new_toast, 1)

# --- 3) Active panel frame glow ---
old_apf = """    const unsigned glow = RGBA8(0x3B, 0xFF, 0x00, static_cast<unsigned>(40 + pulse * 70));
    const unsigned solid = ACCENT;
"""
new_apf = """    const unsigned glow = withAlpha(ACCENT, static_cast<unsigned>(40 + pulse * 70));
    const unsigned solid = ACCENT;
"""
if old_apf not in cpp:
    raise SystemExit("active panel frame glow not found")
cpp = cpp.replace(old_apf, new_apf, 1)

# --- 4) Catalog card focus glow ---
old_card = """        const unsigned glow = RGBA8(0x3B, 0xFF, 0x00, static_cast<unsigned>(55 + pulse * 100));
"""
new_card = """        const unsigned glow = withAlpha(ACCENT, static_cast<unsigned>(55 + pulse * 100));
"""
if old_card not in cpp:
    raise SystemExit("card focus glow not found")
cpp = cpp.replace(old_card, new_card, 1)

# --- 5) Detail link-mode glow ---
old_link = """                const unsigned glow = RGBA8(0x3B, 0xFF, 0x00, static_cast<unsigned>(40 + pulse * 80));
"""
new_link = """                const unsigned glow = withAlpha(ACCENT, static_cast<unsigned>(40 + pulse * 80));
"""
if old_link not in cpp:
    raise SystemExit("link mode glow not found")
cpp = cpp.replace(old_link, new_link, 1)

# --- 6) Catalog splash strip accent ---
old_strip = """    vita2d_draw_rectangle(0, stripY, SCREEN_W, 3, RGBA8(0x3B, 0xFF, 0x00, ta > 255 ? 255 : ta));
"""
new_strip = """    vita2d_draw_rectangle(0, stripY, SCREEN_W, 3, withAlpha(ACCENT, ta > 255 ? 255 : ta));
"""
if old_strip not in cpp:
    raise SystemExit("splash strip not found")
cpp = cpp.replace(old_strip, new_strip, 1)

# --- 7) Install overlay: larger panel + readable text + roomier lock banner ---
old_panel = "const int w=640,h=400,x=(SCREEN_W-w)/2,y=(SCREEN_H-h)/2;"
new_panel = "const int w=700,h=440,x=(SCREEN_W-w)/2,y=(SCREEN_H-h)/2;"
if old_panel not in cpp:
    raise SystemExit("install panel size not found")
cpp = cpp.replace(old_panel, new_panel, 1)

# Stage title + file + bar + stats + info + message + waitHint + lock banner + cancel
old_progress_tail = """{
  const char* title = "Installing";
  if (installProgressStage_ == "BGDL") title = "Preparing download";
  else if (installProgressStage_ == "Downloading" || installProgressStage_ == "Cancelling" || installProgressStage_.empty())
    title = "Downloading";
  else if (installProgressStage_ == "Installing") title = "Installing";
  else if (!installProgressStage_.empty()) title = installProgressStage_.c_str();
  vita2d_pgf_draw_text(font_,x+28,y+76,WHITE,1.00f,title);
}
std::string file=installProgressFile_.empty()?"Preparing...":ellipsize(installProgressFile_,72);
vita2d_pgf_draw_text(font_,x+28,y+108,TEXT,.62f,file.c_str());
const uint64_t total=installProgressTotal_,current=std::min<uint64_t>(installProgressCurrent_,total?total:installProgressCurrent_);
const uint64_t pct=total?std::min<uint64_t>(100,(current*100)/total):0;
int bx=x+28,by=y+140,bw=w-56,bh=12;
"""

# Keep the progress bar logic the same; only bump sizes/positions later in the banner block.
# Replace text scales and banner layout.
old_stats_draw = """vita2d_pgf_draw_text(font_,x+28,y+168,TEXT,.58f,stats);
uint64_t eta=0;if(installProgressSpeed_>0&&total>current)eta=(total-current)/installProgressSpeed_;
char info[180];
if (indeterminate)
  sceClibSnprintf(info,sizeof(info),"File: 1 / 1   ETA: —");
else
  sceClibSnprintf(info,sizeof(info),"File: 1 / 1   ETA: %s",formatEta(eta).c_str());
vita2d_pgf_draw_text(font_,x+28,y+194,ACCENT,.62f,info);
if(!installProgressMessage_.empty())vita2d_pgf_draw_text(font_,x+28,y+218,DIM,.54f,ellipsize(installProgressMessage_,82).c_str());
"""
new_stats_draw = """vita2d_pgf_draw_text(font_,x+28,y+172,TEXT,.66f,stats);
uint64_t eta=0;if(installProgressSpeed_>0&&total>current)eta=(total-current)/installProgressSpeed_;
char info[180];
if (indeterminate)
  sceClibSnprintf(info,sizeof(info),"File: 1 / 1   ETA: —");
else
  sceClibSnprintf(info,sizeof(info),"File: 1 / 1   ETA: %s",formatEta(eta).c_str());
vita2d_pgf_draw_text(font_,x+28,y+198,ACCENT,.70f,info);
if(!installProgressMessage_.empty())vita2d_pgf_draw_text(font_,x+28,y+224,DIM,.60f,ellipsize(installProgressMessage_,78).c_str());
"""
if old_stats_draw not in cpp:
    raise SystemExit("stats draw block not found")
cpp = cpp.replace(old_stats_draw, new_stats_draw, 1)

old_title_file = """  vita2d_pgf_draw_text(font_,x+28,y+76,WHITE,1.00f,title);
}
std::string file=installProgressFile_.empty()?"Preparing...":ellipsize(installProgressFile_,72);
vita2d_pgf_draw_text(font_,x+28,y+108,TEXT,.62f,file.c_str());
const uint64_t total=installProgressTotal_,current=std::min<uint64_t>(installProgressCurrent_,total?total:installProgressCurrent_);
const uint64_t pct=total?std::min<uint64_t>(100,(current*100)/total):0;
int bx=x+28,by=y+140,bw=w-56,bh=12;
"""
new_title_file = """  vita2d_pgf_draw_text(font_,x+28,y+72,WHITE,1.12f,title);
}
std::string file=installProgressFile_.empty()?"Preparing...":ellipsize(installProgressFile_,68);
vita2d_pgf_draw_text(font_,x+28,y+106,TEXT,.72f,file.c_str());
const uint64_t total=installProgressTotal_,current=std::min<uint64_t>(installProgressCurrent_,total?total:installProgressCurrent_);
const uint64_t pct=total?std::min<uint64_t>(100,(current*100)/total):0;
int bx=x+28,by=y+138,bw=w-56,bh=14;
"""
if old_title_file not in cpp:
    raise SystemExit("title/file block not found")
cpp = cpp.replace(old_title_file, new_title_file, 1)

old_wait_banner = """vita2d_pgf_draw_text(font_,x+28,y+230,
    (stageDownload && !indeterminate && !msgRetry) ? DIM : ACCENT,
    .50f, waitHint);
// High-visibility lock banner — must be obvious on device.
{
  const int bx = x + 12, by = y + 248, bw = w - 24, bh = 36;
  vita2d_draw_rectangle(bx, by, bw, bh, RGBA8(0x40, 0x10, 0x10, 255));
  vita2d_draw_rectangle(bx, by, bw, 2, RED);
  vita2d_draw_rectangle(bx, by + bh - 2, bw, 2, RED);
  vita2d_draw_rectangle(bx, by, 4, bh, RED);
  vita2d_draw_rectangle(bx + bw - 4, by, 4, bh, RED);
  vita2d_pgf_draw_text(font_, bx + 12, by + 15, RED, .52f,
      "LOCKED: PS button and power menu disabled");
  vita2d_pgf_draw_text(font_, bx + 12, by + 30, WHITE, .48f,
      "Screen stays ON. Do NOT force power-off until finished.");
}
const int by2=y+292,bw2=330,bh2=38;
vita2d_draw_rectangle(x+28,by2,bw2,bh2,SURFACE2);
vita2d_draw_rectangle(x+28,by2,bw2,1,BORDER);
vita2d_pgf_draw_text(font_,x+92,by2+25,WHITE,.60f,"CIRCLE  CANCEL DOWNLOAD");
vita2d_pgf_draw_text(font_,x+28,y+h-12,DIM,.46f,
    "Only CIRCLE works now  |  PS / power menu blocked  |  Screen forced on");
}
"""
new_wait_banner = """vita2d_pgf_draw_text(font_,x+28,y+248,
    (stageDownload && !indeterminate && !msgRetry) ? DIM : ACCENT,
    .58f, waitHint);
// High-visibility lock banner — larger type for Vita screen readability.
{
  const int bx = x + 16, by = y + 268, bw = w - 32, bh = 52;
  vita2d_draw_rectangle(bx, by, bw, bh, RGBA8(0x40, 0x10, 0x10, 255));
  vita2d_draw_rectangle(bx, by, bw, 2, RED);
  vita2d_draw_rectangle(bx, by + bh - 2, bw, 2, RED);
  vita2d_draw_rectangle(bx, by, 4, bh, RED);
  vita2d_draw_rectangle(bx + bw - 4, by, 4, bh, RED);
  vita2d_pgf_draw_text(font_, bx + 14, by + 20, RED, .64f,
      "LOCKED: PS button and power menu disabled");
  vita2d_pgf_draw_text(font_, bx + 14, by + 40, WHITE, .58f,
      "Screen stays ON. Do NOT force power-off until finished.");
}
const int by2=y+332,bw2=380,bh2=42;
vita2d_draw_rectangle(x+28,by2,bw2,bh2,SURFACE2);
vita2d_draw_rectangle(x+28,by2,bw2,1,BORDER);
{
  const char* clab = "CIRCLE  CANCEL DOWNLOAD";
  const float csc = 0.68f;
  const int ctw = vita2d_pgf_text_width(font_, csc, clab);
  vita2d_pgf_draw_text(font_, x + 28 + (bw2 - ctw) / 2, by2 + 28, WHITE, csc, clab);
}
vita2d_pgf_draw_text(font_,x+28,y+h-14,DIM,.52f,
    "Only CIRCLE works  |  PS / power menu blocked  |  Screen forced on");
}
"""
if old_wait_banner not in cpp:
    raise SystemExit("wait/banner block not found")
cpp = cpp.replace(old_wait_banner, new_wait_banner, 1)

if cpp == orig:
    raise SystemExit("no changes applied")
CPP.write_text(cpp, encoding="utf-8")
print("OK theme + readability applied")
print("remaining hardcoded neon samples:", cpp.count("RGBA8(0x3B, 0xFF, 0x00"))

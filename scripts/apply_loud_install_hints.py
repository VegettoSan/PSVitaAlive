#!/usr/bin/env python3
"""Make install lock / screen-on messaging very explicit in overlay + toasts."""
from pathlib import Path

UI = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
ui = UI.read_text(encoding="utf-8")

old_hints = """vita2d_pgf_draw_text(font_,x+28,y+242,
    (stageDownload && !indeterminate && !msgRetry) ? DIM : ACCENT,
    .52f, waitHint);
// Always-visible safety note while a job runs (screen on + PS locked).
vita2d_pgf_draw_text(font_,x+28,y+260,ACCENT,.48f,
    "Screen stays on. PS button locked — do not force power-off.");
const int by2=y+278,bw2=330,bh2=40;
vita2d_draw_rectangle(x+28,by2,bw2,bh2,SURFACE2);
vita2d_draw_rectangle(x+28,by2,bw2,1,BORDER);
vita2d_pgf_draw_text(font_,x+92,by2+26,WHITE,.62f,"CIRCLE  CANCEL DOWNLOAD");
vita2d_pgf_draw_text(font_,x+28,y+h-14,DIM,.48f,
    "Circle: Cancel  |  Screen on  |  PS locked until finished");
}"""

new_hints = """vita2d_pgf_draw_text(font_,x+28,y+230,
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
}"""

if old_hints not in ui:
    raise SystemExit("progress hints block not found")
ui = ui.replace(old_hints, new_hints, 1)

old_panel = "const int w=640,h=380,x=(SCREEN_W-w)/2,y=(SCREEN_H-h)/2;"
new_panel = "const int w=640,h=400,x=(SCREEN_W-w)/2,y=(SCREEN_H-h)/2;"
if old_panel not in ui:
    raise SystemExit("panel size not found")
ui = ui.replace(old_panel, new_panel, 1)

ui = ui.replace(
    'showToast("Wait — download/install in progress (screen on, PS locked)", 2200);',
    'showToast("LOCKED: finish download/install first. PS & power menu disabled.", 2800);',
)

ui = ui.replace(
    'showToast("Wait — an install is still running (do not power off)", 2200);',
    'showToast("LOCKED: install still running. Screen on — do not power off.", 2800);',
)

old_start = """        if(installProgressActive_ && installOutcome_==0){
            showToast("A download/install is in progress",1800);
            return;
        }"""
new_start = """        if(installProgressActive_ && installOutcome_==0){
            showToast("LOCKED: cannot exit yet. Wait until download/install finishes.", 2800);
            return;
        }"""
if old_start not in ui:
    raise SystemExit("START toast block not found")
ui = ui.replace(old_start, new_start, 1)

old_lr = """    }if((pressed&SCE_CTRL_LTRIGGER)||(pressed&SCE_CTRL_RTRIGGER)){
        const bool canSwitch=!catalogLoading_&&!installProgressActive_&&!isTransitioning()
            &&catalogSwitchCooldownFrames_<=0&&deferredFreeTextures_.empty();
        if(canSwitch){
            if(pressed&SCE_CTRL_LTRIGGER)changeCatalog(-1);else changeCatalog(1);
        }else if(catalogLoading_){
            showToast("Cambiando catalogo...", 1000);
        }else if(catalogSwitchCooldownFrames_>0||!deferredFreeTextures_.empty()){
            showToast("Please wait...", 900);
        }
        return;
    }"""
new_lr = """    }if((pressed&SCE_CTRL_LTRIGGER)||(pressed&SCE_CTRL_RTRIGGER)){
        const bool canSwitch=!catalogLoading_&&!installProgressActive_&&!isTransitioning()
            &&catalogSwitchCooldownFrames_<=0&&deferredFreeTextures_.empty();
        if(canSwitch){
            if(pressed&SCE_CTRL_LTRIGGER)changeCatalog(-1);else changeCatalog(1);
        }else if(installProgressActive_){
            showToast("LOCKED: cannot change catalog during download/install.", 2600);
        }else if(catalogLoading_){
            showToast("Cambiando catalogo...", 1000);
        }else if(catalogSwitchCooldownFrames_>0||!deferredFreeTextures_.empty()){
            showToast("Please wait...", 900);
        }
        return;
    }"""
if old_lr not in ui:
    raise SystemExit("L/R block not found")
ui = ui.replace(old_lr, new_lr, 1)

old_early = """if(installProgressActive_&&(pressed&SCE_CTRL_SQUARE)&&(installOutcome_==2)&&!isNonReportableInstallError(installProgressMessage_)){trySendErrorReport("Installation failed",installProgressMessage_+" | file="+installProgressFile_);return;}if(installProgressActive_&&(pressed&SCE_CTRL_CIRCLE)){if(installOutcome_==1||installOutcome_==2||installOutcome_==3){if(installAcknowledge_)installAcknowledge_();if(installOutcome_==1&&installAllFinishedToast_){installAllFinishedToast_=false;showToast("All installed — ready to use",2800);}reportUiState_=0;}else if(installCancel_)installCancel_();return;}if(catalogLoading_||installProgressActive_)return;"""

new_early = """if(installProgressActive_&&(pressed&SCE_CTRL_SQUARE)&&(installOutcome_==2)&&!isNonReportableInstallError(installProgressMessage_)){trySendErrorReport("Installation failed",installProgressMessage_+" | file="+installProgressFile_);return;}if(installProgressActive_&&(pressed&SCE_CTRL_CIRCLE)){if(installOutcome_==1||installOutcome_==2||installOutcome_==3){if(installAcknowledge_)installAcknowledge_();if(installOutcome_==1&&installAllFinishedToast_){installAllFinishedToast_=false;showToast("All installed — ready to use",2800);}reportUiState_=0;}else if(installCancel_)installCancel_();return;}if(installProgressActive_){if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_TRIANGLE|SCE_CTRL_SQUARE|SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT)){if(installOutcome_==0)showToast("LOCKED: only CIRCLE (cancel) works until finished.",2400);}return;}if(catalogLoading_)return;"""

if old_early not in ui:
    raise SystemExit("early return install block not found")
ui = ui.replace(old_early, new_early, 1)

UI.write_text(ui, encoding="utf-8")
print("OK loud install hints applied")

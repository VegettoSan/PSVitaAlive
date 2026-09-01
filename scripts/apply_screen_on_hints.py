#!/usr/bin/env python3
"""Keep screen fully on during jobs + clearer user-facing progress hints."""
from pathlib import Path

CPP = Path("Client PSVitaAlive/source/installer/install_controller.cpp")
UI = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")

cpp = CPP.read_text(encoding="utf-8")
ui = UI.read_text(encoding="utf-8")

old_ka = """int InstallController::keepAwakeEntry(SceSize args, void* argp) {
    (void)args;
    InstallController* self = nullptr;
    if (argp) std::memcpy(&self, argp, sizeof(self));
    if (!self) return -1;
    // Only DISABLE_AUTO_SUSPEND: screen may dim/off; console stays awake for network I/O.
    while (!self->keepAwakeStop_.load()) {
        if (self->busy()) {
            sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
        }
        // 5s is enough; PowerTick effect lasts until the idle timer would fire again.
        sceKernelDelayThread(5 * 1000 * 1000);
    }
    return 0;
}"""

new_ka = """int InstallController::keepAwakeEntry(SceSize args, void* argp) {
    (void)args;
    InstallController* self = nullptr;
    if (argp) std::memcpy(&self, argp, sizeof(self));
    if (!self) return -1;
    // While a job runs: no auto-suspend AND keep the screen fully on (no dim / no OLED off).
    while (!self->keepAwakeStop_.load()) {
        if (self->busy()) {
            sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
            sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF);
            sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_DIMMING);
        }
        // 5s is enough; PowerTick effect lasts until the idle timer would fire again.
        sceKernelDelayThread(5 * 1000 * 1000);
    }
    return 0;
}"""

if old_ka not in cpp:
    raise SystemExit("keepAwakeEntry block not found")
cpp = cpp.replace(old_ka, new_ka, 1)

cpp = cpp.replace(
    'diagnostics::log("[Installer] keep-awake thread started (anti auto-suspend while busy)");',
    'diagnostics::log("[Installer] keep-awake thread started (anti-suspend + screen on while busy)");',
)
cpp = cpp.replace(
    'diagnostics::log("[Installer] shell locked (PS + power-off menu) during job");',
    'diagnostics::log("[Installer] shell locked: PS blocked, soft power-off menu blocked, screen forced on");',
)

CPP.write_text(cpp, encoding="utf-8")
print("cpp ok")

old_hints = """const char* waitHint = nullptr;
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
const int by2=y+268,bw2=330,bh2=40;
vita2d_draw_rectangle(x+28,by2,bw2,bh2,SURFACE2);
vita2d_draw_rectangle(x+28,by2,bw2,1,BORDER);
vita2d_pgf_draw_text(font_,x+92,by2+26,WHITE,.62f,"CIRCLE  CANCEL DOWNLOAD");
vita2d_pgf_draw_text(font_,x+28,y+h-14,DIM,.50f,"Circle: Cancel download and remove incomplete file");
}"""

new_hints = """const char* waitHint = nullptr;
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

if old_hints not in ui:
    raise SystemExit("UI waitHint block not found")
ui = ui.replace(old_hints, new_hints, 1)

ui = ui.replace(
    'showToast("Wait until loading/install finishes", 1800);',
    'showToast("Wait — download/install in progress (screen on, PS locked)", 2200);',
)
ui = ui.replace(
    'showToast("Wait until the current operation finishes", 1800);',
    'showToast("Wait — an install is still running (do not power off)", 2200);',
)

UI.write_text(ui, encoding="utf-8")
print("ui ok")
print("OK screen-on + hints applied")

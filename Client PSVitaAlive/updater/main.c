/*
 * PSVitaAlive Updater (TITLE_ID: PSVAUPDT1)
 *
 * VitaShell hand-off process: promotes the staged client package while
 * PSVAS1178 is not running. Install sequence matches the client homebrew
 * installer (VitaDB-style):
 *   path      = ux0:data/psva_vpk
 *   PromotePkg(sync=0) + GetState poll
 *   success   = promote directory consumed (stat fails)
 *
 * Verbose log file:
 *   ux0:data/psvitaalive/logs/updater.log
 */

#include <psp2/appmgr.h>
#include <psp2/display.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/promoterutil.h>
#include <psp2/sysmodule.h>
#include <psp2/types.h>

#include <vita2d.h>

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define CLIENT_TITLE_ID    "PSVAS1178"
/* Same shallow promote path used by HomebrewInstaller (working path). */
#define PROMOTE_DIR        "ux0:data/psva_vpk"
#define LEGACY_PKG_DIR     "ux0:data/psvitaalive/pkg"
#define VPK_PATH           "ux0:data/psvitaalive/update/PSVitaAlive.vpk"
#define LOG_DIR            "ux0:data/psvitaalive/logs"
#define LOG_PATH           "ux0:data/psvitaalive/logs/updater.log"
#define SCREEN_W           960
#define SCREEN_H           544

static SceUID gLogFd = -1;

static void logClose(void) {
    if (gLogFd >= 0) {
        sceIoClose(gLogFd);
        gLogFd = -1;
    }
}

static void logOpen(void) {
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir("ux0:data/psvitaalive", 0777);
    sceIoMkdir(LOG_DIR, 0777);
    gLogFd = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
}

static void logLine(const char* msg) {
    char line[512];
    sceClibSnprintf(line, sizeof(line), "%s\n", msg ? msg : "");
    sceClibPrintf("[PSVAUPDT1] %s", line);
    if (gLogFd >= 0) {
        sceIoWrite(gLogFd, line, (SceSize)strlen(line));
    }
}


static int fileExists(const char* path) {
    SceIoStat st;
    memset(&st, 0, sizeof(st));
    return sceIoGetstat(path, &st) >= 0;
}

static void logPathState(const char* label, const char* path) {
    SceIoStat st;
    memset(&st, 0, sizeof(st));
    const int r = sceIoGetstat(path, &st);
    if (r < 0) {
        char b[256];
        sceClibSnprintf(b, sizeof(b), "%s: MISSING path=%s err=0x%08X", label, path, (unsigned)r);
        logLine(b);
    } else {
        char b[256];
        sceClibSnprintf(
            b, sizeof(b),
            "%s: OK path=%s size=%u mode=0x%X",
            label, path, (unsigned)st.st_size, (unsigned)st.st_mode
        );
        logLine(b);
    }
}

static int removeTree(const char* path);

static int removeTreeContents(const char* path) {
    SceUID fd = sceIoDopen(path);
    if (fd < 0) return 0;
    int ok = 1;
    SceIoDirent de;
    memset(&de, 0, sizeof(de));
    while (sceIoDread(fd, &de) > 0) {
        if (strcmp(de.d_name, ".") == 0 || strcmp(de.d_name, "..") == 0) continue;
        char child[512];
        sceClibSnprintf(child, sizeof(child), "%s/%s", path, de.d_name);
        if (SCE_S_ISDIR(de.d_stat.st_mode)) {
            if (removeTree(child) != 0) ok = 0;
        } else {
            if (sceIoRemove(child) < 0) ok = 0;
        }
        memset(&de, 0, sizeof(de));
    }
    sceIoDclose(fd);
    return ok ? 0 : -1;
}

static int removeTree(const char* path) {
    if (!fileExists(path)) return 0;
    removeTreeContents(path);
    if (sceIoRmdir(path) < 0) sceIoRemove(path);
    return 0;
}

static int launchClientAndExit(void) {
    char uri[48];
    sceClibSnprintf(uri, sizeof(uri), "psgm:play?titleid=%s", CLIENT_TITLE_ID);
    { char _lb[480]; sceClibSnprintf(_lb, sizeof(_lb), "launchClient uri=%s", uri); logLine(_lb); };
    logClose();
    sceAppMgrLaunchAppByUri(0xFFFFF, uri);
    sceKernelExitProcess(0);
    return 0;
}

static int loadScePaf(void) {
    /* Identical approach to HomebrewInstaller::loadPromoterModule (client builds OK). */
    uint32_t ptr[0x100];
    unsigned i;
    for (i = 0; i < 0x100; ++i) ptr[i] = 0;
    ptr[0] = 0;
    ptr[1] = (uint32_t)(uintptr_t)&ptr[0];

    uint32_t scepafArgp[] = { 0x400000u, 0xEA60u, 0x40000u, 0u, 0u };
    const int r = sceSysmoduleLoadModuleInternalWithArg(
        SCE_SYSMODULE_INTERNAL_PAF,
        sizeof(scepafArgp),
        scepafArgp,
        (const SceSysmoduleOpt*)ptr
    );
    { char _lb[480]; sceClibSnprintf(_lb, sizeof(_lb), "loadScePaf -> 0x%08X", (unsigned)r); logLine(_lb); };
    return r;
}

static int unloadScePaf(void) {
    SceSysmoduleOpt opt;
    memset(&opt, 0, sizeof(opt));
    const int r = sceSysmoduleUnloadModuleInternalWithArg(
        SCE_SYSMODULE_INTERNAL_PAF,
        0,
        NULL,
        &opt
    );
    { char _lb[480]; sceClibSnprintf(_lb, sizeof(_lb), "unloadScePaf -> 0x%08X", (unsigned)r); logLine(_lb); };
    return r;
}



/* Mirror HomebrewInstaller::promoteExtractedDir success rules. */
static int promotePackageHomebrewStyle(const char* path) {
    { char _lb[480]; sceClibSnprintf(_lb, sizeof(_lb), "promote begin path=%s", path); logLine(_lb); };
    logPathState("package root", path);
    logPathState("eboot.bin", "ux0:data/psva_vpk/eboot.bin");
    logPathState("param.sfo", "ux0:data/psva_vpk/sce_sys/param.sfo");
    logPathState("head.bin", "ux0:data/psva_vpk/sce_sys/package/head.bin");

    if (!fileExists("ux0:data/psva_vpk/eboot.bin") ||
        !fileExists("ux0:data/psva_vpk/sce_sys/param.sfo")) {
        logLine("ERROR: invalid package layout before promote");
        return -1;
    }
    if (!fileExists("ux0:data/psva_vpk/sce_sys/package/head.bin")) {
        logLine("ERROR: missing head.bin before promote");
        return -2;
    }

    int res = loadScePaf();
    if (res < 0) {
        logLine("WARN: PAF load failed - continuing (homebrew path may still work)");
    }

    res = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    { char _lb[480]; sceClibSnprintf(_lb, sizeof(_lb), "LoadModuleInternal(PROMOTER_UTIL) -> 0x%08X", (unsigned)res); logLine(_lb); };
    if (res < 0 && sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL) < 0) {
        unloadScePaf();
        return res;
    }

    const int initResult = scePromoterUtilityInit();
    { char _lb[480]; sceClibSnprintf(_lb, sizeof(_lb), "scePromoterUtilityInit -> 0x%08X", (unsigned)initResult); logLine(_lb); };
    if (initResult < 0) {
        sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
        unloadScePaf();
        return initResult;
    }

    logLine("PromotePkg async begin (homebrew/VitaDB: sync=0 + GetState)");
    const int promoteResult = scePromoterUtilityPromotePkg(path, 0);
    { char _lb[480]; sceClibSnprintf(_lb, sizeof(_lb), "scePromoterUtilityPromotePkg(sync=0) -> 0x%08X (%d)",
         (unsigned)promoteResult, promoteResult); logLine(_lb); };

    if (promoteResult < 0) {
        scePromoterUtilityExit();
        sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
        unloadScePaf();
        return promoteResult;
    }

    int state = 1;
    int pollCount = 0;
    const int kMaxPolls = 12000;
    while (pollCount < kMaxPolls) {
        state = 0;
        const int stRes = scePromoterUtilityGetState(&state);
        if (stRes < 0) {
            { char _lb[480]; sceClibSnprintf(_lb, sizeof(_lb), "scePromoterUtilityGetState -> 0x%08X", (unsigned)stRes); logLine(_lb); };
            break;
        }
        if (state == 0) break;
        ++pollCount;
        if ((pollCount % 200) == 0) {
            { char _lb[480]; sceClibSnprintf(_lb, sizeof(_lb), "PromotePkg waiting state=%d polls=%d", state, pollCount); logLine(_lb); };
        }
        sceKernelDelayThread(10 * 1000);
    }

    int operationResult = 0;
    const int getResultCall = scePromoterUtilityGetResult(&operationResult);
    { char _lb[480]; sceClibSnprintf(_lb, sizeof(_lb), "GetResult (diagnostic) call=0x%08X op=0x%08X polls=%d finalState=%d",
         (unsigned)getResultCall, (unsigned)operationResult, pollCount, state); logLine(_lb); };

    const int exitResult = scePromoterUtilityExit();
    { char _lb[480]; sceClibSnprintf(_lb, sizeof(_lb), "scePromoterUtilityExit -> 0x%08X", (unsigned)exitResult); logLine(_lb); };
    sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    unloadScePaf();

    const int promoteDirGone = !fileExists(path);
    logLine(promoteDirGone
        ? "promote dir consumed (VitaDB/homebrew success signal)"
        : "promote dir STILL PRESENT after PromotePkg");

    if (promoteResult < 0) return promoteResult;
    if (!promoteDirGone && state != 0) {
        logLine("ERROR: promoter did not finish (timeout)");
        return -3;
    }
    if (!promoteDirGone) {
        logLine("WARN: promote finished but staging dir remains");
    }
    return 0;
}

static int ensurePromoteDirReady(void) {
    /* Prefer the homebrew path. If only legacy pkg exists, rename it. */
    if (fileExists(PROMOTE_DIR "/eboot.bin")) {
        logLine("promote dir already has eboot.bin");
        return 0;
    }
    if (fileExists(LEGACY_PKG_DIR "/eboot.bin")) {
        logLine("migrating legacy pkg -> ux0:data/psva_vpk");
        removeTree(PROMOTE_DIR);
        const int ren = sceIoRename(LEGACY_PKG_DIR, PROMOTE_DIR);
        { char _lb[480]; sceClibSnprintf(_lb, sizeof(_lb), "sceIoRename(legacy->psva_vpk) -> 0x%08X", (unsigned)ren); logLine(_lb); };
        if (ren < 0) {
            logLine("ERROR: cannot migrate legacy package dir");
            return -1;
        }
        return 0;
    }
    logLine("ERROR: no staged package at psva_vpk or legacy pkg");
    return -1;
}

static void drawFrame(
    vita2d_pgf* font,
    vita2d_texture* bg,
    const char* title,
    const char* line1,
    const char* line2,
    float progress01,
    int showBar
) {
    const unsigned ACCENT = RGBA8(0x3B, 0xFF, 0x00, 255);
    const unsigned WHITE  = RGBA8(255, 255, 255, 255);
    const unsigned TEXT   = RGBA8(0xAA, 0xAA, 0xAA, 255);
    const unsigned DIM    = RGBA8(0x6E, 0x6E, 0x6E, 255);
    const unsigned BAR_BG = RGBA8(0x28, 0x28, 0x28, 255);

    vita2d_start_drawing();
    vita2d_clear_screen();

    if (bg) {
        const float sw = (float)vita2d_texture_get_width(bg);
        const float sh = (float)vita2d_texture_get_height(bg);
        float sx = (float)SCREEN_W / (sw > 1.f ? sw : 1.f);
        float sy = (float)SCREEN_H / (sh > 1.f ? sh : 1.f);
        float s = sx > sy ? sx : sy;
        vita2d_draw_texture_scale(bg, 0, 0, s, s);
        vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, 140));
    } else {
        vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0x12, 0x12, 0x12, 255));
    }

    vita2d_pgf_draw_text(font, 48, 120, ACCENT, 0.70f, "PSVitaAlive");
    vita2d_pgf_draw_text(font, 48, 165, WHITE, 1.15f, title ? title : "Updating");
    if (line1 && line1[0]) vita2d_pgf_draw_text(font, 48, 210, TEXT, 0.72f, line1);
    if (line2 && line2[0]) vita2d_pgf_draw_text(font, 48, 240, DIM, 0.60f, line2);

    if (showBar) {
        const int barX = 48, barY = 300, barW = SCREEN_W - 96, barH = 18;
        vita2d_draw_rectangle(barX, barY, barW, barH, BAR_BG);
        float p = progress01;
        if (p < 0.f) p = 0.f;
        if (p > 1.f) p = 1.f;
        if (p < 0.05f) p = 0.05f;
        vita2d_draw_rectangle(barX, barY, (int)(barW * p), barH, ACCENT);
    }

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

static void waitFrames(vita2d_pgf* font, vita2d_texture* bg,
                       const char* title, const char* l1, const char* l2,
                       float progress, int showBar, int frames) {
    for (int i = 0; i < frames; ++i) {
        drawFrame(font, bg, title, l1, l2, progress, showBar);
        sceKernelDelayThread(16 * 1000);
    }
}

static void failLoop(vita2d_pgf* font, vita2d_texture* bg,
                     const char* title, const char* l1, const char* l2) {
    { char _lb[480]; sceClibSnprintf(_lb, sizeof(_lb), "FAIL: %s | %s | %s", title ? title : "", l1 ? l1 : "", l2 ? l2 : ""); logLine(_lb); };
    logClose();
    for (;;) {
        drawFrame(font, bg, title, l1, l2, 0.f, 0);
        sceKernelDelayThread(16 * 1000);
    }
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    logOpen();
    logLine("============================================================");
    logLine("PSVitaAlive Updater (PSVAUPDT1) BEGIN");
    logLine("Install path aligned with client homebrew promoter (psva_vpk)");
    logLine("============================================================");

    vita2d_init();
    vita2d_set_clear_color(RGBA8(0x12, 0x12, 0x12, 255));
    vita2d_pgf* font = vita2d_load_default_pgf();
    vita2d_texture* bg = vita2d_load_PNG_file("app0:ui/catalog_loading.png");
    { char _lb[480]; sceClibSnprintf(_lb, sizeof(_lb), "ui font=%p bg=%p", (void*)font, (void*)bg); logLine(_lb); };

    waitFrames(font, bg, "Installing update", "Closing other applications...", "", 0.10f, 1, 20);
    logLine("sceAppMgrDestroyOtherApp()");
    sceAppMgrDestroyOtherApp();

    waitFrames(font, bg, "Installing update", "Preparing package...",
               "Same installer path as homebrew VPKs", 0.25f, 1, 12);

    if (ensurePromoteDirReady() < 0) {
        char line2[160];
        sceClibSnprintf(line2, sizeof(line2), "Manual VPK: %s", VPK_PATH);
        failLoop(font, bg, "Update failed", "Update package not found.", line2);
    }

    waitFrames(font, bg, "Installing update", "Installing PSVitaAlive...",
               "Please do not power off the device.", 0.45f, 1, 10);

    const int promoteRes = promotePackageHomebrewStyle(PROMOTE_DIR);
    if (promoteRes < 0) {
        char line1[96];
        char line2[160];
        sceClibSnprintf(line1, sizeof(line1), "Promoter error: 0x%08X", (unsigned)promoteRes);
        sceClibSnprintf(line2, sizeof(line2), "Install manually: %s", VPK_PATH);
        failLoop(font, bg, "Update failed", line1, line2);
    }

    waitFrames(font, bg, "Installing update", "Verifying installation...", "", 0.80f, 1, 15);

    const int ok =
        fileExists("ux0:app/" CLIENT_TITLE_ID "/eboot.bin") ||
        fileExists("ux0:/app/" CLIENT_TITLE_ID "/eboot.bin");
    { char _lb[480]; sceClibSnprintf(_lb, sizeof(_lb), "verify client eboot -> %s", ok ? "OK" : "MISSING"); logLine(_lb); };
    logPathState("client eboot", "ux0:app/" CLIENT_TITLE_ID "/eboot.bin");
    logPathState("client param", "ux0:app/" CLIENT_TITLE_ID "/sce_sys/param.sfo");

    if (!ok) {
        char line2[160];
        sceClibSnprintf(line2, sizeof(line2), "Install manually with VitaShell: %s", VPK_PATH);
        failLoop(font, bg, "Update failed",
                 "Client files were not found after install.", line2);
    }

    /* Staging should already be consumed; clean leftovers just in case. */
    removeTree(PROMOTE_DIR);
    removeTree(LEGACY_PKG_DIR);
    logLine("staging cleanup done");

    waitFrames(font, bg, "Update complete", "Starting PSVitaAlive...",
               "Updater bubble is removed on next client boot", 1.0f, 1, 25);

    logLine("PSVitaAlive Updater SUCCESS - launching client");
    if (bg) vita2d_free_texture(bg);
    if (font) vita2d_free_pgf(font);
    vita2d_fini();
    return launchClientAndExit();
}

/*
 * PSVitaAlive Updater (TITLE_ID: PSVAUPDT1)
 *
 * VitaShell-style helper process:
 *  - Runs as a separate app so it can promote PSVAS1178 while the store is closed.
 *  - Expects the extracted update package at ux0:data/psvitaalive/pkg/
 *  - Shows the same catalog_loading visual language as the main client.
 *  - On success: verifies the client, launches PSVAS1178, exits.
 *  - Does NOT delete its own LiveArea bubble (the client removes PSVAUPDT1 on boot).
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

#define CLIENT_TITLE_ID   "PSVAS1178"
#define PACKAGE_DIR       "ux0:data/psvitaalive/pkg"
#define VPK_PATH          "ux0:data/psvitaalive/update/PSVitaAlive.vpk"
#define SCREEN_W          960
#define SCREEN_H          544

static int fileExists(const char* path) {
    SceIoStat st;
    memset(&st, 0, sizeof(st));
    return sceIoGetstat(path, &st) >= 0;
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
    if (sceIoRmdir(path) < 0) {
        /* may be a file */
        sceIoRemove(path);
    }
    return 0;
}

static int launchClientAndExit(void) {
    char uri[48];
    sceClibSnprintf(uri, sizeof(uri), "psgm:play?titleid=%s", CLIENT_TITLE_ID);
    sceAppMgrLaunchAppByUri(0xFFFFF, uri);
    sceKernelExitProcess(0);
    return 0;
}

static int loadScePaf(void) {
    static uint32_t argp[] = { 0x180000, (uint32_t)-1, (uint32_t)-1, 1, (uint32_t)-1, (uint32_t)-1 };
    int result = -1;
    uint32_t buf[4];
    buf[0] = sizeof(buf);
    buf[1] = (uint32_t)&result;
    buf[2] = (uint32_t)-1;
    buf[3] = (uint32_t)-1;
    return sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, sizeof(argp), argp, buf);
}

static int unloadScePaf(void) {
    uint32_t buf = 0;
    return sceSysmoduleUnloadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, 0, NULL, &buf);
}

static int promotePackage(const char* path) {
    int res = loadScePaf();
    if (res < 0) return res;

    res = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    if (res < 0) {
        unloadScePaf();
        return res;
    }

    res = scePromoterUtilityInit();
    if (res < 0) {
        sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
        unloadScePaf();
        return res;
    }

    /* Async promote + wait (VitaDB / our installer style). */
    res = scePromoterUtilityPromotePkg(path, 0);
    if (res < 0) {
        scePromoterUtilityExit();
        sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
        unloadScePaf();
        return res;
    }

    int state = 1;
    int polls = 0;
    while (polls < 12000) {
        state = 0;
        const int sr = scePromoterUtilityGetState(&state);
        if (sr < 0) {
            res = sr;
            break;
        }
        if (state == 0) {
            res = 0;
            break;
        }
        sceKernelDelayThread(10 * 1000);
        ++polls;
    }

    int op = 0;
    scePromoterUtilityGetResult(&op);
    scePromoterUtilityExit();
    sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    unloadScePaf();

    if (res < 0) return res;
    if (op != 0) return op < 0 ? op : -1;
    return 0;
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
        if (p < 0.05f && showBar == 1) p = 0.05f; /* indeterminate minimum */
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

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    vita2d_init();
    vita2d_set_clear_color(RGBA8(0x12, 0x12, 0x12, 255));
    vita2d_pgf* font = vita2d_load_default_pgf();
    vita2d_texture* bg = vita2d_load_PNG_file("app0:ui/catalog_loading.png");

    waitFrames(font, bg, "Installing update", "Closing other applications...", "", 0.10f, 1, 20);
    sceAppMgrDestroyOtherApp();

    waitFrames(font, bg, "Installing update", "Installing PSVitaAlive...",
               "Please do not power off the device.", 0.35f, 1, 10);

    if (!fileExists(PACKAGE_DIR "/eboot.bin") && !fileExists(PACKAGE_DIR "/sce_sys/param.sfo")) {
        char line2[160];
        sceClibSnprintf(line2, sizeof(line2), "Manual VPK: %s", VPK_PATH);
        for (;;) {
            drawFrame(font, bg, "Update failed",
                      "Update package not found.",
                      line2, 0.f, 0);
            sceKernelDelayThread(16 * 1000);
        }
    }

    const int promoteRes = promotePackage(PACKAGE_DIR);
    if (promoteRes < 0) {
        char line1[96];
        char line2[160];
        sceClibSnprintf(line1, sizeof(line1), "Promoter error: 0x%08X", (unsigned)promoteRes);
        sceClibSnprintf(line2, sizeof(line2), "Install manually: %s", VPK_PATH);
        for (;;) {
            drawFrame(font, bg, "Update failed", line1, line2, 0.f, 0);
            sceKernelDelayThread(16 * 1000);
        }
    }

    waitFrames(font, bg, "Installing update", "Verifying installation...", "", 0.75f, 1, 15);

    const int ok =
        fileExists("ux0:app/" CLIENT_TITLE_ID "/eboot.bin") ||
        fileExists("ux0:/app/" CLIENT_TITLE_ID "/eboot.bin");

    if (!ok) {
        char line2[160];
        sceClibSnprintf(line2, sizeof(line2), "Install manually with VitaShell: %s", VPK_PATH);
        for (;;) {
            drawFrame(font, bg, "Update failed",
                      "Client files were not found after install.",
                      line2, 0.f, 0);
            sceKernelDelayThread(16 * 1000);
        }
    }

    /* Success: clean staging package (keep VPK until client boot cleanup). */
    removeTree(PACKAGE_DIR);

    waitFrames(font, bg, "Update complete", "Starting PSVitaAlive...", "", 1.0f, 1, 25);

    if (bg) vita2d_free_texture(bg);
    if (font) vita2d_free_pgf(font);
    vita2d_fini();

    return launchClientAndExit();
}

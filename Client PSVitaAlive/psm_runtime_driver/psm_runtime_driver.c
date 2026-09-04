/*
 * PSVitaAlive PSM Runtime Package Installer bridge.
 *
 * This is intentionally a short-lived kernel helper. It mirrors the proven
 * CrystalPSM approach: expose a private staging directory as host0:/package
 * for NPXS10031 and make the Package Installer pass its retail/debug gate.
 *
 * It does not decrypt, extract or promote PKGs itself.
 */
#include <string.h>
#include <stdio.h>
#include <vitasdkkern.h>
#include <psp2kern/kernel/proc_event.h>
#include <psp2kern/fios2.h>
#include <taihen.h>

#define SceSblQafMgr 0x756B7E89
#define SceSblQafMgrIsAllowLimitedDebugMenuDisplay 0xC456212D

static int g_qaf_hook = -1;
static tai_hook_ref_t g_qaf_hook_ref;
static SceFiosOverlay g_overlay;
static int g_overlay_out;

static int ProcessCreated(SceUID pid, SceProcEventInvokeParam2 *param, int unknown) {
    (void)param;
    (void)unknown;

    memset(&g_overlay, 0, sizeof(g_overlay));
    g_overlay.type = SCE_FIOS_OVERLAY_TYPE_OPAQUE;
    g_overlay.order = 0xFF;
    g_overlay.dst_len = strlen("host0:/package") + 1;
    g_overlay.src_len = strlen("ux0:/data/psvitaalive/psm_runtime") + 1;
    g_overlay.pid = pid;
    strncpy(g_overlay.dst, "host0:/package", sizeof(g_overlay.dst) - 1);
    strncpy(g_overlay.src, "ux0:/data/psvitaalive/psm_runtime", sizeof(g_overlay.src) - 1);
    g_overlay_out = 0;

    ksceFiosKernelOverlayAddForProcess(pid, &g_overlay, &g_overlay_out);
    return 0;
}

static int qafAllowPackageInstaller(void) {
    if (g_qaf_hook >= 0) {
        taiHookReleaseForKernel(g_qaf_hook, g_qaf_hook_ref);
        g_qaf_hook = -1;
    }
    return 1;
}

void _start(void) __attribute__((weak, alias("module_start")));

int module_start(SceSize argc, const void *args) {
    (void)argc;
    (void)args;

    SceProcEventHandler handler;
    memset(&handler, 0, sizeof(handler));
    handler.size = sizeof(handler);
    handler.create = ProcessCreated;
    ksceKernelRegisterProcEventHandler("PSVitaAlivePsmRuntime", &handler, 0);

    g_qaf_hook = taiHookFunctionExportForKernel(
        KERNEL_PID,
        &g_qaf_hook_ref,
        "SceSblSsMgr",
        SceSblQafMgr,
        SceSblQafMgrIsAllowLimitedDebugMenuDisplay,
        qafAllowPackageInstaller);

    if (g_qaf_hook < 0) {
        return SCE_KERNEL_START_FAILED;
    }

    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
    (void)argc;
    (void)args;
    if (g_qaf_hook >= 0) {
        taiHookReleaseForKernel(g_qaf_hook, g_qaf_hook_ref);
        g_qaf_hook = -1;
    }
    return SCE_KERNEL_STOP_SUCCESS;
}

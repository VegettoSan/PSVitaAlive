from pathlib import Path
ROOT = Path("Client PSVitaAlive")
HPP = ROOT / "include/installer/install_controller.hpp"
CPP = ROOT / "source/installer/install_controller.cpp"

hpp = HPP.read_text(encoding="utf-8")
cpp = CPP.read_text(encoding="utf-8")

if "lockShellDuringJob" not in hpp:
    old = """    static int keepAwakeEntry(SceSize args, void* argp);
    void startKeepAwakeThread();
    void stopKeepAwakeThread();

    void setMessage(const char* text);"""
    new = """    static int keepAwakeEntry(SceSize args, void* argp);
    void startKeepAwakeThread();
    void stopKeepAwakeThread();

    /** Block PS button (+ soft power-off menu) while a job is running so the user
     *  cannot exit to LiveArea mid-download/extract. Always unlocked on finish. */
    void lockShellDuringJob();
    void unlockShellDuringJob();

    void setMessage(const char* text);"""
    if old not in hpp:
        raise SystemExit("hpp keepAwake block not found")
    hpp = hpp.replace(old, new, 1)

if "shellLocked_" not in hpp:
    old = """    /** Background tick: prevent auto-suspend while download/extract is active. */
    SceUID keepAwakeThread_ = -1;
    std::atomic<bool> keepAwakeStop_{true};"""
    new = """    /** Background tick: prevent auto-suspend while download/extract is active. */
    SceUID keepAwakeThread_ = -1;
    std::atomic<bool> keepAwakeStop_{true};
    bool shellUtilReady_ = false;
    bool shellLocked_ = false;"""
    if old not in hpp:
        raise SystemExit("hpp members not found")
    hpp = hpp.replace(old, new, 1)

HPP.write_text(hpp, encoding="utf-8")
print("hpp updated")

if "psp2/shellutil.h" not in cpp:
    old = """#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>"""
    new = """#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/shellutil.h>"""
    if old not in cpp:
        raise SystemExit("includes not found")
    cpp = cpp.replace(old, new, 1)

if "sceShellUtilInitEvents" not in cpp:
    old = """    diagnostics::log("[Installer] BGDL deferred until first PKG install request");
    startKeepAwakeThread();
    diagnostics::log("[Installer] initialized");"""
    new = """    diagnostics::log("[Installer] BGDL deferred until first PKG install request");
    if (sceShellUtilInitEvents(0) >= 0) {
        shellUtilReady_ = true;
        diagnostics::log("[Installer] sceShellUtilInitEvents ok");
    } else {
        shellUtilReady_ = false;
        diagnostics::log("[Installer] sceShellUtilInitEvents failed (PS lock unavailable)");
    }
    startKeepAwakeThread();
    diagnostics::log("[Installer] initialized");"""
    if old not in cpp:
        raise SystemExit("init block not found")
    cpp = cpp.replace(old, new, 1)

if "void InstallController::shutdown()" in cpp and "unlockShellDuringJob();" not in cpp.split("void InstallController::shutdown()")[1][:300]:
    old = """void InstallController::shutdown() {
    stopKeepAwakeThread();"""
    new = """void InstallController::shutdown() {
    unlockShellDuringJob();
    stopKeepAwakeThread();"""
    if old not in cpp:
        raise SystemExit("shutdown not found")
    cpp = cpp.replace(old, new, 1)

old_ss = """void InstallController::setState(InstallStatus::State state, const char* message) {
    setMessage(message);
    state_.store(static_cast<int>(state));
}"""
new_ss = """void InstallController::setState(InstallStatus::State state, const char* message) {
    setMessage(message);
    state_.store(static_cast<int>(state));
    const bool jobActive = (state == InstallStatus::State::Downloading ||
                            state == InstallStatus::State::Installing);
    if (jobActive) lockShellDuringJob();
    else unlockShellDuringJob();
}"""
if "jobActive" not in cpp:
    if old_ss not in cpp:
        raise SystemExit("setState not found")
    cpp = cpp.replace(old_ss, new_ss, 1)

lock_methods = """
void InstallController::lockShellDuringJob() {
    if (shellLocked_) return;
    if (!shellUtilReady_) return;
    sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);
    sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_POWEROFF_MENU);
    shellLocked_ = true;
    diagnostics::log("[Installer] shell locked (PS + power-off menu) during job");
}

void InstallController::unlockShellDuringJob() {
    if (!shellLocked_) return;
    if (shellUtilReady_) {
        sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);
        sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_POWEROFF_MENU);
    }
    shellLocked_ = false;
    diagnostics::log("[Installer] shell unlocked");
}

"""

if "void InstallController::lockShellDuringJob" not in cpp:
    marker = """void InstallController::stopKeepAwakeThread() {
    if (keepAwakeThread_ < 0) return;
    keepAwakeStop_.store(true);
    // Wake quickly: thread sleeps 5s max
    sceKernelWaitThreadEnd(keepAwakeThread_, nullptr, nullptr);
    sceKernelDeleteThread(keepAwakeThread_);
    keepAwakeThread_ = -1;
}
"""
    if marker not in cpp:
        raise SystemExit("stopKeepAwake marker not found")
    cpp = cpp.replace(marker, marker + "\n" + lock_methods, 1)

CPP.write_text(cpp, encoding="utf-8")
print("cpp updated")
print("OK shell lock patch applied")

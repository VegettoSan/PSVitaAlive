from pathlib import Path

hpp = Path("Client PSVitaAlive/include/installer/install_controller.hpp")
cpp = Path("Client PSVitaAlive/source/installer/install_controller.cpp")

h = hpp.read_text(encoding="utf-8")
c = cpp.read_text(encoding="utf-8")
changed = 0

def rep(text, old, new, label):
    global changed
    if old not in text:
        print(f"FAIL {label}")
        raise SystemExit(1)
    text = text.replace(old, new, 1)
    changed += 1
    print(f"OK {label}")
    return text

old_h = """    SceUID workerThread_ = -1;
    /** When true, worker runs PKG BGDL enqueue instead of HTTP download. */
    bool activeBgdlJob_ = false;"""
new_h = """    SceUID workerThread_ = -1;
    /** Background tick: prevent auto-suspend while download/extract is active. */
    SceUID keepAwakeThread_ = -1;
    std::atomic<bool> keepAwakeStop_{true};

    /** When true, worker runs PKG BGDL enqueue instead of HTTP download. */
    bool activeBgdlJob_ = false;"""
h = rep(h, old_h, new_h, "hpp members")

old_h2 = """    static int workerEntry(SceSize args, void* argp);
    int workerMain();"""
new_h2 = """    static int workerEntry(SceSize args, void* argp);
    int workerMain();

    static int keepAwakeEntry(SceSize args, void* argp);
    void startKeepAwakeThread();
    void stopKeepAwakeThread();"""
h = rep(h, old_h2, new_h2, "hpp methods")

old_worker = """int InstallController::workerEntry(SceSize args, void* argp) {
    (void)args;
    InstallController* self = nullptr;
    if (argp) std::memcpy(&self, argp, sizeof(self));
    return self ? self->workerMain() : -1;
}"""

new_worker = """void InstallController::startKeepAwakeThread() {
    if (keepAwakeThread_ >= 0) return;
    keepAwakeStop_.store(false);
    InstallController* self = this;
    keepAwakeThread_ = sceKernelCreateThread(
        "PSVitaAliveKeepAwake",
        &InstallController::keepAwakeEntry,
        0x10000100,
        0x1000,
        0,
        0,
        nullptr);
    if (keepAwakeThread_ < 0) {
        keepAwakeStop_.store(true);
        diagnostics::log("[Installer] keep-awake thread create failed");
        return;
    }
    const int st = sceKernelStartThread(keepAwakeThread_, sizeof(self), &self);
    if (st < 0) {
        sceKernelDeleteThread(keepAwakeThread_);
        keepAwakeThread_ = -1;
        keepAwakeStop_.store(true);
        diagnostics::log("[Installer] keep-awake thread start failed");
        return;
    }
    diagnostics::log("[Installer] keep-awake thread started (anti auto-suspend while busy)");
}

void InstallController::stopKeepAwakeThread() {
    if (keepAwakeThread_ < 0) return;
    keepAwakeStop_.store(true);
    // Wake quickly: thread sleeps 5s max
    sceKernelWaitThreadEnd(keepAwakeThread_, nullptr, nullptr);
    sceKernelDeleteThread(keepAwakeThread_);
    keepAwakeThread_ = -1;
}

int InstallController::keepAwakeEntry(SceSize args, void* argp) {
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
}

int InstallController::workerEntry(SceSize args, void* argp) {
    (void)args;
    InstallController* self = nullptr;
    if (argp) std::memcpy(&self, argp, sizeof(self));
    return self ? self->workerMain() : -1;
}"""

c = rep(c, old_worker, new_worker, "cpp keepAwake helpers")

old_init_end = """    diagnostics::log("[Installer] BGDL deferred until first PKG install request");
    diagnostics::log("[Installer] initialized");
    return true;
}"""

new_init_end = """    diagnostics::log("[Installer] BGDL deferred until first PKG install request");
    startKeepAwakeThread();
    diagnostics::log("[Installer] initialized");
    return true;
}"""
c = rep(c, old_init_end, new_init_end, "cpp init start keepAwake")

old_shutdown = """void InstallController::shutdown() {
    if (workerThread_ >= 0) {
        sceKernelWaitThreadEnd(workerThread_, nullptr, nullptr);
        sceKernelDeleteThread(workerThread_);
        workerThread_ = -1;
    }
    http_.shutdown();"""

new_shutdown = """void InstallController::shutdown() {
    stopKeepAwakeThread();
    if (workerThread_ >= 0) {
        sceKernelWaitThreadEnd(workerThread_, nullptr, nullptr);
        sceKernelDeleteThread(workerThread_);
        workerThread_ = -1;
    }
    http_.shutdown();"""
c = rep(c, old_shutdown, new_shutdown, "cpp shutdown stop keepAwake")

hpp.write_text(h, encoding="utf-8")
cpp.write_text(c, encoding="utf-8")
print("changed", changed)

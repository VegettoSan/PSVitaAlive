from pathlib import Path
import re

root = Path('.')
client = root / 'Client PSVitaAlive'
main = client / 'source/main.cpp'
catalog_h = client / 'include/catalog/catalog_manager.hpp'
catalog_cpp = client / 'source/catalog/catalog_manager.cpp'
cmake = client / 'CMakeLists.txt'

upd_h = client / 'include/update/startup_update_manager.hpp'
upd_cpp = client / 'source/update/startup_update_manager.cpp'
upd_h.parent.mkdir(parents=True, exist_ok=True)

upd_h.write_text(r'''#pragma once

#include "update/update_checker.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include <psp2/kernel/threadmgr.h>

namespace psvitaalive {

class StartupUpdateManager {
public:
    enum class State { Idle = 0, Checking, Downloading, Installing, UpToDate, Failed, Cancelled, Completed };

    struct Snapshot {
        State state = State::Idle;
        uint64_t current = 0;
        uint64_t total = 0;
        uint64_t bytesPerSecond = 0;
        std::string message;
        std::string error;
        bool restartRequired = false;
    };

    StartupUpdateManager();
    ~StartupUpdateManager();
    bool start(const std::string& currentVersion);
    bool isBusy() const;
    Snapshot snapshot() const;
    void requestCancel();
    void wait();

private:
    mutable std::mutex stateMutex_;
    SceUID thread_ = -1;
    std::atomic<bool> cancelRequested_{false};
    std::atomic<int> state_{static_cast<int>(State::Idle)};
    std::atomic<uint64_t> current_{0};
    std::atomic<uint64_t> total_{0};
    std::atomic<uint64_t> bytesPerSecond_{0};
    std::atomic<bool> restartRequired_{false};
    std::string message_;
    std::string error_;
    std::string version_;

    static int workerEntry(SceSize args, void* argp);
    int workerMain();
    void setProgress(State state, uint64_t current, uint64_t total, uint64_t bytesPerSecond, const std::string& message);
    void setError(const std::string& error);
};

} // namespace psvitaalive
''', encoding='utf-8')

upd_cpp.write_text(r'''#include "update/startup_update_manager.hpp"

#include "diagnostic_logger.hpp"

#include <cstring>

namespace psvitaalive {

StartupUpdateManager::StartupUpdateManager() = default;
StartupUpdateManager::~StartupUpdateManager() { requestCancel(); wait(); }

bool StartupUpdateManager::start(const std::string& currentVersion) {
    if (thread_ >= 0) return false;
    version_ = currentVersion;
    cancelRequested_.store(false, std::memory_order_release);
    restartRequired_.store(false, std::memory_order_release);
    state_.store(static_cast<int>(State::Checking), std::memory_order_release);
    current_.store(0, std::memory_order_release);
    total_.store(0, std::memory_order_release);
    bytesPerSecond_.store(0, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        message_ = "Checking for application updates...";
        error_.clear();
    }
    thread_ = sceKernelCreateThread("PSVitaAliveUpdateWorker", &StartupUpdateManager::workerEntry,
                                    0x10000100, 64 * 1024, 0, 0, nullptr);
    if (thread_ < 0) {
        state_.store(static_cast<int>(State::Failed), std::memory_order_release);
        setError("Unable to create update worker thread");
        return false;
    }
    StartupUpdateManager* self = this;
    const int rc = sceKernelStartThread(thread_, sizeof(self), &self);
    if (rc < 0) {
        sceKernelDeleteThread(thread_);
        thread_ = -1;
        state_.store(static_cast<int>(State::Failed), std::memory_order_release);
        setError("Unable to start update worker thread");
        return false;
    }
    diagnostics::log("[StartupUpdate] worker started before catalog/image workers");
    return true;
}

bool StartupUpdateManager::isBusy() const {
    const State s = static_cast<State>(state_.load(std::memory_order_acquire));
    return s == State::Checking || s == State::Downloading || s == State::Installing;
}

StartupUpdateManager::Snapshot StartupUpdateManager::snapshot() const {
    Snapshot out;
    out.state = static_cast<State>(state_.load(std::memory_order_acquire));
    out.current = current_.load(std::memory_order_acquire);
    out.total = total_.load(std::memory_order_acquire);
    out.bytesPerSecond = bytesPerSecond_.load(std::memory_order_acquire);
    out.restartRequired = restartRequired_.load(std::memory_order_acquire);
    std::lock_guard<std::mutex> lock(stateMutex_);
    out.message = message_;
    out.error = error_;
    return out;
}

void StartupUpdateManager::requestCancel() { cancelRequested_.store(true, std::memory_order_release); }

void StartupUpdateManager::wait() {
    if (thread_ < 0) return;
    sceKernelWaitThreadEnd(thread_, nullptr, nullptr);
    sceKernelDeleteThread(thread_);
    thread_ = -1;
}

void StartupUpdateManager::setProgress(State state, uint64_t current, uint64_t total,
                                       uint64_t bytesPerSecond, const std::string& message) {
    state_.store(static_cast<int>(state), std::memory_order_release);
    current_.store(current, std::memory_order_release);
    total_.store(total, std::memory_order_release);
    bytesPerSecond_.store(bytesPerSecond, std::memory_order_release);
    std::lock_guard<std::mutex> lock(stateMutex_);
    message_ = message;
}

void StartupUpdateManager::setError(const std::string& error) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    error_ = error;
    message_ = error;
}

int StartupUpdateManager::workerEntry(SceSize args, void* argp) {
    (void)args;
    StartupUpdateManager* self = nullptr;
    if (argp) std::memcpy(&self, argp, sizeof(self));
    return self ? self->workerMain() : -1;
}

int StartupUpdateManager::workerMain() {
    const UpdateChecker::Result check = UpdateChecker::checkLatest(version_);
    if (cancelRequested_.load(std::memory_order_acquire)) {
        state_.store(static_cast<int>(State::Cancelled), std::memory_order_release);
        return 0;
    }
    if (check.state == UpdateChecker::State::UpToDate) {
        setProgress(State::UpToDate, 1, 1, 0, "Application is up to date  ·  v" + check.localVersion);
        diagnostics::log("[StartupUpdate] up to date local=" + check.localVersion + " remote=" + check.remoteVersion);
        return 0;
    }
    if (check.state == UpdateChecker::State::Failed) {
        state_.store(static_cast<int>(State::Failed), std::memory_order_release);
        setError(check.error.empty() ? "Update check failed" : check.error);
        diagnostics::log("[StartupUpdate] check failed: " + (check.error.empty() ? "unknown" : check.error));
        return 0;
    }

    setProgress(State::Downloading, 0, check.assetSize, 0, "Downloading update...");
    const bool applied = UpdateChecker::applyUpdate(
        check,
        [this](const UpdateChecker::ApplyProgress& progress) {
            State s = State::Downloading;
            if (progress.stage == UpdateChecker::ApplyStage::Extracting || progress.stage == UpdateChecker::ApplyStage::Finalizing) s = State::Installing;
            else if (progress.stage == UpdateChecker::ApplyStage::Error) s = State::Failed;
            setProgress(s, progress.current, progress.total, progress.bytesPerSecond,
                        progress.message.empty() ? "Working..." : progress.message);
        },
        [this]() { return cancelRequested_.load(std::memory_order_acquire); }
    );

    if (cancelRequested_.load(std::memory_order_acquire)) {
        state_.store(static_cast<int>(State::Cancelled), std::memory_order_release);
        std::lock_guard<std::mutex> lock(stateMutex_);
        message_ = "Update cancelled";
        return 0;
    }
    if (!applied) {
        state_.store(static_cast<int>(State::Failed), std::memory_order_release);
        setError("Update download/install failed");
        diagnostics::log("[StartupUpdate] update apply failed");
        return 0;
    }
    restartRequired_.store(true, std::memory_order_release);
    setProgress(State::Completed, 1, 1, 0, "Update installed — press START to restart");
    diagnostics::log("[StartupUpdate] update installed successfully; restart required");
    return 0;
}

} // namespace psvitaalive
''', encoding='utf-8')

h = catalog_h.read_text(encoding='utf-8')
h = re.sub(r'\n    // The update check is performed once per client process, before the first\n.*?\n    bool updateChecked_ = false;\n', '\n', h, flags=re.S)
catalog_h.write_text(h, encoding='utf-8')

c = catalog_cpp.read_text(encoding='utf-8')
c = c.replace('#include "update/update_checker.hpp"\n', '')
c = re.sub(r'\n    // IMPORTANT: perform the application update phase synchronously before\n.*?\n    workerThread_=sceKernelCreateThread\("PSVitaAliveCatalogWorker"', '\n    workerThread_=sceKernelCreateThread("PSVitaAliveCatalogWorker"', c, flags=re.S)
c = re.sub(r'\n    // Startup update work is completed in init\(\) before this worker exists\..*?\n    const int idx=', '\n    const int idx=', c, flags=re.S)
if 'UpdateChecker' in c or 'updateChecked_' in c:
    raise SystemExit('CatalogManager still owns update logic')
catalog_cpp.write_text(c, encoding='utf-8')

m = main.read_text(encoding='utf-8')
if '#include "update/startup_update_manager.hpp"' not in m:
    m = m.replace('#include "catalog/catalog_manager.hpp"\n', '#include "catalog/catalog_manager.hpp"\n#include "update/startup_update_manager.hpp"\n')
old_init = '''    psvitaalive::StorageManager storage;storage.initProjectDirs();\n    psvitaalive::InstallController installer;psvitaalive::CatalogManager catalogs;psvitaalive::ui::ImageCache images;\n    if(!installer.init())psvitaalive::diagnostics::log("[System] InstallController init failed");\n    if(!catalogs.init())psvitaalive::diagnostics::log("[System] CatalogManager init failed");\n    if(!images.init())psvitaalive::diagnostics::log("[System] ImageCache init failed");\n'''
if old_init not in m:
    raise SystemExit('main init block not found')
m = m.replace(old_init, '''    psvitaalive::StorageManager storage;storage.initProjectDirs();\n    psvitaalive::InstallController installer;psvitaalive::CatalogManager catalogs;psvitaalive::ui::ImageCache images;\n    if(!installer.init())psvitaalive::diagnostics::log("[System] InstallController init failed");\n''', 1)
marker = '    const int catalogCount=(int)psvitaalive::ui::CatalogType::Count;'
if marker not in m:
    raise SystemExit('main catalog marker not found')
startup = r'''    psvitaalive::StartupUpdateManager startupUpdate;
    screen.setCatalogLoading(true, "Startup Update", 0, 0, "Checking for application updates...");
    if(!startupUpdate.start(PSVITAALIVE_VERSION)){
        screen.showToast("Update startup unavailable — continuing", 1800);
    }else{
        while(startupUpdate.isBusy()){
            const auto us = startupUpdate.snapshot();
            screen.setCatalogLoading(true, "Startup Update", us.current, us.total,
                                      us.message.empty()?"Updating application...":us.message);
            if(!screen.updateAndDraw()){
                startupUpdate.requestCancel();
                break;
            }
            sceKernelDelayThread(16 * 1000);
        }
        startupUpdate.wait();
        const auto us = startupUpdate.snapshot();
        if(us.restartRequired){
            screen.setCatalogLoading(true, "Self-update", 1, 1, "Update installed — press START to restart");
            while(screen.updateAndDraw()){
                SceCtrlData pad{};
                sceCtrlPeekBufferPositive(0, &pad, 1);
                if(pad.buttons & SCE_CTRL_START){
                    screen.shutdown();
                    installer.shutdown();
                    psvitaalive::diagnostics::log("PSVitaAlive session END after self-update");
                    psvitaalive::diagnostics::shutdown();
                    sceKernelExitProcess(0);
                }
                sceKernelDelayThread(50 * 1000);
            }
            screen.shutdown();
            installer.shutdown();
            psvitaalive::diagnostics::shutdown();
            sceKernelExitProcess(0);
        }
        if(us.state == psvitaalive::StartupUpdateManager::State::Failed)
            screen.showToast("Update unavailable — continuing", 1800);
        else if(us.state == psvitaalive::StartupUpdateManager::State::Cancelled)
            screen.showToast("Update cancelled — continuing", 1600);
    }

    // Catalog and image workers do not exist until the startup update phase is finished.
    if(!catalogs.init())psvitaalive::diagnostics::log("[System] CatalogManager init failed");
    if(!images.init())psvitaalive::diagnostics::log("[System] ImageCache init failed");
    screen.setImageCache(&images);

'''
m = m.replace(marker, startup + marker, 1)
main.write_text(m, encoding='utf-8')

cm = cmake.read_text(encoding='utf-8')
if 'source/update/startup_update_manager.cpp' not in cm:
    cm = cm.replace('  source/update/update_checker.cpp\n', '  source/update/update_checker.cpp\n  source/update/startup_update_manager.cpp\n')
cmake.write_text(cm, encoding='utf-8')

# Architecture checks.
text = main.read_text(encoding='utf-8')
assert text.index('StartupUpdateManager startupUpdate') < text.index('if(!catalogs.init())')
assert text.index('if(!catalogs.init())') < text.index('if(!catalogs.request(')
assert 'UpdateChecker' not in catalog_cpp.read_text(encoding='utf-8')
assert 'updateChecked_' not in catalog_h.read_text(encoding='utf-8')
assert 'source/update/startup_update_manager.cpp' in cmake.read_text(encoding='utf-8')
print('OK: startup update is separate from CatalogManager and precedes catalog/image workers.')

#include "installer/install_controller.hpp"
#include "diagnostic_logger.hpp"
#include <psp2/net/netctl.h>
#include "storage/storage_manager.hpp"
#include "installer/plugin_detector.hpp"
#include "installer/app_settings.hpp"
#include "installer/refresh_manager.hpp"
#include "installer/bgdl_client.hpp"
#include "installer/pkg_bgdl_installer.hpp"
#include "installer/license_helper.hpp"
#include "archive/format_detector.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/shellutil.h>

#include <cstring>
#include <cstdlib>

namespace psvitaalive {
namespace {
/** Parse catalog size strings ("6.0 GB", "512MB", "1234567") to bytes. */
uint64_t parseHumanSizeBytes(const std::string& raw) {
    if (raw.empty()) return 0;
    std::string s;
    s.reserve(raw.size());
    for (unsigned char c : raw) {
        if (c == ' ' || c == '\t') continue;
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
        s.push_back(static_cast<char>(c));
    }
    if (s.empty()) return 0;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return 0;
    std::string u = end ? std::string(end) : std::string();
    uint64_t mul = 1;
    if (u.empty() || u == "b" || u == "byte" || u == "bytes") {
        if (v > 0 && u.empty() && s.find('.') == std::string::npos) {
            // pure integer digit string
            uint64_t n = 0;
            for (char c : s) {
                if (c < '0' || c > '9') return 0;
                n = n * 10ULL + static_cast<uint64_t>(c - '0');
            }
            return n;
        }
        mul = 1;
    } else if (u == "k" || u == "kb" || u == "kib") mul = 1024ULL;
    else if (u == "m" || u == "mb" || u == "mib") mul = 1024ULL * 1024ULL;
    else if (u == "g" || u == "gb" || u == "gib") mul = 1024ULL * 1024ULL * 1024ULL;
    else if (u == "t" || u == "tb" || u == "tib") mul = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
    else return 0;
    if (v <= 0.0) return 0;
    const double bytes = v * static_cast<double>(mul);
    if (bytes < 1.0) return 0;
    return static_cast<uint64_t>(bytes + 0.5);
}

std::string formatBytesShort(uint64_t b) {
    char o[48];
    const double v = static_cast<double>(b);
    if (b >= 1024ULL * 1024ULL * 1024ULL)
        sceClibSnprintf(o, sizeof(o), "%.2f GB", v / (1024.0 * 1024.0 * 1024.0));
    else if (b >= 1024ULL * 1024ULL)
        sceClibSnprintf(o, sizeof(o), "%.2f MB", v / (1024.0 * 1024.0));
    else if (b >= 1024ULL)
        sceClibSnprintf(o, sizeof(o), "%.2f KB", v / 1024.0);
    else
        sceClibSnprintf(o, sizeof(o), "%llu B", (unsigned long long)b);
    return o;
}


constexpr uint64_t RESULT_AUTO_DISMISS_MS = 8000;
}


namespace {

/** True when Wi-Fi/Ethernet is fully connected. Fail-open if NetCtl unavailable. */
bool networkIsConnected() {
    int state = 0;
    // VitaSDK: sceNetCtlInetGetState + SCE_NETCTL_STATE_*
    int r = sceNetCtlInetGetState(&state);
    if (r < 0) {
        // Not initialized yet — try init once (safe if already inited elsewhere).
        sceNetCtlInit();
        r = sceNetCtlInetGetState(&state);
    }
    if (r < 0) {
        // Unknown: do not block downloads on emulator/edge cases.
        return true;
    }
    return state == SCE_NETCTL_STATE_CONNECTED;
}

} // namespace

InstallController::InstallController() : downloads_(http_) {
    std::memset(message_, 0, sizeof(message_));
    std::memset(fileName_, 0, sizeof(fileName_));
    std::memset(stage_, 0, sizeof(stage_));
    std::memset(installPath_, 0, sizeof(installPath_));
    std::memset(titleId_, 0, sizeof(titleId_));
}

InstallController::~InstallController() { shutdown(); }

bool InstallController::init() {
    if (http_.isInitialized()) return true;
    settings_ = AppSettings::load();
    const HttpResult result = http_.init();
    if (result != HttpResult::Ok) {
        setState(InstallStatus::State::Failed, http_.lastError().c_str());
        diagnostics::log(std::string("[Installer] HTTP init failed: ") + http_.lastError());
        return false;
    }
    downloads_.setProgressCallback([this](const DownloadProgressEvent& event) {
        current_.store(event.downloaded);
        total_.store(event.total);
        speed_.store(event.bytesPerSecond);
        setFileName(event.fileName.c_str());
        setStage("Downloading");
        state_.store(static_cast<int>(InstallStatus::State::Downloading));
        // Prefer explicit status from DownloadManager (retries); else generic line.
        if (!event.message.empty())
            setMessage(event.message.c_str());
        else
            setMessage("Downloading...");
    });
    const int purged = downloads_.purgeIncompleteJobs();
    if (purged > 0) {
        char m[96];
        sceClibSnprintf(m, sizeof(m), "[Installer] purged %d residual download jobs", purged);
        diagnostics::log(m);
    }
    setStage("Idle");
    setState(InstallStatus::State::Idle, "Ready");
    if (settings_.startupPluginDetection) {
        plugins_ = PluginDetector::scan();
        diagnostics::log(std::string("[Installer] plugins: ") + plugins_.detail);
    } else {
        plugins_ = PluginStatus{};
        diagnostics::log("[Startup] plugin detection disabled by config");
    }
    diagnostics::log(std::string("[Installer] settings method=") + AppSettings::toString(settings_.installMethod) +
        " psp=" + AppSettings::toString(settings_.pspTarget));
    if (settings_.startupPluginDetection) {
        if (!plugins_.nonpdrm) diagnostics::log("[Installer] NoNpDrm not detected - licensed Vita PKG installs may fail");
        if (!plugins_.nopspemudrmKern) diagnostics::log("[Installer] NoPspEmuDrm not detected - PSP LiveArea bubbles unavailable (Adrenaline ISO path still works)");
    }
    diagnostics::log("[Installer] BGDL deferred until first PKG install request");
    if (sceShellUtilInitEvents(0) >= 0) {
        shellUtilReady_ = true;
        diagnostics::log("[Installer] sceShellUtilInitEvents ok");
    } else {
        shellUtilReady_ = false;
        diagnostics::log("[Installer] sceShellUtilInitEvents failed (PS lock unavailable)");
    }
    startKeepAwakeThread();
    diagnostics::log("[Installer] initialized");
    return true;
}

void InstallController::shutdown() {
    unlockShellDuringJob();
    stopKeepAwakeThread();
    if (workerThread_ >= 0) {
        sceKernelWaitThreadEnd(workerThread_, nullptr, nullptr);
        sceKernelDeleteThread(workerThread_);
        workerThread_ = -1;
    }
    http_.shutdown();
    activeJobId_.clear();
    activeZipDestination_.clear();
    activeFileName_.clear();
    workerDone_.store(true);
    setState(InstallStatus::State::Idle, "Stopped");
    diagnostics::log("[Installer] shutdown");
}

void InstallController::setSettings(const AppSettingsData& s) {
    settings_ = s;
    AppSettings::save(settings_);
    diagnostics::log(std::string("[Installer] settings saved method=") + AppSettings::toString(settings_.installMethod) +
        " psp=" + AppSettings::toString(settings_.pspTarget));
}

void InstallController::cancel() {
    const auto currentState = static_cast<InstallStatus::State>(state_.load());
    if (currentState != InstallStatus::State::Downloading || activeJobId_.empty()) return;
    downloads_.cancel(activeJobId_);
    setStage("Cancelling");
    setState(InstallStatus::State::Downloading, "Cancelling download...");
    diagnostics::log(std::string("[Installer] cancel requested job=") + activeJobId_);
}

void InstallController::acknowledgeResult() {
    const auto s = static_cast<InstallStatus::State>(state_.load());
    if (s != InstallStatus::State::Completed && s != InstallStatus::State::Failed
        && s != InstallStatus::State::Cancelled) return;
    diagnostics::log("[Installer] result acknowledged by UI");
    resultShownAtMs_.store(0);
    liveAreaOk_.store(false);
    setInstallPath("");
    setTitleId("");
    setStage("Idle");
    setState(InstallStatus::State::Idle, "Ready");
}

bool InstallController::busy() const {
    const auto s = static_cast<InstallStatus::State>(state_.load());
    return s == InstallStatus::State::Downloading || s == InstallStatus::State::Installing;
}

bool InstallController::requestInstall(
    const std::string& url,
    const std::string& fileName,
    const std::string& zipDestination,
    const std::string& zrif,
    const std::string& linkType,
    const std::string& contentId,
    const std::string& displayTitle,
    uint64_t expectedBytes
) {
    if (url.empty() || fileName.empty() || busy()) return false;

    // Downloads need network; extraction/install of an already-downloaded file does not.
    if (!networkIsConnected()) {
        setFileName(fileName.c_str());
        setStage("Network");
        setInstallPath("");
        setTitleId("");
        liveAreaOk_.store(false);
        current_.store(0);
        total_.store(expectedBytes);
        speed_.store(0);
        resultShownAtMs_.store(0);
        setState(InstallStatus::State::Failed,
            "No internet connection. Connect to Wi-Fi and try the download again. "
            "Extraction and install work offline once the file is fully downloaded.");
        diagnostics::log("[Installer] blocked: no network connection before download");
        resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
        return true; // accepted as a finished failed request so UI shows the modal
    }

    std::string niceTitle = displayTitle;
    if (niceTitle.empty()) niceTitle = fileName;
    {
        std::string cleaned;
        cleaned.reserve(niceTitle.size());
        bool space = false;
        for (unsigned char c : niceTitle) {
            if (c < 32) continue;
            if (c == ' ' || c == '\t') {
                if (!cleaned.empty() && !space) { cleaned.push_back(' '); space = true; }
            } else {
                cleaned.push_back(static_cast<char>(c));
                space = false;
            }
        }
        while (!cleaned.empty() && cleaned.back() == ' ') cleaned.pop_back();
        if (cleaned.size() > 80) cleaned.resize(80);
        if (!cleaned.empty()) niceTitle = cleaned;
    }

    // Pre-flight: require ~2.1x payload size free on ux0 (2x + 5%) when size is known.
    if (expectedBytes > 0) {
        uint64_t freeB = 0, totalB = 0;
        if (StorageManager::queryUx0Space(freeB, totalB)) {
            const uint64_t need = (expectedBytes / 10ULL) * 21ULL; // 2.1x, avoid overflow a bit
            // Prefer wider multiply when safe
            const uint64_t needExact = (expectedBytes <= (~0ULL / 21ULL))
                ? (expectedBytes * 21ULL) / 10ULL
                : need;
            if (freeB < needExact) {
                const uint64_t missing = needExact - freeB;
                char msg[320];
                sceClibSnprintf(
                    msg, sizeof(msg),
                    "Not enough free space on ux0. Need ~%s free (have %s). Free about %s more to continue.",
                    formatBytesShort(needExact).c_str(),
                    formatBytesShort(freeB).c_str(),
                    formatBytesShort(missing).c_str());
                setFileName(niceTitle.empty() ? fileName.c_str() : niceTitle.c_str());
                setStage("Space");
                setInstallPath("");
                setTitleId("");
                liveAreaOk_.store(false);
                current_.store(0);
                total_.store(expectedBytes);
                speed_.store(0);
                resultShownAtMs_.store(0);
                setState(InstallStatus::State::Failed, msg);
                diagnostics::log(std::string("[Installer] blocked: insufficient space need=") +
                                 formatBytesShort(needExact) + " free=" + formatBytesShort(freeB) +
                                 " payload=" + formatBytesShort(expectedBytes));
                return true;
            }
            diagnostics::log(std::string("[Installer] space check OK need=") +
                             formatBytesShort(needExact) + " free=" + formatBytesShort(freeB));
        } else {
            diagnostics::log("[Installer] ux0 space probe failed; skipping pre-check");
        }
    } else {
        diagnostics::log("[Installer] no expected size; space pre-check skipped");
    }

    activeZrif_ = zrif;
    activeContentId_ = contentId;

    const bool pkgInstall = BgdlClient::looksLikePkgUrl(url, fileName);
    const bool wantBgdl =
        pkgInstall &&
        (settings_.installMethod == InstallMethod::Bgdl ||
         settings_.installMethod == InstallMethod::Auto ||
         settings_.installMethod == InstallMethod::Direct);

    if (wantBgdl && pkgInstall) {
        // Show the preparation state now; the next status poll performs the BGDL
        // work on the main thread, preserving the previously working context.
        setFileName(niceTitle.c_str());
        setStage("BGDL");
        setInstallPath("");
        setTitleId("");
        liveAreaOk_.store(false);
        current_.store(0);
        total_.store(0);
        speed_.store(0);
        resultShownAtMs_.store(0);
        setState(InstallStatus::State::Downloading,
                 "Preparing license and queuing system download...");

        activeBgdlJob_ = true;
        activeBgdlUrl_ = url;
        activeBgdlTitle_ = niceTitle;
        activeBgdlLinkType_ = linkType;
        activeZrif_ = zrif;
        activeContentId_ = contentId;
        activeJobId_.clear();
        activeZipDestination_.clear();
        activeFileName_ = niceTitle;
        diagnostics::log(std::string("[Installer] PKG BGDL deferred one UI cycle title=") + niceTitle);
        if (settings_.pspTarget == PspTarget::Adrenaline) {
            diagnostics::log("[Installer] NOTE: PSP target is Adrenaline but file is official PKG — "
                "system BGDL always creates a LiveArea entry. Use ISO/CSO/PBP (or a VPK that contains them) for Adrenaline-only.");
        }
        return true;
    } else if (settings_.installMethod == InstallMethod::Bgdl) {
        diagnostics::log("[Installer] BGDL selected but file is not PKG - using direct path");
    }

    const auto s = static_cast<InstallStatus::State>(state_.load());
    if (s == InstallStatus::State::Completed || s == InstallStatus::State::Failed
        || s == InstallStatus::State::Cancelled) acknowledgeResult();

    if (workerThread_ >= 0 && workerDone_.load()) {
        sceKernelWaitThreadEnd(workerThread_, nullptr, nullptr);
        sceKernelDeleteThread(workerThread_);
        workerThread_ = -1;
    }
    if (!http_.isInitialized() && !init()) return false;

    const std::string jobId = downloads_.enqueue(url, fileName);
    if (jobId.empty()) {
        setState(InstallStatus::State::Failed, "Could not create download job");
        diagnostics::log("[Installer] could not create download job");
        return false;
    }

    activeJobId_ = jobId;
    activeZipDestination_ = zipDestination;
    activeFileName_ = fileName;
    current_.store(0);
    total_.store(0);
    speed_.store(0);
    liveAreaOk_.store(false);
    resultShownAtMs_.store(0);
    setInstallPath("");
    setTitleId("");
    setFileName(fileName.c_str());
    setStage("Downloading");
    workerDone_.store(false);
    setState(InstallStatus::State::Downloading, "Starting download...");
    diagnostics::log(std::string("[Installer] request job=") + jobId + " file=" + fileName);

    workerThread_ = sceKernelCreateThread("PSVitaAliveInstall", &InstallController::workerEntry,
        0x10000100, 64 * 1024, 0, 0, nullptr);
    if (workerThread_ < 0) {
        setState(InstallStatus::State::Failed, "Could not create worker thread");
        workerDone_.store(true);
        downloads_.cleanupCompletedJob(activeJobId_);
        activeJobId_.clear();
        diagnostics::log("[Installer] worker thread creation failed; job cleaned");
        return false;
    }

    InstallController* self = this;
    const int result = sceKernelStartThread(workerThread_, sizeof(self), &self);
    if (result < 0) {
        sceKernelDeleteThread(workerThread_);
        workerThread_ = -1;
        workerDone_.store(true);
        setState(InstallStatus::State::Failed, "Could not start worker thread");
        downloads_.cleanupCompletedJob(activeJobId_);
        activeJobId_.clear();
        diagnostics::log("[Installer] worker start failed; job cleaned");
        return false;
    }
    return true;
}

InstallStatus InstallController::status() const {
    if (activeBgdlJob_) {
        // The UI has already submitted the preparation frame. Execute BGDL now,
        // still on the main thread, without the new worker-thread path that caused
        // the regression.
        InstallController* self = const_cast<InstallController*>(this);
        self->activeBgdlJob_ = false;
        const std::string title = self->activeBgdlTitle_;
        const std::string url = self->activeBgdlUrl_;
        const std::string linkType = self->activeBgdlLinkType_;
        const std::string zrif = self->activeZrif_;
        const std::string contentId = self->activeContentId_;

        self->setMessage("Preparing license...");
        if (!BgdlClient::instance().available() && !BgdlClient::instance().init()) {
            self->setState(InstallStatus::State::Failed,
                           "BGDL unavailable on this device. Try again or check plugins.");
            self->resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
            diagnostics::log("[Installer] PKG BGDL failed: BGDL unavailable");
        } else {
            self->setMessage("Queuing system download...");
            PkgBgdlRequest preq;
            preq.title = title;
            preq.url = url;
            preq.zrif = zrif;
            preq.contentId = contentId;
            preq.type = PkgBgdlInstaller::typeFromLinkType(linkType);
            if (preq.zrif.empty() && !preq.contentId.empty() &&
                (preq.type == BgdlTaskType::Game || preq.type == BgdlTaskType::Psp)) {
                if (preq.contentId.find("-NPU") != std::string::npos ||
                    preq.contentId.find("-ULES") != std::string::npos ||
                    preq.contentId.find("-ULUS") != std::string::npos ||
                    preq.contentId.find("-UCUS") != std::string::npos ||
                    preq.contentId.find("-NPE") != std::string::npos ||
                    preq.contentId.find("-NPJ") != std::string::npos ||
                    preq.contentId.find("-NPH") != std::string::npos ||
                    linkType.find("PSP") != std::string::npos ||
                    linkType.find("PS1") != std::string::npos ||
                    linkType.find("PSX") != std::string::npos) {
                    preq.type = BgdlTaskType::Psp;
                }
            }
            const PkgBgdlResult bg = PkgBgdlInstaller::enqueue(preq);
            if (bg.ok) {
                self->setFileName(title.c_str());
                self->setStage("BGDL");
                self->setInstallPath("");
                self->setTitleId("");
                self->liveAreaOk_.store(false);
                char msg[384];
                sceClibSnprintf(msg, sizeof(msg),
                    "Queued: %s — open LiveArea notifications to watch download/install.",
                    title.c_str());
                self->setState(InstallStatus::State::Completed, msg);
                self->resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
                diagnostics::log(std::string("[Installer] PKG BGDL queued id=") + std::to_string(bg.bgdlId) +
                                 " title=" + title);
            } else {
                const char* failMsg = bg.message.empty()
                    ? "PKG license (zRIF) missing or BGDL queue failed"
                    : bg.message.c_str();
                self->setState(InstallStatus::State::Failed, failMsg);
                self->resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
                diagnostics::log(std::string("[Installer] PKG BGDL failed: ") + failMsg);
            }
        }
    }

    const_cast<InstallController*>(this)->maybeAutoAcknowledgeResult();

    InstallStatus result;
    result.state = static_cast<InstallStatus::State>(state_.load());
    result.current = current_.load();
    result.total = total_.load();
    result.bytesPerSecond = speed_.load();
    result.fileName = fileName_;
    result.stage = stage_;
    result.message = message_;
    result.installPath = installPath_;
    result.titleId = titleId_;
    result.liveAreaOk = liveAreaOk_.load();
    result.resultAutoCloseRemainingMs = 0;
    if (result.state == InstallStatus::State::Completed) {
        const uint64_t shown = resultShownAtMs_.load();
        if (shown != 0) {
            const uint64_t now = sceKernelGetSystemTimeWide() / 1000ULL;
            const uint64_t elapsed = (now > shown) ? (now - shown) : 0;
            if (elapsed < RESULT_AUTO_DISMISS_MS)
                result.resultAutoCloseRemainingMs = RESULT_AUTO_DISMISS_MS - elapsed;
        }
    }
    return result;
}

void InstallController::maybeAutoAcknowledgeResult() {
    const auto s = static_cast<InstallStatus::State>(state_.load());
    if (s != InstallStatus::State::Completed) return;
    const uint64_t shown = resultShownAtMs_.load();
    if (shown == 0) return;
    const uint64_t now = sceKernelGetSystemTimeWide() / 1000ULL;
    if (now >= shown && (now - shown) >= RESULT_AUTO_DISMISS_MS) {
        diagnostics::log("[Installer] success result auto-dismiss after timeout");
        acknowledgeResult();
    }
}

void InstallController::setMessage(const char* text) {
    if (!text) text = "";
    sceClibSnprintf(message_, sizeof(message_), "%s", text);
}
void InstallController::setFileName(const char* text) {
    if (!text) text = "";
    sceClibSnprintf(fileName_, sizeof(fileName_), "%s", text);
}
void InstallController::setStage(const char* text) {
    if (!text) text = "";
    sceClibSnprintf(stage_, sizeof(stage_), "%s", text);
}
void InstallController::setInstallPath(const char* text) {
    if (!text) text = "";
    sceClibSnprintf(installPath_, sizeof(installPath_), "%s", text);
}
void InstallController::setTitleId(const char* text) {
    if (!text) text = "";
    sceClibSnprintf(titleId_, sizeof(titleId_), "%s", text);
}
void InstallController::setState(InstallStatus::State state, const char* message) {
    setMessage(message);
    state_.store(static_cast<int>(state));
    const bool jobActive = (state == InstallStatus::State::Downloading ||
                            state == InstallStatus::State::Installing);
    if (jobActive) lockShellDuringJob();
    else unlockShellDuringJob();
}

void InstallController::startKeepAwakeThread() {
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
    diagnostics::log("[Installer] keep-awake thread started (anti-suspend + screen on while busy)");
}

void InstallController::stopKeepAwakeThread() {
    if (keepAwakeThread_ < 0) return;
    keepAwakeStop_.store(true);
    // Wake quickly: thread sleeps 5s max
    sceKernelWaitThreadEnd(keepAwakeThread_, nullptr, nullptr);
    sceKernelDeleteThread(keepAwakeThread_);
    keepAwakeThread_ = -1;
}


void InstallController::lockShellDuringJob() {
    if (shellLocked_) return;
    if (!shellUtilReady_) return;
    sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);
    sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_POWEROFF_MENU);
    shellLocked_ = true;
    diagnostics::log("[Installer] shell locked: PS blocked, soft power-off menu blocked, screen forced on");
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


int InstallController::keepAwakeEntry(SceSize args, void* argp) {
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
}

int InstallController::workerEntry(SceSize args, void* argp) {
    (void)args;
    InstallController* self = nullptr;
    if (argp) std::memcpy(&self, argp, sizeof(self));
    return self ? self->workerMain() : -1;
}

int InstallController::workerMain() {
    if (activeBgdlJob_) {
        activeBgdlJob_ = false;
        const std::string title = activeBgdlTitle_;
        const std::string url = activeBgdlUrl_;
        const std::string linkType = activeBgdlLinkType_;
        const std::string zrif = activeZrif_;
        const std::string contentId = activeContentId_;
        setMessage("Preparing license...");
        if (!BgdlClient::instance().available() && !BgdlClient::instance().init()) {
            setState(InstallStatus::State::Failed,
                     "BGDL unavailable on this device. Try again or check plugins.");
            resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
            workerDone_.store(true);
            workerThread_ = -1;
            return 0;
        }
        setMessage("Queuing system download...");
        PkgBgdlRequest preq;
        preq.title = title;
        preq.url = url;
        preq.zrif = zrif;
        preq.contentId = contentId;
        preq.type = PkgBgdlInstaller::typeFromLinkType(linkType);
        if (preq.zrif.empty() && !preq.contentId.empty() &&
            (preq.type == BgdlTaskType::Game || preq.type == BgdlTaskType::Psp)) {
            if (preq.contentId.find("-NPU") != std::string::npos ||
                preq.contentId.find("-ULES") != std::string::npos ||
                preq.contentId.find("-ULUS") != std::string::npos ||
                preq.contentId.find("-UCUS") != std::string::npos ||
                preq.contentId.find("-NPE") != std::string::npos ||
                preq.contentId.find("-NPJ") != std::string::npos ||
                preq.contentId.find("-NPH") != std::string::npos ||
                linkType.find("PSP") != std::string::npos ||
                linkType.find("PS1") != std::string::npos ||
                linkType.find("PSX") != std::string::npos) {
                preq.type = BgdlTaskType::Psp;
            }
        }
        diagnostics::log(std::string("[Installer] PKG via PkgBgdlInstaller (worker) type=") +
                         std::to_string(static_cast<int>(preq.type)) +
                         " zrif=" + (zrif.empty() ? "no" : "yes"));
        const PkgBgdlResult bg = PkgBgdlInstaller::enqueue(preq);
        if (bg.ok) {
            setFileName(title.c_str());
            setStage("BGDL");
            setInstallPath("");
            setTitleId("");
            liveAreaOk_.store(false);
            char msg[384];
            sceClibSnprintf(msg, sizeof(msg),
                "Queued: %s — open LiveArea notifications to watch download/install.",
                title.c_str());
            setState(InstallStatus::State::Completed, msg);
            resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
            diagnostics::log(std::string("[Installer] PKG BGDL queued id=") + std::to_string(bg.bgdlId) +
                             " title=" + title);
        } else {
            const char* failMsg = bg.message.empty()
                ? "PKG license (zRIF) missing or BGDL queue failed"
                : bg.message.c_str();
            setState(InstallStatus::State::Failed, failMsg);
            resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
            diagnostics::log(std::string("[Installer] PKG BGDL failed: ") + failMsg);
        }
        workerDone_.store(true);
        workerThread_ = -1;
        return 0;
    }

    const bool downloaded = downloads_.processQueue();
    DownloadJob* job = downloads_.findJob(activeJobId_);
    if (!downloaded || !job || job->state != DownloadState::Completed) {
        const bool cancelled = job && job->state == DownloadState::Cancelled;
        const std::string error = cancelled ? "Download cancelled"
            : (job && !job->lastError.empty() ? job->lastError : "Download failed");
        setStage(cancelled ? "Cancelled" : "Error");
        setState(cancelled ? InstallStatus::State::Cancelled : InstallStatus::State::Failed, error.c_str());
        liveAreaOk_.store(false);
        setInstallPath("");
        diagnostics::log(std::string("[Installer] ") + (cancelled ? "download cancelled" : "download failed") + ": " + error);
        if (!activeJobId_.empty()) downloads_.cleanupCompletedJob(activeJobId_);
        activeJobId_.clear();
        activeZrif_.clear();
        activeContentId_.clear();
        resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
        workerDone_.store(true);
        return 0;
    }
    current_.store(job->downloadedSize);
    total_.store(job->expectedSize ? job->expectedSize : job->downloadedSize);
    speed_.store(0);
    // Slower SD2Vita / USB media may still be flushing the last blocks after rename.
    // ZIP EOCD / ZIP64 locators live at EOF — reading too early can look "incomplete".
    setStage("Installing");
    setState(InstallStatus::State::Installing, "Finalizing file on storage...");
    diagnostics::log(std::string("[Installer] post-download storage settle before extract path=") + job->finalPath);
    sceKernelDelayThread(3000 * 1000); // 3s settle — margin for slower SD2Vita/USB
    {
        // Touch the file end so the FS materializes size/metadata before zip_open.
        const SceUID fd = sceIoOpen(job->finalPath.c_str(), SCE_O_RDONLY, 0);
        if (fd >= 0) {
            const SceOff sz = sceIoLseek(fd, 0, SCE_SEEK_END);
            if (sz > 0) current_.store(static_cast<uint64_t>(sz));
            sceIoClose(fd);
            diagnostics::log(std::string("[Installer] storage settle size=") + std::to_string(static_cast<long long>(sz > 0 ? sz : -1)));
        } else {
            diagnostics::log("[Installer] storage settle: open failed (continuing)");
        }
    }
    setState(InstallStatus::State::Installing, "Preparing installation...");
    diagnostics::log(std::string("[Installer] installing job=") + activeJobId_ + " file=" + job->finalPath);

    std::string directRifPath;
    {
        FormatDetector det;
        const DetectResult dr = det.detectFile(job->finalPath);
        const std::string ext = FormatDetector::extensionOf(job->finalPath);
        const bool isPkg = (dr.format == FileFormat::Pkg) || (ext == "pkg");
        if (isPkg) {
            std::string zrif = activeZrif_;
            if (zrif.empty() && !activeContentId_.empty()) {
                std::string looked;
                if (LicenseHelper::lookupZrifForContentId(activeContentId_, looked)) {
                    zrif = looked;
                    diagnostics::log("[Installer] Direct PKG: zRIF from content_id index");
                }
            }
            if (!zrif.empty()) {
                std::string err;
                if (LicenseHelper::prepareBgdlLicense(zrif, std::string(), directRifPath, err)) {
                    diagnostics::log(std::string("[Installer] Direct PKG: RIF ready at ") + directRifPath);
                } else {
                    diagnostics::log(std::string("[Installer] Direct PKG: license prepare failed: ") + err);
                    directRifPath.clear();
                }
            } else {
                diagnostics::log("[Installer] Direct PKG: no zRIF/content_id — promote without RIF (may fail)");
            }
        }
    }

    // ZIP extract / VPK promote can fail transiently (I/O, promoter glitch, FS lag).
    // Retry with clear UI + logs. EOCD/ZIP64/incomplete: allow a few slower retries
    // (storage may still be flushing the end of a large file); then stop.
    static const int kMaxInstallAttempts = 5;
    static const int kMaxIntegrityAttempts = 3; // EOCD / ZIP64 / incomplete
    InstallDispatchResult result = InstallDispatchResult::InstallFailed;
    std::string lastInstallError;
    int attemptsUsed = 0;
    for (int attempt = 1; attempt <= kMaxInstallAttempts; ++attempt) {
        attemptsUsed = attempt;
        if (attempt > 1) {
            const bool integrityHint =
                lastInstallError.find("incomplete") != std::string::npos ||
                lastInstallError.find("EOCD") != std::string::npos ||
                lastInstallError.find("ZIP64") != std::string::npos;
            char retryMsg[240];
            if (integrityHint) {
                sceClibSnprintf(
                    retryMsg, sizeof(retryMsg),
                    "Archive looked incomplete — waiting and retrying (%d/%d)...",
                    attempt, kMaxIntegrityAttempts);
            } else {
                sceClibSnprintf(
                    retryMsg, sizeof(retryMsg),
                    "Something went wrong — retrying extract/install (%d/%d)...",
                    attempt, kMaxInstallAttempts);
            }
            setStage("Retrying");
            setState(InstallStatus::State::Installing, retryMsg);
            diagnostics::log(
                std::string("[Installer] extract/install retry ") +
                std::to_string(attempt) + "/" +
                std::to_string(integrityHint ? kMaxIntegrityAttempts : kMaxInstallAttempts) +
                (integrityHint ? " (integrity/FS settle)" : "") +
                " after: " + lastInstallError);
            // Longer pause when EOCD may be a short FS lag after a big write.
            sceKernelDelayThread((integrityHint ? 4 : 2) * 1000 * 1000);
        }

        dispatcher_.setPspTarget(settings_.pspTarget);
        result = dispatcher_.installFile(
            job->finalPath,
            [&](const InstallDispatchProgress& progress) {
                current_.store(progress.current);
                total_.store(progress.total);
                setStage(progress.message.empty() ? "Installing" : progress.message.c_str());
                if (!progress.message.empty()) setMessage(progress.message.c_str());
            },
            nullptr,
            activeZipDestination_,
            directRifPath
        );

        if (result == InstallDispatchResult::Ok ||
            result == InstallDispatchResult::Cancelled) {
            break;
        }

        lastInstallError = dispatcher_.lastError().empty()
            ? "Installation failed"
            : dispatcher_.lastError();
        diagnostics::log(
            std::string("[Installer] extract/install attempt ") +
            std::to_string(attempt) + "/" + std::to_string(kMaxInstallAttempts) +
            " failed: " + lastInstallError);

        const bool hardPermanent =
            result == InstallDispatchResult::InvalidArgument ||
            result == InstallDispatchResult::UnsupportedFormat ||
            result == InstallDispatchResult::DetectFailed ||
            lastInstallError.find("cancelled") != std::string::npos ||
            lastInstallError.find("not found") != std::string::npos ||
            lastInstallError.find("empty installation") != std::string::npos;
        if (hardPermanent) {
            diagnostics::log(
                std::string("[Installer] permanent install error — stop retries: ") +
                lastInstallError);
            break;
        }

        // Soft integrity errors: retry a few times with longer delay, then give up.
        const bool integrityError =
            lastInstallError.find("incomplete") != std::string::npos ||
            lastInstallError.find("EOCD") != std::string::npos ||
            lastInstallError.find("ZIP64") != std::string::npos;
        if (integrityError && attempt >= kMaxIntegrityAttempts) {
            diagnostics::log(
                std::string("[Installer] integrity error after ") +
                std::to_string(attempt) + " attempts — stop retries: " + lastInstallError);
            break;
        }
    }

    if (result != InstallDispatchResult::Ok) {
        const bool cancelled = (result == InstallDispatchResult::Cancelled);
        std::string error = cancelled
            ? (dispatcher_.lastError().empty() ? "Installation cancelled" : dispatcher_.lastError())
            : (lastInstallError.empty()
                   ? (dispatcher_.lastError().empty() ? "Installation failed" : dispatcher_.lastError())
                   : lastInstallError);
        if (!cancelled && attemptsUsed > 1) {
            error = "Failed after " + std::to_string(attemptsUsed) + " attempts: " + error;
        }
        setStage(cancelled ? "Cancelled" : "Error");
        setState(cancelled ? InstallStatus::State::Cancelled : InstallStatus::State::Failed, error.c_str());
        liveAreaOk_.store(false);
        setInstallPath(dispatcher_.lastInstallPath().c_str());
        setTitleId(dispatcher_.lastTitleId().c_str());
        diagnostics::log(std::string("[Installer] ") + (cancelled ? "installation cancelled: " : "installation failed: ") + error +
            " attempts=" + std::to_string(attemptsUsed));
        downloads_.cleanupCompletedJob(activeJobId_);
        activeJobId_.clear();
        resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
    } else {
        setInstallPath(dispatcher_.lastInstallPath().c_str());
        setTitleId(dispatcher_.lastTitleId().c_str());
        liveAreaOk_.store(dispatcher_.lastLiveAreaOk());

        char okMsg[320];
        if (!dispatcher_.lastInstallPath().empty()) {
            sceClibSnprintf(okMsg, sizeof(okMsg), "Installed at %s", dispatcher_.lastInstallPath().c_str());
        } else {
            sceClibSnprintf(okMsg, sizeof(okMsg), "Installation completed");
        }
        std::string verifyMsg;
        const bool verified = RefreshManager::verifyAfterInstall(
            dispatcher_.lastTitleId(),
            dispatcher_.lastInstallPath(),
            dispatcher_.lastLiveAreaOk(),
            verifyMsg
        );
        (void)verified;
        liveAreaOk_.store(dispatcher_.lastLiveAreaOk());
        if (!verifyMsg.empty()) {
            sceClibSnprintf(okMsg, sizeof(okMsg), "%s", verifyMsg.c_str());
        }
        {
            const std::string& p = dispatcher_.lastInstallPath();
            const bool zipLike = !dispatcher_.lastLiveAreaOk() && !p.empty() &&
                (p.find("ux0:data") != std::string::npos ||
                 p.find("ux0:repatch") != std::string::npos ||
                 p.find("ux0:app/") == std::string::npos);
            if (zipLike && dispatcher_.lastTitleId().empty()) {
                sceClibSnprintf(okMsg, sizeof(okMsg), "ZIP extracted to %s", p.c_str());
            }
        }
        if (!dispatcher_.lastLiveAreaOk() && !dispatcher_.lastTitleId().empty() &&
            dispatcher_.lastInstallPath().find("ux0:app/") != std::string::npos) {
            sceClibSnprintf(
                okMsg, sizeof(okMsg),
                "Installed %s — if bubble missing: VitaShell Refresh LiveArea or reboot",
                dispatcher_.lastTitleId().c_str()
            );
        }
        setStage("Completed");
        setState(InstallStatus::State::Completed, okMsg);
        diagnostics::log(std::string("[Installer] installation completed path=") +
            dispatcher_.lastInstallPath() + " titleId=" + dispatcher_.lastTitleId() +
            " liveArea=" + (dispatcher_.lastLiveAreaOk() ? "yes" : "no") +
            " verify=" + verifyMsg +
            " attempts=" + std::to_string(attemptsUsed));
        downloads_.cleanupCompletedJob(activeJobId_);
        activeJobId_.clear();
        resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
    }

    workerDone_.store(true);
    return 0;
}

} // namespace psvitaalive

#include "installer/install_controller.hpp"
#include "network/parallel_archive_download.hpp"
#include "update/installed_release_tracker.hpp"
#include "localization/localization.hpp"
#include "diagnostic_logger.hpp"
#include <psp2/net/netctl.h>
#include "storage/storage_manager.hpp"
#include "installer/plugin_detector.hpp"
#include "installer/tai_config_editor.hpp"
#include "storage/storage_manager.hpp"
#include "installer/app_settings.hpp"
#include "installer/install_archive_policy.hpp"
#include "installer/install_retry_policy.hpp"
#include "installer/install_space_policy.hpp"
#include "installer/refresh_manager.hpp"
#include "installer/bgdl_client.hpp"
#include "installer/pkg_bgdl_installer.hpp"
#include "installer/license_helper.hpp"
#include "archive/format_detector.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/shellutil.h>

#include <cstring>
#include <algorithm>
#include <cstdlib>


namespace {
// Official PSP/PS1 PKG detection (PKGj-style): not Vita VPK.
bool looksLikePspPs1Pkg(const std::string& linkType, const std::string& contentId, const std::string& fileName) {
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string lt = lower(linkType);
    if (lt.find("psp") != std::string::npos || lt.find("ps1") != std::string::npos ||
        lt.find("psx") != std::string::npos) {
        return true;
    }
    const std::string cid = contentId; // keep case for ID tokens
    // PS Vita retail/content IDs use PCS* title IDs (PCSA/PCSB/PCSC/.../PCSG/etc).
    // Never classify those as PSP/PS1: Vita PKGs must stay on the system BGDL path.
    if (cid.find("-PCS") != std::string::npos) return false;
    const char* tags[] = {
        "-NPU", "-NPE", "-NPJ", "-NPH", "-NPG",
        "-ULES", "-ULUS", "-ULJS", "-UCUS", "-UCES", "-UCJS",
        "-NPEZ", "-NPUZ", "-NPJH", "-NPHG", "-NPJG",
    };
    for (const char* tag : tags) {
        if (cid.find(tag) != std::string::npos) return true;
    }
    const std::string fn = lower(fileName);
    if (fn.find("psp") != std::string::npos || fn.find("ps1") != std::string::npos) return true;
    return false;
}

// Vita commercial PKG URLs from Sony/NPS catalogs may end in an opaque CDN token
// rather than ".pkg". content_id is therefore authoritative metadata for routing:
// any Vita PCS* content ID must enter BGDL before DownloadManager sees the URL.
bool looksLikeVitaPkgMetadata(const std::string& contentId, const std::string& zrif) {
    if (!zrif.empty()) return true;
    if (contentId.empty()) return false;
    std::string cid = contentId;
    for (char& c : cid) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (cid.find("-PCS") != std::string::npos) return true;
    return cid.size() >= 3 && cid[0] == 'P' && cid[1] == 'C' && cid[2] == 'S';
}
} // namespace

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

bool looksLikeDamagedZipError(const std::string& e) {
    static const char* needles[] = {
        "CRC", "crc", "compressed data", "data error", "unexpected end",
        "premature", "truncated", "incomplete", "EOCD", "ZIP64",
        "size mismatch after extract", "size mismatch after download"
    };
    for (const char* n : needles) if (e.find(n) != std::string::npos) return true;
    return false;
}

std::string sanitizeRecoveryComponent(const std::string& input, const char* fallback) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char uc : input) {
        const char ch = static_cast<char>(uc);
        if (uc < 32 || ch == '/' || ch == '\\' || ch == ':') {
  if (!out.empty() && out.back() != '_') out.push_back('_');
  continue;
        }
        out.push_back(ch);
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    if (out.empty()) out = fallback ? fallback : "unknown";
    if (out.size() > 96) out.resize(96);
    return out;
}

std::string uniqueRecoveryPath(StorageManager& st, const std::string& dir, const std::string& fileName) {
    const std::string safe = sanitizeRecoveryComponent(fileName, "archive.zip");
    std::string candidate = dir + "/" + safe;
    if (!st.exists(candidate)) return candidate;
    const size_t dot = safe.find_last_of('.');
    const std::string stem = (dot == std::string::npos) ? safe : safe.substr(0, dot);
    const std::string ext = (dot == std::string::npos) ? std::string() : safe.substr(dot);
    for (int i = 2; i <= 999; ++i) {
        candidate = dir + "/" + stem + " (" + std::to_string(i) + ")" + ext;
        if (!st.exists(candidate)) return candidate;
    }
    return dir + "/" + stem + "_recovered" + ext;
}
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


bool isPluginLinkType(const std::string& linkType) {
    std::string lt;
    lt.reserve(linkType.size());
    for (unsigned char c : linkType) lt.push_back(static_cast<char>(std::tolower(c)));
    return lt == "plugin" || lt == "plugins";
}

std::string basenameFromPathOrUrl(const std::string& s) {
    if (s.empty()) return "plugin.bin";
    size_t q = s.find('?');
    std::string u = (q == std::string::npos) ? s : s.substr(0, q);
    size_t slash = u.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? u : u.substr(slash + 1);
    if (name.empty()) name = "plugin.bin";
    return name;
}

InstallController::InstallController() : downloads_(http_) {
    std::memset(message_, 0, sizeof(message_));
    std::memset(fileName_, 0, sizeof(fileName_));
    std::memset(stage_, 0, sizeof(stage_));
    std::memset(installPath_, 0, sizeof(installPath_));
    std::memset(titleId_, 0, sizeof(titleId_));
}

InstallController::~InstallController() { shutdown(); }

int InstallController::cleanupStartupDownloads(const StartupCleanupProgressFn& onProgress) {
    // No install/download worker has started while the loading overlay is active.
    return downloads_.purgeIncompleteJobs(onProgress);
}

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
        setStage(event.state == DownloadState::Verifying ? "Verifying" : "Downloading");
        state_.store(static_cast<int>(InstallStatus::State::Downloading));
        // Prefer explicit status from DownloadManager (retries); else generic line.
        if (!event.message.empty())
            setMessage(event.message.c_str());
        else
            setMessage(::psvitaalive::L(::psvitaalive::TextId::InstMsgDownloading));
    });
    setStage("Idle");
    setState(InstallStatus::State::Idle, ::psvitaalive::L(::psvitaalive::TextId::InstMsgReady));
    if (settings_.startupPluginDetection) {
        plugins_ = PluginDetector::scan();
        diagnostics::log(std::string("[Installer] plugins: ") + plugins_.detail);
    } else {
        plugins_ = PluginStatus{};
        diagnostics::log("[Startup] plugin detection disabled by config");
    }
    diagnostics::log(std::string("[Installer] settings method=") + AppSettings::toString(settings_.installMethod) +
        " psp=" + AppSettings::toString(settings_.pspTarget) +
        " media=" + AppSettings::toString(settings_.pspMediaFormat));
    if (settings_.startupPluginDetection) {
        if (!plugins_.nonpdrm) diagnostics::log("[Installer] NoNpDrm not detected - licensed Vita PKG installs may fail");
        if (!plugins_.nopspemudrmKern || !plugins_.nopspemudrmUser)
            diagnostics::log("[Installer] NoPspEmuDrm incomplete (missing/commented in config or file absent) - PSP LiveArea blocked; Adrenaline path still works");
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
    setState(InstallStatus::State::Idle, ::psvitaalive::L(::psvitaalive::TextId::InstMsgStopped));
    diagnostics::log("[Installer] shutdown");
}

void InstallController::setSettings(const AppSettingsData& s) {
    settings_ = s;
    AppSettings::save(settings_);
    diagnostics::log(std::string("[Installer] settings saved method=") + AppSettings::toString(settings_.installMethod) +
        " psp=" + AppSettings::toString(settings_.pspTarget) +
        " media=" + AppSettings::toString(settings_.pspMediaFormat));
}

void InstallController::cancel() {
    const auto currentState = static_cast<InstallStatus::State>(state_.load());
    if (currentState == InstallStatus::State::Downloading && !activeJobId_.empty()) {
        installCancelRequested_.store(true, std::memory_order_release);
        downloads_.cancel(activeJobId_);
        setStage("Cancelling");
        setState(InstallStatus::State::Downloading, ::psvitaalive::L(::psvitaalive::TextId::InstMsgCancellingDownload));
        diagnostics::log(std::string("[Installer] download cancellation requested job=") + activeJobId_);
    } else if (currentState == InstallStatus::State::Installing &&
               workerThread_ >= 0 && !workerDone_.load(std::memory_order_acquire) &&
               (savedZipMode_ || !isPluginLinkType(activeLinkType_))) {
        // Plugin copies have their own installer path without a cancellation
        // callback; retain the old behavior rather than showing a false
        // "Cancelling" status. Normal ZIP/VPK installs are cooperative.
        // Never terminate a worker mid-write.
        installCancelRequested_.store(true, std::memory_order_release);
        setStage("Cancelling");
        setState(InstallStatus::State::Installing, "Cancelling installation...");
        diagnostics::log("[Installer] extraction/installation cancellation requested");
    }
}

void InstallController::acknowledgeResult() {
    // A terminal state may be published while a worker is still persisting its
    // receipt or cleaning its DownloadManager job. Never clear shared fields
    // before that worker has finished.
    if (workerThread_ >= 0 && !workerDone_.load(std::memory_order_acquire))
        return;
    const auto s = static_cast<InstallStatus::State>(state_.load());
    if (s != InstallStatus::State::Completed && s != InstallStatus::State::Failed
        && s != InstallStatus::State::Cancelled) return;
    // A recovered ZIP needs an explicit Keep/Delete decision. Do not let a generic
    // close path accidentally leave a multi-GB file or erase it without consent.
    if (zipRecoveryAvailable_.load()) {
        diagnostics::log("[Installer] result acknowledge ignored: ZIP recovery decision pending");
        return;
    }
    diagnostics::log("[Installer] result acknowledged by UI");
    resultShownAtMs_.store(0);
    liveAreaOk_.store(false);
    needsReboot_.store(false);
    setInstallPath("");
    setTitleId("");
    archiveSourceUnavailable_.store(false);
    zipRecoveryAvailable_.store(false);
    zipRecoveryMayBeCorrupt_.store(false);
    zipRecoveryPath_.clear();
    zipRecoveryInfoPath_.clear();
    zipRecoveryExtractPath_.clear();
    zipRecoveryJobId_.clear();
    setStage("Idle");
    setState(InstallStatus::State::Idle, ::psvitaalive::L(::psvitaalive::TextId::InstMsgReady));
}

bool InstallController::resolveZipRecovery(bool keep, std::string* savedPath) {
    if (savedPath) savedPath->clear();
    if (!zipRecoveryAvailable_.load() ||
        static_cast<InstallStatus::State>(state_.load()) != InstallStatus::State::Failed)
        return false;

    StorageManager st;
    if (keep) {
        if (zipRecoveryPath_.empty() || !st.exists(zipRecoveryPath_)) {
            setMessage(::psvitaalive::L("ZIP_RECOVERY_KEEP_FAILED"));
            diagnostics::log("[Installer] ZIP recovery keep failed: archive missing");
            return false;
        }
        // A completed ZIP stays inside its transient job until the user chooses Keep.
        // Only at this moment does it become a persistent /manual/ archive.
        if (!zipRecoveryJobId_.empty()) {
            const DownloadJob* job = downloads_.findJob(zipRecoveryJobId_);
            if (!job) {
                setMessage(::psvitaalive::L("ZIP_RECOVERY_KEEP_FAILED"));
                return false;
            }
            const std::string key = sanitizeRecoveryComponent(
                !pendingTitleId_.empty() ? pendingTitleId_ : pendingAppId_, "unknown");
            const std::string dir = std::string(StorageManager::BASE_DIR) + "/manual/" + key;
            if (!st.createDirectories(dir)) {
                setMessage(::psvitaalive::L("ZIP_RECOVERY_KEEP_FAILED"));
                diagnostics::log("[Installer] ZIP recovery keep failed: cannot create " + dir);
                return false;
            }
            const std::string target = uniqueRecoveryPath(st, dir, job->fileName);
            const std::string infoPath = target + ".info.txt";
            std::string info;
            info += "PS Vita Alive Store - ZIP manual recovery\n\n";
            info += "App: " + (activeDisplayTitle_.empty() ? pendingAppId_ : activeDisplayTitle_) + "\n";
            info += "App ID: " + (pendingAppId_.empty() ? std::string("-") : pendingAppId_) + "\n";
            info += "Title ID: " + (pendingTitleId_.empty() ? std::string("-") : pendingTitleId_) + "\n";
            info += "Archive: " + job->fileName + "\n";
            info += "Saved path: " + target + "\n";
            info += "Manual extraction target: " + zipRecoveryExtractPath_ + "\n";
            info += "Recovery decision: kept\n";
            if (zipRecoveryMayBeCorrupt_.load())
                info += "Warning: archive may be damaged/incomplete; manual extraction may also fail.\n";

            // Metadata first: after an unexpected shutdown we may leave a tiny
            // orphan info file, but never a multi-GB untracked ZIP in /manual/.
            if (!st.writeTextFile(infoPath, info)) {
                setMessage(::psvitaalive::L("ZIP_RECOVERY_KEEP_FAILED"));
                diagnostics::log("[Installer] ZIP recovery keep failed: metadata write " + infoPath);
                return false;
            }
            if (!st.rename(zipRecoveryPath_, target)) {
                st.removeFile(infoPath);
                setMessage(::psvitaalive::L("ZIP_RECOVERY_KEEP_FAILED"));
                diagnostics::log("[Installer] ZIP recovery keep failed: move to " + target);
                return false;
            }
            const std::string oldJobId = zipRecoveryJobId_;
            zipRecoveryPath_ = target;
            zipRecoveryInfoPath_ = infoPath;
            zipRecoveryJobId_.clear();
            if (!downloads_.cleanupCompletedJob(oldJobId))
                diagnostics::log("[Installer] saved ZIP; transient metadata cleanup pending at startup");
        }
        diagnostics::log("[Installer] ZIP recovery action=kept path=" + zipRecoveryPath_);
    } else {
        bool ok = true;
        if (!zipRecoveryJobId_.empty())
            ok = downloads_.cleanupCompletedJob(zipRecoveryJobId_);
        else if (!zipRecoveryPath_.empty() && st.exists(zipRecoveryPath_))
            ok = st.removeFile(zipRecoveryPath_);
        if (ok && !zipRecoveryInfoPath_.empty() && st.exists(zipRecoveryInfoPath_))
            ok = st.removeFile(zipRecoveryInfoPath_);
        if (!ok) {
            setMessage(::psvitaalive::L("ZIP_RECOVERY_DELETE_FAILED"));
            diagnostics::log("[Installer] ZIP recovery delete failed path=" + zipRecoveryPath_);
            return false;
        }
        diagnostics::log("[Installer] ZIP recovery action=deleted path=" + zipRecoveryPath_);
    }

    // Return the final, persistent manual archive path only after Keep succeeds.
    if (keep && savedPath) *savedPath = zipRecoveryPath_;
    archiveSourceUnavailable_.store(false);
    zipRecoveryAvailable_.store(false);
    zipRecoveryMayBeCorrupt_.store(false);
    zipRecoveryJobId_.clear();
    acknowledgeResult();
    return true;
}


std::vector<SavedZipArchive> InstallController::savedZips(
    const std::string& appId, const std::string& titleId) const {
    std::vector<SavedZipArchive> results;
    if (appId.empty() && titleId.empty()) return results;
    StorageManager st;
    const std::string root = std::string(StorageManager::BASE_DIR) + "/manual/";
    std::vector<std::string> keys;
    if (!titleId.empty()) keys.push_back(sanitizeRecoveryComponent(titleId, "unknown"));
    if (!appId.empty()) {
        const std::string key = sanitizeRecoveryComponent(appId, "unknown");
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
    }
    for (const auto& key : keys) {
        const std::string folder = root + key;
        const SceUID dir = sceIoDopen(folder.c_str());
        if (dir < 0) continue;
        SceIoDirent entry{};
        int seen = 0;
        while (++seen <= 128 && results.size() < 32 && sceIoDread(dir, &entry) > 0) {
            const std::string name(entry.d_name);
            const std::string suffix = ".zip.info.txt";
            if (name.size() <= suffix.size() ||
                name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
                continue;
            const std::string info = folder + "/" + name;
            const std::string archive = folder + "/" + name.substr(0, name.size() - 9);
            const int64_t infoSize = st.fileSize(info);
            if (infoSize <= 0 || infoSize > 8192 || st.fileSize(archive) <= 0)
                continue;
            std::string metadata;
            if (!st.readTextFile(info, metadata)) continue;
            const auto field = [&](const char* label) -> std::string {
                const size_t p = metadata.find(label);
                if (p == std::string::npos || (p != 0 && metadata[p - 1] != '\n')) return {};
                const size_t at = p + std::strlen(label);
                const size_t end = metadata.find_first_of("\r\n", at);
                return metadata.substr(at, end == std::string::npos ? end : end - at);
            };
            if (field("App ID: ") != appId ||
                (!titleId.empty() && field("Title ID: ") != titleId)) continue;
            const std::string dest = field("Manual extraction target: ");
            if (dest.size() < 5 || dest.size() > 180 ||
                dest.find("..") != std::string::npos ||
                !(dest.compare(0, 4, "ux0:") == 0 ||
                  dest.compare(0, 4, "ur0:") == 0 ||
                  dest.compare(0, 5, "uma0:") == 0)) continue;
            bool safe = true;
            for (unsigned char ch : dest) if (ch < 0x21 || ch > 0x7e) safe = false;
            if (!safe) continue;
            SavedZipArchive saved;
            saved.fileName = name.substr(0, name.size() - 9);
            saved.archivePath = archive;
            saved.target = dest;
            saved.infoPath = info;
            results.push_back(std::move(saved));
        }
        sceIoDclose(dir);
    }
    std::sort(results.begin(), results.end(), [](const SavedZipArchive& a, const SavedZipArchive& b) {
        if (a.fileName != b.fileName) return a.fileName < b.fileName;
        return a.archivePath < b.archivePath;
    });
    return results;
}

bool InstallController::deleteSavedZip(const std::string& appId,
                                       const std::string& titleId,
                                       const std::string& archivePath) {
    // Trust only archives returned by the existing app/title-bound metadata scan.
    if (archivePath.empty() || busy() || zipRecoveryAvailable_.load()) return false;
    const auto archives = savedZips(appId, titleId);
    const auto it = std::find_if(archives.begin(), archives.end(),
        [&](const SavedZipArchive& z) { return z.archivePath == archivePath; });
    if (it == archives.end()) {
        diagnostics::log("[Installer] refused manual ZIP deletion: path not owned by app");
        return false;
    }
    StorageManager st;
    if (!st.removeFile(it->archivePath)) {
        diagnostics::log("[Installer] failed to delete saved manual ZIP: " + it->archivePath);
        return false;
    }
    // The archive is gone; a tiny orphan metadata file must not turn a real
    // deletion into a reported failure. Attempt metadata cleanup separately.
    if (!st.removeFile(it->infoPath))
        diagnostics::log("[Installer] saved ZIP deleted; metadata cleanup pending: " + it->infoPath);
    diagnostics::log("[Installer] deleted saved manual ZIP: " + archivePath);
    return true;
}

bool InstallController::hasSavedZip(const std::string& appId, const std::string& titleId) const {
    return !savedZips(appId, titleId).empty();
}

bool InstallController::requestSavedZipExtraction(
    const std::string& appId, const std::string& titleId,
    const std::string& archivePath) {
    if (busy() || zipRecoveryAvailable_.load()) return false;
    // Never trust a UI-provided path: resolve the selected archive afresh from
    // its app-specific metadata. This also handles deletion between UI frames.
    const auto matches = savedZips(appId, titleId);
    const auto selected = std::find_if(matches.begin(), matches.end(),
        [&](const SavedZipArchive& z) { return z.archivePath == archivePath; });
    if (selected == matches.end()) {
        diagnostics::log("[Installer] selected saved ZIP missing or metadata invalid app=" + appId);
        return false;
    }
    if (workerThread_ >= 0) {
        if (!workerDone_.load(std::memory_order_acquire)) return false;
        sceKernelWaitThreadEnd(workerThread_, nullptr, nullptr);
        sceKernelDeleteThread(workerThread_);
        workerThread_ = -1;
    }
    const auto s = static_cast<InstallStatus::State>(state_.load());
    if (s == InstallStatus::State::Completed || s == InstallStatus::State::Failed ||
        s == InstallStatus::State::Cancelled) acknowledgeResult();
    savedZipPath_ = selected->archivePath;
    savedZipTarget_ = selected->target;
    savedZipInfoPath_ = selected->infoPath;
    savedZipMode_ = true;
    installCancelRequested_.store(false, std::memory_order_release);
    activeJobId_.clear();
    activeZipDestination_ = selected->target;
    zipRecoveryAvailable_.store(false);
    current_.store(0); total_.store(0); speed_.store(0);
    liveAreaOk_.store(false); needsReboot_.store(false);
    resultShownAtMs_.store(0);
    setFileName(selected->fileName.c_str());
    setStage("Extracting");
    setState(InstallStatus::State::Installing, "Extracting saved ZIP without downloading...");
    workerDone_.store(false, std::memory_order_release);
    workerThread_ = sceKernelCreateThread("PSVitaAliveInstall", &InstallController::workerEntry,
                                         0x10000100, 512 * 1024, 0, 0, nullptr);
    if (workerThread_ < 0) {
        savedZipMode_ = false;
        workerDone_.store(true);
        setState(InstallStatus::State::Failed, "Could not create ZIP extraction worker");
        return false;
    }
    InstallController* self = this;
    const int started = sceKernelStartThread(workerThread_, sizeof(self), &self);
    if (started < 0) {
        sceKernelDeleteThread(workerThread_);
        workerThread_ = -1;
        savedZipMode_ = false;
        workerDone_.store(true);
        setState(InstallStatus::State::Failed, "Could not start ZIP extraction worker");
        return false;
    }
    diagnostics::log("[Installer] offline ZIP retry started path=" + savedZipPath_ +
                     " target=" + savedZipTarget_);
    return true;
}

bool InstallController::busy() const {
    // The terminal state can be published before the worker exits, and
    // completed-job cleanup still touches shared job and receipt state.
    if (workerThread_ >= 0 && !workerDone_.load(std::memory_order_acquire))
        return true;
    const auto s = static_cast<InstallStatus::State>(state_.load());
    return s == InstallStatus::State::Downloading || s == InstallStatus::State::Installing;
}

void InstallController::setPendingCatalogMeta(const std::string& appId, const std::string& version,
                                                   const std::string& versionDate, int revision,
                                                   const std::string& titleId) {
    // Metadata is also consumed by the worker when writing the install receipt.
    // Never replace it during an earlier installation's cleanup tail.
    if (workerThread_ >= 0 && !workerDone_.load(std::memory_order_acquire))
        return;
    pendingAppId_ = appId;
    pendingCatalogVersion_ = version;
    pendingVersionDate_ = versionDate;
    pendingReleaseRevision_ = revision;
    pendingTitleId_ = titleId;
}

bool InstallController::requestInstall(
    const std::string& url,
    const std::string& fileName,
    const std::string& zipDestination,
    const std::string& zrif,
    const std::string& linkType,
    const std::string& contentId,
    const std::string& displayTitle,
    uint64_t expectedBytes,
    const std::string& pluginSection,
    const std::string& pluginLine
) {
    if (url.empty() || fileName.empty() || busy()) return false;
    // Game Files / Data Files are extraction-only. Do not enqueue a download
    // that could later be mistaken for an installable VPK.
    if (isDataOrGameFilesLinkType(linkType) && zipDestination.empty()) {
        diagnostics::log("[Installer] Data/Game Files ZIP rejected: missing extract destination");
        return false;
    }

    // IMPORTANT: a previous worker may have reported Completed before its
    // cleanup/logging tail returned. The UI's Install All can immediately
    // request another link: do not mutate DownloadManager jobs, activeJobId_
    // or dispatcher state until the old worker has truly exited.
    if (workerThread_ >= 0) {
        if (!workerDone_.load(std::memory_order_acquire)) {
            diagnostics::log("[Installer] start deferred: previous worker finishing cleanup");
            return false;
        }
        sceKernelWaitThreadEnd(workerThread_, nullptr, nullptr);
        sceKernelDeleteThread(workerThread_);
        workerThread_ = -1;
        diagnostics::log("[Installer] previous worker reaped before next request");
    }

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
        setState(InstallStatus::State::Failed, ::psvitaalive::L(::psvitaalive::TextId::InstMsgNoInternet));
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

    // Do not rely only on a .pkg suffix. Sony/NPS Vita CDN URLs often end in an
    // opaque token, while their content_id still unambiguously identifies PCS*.
    const bool vitaPkgByMetadata = looksLikeVitaPkgMetadata(contentId, zrif);
    const bool pkgInstall = vitaPkgByMetadata || BgdlClient::looksLikePkgUrl(url, fileName);
    const bool pspPs1Pkg = pkgInstall && !vitaPkgByMetadata &&
        looksLikePspPs1Pkg(linkType, contentId, fileName);
    // Vita PKGs must never be downloaded by PSVitaAlive itself. The only direct-PKG
    // exception is PSP/PS1 when the user explicitly targets Adrenaline/pspemu.
    const bool pspPs1Adrenaline =
        pspPs1Pkg && settings_.pspTarget == PspTarget::Adrenaline;
    const bool wantBgdl = pkgInstall && !pspPs1Adrenaline;

    // ZIP preflight covers compressed download only; ZipExtractor later checks
    // the *actual uncompressed* size. System BGDL never uses this local budget.
    const bool isZipExtraction = !isPluginLinkType(linkType) && !zipDestination.empty() &&
        (isDataOrGameFilesLinkType(linkType) || FormatDetector::extensionOf(fileName) == "zip");
    const InstallSpaceMode budgetMode = selectInstallSpaceMode(
        wantBgdl, isZipExtraction, isPluginLinkType(linkType));
    const uint64_t requiredSpace = preflightRequiredSpaceBytes(expectedBytes, budgetMode);
    if (wantBgdl) diagnostics::log("[Installer] space preflight delegated to system BGDL");

    if (requiredSpace > 0) {
        uint64_t freeB = 0, totalB = 0;
        if (StorageManager::queryUx0Space(freeB, totalB)) {
            const uint64_t needExact = requiredSpace;
            if (freeB < needExact) {
                const uint64_t missing = needExact - freeB;
                char msg[320];
                sceClibSnprintf(
                    msg, sizeof(msg),
                    ::psvitaalive::L(::psvitaalive::TextId::InstMsgNotEnoughSpace),
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
        if (!wantBgdl)
            diagnostics::log("[Installer] no usable payload size hint; space pre-check skipped");
    }

    installCancelRequested_.store(false, std::memory_order_release);
    activeZrif_ = zrif;
    activeContentId_ = contentId;

    if (vitaPkgByMetadata) {
        diagnostics::log("[Installer] Vita PCS* PKG detected from metadata; forcing system BGDL before client download");
    }
    if (pspPs1Adrenaline) {
        diagnostics::log("[Installer] PSP/PS1 PKG + Adrenaline target — skipping BGDL/LiveArea; direct download for pspemu path");
    } else if (pkgInstall) {
        diagnostics::log("[Installer] PKG routed to system BGDL; client-side PKG download disabled for Vita/LiveArea installs");
    }

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
        return true;
    } else if (settings_.installMethod == InstallMethod::Bgdl) {
        diagnostics::log("[Installer] BGDL selected but file is not PKG - using direct path");
    }

    const auto s = static_cast<InstallStatus::State>(state_.load());
    if (s == InstallStatus::State::Completed || s == InstallStatus::State::Failed
        || s == InstallStatus::State::Cancelled) acknowledgeResult();

    if (!http_.isInitialized() && !init()) return false;

    // Verified parallel ranges may be extracted without an intermediate join
    // for both typed Game/Data ZIP archives and installable VPK ZIP containers.
    // All other formats (PKG/PSP/PS1/plugins/opaque names) retain their path.
    const bool keepSegmented = shouldKeepSegmentedContainer(
        FormatDetector::extensionOf(fileName),
        wantBgdl, isPluginLinkType(linkType),
        isDataOrGameFilesLinkType(linkType),
        isZipExtraction, !zipDestination.empty());
    const std::string jobId = downloads_.enqueue(
        url, fileName, expectedBytes, keepSegmented);
    diagnostics::log(std::string("[Installer] segmented container opt-in=") +
                     (keepSegmented ? "yes" : "no") + " file=" + fileName);
    if (jobId.empty()) {
        setState(InstallStatus::State::Failed, ::psvitaalive::L(::psvitaalive::TextId::InstMsgCouldNotCreateJob));
        diagnostics::log("[Installer] could not create download job");
        return false;
    }

    activeJobId_ = jobId;
    activeZipDestination_ = zipDestination;
    activeFileName_ = fileName;
    activeDisplayTitle_ = niceTitle;
    archiveSourceUnavailable_.store(false);
    zipRecoveryAvailable_.store(false);
    zipRecoveryMayBeCorrupt_.store(false);
    zipRecoveryPath_.clear();
    zipRecoveryInfoPath_.clear();
    zipRecoveryExtractPath_.clear();
    zipRecoveryJobId_.clear();
    activeLinkType_ = linkType;
    activePluginSection_ = pluginSection;
    activePluginLine_ = pluginLine;
    needsReboot_.store(false);
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
    setState(InstallStatus::State::Downloading, ::psvitaalive::L(::psvitaalive::TextId::InstMsgStartingDownload));
    diagnostics::log(std::string("[Installer] request job=") + jobId + " file=" + fileName +
        (isPluginLinkType(linkType) ? " type=Plugin" : ""));

    workerThread_ = sceKernelCreateThread("PSVitaAliveInstall", &InstallController::workerEntry,
        0x10000100, 512 * 1024, 0, 0, nullptr); /* pkg2zip unpack needs large stack */
    if (workerThread_ < 0) {
        setState(InstallStatus::State::Failed, ::psvitaalive::L(::psvitaalive::TextId::InstMsgCouldNotCreateThread));
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
        setState(InstallStatus::State::Failed, ::psvitaalive::L(::psvitaalive::TextId::InstMsgCouldNotStartThread));
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

        self->setMessage(::psvitaalive::L(::psvitaalive::TextId::InstMsgPreparingLicense));
        if (!BgdlClient::instance().available() && !BgdlClient::instance().init()) {
            self->setState(InstallStatus::State::Failed,
                           "BGDL unavailable on this device. Try again or check plugins.");
            self->resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
            diagnostics::log("[Installer] PKG BGDL failed: BGDL unavailable");
        } else {
            self->setMessage(::psvitaalive::L(::psvitaalive::TextId::InstMsgQueuingBgdl));
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
                    ::psvitaalive::L(::psvitaalive::TextId::InstMsgQueuedBgdl),
                    title.c_str());
                self->setState(InstallStatus::State::Completed, msg);
                self->resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
                diagnostics::log(std::string("[Installer] PKG BGDL queued id=") + std::to_string(bg.bgdlId) +
                                 " title=" + title);
            } else {
                const char* failMsg = bg.message.empty()
                    ? ::psvitaalive::L(::psvitaalive::TextId::InstMsgBgdlLicenseFailed)
                    : bg.message.c_str();
                self->setState(InstallStatus::State::Failed, failMsg);
                self->resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
                diagnostics::log(std::string("[Installer] PKG BGDL failed: ") + failMsg);
            }
        }
    }

    // The UI must not acknowledge/advance a finished job until the worker's
    // final metadata and cleanup operations have finished.
    const bool priorWorkerFinishing =
        workerThread_ >= 0 && !workerDone_.load(std::memory_order_acquire);
    if (!priorWorkerFinishing)
        const_cast<InstallController*>(this)->maybeAutoAcknowledgeResult();

    InstallStatus result;
    result.state = static_cast<InstallStatus::State>(state_.load());
    if (priorWorkerFinishing &&
        (result.state == InstallStatus::State::Completed ||
         result.state == InstallStatus::State::Failed ||
         result.state == InstallStatus::State::Cancelled)) {
        // Prevent Install All from advancing early. Keep the current overlay
        // until the background worker publishes workerDone_ with release order.
        result.state = InstallStatus::State::Installing;
    }
    result.current = current_.load();
    result.total = total_.load();
    result.bytesPerSecond = speed_.load();
    result.fileName = fileName_;
    result.stage = stage_;
    result.message = message_;
    result.installPath = installPath_;
    result.titleId = titleId_;
    result.liveAreaOk = liveAreaOk_.load();
    result.archiveSourceUnavailable = archiveSourceUnavailable_.load(std::memory_order_acquire);
    result.needsReboot = needsReboot_.load();
    result.zipRecoveryAvailable = zipRecoveryAvailable_.load();
    result.zipRecoveryMayBeCorrupt = zipRecoveryMayBeCorrupt_.load();
    if (result.zipRecoveryAvailable) {
        result.zipRecoveryPath = zipRecoveryPath_;
        result.zipRecoveryExtractPath = zipRecoveryExtractPath_;
    }
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
    if (needsReboot_.load()) return; // wait for reboot modal
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


int InstallController::workerSavedZipMain() {
    const std::string path = savedZipPath_;
    const std::string target = savedZipTarget_;
    const std::string infoPath = savedZipInfoPath_;
    uint64_t startedUs = 0;
    dispatcher_.setPspTarget(settings_.pspTarget);
    dispatcher_.setPspMediaFormat(settings_.pspMediaFormat);
    const InstallDispatchResult result = dispatcher_.installFile(
        path,
        [&](const InstallDispatchProgress& progress) {
            current_.store(progress.current);
            total_.store(progress.total);
            if (progress.stage == InstallDispatchProgress::Extracting) {
                setStage("Extracting");
                const uint64_t now = sceKernelGetProcessTimeWide();
                if (!startedUs) startedUs = now;
                const uint64_t elapsed = now - startedUs;
                speed_.store(elapsed >= 2000000ULL && progress.current > 0
                    ? (progress.current * 1000000ULL) / elapsed : 0);
            } else {
                speed_.store(0);
                setStage("Installing");
            }
            if (!progress.message.empty()) setMessage(progress.message.c_str());
        },
        [this]() { return installCancelRequested_.load(std::memory_order_acquire); },
        target, std::string(), true);
    if (result == InstallDispatchResult::Ok) {
        const bool staged = dispatcher_.lastZipUsedStagedMoves();
        // All ZIP entries and whole-directory relocations completed successfully.
        // Delete only the selected archive; other recovery ZIPs of the app stay.
        StorageManager st;
        const bool removed = st.removeFile(path);
        if (removed) st.removeFile(infoPath);
        diagnostics::log(std::string("[Installer] offline ZIP result moved_deep_folders=") +
                         (staged ? "yes" : "no") +
                         " archive_deleted=" + (removed ? "yes" : "no") +
                         " target=" + target);
        setInstallPath(target.c_str());
        setStage("Completed");
        setState(InstallStatus::State::Completed,
                 removed ? "Saved ZIP extracted and removed successfully"
                         : "ZIP extracted, but could not delete the saved archive");
    } else {
        const bool cancelled = result == InstallDispatchResult::Cancelled;
        const std::string error = cancelled ? "ZIP extraction cancelled (saved ZIP preserved)"
            : (dispatcher_.lastError().empty()
                ? "ZIP extraction failed" : dispatcher_.lastError());
        diagnostics::log("[Installer] offline ZIP extraction interrupted (archive preserved): " + error);
        zipRecoveryPath_ = path;
        zipRecoveryExtractPath_ = target;
        zipRecoveryInfoPath_ = infoPath;
        zipRecoveryJobId_.clear();
        zipRecoveryMayBeCorrupt_.store(!cancelled && looksLikeDamagedZipError(error));
        // A cancelled offline ZIP remains available from the saved ZIP list.
        zipRecoveryAvailable_.store(!cancelled);
        setStage(cancelled ? "Cancelled" : "ZipRecovery");
        setState(cancelled ? InstallStatus::State::Cancelled
                           : InstallStatus::State::Failed, error.c_str());
    }
    resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
    savedZipMode_ = false;
    workerDone_.store(true, std::memory_order_release);
    return 0;
}

int InstallController::workerMain() {
    if (savedZipMode_) return workerSavedZipMain();
    if (activeBgdlJob_) {
        activeBgdlJob_ = false;
        const std::string title = activeBgdlTitle_;
        const std::string url = activeBgdlUrl_;
        const std::string linkType = activeBgdlLinkType_;
        const std::string zrif = activeZrif_;
        const std::string contentId = activeContentId_;
        setMessage(::psvitaalive::L(::psvitaalive::TextId::InstMsgPreparingLicense));
        if (!BgdlClient::instance().available() && !BgdlClient::instance().init()) {
            setState(InstallStatus::State::Failed,
                     "BGDL unavailable on this device. Try again or check plugins.");
            resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
            workerDone_.store(true, std::memory_order_release);
            return 0;
        }
        setMessage(::psvitaalive::L(::psvitaalive::TextId::InstMsgQueuingBgdl));
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
                ::psvitaalive::L(::psvitaalive::TextId::InstMsgQueuedBgdl),
                title.c_str());
            setState(InstallStatus::State::Completed, msg);
            resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
            diagnostics::log(std::string("[Installer] PKG BGDL queued id=") + std::to_string(bg.bgdlId) +
                             " title=" + title);
        } else {
            const char* failMsg = bg.message.empty()
                ? ::psvitaalive::L(::psvitaalive::TextId::InstMsgBgdlLicenseFailed)
                : bg.message.c_str();
            setState(InstallStatus::State::Failed, failMsg);
            resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
            diagnostics::log(std::string("[Installer] PKG BGDL failed: ") + failMsg);
        }
        workerDone_.store(true, std::memory_order_release);
        return 0;
    }

    const bool downloaded = downloads_.processQueue();
    DownloadJob* job = downloads_.findJob(activeJobId_);
    if (!downloaded || !job || job->state != DownloadState::Completed) {
        const bool cancelled = job && job->state == DownloadState::Cancelled;
        // Only show the service warning when the failing user download
        // actually came from Archive.org and the HTTP client classified
        // its resource as unavailable. Do not label VPK/ZIP/SSL/disk errors
        // from other providers as an Archive outage.
        const bool archiveFailure = !cancelled && job && job->sourceUnavailable &&
            job->url.find("archive.org") != std::string::npos;
        archiveSourceUnavailable_.store(archiveFailure, std::memory_order_release);
        const std::string error = cancelled ? ::psvitaalive::L(::psvitaalive::TextId::InstMsgDownloadCancelled)
            : (job && !job->lastError.empty() ? job->lastError : ::psvitaalive::L(::psvitaalive::TextId::InstMsgDownloadFailed));
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
    current_.store(0);
    total_.store(0);
    speed_.store(0);
    // With direct segmented ZIP/VPK, DownloadManager has already closed and
    // renamed every range and checked the authoritative combined size.
    // Avoid an unconditional 3-second delay on each successful transfer.
    // Non-segmented/serial installs keep the conservative SD2Vita settle.
    setStage("Installing");
    if (job->segmentedZip) {
        diagnostics::log(std::string("[Installer] segmented ZIP/VPK finalized; skipping 3s storage settle path=") +
                         job->finalPath);
    } else {
        setState(InstallStatus::State::Installing, ::psvitaalive::L(::psvitaalive::TextId::InstMsgFinalizingStorage));
        diagnostics::log(std::string("[Installer] post-download storage settle before extract path=") + job->finalPath);
        sceKernelDelayThread(3000 * 1000); // preserve legacy behavior for serial/joined files
    }
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
    setState(InstallStatus::State::Installing, ::psvitaalive::L(::psvitaalive::TextId::InstMsgPreparingInstall));
    diagnostics::log(std::string("[Installer] installing job=") + activeJobId_ + " file=" + job->finalPath);

    // --- Plugin link: copy binary to extract_path + append config.txt line (no VPK/ZIP path) ---
    if (isPluginLinkType(activeLinkType_)) {
        setStage("Plugin");
        setState(InstallStatus::State::Installing, ::psvitaalive::L(::psvitaalive::TextId::InstMsgInstallingPlugin));
        std::string destDir = activeZipDestination_;
        if (destDir.empty()) destDir = "ur0:tai/";
        // Keep extract_path as authored (may be ux0: for rare app-specific plugins).
        if (!destDir.empty() && destDir.back() != '/' && destDir.back() != ':') destDir.push_back('/');

        std::string destName = basenameFromPathOrUrl(activePluginLine_.empty() ? activeFileName_ : activePluginLine_);
        if (destName.find('.') == std::string::npos) {
            destName = basenameFromPathOrUrl(activeFileName_);
        }
        const std::string destPath = destDir + destName;

        StorageManager st;
        if (!st.createDirectories(destDir)) {
            setStage("Error");
            setState(InstallStatus::State::Failed, ::psvitaalive::L(::psvitaalive::TextId::InstMsgCannotCreatePluginDir));
            diagnostics::log(std::string("[Installer] plugin mkdir failed: ") + destDir);
            resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
            workerDone_.store(true);
            return 0;
        }


        // Stage the fully downloaded plugin in the destination directory first.
        // The previous plugin must remain intact if read/write/size checks fail.
        const std::string stagePath = destPath + ".psvitaalive.new";
        const std::string backupPath = destPath + ".psvitaalive.bak";
        const bool replacing = st.exists(destPath);

        auto failPluginInstall = [&](const std::string& message) {
            setStage("Error");
            setState(InstallStatus::State::Failed, message.c_str());
            resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
            diagnostics::log("[Installer] plugin install failed: " + message);
            workerDone_.store(true, std::memory_order_release);
        };

        // A leftover backup could be the user's only intact plugin after a
        // power loss. Never delete or overwrite it automatically.
        if (st.exists(backupPath)) {
            failPluginInstall("Existing plugin backup found. Check the .psvitaalive.bak file in VitaShell.");
            return 0;
        }
        if (st.exists(stagePath) && !st.removeFile(stagePath)) {
            failPluginInstall("Could not remove stale plugin staging file");
            return 0;
        }
        const int64_t expectedBytes = st.fileSize(job->finalPath);
        if (expectedBytes <= 0) {
            failPluginInstall(::psvitaalive::L(::psvitaalive::TextId::InstMsgCannotOpenPlugin));
            return 0;
        }

        const SceUID in = sceIoOpen(job->finalPath.c_str(), SCE_O_RDONLY, 0);
        if (in < 0) {
            failPluginInstall(::psvitaalive::L(::psvitaalive::TextId::InstMsgCannotOpenPlugin));
            return 0;
        }
        const SceUID out = sceIoOpen(stagePath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
        if (out < 0) {
            sceIoClose(in);
            failPluginInstall(::psvitaalive::L(::psvitaalive::TextId::InstMsgCannotWritePlugin));
            return 0;
        }
        bool copyOk = true, readFailed = false;
        char buf[8 * 1024];
        for (;;) {
            const int n = sceIoRead(in, buf, sizeof(buf));
            if (n < 0) { copyOk = false; readFailed = true; break; }
            if (n == 0) break;
            if (sceIoWrite(out, buf, n) != n) { copyOk = false; break; }
        }
        if (sceIoClose(in) < 0) copyOk = false;
        if (sceIoClose(out) < 0) copyOk = false;
        if (!copyOk || st.fileSize(stagePath) != expectedBytes) {
            st.removeFile(stagePath);
            failPluginInstall(::psvitaalive::L(readFailed
                ? ::psvitaalive::TextId::InstMsgPluginCopyRead
                : ::psvitaalive::TextId::InstMsgPluginCopyWrite));
            return 0;
        }

        // Only now move the existing file aside. If activation or tai config
        // editing fails, restore it; avoid leaving half-copied kernel plugins.
        if (replacing && !st.rename(destPath, backupPath)) {
            st.removeFile(stagePath);
            failPluginInstall("Could not back up the existing plugin");
            return 0;
        }
        if (!st.rename(stagePath, destPath)) {
            if (replacing && !st.rename(backupPath, destPath))
                diagnostics::log("[Installer] URGENT: restore plugin backup manually from " + backupPath);
            st.removeFile(stagePath);
            failPluginInstall("Could not activate the new plugin file");
            return 0;
        }
        diagnostics::log(std::string("[Installer] plugin file installed: ") + destPath);
        setInstallPath(destPath.c_str());

        // Append line exactly as authored. Existing lines are already handled
        // idempotently by TaiConfigEditor (no duplicated config entries).
        std::string cfgErr;
        if (!TaiConfigEditor::appendLineToSection(activePluginSection_, activePluginLine_, &cfgErr)) {
            const std::string msg = cfgErr.empty() ? "Failed to update tai config.txt" : cfgErr;
            // Do not keep a new plugin that failed to activate through taiHEN.
            if (replacing) {
                if (!st.removeFile(destPath) || !st.rename(backupPath, destPath))
                    diagnostics::log("[Installer] URGENT: restore plugin backup manually from " + backupPath);
            } else if (!st.removeFile(destPath)) {
                diagnostics::log("[Installer] failed to remove unconfigured plugin: " + destPath);
            }
            failPluginInstall(msg);
            return 0;
        }
        if (replacing && !st.removeFile(backupPath)) {
            // Plugin is installed, but keep the backup rather than failing a
            // successful installation or deleting any unrelated file.
            diagnostics::log("[Installer] previous plugin backup cleanup pending: " + backupPath);
        }

        needsReboot_.store(true);
        liveAreaOk_.store(false);
        setStage("Done");
        setState(InstallStatus::State::Completed,
            ::psvitaalive::L(::psvitaalive::TextId::InstMsgPluginInstalled));
        resultShownAtMs_.store(0); // no auto-dismiss — UI shows reboot modal
        diagnostics::log("[Installer] plugin install completed — reboot required");
        downloads_.cleanupCompletedJob(activeJobId_);
        activeJobId_.clear();
        workerDone_.store(true);
        return 0;
    }


    FormatDetector payloadDetector;
    const DetectResult downloadedPayload = payloadDetector.detectFile(job->finalPath);
    // VPK-looking ZIPs are still recoverable when the user requested extraction.
    // Keep this in sync with InstallDispatcher ZIP routing.
    const bool zipRecoveryCandidate = !activeZipDestination_.empty() &&
        (downloadedPayload.format == FileFormat::Zip ||
         shouldExtractZipArchive(
             FormatDetector::extensionOf(job->finalPath),
             true,
             isDataOrGameFilesLinkType(activeLinkType_),
             downloadedPayload.format == FileFormat::Zip ||
                 downloadedPayload.format == FileFormat::Vpk));
    diagnostics::log(std::string("[Installer] ZIP recovery candidate=") +
        (zipRecoveryCandidate ? "yes" : "no") +
        " format=" + toString(downloadedPayload.format) +
        " extract_path=" + (activeZipDestination_.empty() ? "-" : activeZipDestination_));

    std::string directRifPath;
    {
        const DetectResult& dr = downloadedPayload;
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
    uint64_t extractStartedUs = 0;
    uint64_t extractLastBytes = 0;
    for (int attempt = 1; attempt <= kMaxInstallAttempts; ++attempt) {
        if (installCancelRequested_.load(std::memory_order_acquire)) {
            result = InstallDispatchResult::Cancelled;
            lastInstallError = "Installation cancelled";
            break;
        }
        attemptsUsed = attempt;
        extractStartedUs = 0;
        extractLastBytes = 0;
        speed_.store(0);
        if (attempt > 1) {
            const bool integrityHint =
                lastInstallError.find("incomplete") != std::string::npos ||
                lastInstallError.find("EOCD") != std::string::npos ||
                lastInstallError.find("ZIP64") != std::string::npos;
            char retryMsg[240];
            if (integrityHint) {
                sceClibSnprintf(
                    retryMsg, sizeof(retryMsg),
                    ::psvitaalive::L(::psvitaalive::TextId::InstMsgRetryIncomplete),
                    attempt, kMaxIntegrityAttempts);
            } else {
                sceClibSnprintf(
                    retryMsg, sizeof(retryMsg),
                    ::psvitaalive::L(::psvitaalive::TextId::InstMsgRetryGeneric),
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
            const int waitMs = integrityHint ? 4000 : 2000;
            for (int waited = 0; waited < waitMs &&
                 !installCancelRequested_.load(std::memory_order_acquire); waited += 100)
                sceKernelDelayThread(100 * 1000);
            if (installCancelRequested_.load(std::memory_order_acquire)) {
                result = InstallDispatchResult::Cancelled;
                lastInstallError = "Installation cancelled";
                break;
            }
        }

        dispatcher_.setPspTarget(settings_.pspTarget);
        dispatcher_.setPspMediaFormat(settings_.pspMediaFormat);
        result = dispatcher_.installFile(
            job->finalPath,
            [&](const InstallDispatchProgress& progress) {
                current_.store(progress.current);
                total_.store(progress.total);
                // The download rate cannot estimate ZIP extraction, and Vita
                // Promoter does not expose an honest progress/remaining time.
                // Derive a rolling, byte-based ETA only for extraction stages.
                if (progress.stage == InstallDispatchProgress::Extracting) {
                    setStage("Extracting");
                    const uint64_t now = sceKernelGetProcessTimeWide();
                    if (extractStartedUs == 0 || progress.current < extractLastBytes) {
                        extractStartedUs = now;
                        extractLastBytes = progress.current;
                        speed_.store(0);
                    }
                    const uint64_t elapsed = now - extractStartedUs;
                    if (elapsed >= 2000000ULL && progress.current > 0 &&
                        progress.total > progress.current) {
                        // Average since start avoids very noisy instantaneous
                        // rates on ZIPs containing hundreds of tiny files.
                        speed_.store((progress.current * 1000000ULL) / elapsed);
                    } else if (progress.total <= progress.current) {
                        speed_.store(0);
                    }
                    extractLastBytes = progress.current;
                } else {
                    speed_.store(0);
                    switch (progress.stage) {
                        case InstallDispatchProgress::Promoting: setStage("Promoting"); break;
                        case InstallDispatchProgress::Cleaning: setStage("Cleaning"); break;
                        case InstallDispatchProgress::Completed: setStage("Completed"); break;
                        case InstallDispatchProgress::Error: setStage("Error"); break;
                        case InstallDispatchProgress::Detecting: setStage("Preparing"); break;
                        default: setStage("Installing"); break;
                    }
                }
                if (!progress.message.empty()) setMessage(progress.message.c_str());
            },
            [this]() { return installCancelRequested_.load(std::memory_order_acquire); },
            activeZipDestination_,
            directRifPath,
            isDataOrGameFilesLinkType(activeLinkType_),
            job->segmentedZip
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

        // Deterministic archive corruption / unsupported formats must not spin retries.
        const bool corruptArchive = looksLikeDamagedZipError(lastInstallError) ||
            lastInstallError.find("Unsupported compression") != std::string::npos ||
            lastInstallError.find("unsupported compression") != std::string::npos ||
            lastInstallError.find("encryption") != std::string::npos;
        const bool hardPermanent =
            isPermanentInstallResult(result) ||
            corruptArchive ||
            lastInstallError.find("cancelled") != std::string::npos ||
            lastInstallError.find("not found") != std::string::npos ||
            lastInstallError.find("empty installation") != std::string::npos ||
            lastInstallError.find("not enough free space") != std::string::npos ||
            lastInstallError.find("cannot prepare ZIP directory:") != std::string::npos ||
            lastInstallError.find("ZIP destination cannot create deep files") != std::string::npos ||
            lastInstallError.find("ZIP preflight probe cleanup failed:") != std::string::npos ||
            lastInstallError.find("cannot test deep ZIP destination safely:") != std::string::npos ||
            lastInstallError.find("cannot create file:") != std::string::npos ||
            lastInstallError.find("mkdir failed:") != std::string::npos ||
            lastInstallError.find("sceIoWrite failed") != std::string::npos;
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
        // Keep the UI in Installing while a recoverable ZIP is moved out of
        // the transient job. Publish Failed only after recovery metadata is ready.
        setStage(cancelled ? "Cancelled" : (zipRecoveryCandidate ? "RecoveringZip" : "Error"));
        if (cancelled || !zipRecoveryCandidate)
            setState(cancelled ? InstallStatus::State::Cancelled : InstallStatus::State::Failed, error.c_str());
        else
            setMessage(error.c_str());
        liveAreaOk_.store(false);
        setInstallPath(dispatcher_.lastInstallPath().c_str());
        setTitleId(dispatcher_.lastTitleId().c_str());
        diagnostics::log(std::string("[Installer] ") + (cancelled ? "installation cancelled: " : "installation failed: ") + error +
  " attempts=" + std::to_string(attemptsUsed));

        // Manual recovery is intentionally ZIP-only. A completed data ZIP that
        // failed in the extraction phase is valuable user data; direct VPK/PKG,
        // plugin jobs, cancellations and incomplete network downloads keep their
        // existing cleanup behavior.
        if (!cancelled && zipRecoveryCandidate) {
            // A complete ZIP may be offered for manual recovery, but is not
            // saved in /manual/ until the user explicitly presses Keep.
            const bool recoveryReady = !job->segmentedZip ||
                materializeSegmentedZip(job->finalPath, job->expectedSize);
            if (!recoveryReady) {
                // No independent, usable ZIP exists. Drop incomplete assembly
                // and every range; never strand a multi-GB failed recovery.
                diagnostics::log("[Installer] failed to assemble recovery ZIP; removing job");
                downloads_.cleanupCompletedJob(activeJobId_);
                activeJobId_.clear();
                setStage("Error");
                setState(InstallStatus::State::Failed,
                    "ZIP recovery assembly failed; incomplete temporary files removed");
            } else {
                job->segmentedZip = false;
                zipRecoveryPath_ = job->finalPath;
                zipRecoveryExtractPath_ = activeZipDestination_;
                zipRecoveryInfoPath_.clear();
                zipRecoveryJobId_ = activeJobId_;
                zipRecoveryMayBeCorrupt_.store(looksLikeDamagedZipError(error));
                zipRecoveryAvailable_.store(true);
                setStage("ZipRecovery");
                setInstallPath(activeZipDestination_.c_str());
                setState(InstallStatus::State::Failed, error.c_str());
                diagnostics::log("[Installer] ZIP recovery pending user decision path=" +
                                 zipRecoveryPath_);
            }
        } else {
            downloads_.cleanupCompletedJob(activeJobId_);
            activeJobId_.clear();
        }
        resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
    } else {
        setInstallPath(dispatcher_.lastInstallPath().c_str());
        setTitleId(dispatcher_.lastTitleId().c_str());
        liveAreaOk_.store(dispatcher_.lastLiveAreaOk());

        char okMsg[320];
        if (!dispatcher_.lastInstallPath().empty()) {
            sceClibSnprintf(okMsg, sizeof(okMsg), ::psvitaalive::L(::psvitaalive::TextId::InstMsgInstalledAt), dispatcher_.lastInstallPath().c_str());
        } else {
            sceClibSnprintf(okMsg, sizeof(okMsg), "%s", ::psvitaalive::L(::psvitaalive::TextId::InstMsgInstallComplete));
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
                sceClibSnprintf(okMsg, sizeof(okMsg), ::psvitaalive::L(::psvitaalive::TextId::InstMsgZipExtractedTo), p.c_str());
            }
        }
        if (!dispatcher_.lastLiveAreaOk() && !dispatcher_.lastTitleId().empty() &&
            dispatcher_.lastInstallPath().find("ux0:app/") != std::string::npos) {
            sceClibSnprintf(
                okMsg, sizeof(okMsg),
                ::psvitaalive::L(::psvitaalive::TextId::InstMsgInstalledRefreshHint),
                dispatcher_.lastTitleId().c_str()
            );
        }
        // The install is technically complete, but do not publish Completed
        // to Install All until the receipt and job cleanup have finished.
        // Local receipt so future queries do not depend on APP_VER alone.
        {
            const std::string tid = dispatcher_.lastTitleId();
            const std::string path = dispatcher_.lastInstallPath();
            if (!tid.empty() && path.find("ux0:app/") != std::string::npos) {
                ::psvitaalive::update::InstallReceipt rec;
                rec.schemaVersion = 1;
                rec.titleId = tid;
                rec.appId = pendingAppId_;
                rec.catalogVersion = pendingCatalogVersion_;
                rec.versionDate = pendingVersionDate_;
                rec.releaseRevision = pendingReleaseRevision_;
                rec.verifyPath = "eboot.bin";
                rec.verifyAlgorithm = "sha256";
                rec.verifyDigest = ""; // fingerprint worker fills later; version receipt still useful
                if (::psvitaalive::update::InstalledReleaseTracker::writeReceipt(rec)) {
                    diagnostics::log(std::string("[UpdateDetect] receipt written titleId=") + tid +
                                     " catalogVersion=" + pendingCatalogVersion_);
                } else {
                    diagnostics::log(std::string("[UpdateDetect] receipt write failed titleId=") + tid);
                }
            }
        }
        if (dispatcher_.lastZipUsedStagedMoves()) {
            sceClibSnprintf(okMsg, sizeof(okMsg),
                            "ZIP folders moved. Deep files may be unreadable; ZIP kept.");
        }
        diagnostics::log(std::string("[Installer] installation completed path=") +
            dispatcher_.lastInstallPath() + " titleId=" + dispatcher_.lastTitleId() +
            " liveArea=" + (dispatcher_.lastLiveAreaOk() ? "yes" : "no") +
            " verify=" + verifyMsg +
            " attempts=" + std::to_string(attemptsUsed));
        // Successful ZIP extraction (including completed staged directory
        // moves) can now free the job's original archive immediately. Recovery
        // archives are kept only when the extraction or move fails.
        downloads_.cleanupCompletedJob(activeJobId_);
        activeJobId_.clear();
        resultShownAtMs_.store(sceKernelGetSystemTimeWide() / 1000ULL);
        setStage("Completed");
        setState(InstallStatus::State::Completed, okMsg);
    }

    // Release/acquire publication gate. UI cannot reuse DownloadManager until
    // all operations above have completed, and requestInstall will join this
    // exact kernel thread before creating the next one.
    workerDone_.store(true, std::memory_order_release);
    return 0;
}

} // namespace psvitaalive
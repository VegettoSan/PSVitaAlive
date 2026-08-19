#include "update/update_checker.hpp"

#include "archive/zip_extractor.hpp"
#include "installer/homebrew_installer.hpp"
#include "diagnostic_logger.hpp"
#include "network/http_client.hpp"
#include "storage/storage_manager.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/json.h>
#include <psp2/kernel/clib.h>
#include <psp2/sysmodule.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace psvitaalive {
namespace {

constexpr const char* RELEASES_LATEST_URL =
    "https://api.github.com/repos/VegettoSan/PSVitaAlive/releases/latest";
// Fallback when /latest is empty (e.g. only prereleases published).
constexpr const char* RELEASES_LIST_URL =
    "https://api.github.com/repos/VegettoSan/PSVitaAlive/releases?per_page=10";
constexpr const char* UPDATE_ASSET_NAME = "PSVitaAlive.vpk";
constexpr const char* VERSION_MARKER = "ux0:app/PSVAS1178/psvitaalive_version.txt";

class VitaJsonAllocator : public sce::Json::MemAllocator {
public:
    void* allocateMemory(SceSize size, void* userData) override {
        (void)userData;
        return std::malloc(size);
    }

    void freeMemory(void* ptr, void* userData) override {
        (void)userData;
        std::free(ptr);
    }
};

std::string getString(const sce::Json::Value& object, const char* key) {
    const sce::Json::Value& value = object[key];
    if (!value) return {};
    return value.getString().c_str();
}

uint64_t getUnsigned(const sce::Json::Value& object, const char* key) {
    const sce::Json::Value& value = object[key];
    if (!value) return 0;
    return value.getUInteger();
}

bool parseVersionPart(const std::string& text, size_t& pos, uint64_t& out) {
    if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) return false;
    uint64_t value = 0;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        value = value * 10ULL + static_cast<uint64_t>(text[pos] - '0');
        ++pos;
    }
    out = value;
    return true;
}

// Missing parts count as 0 so "01.00" vs "1.0.1" compares correctly.
int compareVersions(const std::string& left, const std::string& right) {
    size_t lp = 0;
    size_t rp = 0;
    for (int part = 0; part < 3; ++part) {
        uint64_t lv = 0;
        uint64_t rv = 0;
        if (lp < left.size() && std::isdigit(static_cast<unsigned char>(left[lp]))) {
            parseVersionPart(left, lp, lv);
        }
        if (rp < right.size() && std::isdigit(static_cast<unsigned char>(right[rp]))) {
            parseVersionPart(right, rp, rv);
        }
        if (lv < rv) return -1;
        if (lv > rv) return 1;
        if (lp < left.size() && left[lp] == '.') ++lp;
        if (rp < right.size() && right[rp] == '.') ++rp;
    }
    return 0;
}

// Extract a dotted numeric version starting at index i (digits and up to 2 dots).
std::string extractVersionAt(const std::string& text, size_t i) {
    if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) return {};
    size_t end = i;
    int dots = 0;
    while (end < text.size()) {
        if (std::isdigit(static_cast<unsigned char>(text[end]))) {
            ++end;
            continue;
        }
        if (text[end] == '.' && dots < 2 && end + 1 < text.size() &&
            std::isdigit(static_cast<unsigned char>(text[end + 1]))) {
            ++dots;
            ++end;
            continue;
        }
        break;
    }
    if (end <= i) return {};
    return text.substr(i, end - i);
}

bool isVersionContextChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
}

/**
 * Collect version-like tokens from free text.
 * Handles: 01.00, 1.0.1, v1.2.3, BETA-0.1, BETA_0.1, version 01.01, etc.
 */
void collectVersionsFromText(const std::string& text, std::vector<std::string>& out) {
    if (text.empty()) return;

    for (size_t i = 0; i < text.size(); ++i) {
        // Optional leading v/V immediately before a digit
        size_t start = i;
        if ((text[i] == 'v' || text[i] == 'V') && i + 1 < text.size() &&
            std::isdigit(static_cast<unsigned char>(text[i + 1]))) {
            start = i + 1;
        } else if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
            continue;
        }

        // Avoid matching pure years or long numbers without dots when better candidates exist:
        // still accept them; ranking later prefers dotted forms.
        const std::string ver = extractVersionAt(text, start);
        if (ver.empty()) continue;

        // Skip isolated single digits with no dot (too noisy) unless whole token is that.
        if (ver.find('.') == std::string::npos && ver.size() <= 1) {
            i = start;
            continue;
        }

        out.push_back(ver);
        // Advance past this match to reduce duplicates
        i = start + ver.size() - 1;
    }
}

// Prefer richer / higher versions for the *same* release metadata blob.
std::string pickBestVersion(const std::vector<std::string>& candidates) {
    if (candidates.empty()) return {};
    std::string best = candidates[0];
    for (size_t i = 1; i < candidates.size(); ++i) {
        const std::string& c = candidates[i];
        const int cmp = compareVersions(best, c);
        if (cmp < 0) {
            best = c;
            continue;
        }
        if (cmp == 0) {
            // Prefer more segments / longer string for display stability (01.00.0 vs 01.00)
            if (c.size() > best.size()) best = c;
        }
    }
    return best;
}

std::string resolveRemoteVersion(const std::string& tag, const std::string& name, const std::string& body) {
    std::vector<std::string> candidates;

    // 1) Tag: "v01.01", "01.01", "BETA-0.1", "BETA_0.1"
    collectVersionsFromText(tag, candidates);

    // 2) Release name: "PsVita Alive Store BETA 01.00"
    collectVersionsFromText(name, candidates);

    // 3) Body (limited): "BETA 01.01", "version 1.2.0"
    if (!body.empty()) {
        const std::string slice = body.size() > 4096 ? body.substr(0, 4096) : body;
        collectVersionsFromText(slice, candidates);
    }

    // De-noise: drop empty
    std::vector<std::string> cleaned;
    cleaned.reserve(candidates.size());
    for (const auto& c : candidates) {
        if (!c.empty()) cleaned.push_back(c);
    }
    return pickBestVersion(cleaned);
}

bool endsWithIgnoreCase(const std::string& value, const char* suffix) {
    const size_t n = std::strlen(suffix);
    if (value.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(value[value.size() - n + i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (a != b) return false;
    }
    return true;
}

bool pickVpkAsset(const sce::Json::Value& assetsValue, UpdateChecker::Result& result) {
    if (!assetsValue) return false;
    const sce::Json::Array& assets = assetsValue.getArray();
    std::string fallbackName;
    std::string fallbackUrl;
    uint64_t fallbackSize = 0;
    std::string fallbackDigest;
    int vpkCount = 0;

    for (SceSize i = 0; i < assets.size(); ++i) {
        const sce::Json::Value& asset = assetsValue[i];
        const std::string name = getString(asset, "name");
        if (name == UPDATE_ASSET_NAME) {
            result.assetName = name;
            result.downloadUrl = getString(asset, "browser_download_url");
            result.digest = getString(asset, "digest");
            result.assetSize = getUnsigned(asset, "size");
            return !result.downloadUrl.empty();
        }
        if (endsWithIgnoreCase(name, ".vpk")) {
            ++vpkCount;
            fallbackName = name;
            fallbackUrl = getString(asset, "browser_download_url");
            fallbackSize = getUnsigned(asset, "size");
            fallbackDigest = getString(asset, "digest");
        }
    }
    if (vpkCount == 1 && !fallbackUrl.empty()) {
        result.assetName = fallbackName;
        result.downloadUrl = fallbackUrl;
        result.assetSize = fallbackSize;
        result.digest = fallbackDigest;
        return true;
    }
    return false;
}

void emitProgress(UpdateChecker::ApplyProgressFn& onProgress,
                  UpdateChecker::ApplyStage stage,
                  uint64_t current,
                  uint64_t total,
                  const char* message,
                  uint64_t bytesPerSecond = 0) {
    if (!onProgress) return;
    UpdateChecker::ApplyProgress p;
    p.stage = stage;
    p.current = current;
    p.total = total;
    p.bytesPerSecond = bytesPerSecond;
    p.message = message ? message : "";
    onProgress(p);
}

bool writeVersionMarker(const std::string& version) {
    SceUID fd = sceIoOpen(VERSION_MARKER, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return false;
    const int n = sceIoWrite(fd, version.data(), version.size());
    sceIoClose(fd);
    return n >= 0;
}


bool jsonIsTrue(const sce::Json::Value& object, const char* key) {
    const sce::Json::Value& value = object[key];
    if (!value) return false;
    return value.getBoolean();
}

// Fill Result fields from a single GitHub release object (not an array).
void fillFromReleaseObject(const sce::Json::Value& release, UpdateChecker::Result& result) {
    result.releaseTag = getString(release, "tag_name");
    result.releaseName = getString(release, "name");
    const std::string releaseBody = getString(release, "body");
    result.remoteVersion = resolveRemoteVersion(result.releaseTag, result.releaseName, releaseBody);
    result.downloadUrl.clear();
    result.assetName.clear();
    result.digest.clear();
    result.assetSize = 0;
    pickVpkAsset(release["assets"], result);
}

bool selectReleaseFromRoot(const sce::Json::Value& root, UpdateChecker::Result& result, bool rootIsArray) {
    if (!rootIsArray) {
        if (jsonIsTrue(root, "draft")) return false;
        fillFromReleaseObject(root, result);
        return !result.remoteVersion.empty() && !result.downloadUrl.empty();
    }

    // Array from GET /releases — newest first. Prefer a non-draft entry that has a VPK.
    // Prereleases are intentionally allowed (BETA tags).
    if (!root) return false;
    const sce::Json::Array& releases = root.getArray();
    for (SceSize i = 0; i < releases.size(); ++i) {
        const sce::Json::Value& rel = root[i];
        if (!rel) continue;
        if (jsonIsTrue(rel, "draft")) continue;
        fillFromReleaseObject(rel, result);
        if (!result.remoteVersion.empty() && !result.downloadUrl.empty()) {
            return true;
        }
    }
    return false;
}

std::string extractLocalVersion(const std::string& currentVersion) {
    std::vector<std::string> c;
    collectVersionsFromText(currentVersion, c);
    if (!c.empty()) return pickBestVersion(c);
    // Fallback: entire string if it already looks numeric
    return currentVersion;
}

} // namespace

UpdateChecker::Result UpdateChecker::checkLatest(const std::string& currentVersion) {
    Result result;
    result.localVersion = extractLocalVersion(currentVersion);
    if (result.localVersion.empty()) {
        result.error = "Invalid local application version";
        diagnostics::log("[UpdateChecker] invalid local version: " + currentVersion);
        return result;
    }

    HttpClient http;
    if (http.init() != HttpResult::Ok) {
        result.error = "Unable to initialize HTTP client";
        diagnostics::log("[UpdateChecker] HTTP initialization failed");
        return result;
    }

    std::string body;
    bool usedReleaseList = false;

    auto logFetch = [](const char* label, int status, size_t bytes, HttpResult hr) {
        char line[192];
        sceClibSnprintf(
            line, sizeof(line),
            "[UpdateChecker] %s status=%d bytes=%u result=%s",
            label,
            status,
            (unsigned)bytes,
            toString(hr)
        );
        diagnostics::log(line);
    };

    HttpResult fetchResult = http.fetchToString(RELEASES_LATEST_URL, body, 512 * 1024);
    logFetch("GET releases/latest", http.lastStatusCode(), body.size(), fetchResult);

    const int statusLatest = http.lastStatusCode();
    // Soft-fail rate limits / forbidden: startup continues without treating this as a hard fault path.
    if (statusLatest == 403 || statusLatest == 429) {
        result.error = "GitHub temporarily unavailable (HTTP 403/429) — update check skipped";
        diagnostics::log("[UpdateChecker] " + result.error);
        http.shutdown();
        return result;
    }

    // /releases/latest ignores prereleases. If missing (404) or request failed, try the full list.
    if (fetchResult != HttpResult::Ok || statusLatest == 404 || body.empty()) {
        diagnostics::log("[UpdateChecker] releases/latest unavailable — falling back to releases list");
        body.clear();
        fetchResult = http.fetchToString(RELEASES_LIST_URL, body, 512 * 1024);
        usedReleaseList = true;
        logFetch("GET releases?per_page=10", http.lastStatusCode(), body.size(), fetchResult);

        const int statusList = http.lastStatusCode();
        if (statusList == 403 || statusList == 429) {
            result.error = "GitHub temporarily unavailable (HTTP 403/429) — update check skipped";
            diagnostics::log("[UpdateChecker] " + result.error);
            http.shutdown();
            return result;
        }
        if (fetchResult != HttpResult::Ok) {
            result.error = http.lastError().empty() ? toString(fetchResult) : http.lastError();
            diagnostics::log("[UpdateChecker] GitHub list request failed: " + result.error);
            http.shutdown();
            return result;
        }
    }

    // Finish HTTP before touching sce::Json. Real hardware is unstable if
    // libcurl handles and sysmodule JSON teardown interleave on a worker thread.
    http.shutdown();
    diagnostics::log("[UpdateChecker] HTTP shutdown complete (before JSON parse)");

    // Load JSON module once per process. Never unload: UnloadModule(JSON) from a
    // secondary thread (and often even on main after recent network work) has
    // crashed real PS Vita units while Vita3K still succeeds.
    static bool sJsonModuleLoaded = false;
    if (!sJsonModuleLoaded) {
        const int moduleResult = sceSysmoduleLoadModule(SCE_SYSMODULE_JSON);
        if (moduleResult < 0) {
            result.error = "Unable to load Vita JSON module";
            diagnostics::log("[UpdateChecker] JSON module load failed");
            return result;
        }
        sJsonModuleLoaded = true;
        diagnostics::log("[UpdateChecker] JSON module loaded (kept for process lifetime)");
    } else {
        diagnostics::log("[UpdateChecker] JSON module already loaded");
    }

    VitaJsonAllocator allocator;
    sce::Json::InitParameter params;
    params.allocator = &allocator;
    params.userData = nullptr;
    params.bufSize = 64 * 1024;

    sce::Json::Initializer initializer;
    if (initializer.initialize(&params) < 0) {
        result.error = "Unable to initialize JSON parser";
        diagnostics::log("[UpdateChecker] JSON initializer failed");
        return result;
    }

    bool parseFailed = false;
    {
        // Keep the JSON root alive only while the JSON module/initializer are active.
        // It must be destroyed before terminate() / module unload on real hardware.
        sce::Json::Value root;
        const int parseRc = sce::Json::Parser::parse(root, body.c_str(), static_cast<SceSize>(body.size()));
        if (parseRc < 0) {
            result.error = "Invalid GitHub release JSON";
            char perr[128];
            sceClibSnprintf(
                perr, sizeof(perr),
                "[UpdateChecker] GitHub JSON parse failed: 0x%08X bytes=%u list=%d",
                parseRc, (unsigned)body.size(), usedReleaseList ? 1 : 0
            );
            diagnostics::log(perr);
            parseFailed = true;
        } else {
            char okline[96];
            sceClibSnprintf(
                okline, sizeof(okline),
                "[UpdateChecker] JSON parsed OK bytes=%u list=%d",
                (unsigned)body.size(), usedReleaseList ? 1 : 0
            );
            diagnostics::log(okline);

            const bool ok = selectReleaseFromRoot(root, result, usedReleaseList);
            diagnostics::log(
                std::string("[UpdateChecker] release metadata extracted tag=") + result.releaseTag +
                " remote=" + result.remoteVersion +
                " asset=" + result.assetName +
                " selected=" + (ok ? "yes" : "no")
            );

            if (result.remoteVersion.empty()) {
                result.error = "GitHub release does not expose a usable version (tag/name/body)";
            } else if (result.downloadUrl.empty()) {
                result.error = "GitHub release does not contain a .vpk asset (expected PSVitaAlive.vpk)";
            } else if (compareVersions(result.localVersion, result.remoteVersion) < 0) {
                result.state = State::UpdateAvailable;
                diagnostics::log("[UpdateChecker] update available: local=" + result.localVersion +
                                 " remote=" + result.remoteVersion +
                                 " tag=" + result.releaseTag +
                                 " asset=" + result.assetName);
            } else {
                result.state = State::UpToDate;
                diagnostics::log("[UpdateChecker] client is up to date: local=" + result.localVersion +
                                 " remote=" + result.remoteVersion +
                                 " tag=" + result.releaseTag);
            }

            diagnostics::log("[UpdateChecker] version comparison complete");
        }
    }

    // `root` destroyed while initializer is still valid.
    diagnostics::log("[UpdateChecker] JSON root released");

    if (parseFailed) {
        initializer.terminate();
        diagnostics::log("[UpdateChecker] JSON initializer terminated (module kept loaded)");
        return result;
    }

    if (result.state == State::Failed) {
        diagnostics::log("[UpdateChecker] check failed: " + result.error);
    }

    initializer.terminate();
    diagnostics::log("[UpdateChecker] JSON initializer terminated (module kept loaded)");
    diagnostics::log("[UpdateChecker] checkLatest finished");
    return result;
}

bool UpdateChecker::applyUpdate(
    const Result& info,
    ApplyProgressFn onProgress,
    ApplyCancelFn shouldCancel
) {
    if (info.downloadUrl.empty()) {
        diagnostics::log("[UpdateChecker] apply failed: empty download URL");
        emitProgress(onProgress, ApplyStage::Error, 0, 0, "No download URL");
        return false;
    }

    emitProgress(onProgress, ApplyStage::Preparing, 0, 0, "Preparing update");

    StorageManager st;
    if (!st.createDirectories(kUpdateDir)) {
        diagnostics::log("[UpdateChecker] cannot create update dir");
        emitProgress(onProgress, ApplyStage::Error, 0, 0, "Cannot create update folder");
        return false;
    }
    st.removeFile(kVpkPath);

    if (shouldCancel && shouldCancel()) {
        emitProgress(onProgress, ApplyStage::Error, 0, 0, "Cancelled");
        return false;
    }

    HttpClient http;
    if (http.init() != HttpResult::Ok) {
        emitProgress(onProgress, ApplyStage::Error, 0, 0, "HTTP init failed");
        return false;
    }

    emitProgress(onProgress, ApplyStage::Downloading, 0, info.assetSize, "Downloading update VPK");
    diagnostics::log("[UpdateChecker] downloading " + info.downloadUrl);

    const HttpResult dl = http.downloadToFile(
        info.downloadUrl,
        kVpkPath,
        0,
        [&](const HttpProgress& hp) {
            const uint64_t tot = hp.total > 0 ? hp.total : info.assetSize;
            emitProgress(onProgress, ApplyStage::Downloading, hp.downloaded, tot, "Downloading update", hp.bytesPerSecond);
        },
        shouldCancel
    );

    if (dl != HttpResult::Ok) {
        const std::string err = http.lastError().empty() ? toString(dl) : http.lastError();
        diagnostics::log("[UpdateChecker] download failed: " + err);
        st.removeFile(kVpkPath);
        http.shutdown();
        emitProgress(onProgress, ApplyStage::Error, 0, 0, err.c_str());
        return false;
    }
    http.shutdown();

    // Real hardware cannot reliably unzip into ux0:app/<TITLE> while the app is
    // running ("cannot create destination"). Install the downloaded VPK through
    // the same HomebrewInstaller / Promoter path used for normal VPK installs
    // (VitaDB-style promote of PSVAS1178 over itself).
    emitProgress(onProgress, ApplyStage::Extracting, 0, 0, "Installing update (do not power off)");
    diagnostics::log("[UpdateChecker] installing update VPK via HomebrewInstaller/Promoter");

    HomebrewInstaller installer;
    const InstallResult ir = installer.installVpk(
        kVpkPath,
        [&](const InstallProgress& ip) {
            ApplyStage stage = ApplyStage::Extracting;
            if (ip.stage == InstallProgress::Promoting || ip.stage == InstallProgress::Cleaning) {
                stage = ApplyStage::Finalizing;
            } else if (ip.stage == InstallProgress::Done) {
                stage = ApplyStage::Done;
            } else if (ip.stage == InstallProgress::Error) {
                stage = ApplyStage::Error;
            }
            const uint64_t cur = ip.entriesTotal > 0 ? ip.entriesDone : ip.bytesWritten;
            const uint64_t tot = ip.entriesTotal > 0 ? ip.entriesTotal : ip.bytesTotal;
            emitProgress(
                onProgress,
                stage,
                cur,
                tot,
                ip.message.empty() ? "Installing update (do not power off)" : ip.message
            );
        },
        shouldCancel,
        true
    );

    st.removeFile(kVpkPath);

    if (ir != InstallResult::Ok) {
        const std::string err = installer.lastError().empty() ? toString(ir) : installer.lastError();
        diagnostics::log("[UpdateChecker] self-update install failed: " + err);
        emitProgress(onProgress, ApplyStage::Error, 0, 0, err.c_str());
        return false;
    }

    emitProgress(onProgress, ApplyStage::Finalizing, 0, 0, "Finalizing");
    const std::string ver = info.remoteVersion.empty() ? info.releaseTag : info.remoteVersion;
    if (!writeVersionMarker(ver)) {
        diagnostics::log("[UpdateChecker] version marker write failed (non-fatal)");
    }

    diagnostics::log(
        "[UpdateChecker] apply OK remote=" + ver +
        " titleId=" + installer.lastTitleId() +
        " liveArea=" + (installer.lastLiveAreaOk() ? "yes" : "no")
    );
    emitProgress(onProgress, ApplyStage::Done, 1, 1, "Update installed — press START to restart");
    return true;
}

} // namespace psvitaalive

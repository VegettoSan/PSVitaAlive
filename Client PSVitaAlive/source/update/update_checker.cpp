#include "update/update_checker.hpp"

#include "archive/zip_extractor.hpp"
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

namespace psvitaalive {
namespace {

constexpr const char* RELEASES_LATEST_URL =
    "https://api.github.com/repos/VegettoSan/PSVitaAlive/releases/latest";
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

std::string extractVersion(const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) continue;
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
        if (end > i) return text.substr(i, end - i);
    }
    return {};
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
    // If the preferred name is missing but exactly one .vpk exists, use it.
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
                  const char* message) {
    if (!onProgress) return;
    UpdateChecker::ApplyProgress p;
    p.stage = stage;
    p.current = current;
    p.total = total;
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

} // namespace

UpdateChecker::Result UpdateChecker::checkLatest(const std::string& currentVersion) {
    Result result;
    result.localVersion = extractVersion(currentVersion);
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
    const HttpResult fetchResult = http.fetchToString(RELEASES_LATEST_URL, body, 512 * 1024);
    if (fetchResult != HttpResult::Ok) {
        result.error = http.lastError().empty() ? toString(fetchResult) : http.lastError();
        diagnostics::log("[UpdateChecker] GitHub request failed: " + result.error);
        http.shutdown();
        return result;
    }

    const int moduleResult = sceSysmoduleLoadModule(SCE_SYSMODULE_JSON);
    if (moduleResult < 0) {
        result.error = "Unable to load Vita JSON module";
        diagnostics::log("[UpdateChecker] JSON module load failed");
        http.shutdown();
        return result;
    }

    VitaJsonAllocator allocator;
    sce::Json::Initializer initializer;
    if (initializer.initialize(&allocator, nullptr) < 0) {
        result.error = "Unable to initialize JSON parser";
        diagnostics::log("[UpdateChecker] JSON initializer failed");
        sceSysmoduleUnloadModule(SCE_SYSMODULE_JSON);
        http.shutdown();
        return result;
    }

    sce::Json::Value root;
    if (sce::Json::Parser::parse(root, body.c_str()) < 0) {
        result.error = "Invalid GitHub release JSON";
        diagnostics::log("[UpdateChecker] GitHub release JSON parse failed");
        initializer.terminate();
        sceSysmoduleUnloadModule(SCE_SYSMODULE_JSON);
        http.shutdown();
        return result;
    }

    result.releaseTag = getString(root, "tag_name");
    result.releaseName = getString(root, "name");

    const bool conventionalTag = !result.releaseTag.empty() &&
        (std::isdigit(static_cast<unsigned char>(result.releaseTag[0])) ||
         result.releaseTag[0] == 'v' || result.releaseTag[0] == 'V');
    if (conventionalTag) result.remoteVersion = extractVersion(result.releaseTag);
    if (result.remoteVersion.empty()) result.remoteVersion = extractVersion(result.releaseName);
    if (result.remoteVersion.empty()) result.remoteVersion = extractVersion(result.releaseTag);

    if (!pickVpkAsset(root["assets"], result)) {
        // leave downloadUrl empty
    }

    if (result.remoteVersion.empty()) {
        result.error = "GitHub release does not expose a usable version";
    } else if (result.downloadUrl.empty()) {
        result.error = "GitHub release does not contain a .vpk asset (expected PSVitaAlive.vpk)";
    } else if (compareVersions(result.localVersion, result.remoteVersion) < 0) {
        result.state = State::UpdateAvailable;
        diagnostics::log("[UpdateChecker] update available: local=" + result.localVersion +
                         " remote=" + result.remoteVersion + " asset=" + result.assetName);
    } else {
        result.state = State::UpToDate;
        diagnostics::log("[UpdateChecker] client is up to date: " + result.localVersion +
                         " remote=" + result.remoteVersion);
    }

    if (result.state == State::Failed) {
        diagnostics::log("[UpdateChecker] check failed: " + result.error);
    }

    initializer.terminate();
    sceSysmoduleUnloadModule(SCE_SYSMODULE_JSON);
    http.shutdown();
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
            emitProgress(onProgress, ApplyStage::Downloading, hp.downloaded, hp.total > 0 ? hp.total : info.assetSize, "Downloading update VPK");
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

    // Extraction is not cancellable (VitaDB-style) to avoid a half-written app tree.
    emitProgress(onProgress, ApplyStage::Extracting, 0, 0, "Installing update (do not power off)");
    diagnostics::log("[UpdateChecker] extracting in-place to " + std::string(kAppDir));

    if (!st.createDirectories(kAppDir)) {
        diagnostics::log("[UpdateChecker] app dir missing and could not create");
        // still try extract; folder usually already exists when app is running
    }

    ZipExtractor zip;
    const ZipResult zr = zip.extract(
        kVpkPath,
        kAppDir,
        [&](const ZipProgress& zp) {
            emitProgress(
                onProgress,
                ApplyStage::Extracting,
                zp.entriesDone,
                zp.entriesTotal,
                "Installing update (do not power off)"
            );
        },
        nullptr
    );

    if (zr != ZipResult::Ok) {
        const std::string err = zip.lastError().empty() ? toString(zr) : zip.lastError();
        diagnostics::log("[UpdateChecker] extract failed: " + err);
        st.removeFile(kVpkPath);
        emitProgress(onProgress, ApplyStage::Error, 0, 0, err.c_str());
        return false;
    }

    emitProgress(onProgress, ApplyStage::Finalizing, 0, 0, "Finalizing");
    const std::string ver = info.remoteVersion.empty() ? info.releaseTag : info.remoteVersion;
    if (!writeVersionMarker(ver)) {
        diagnostics::log("[UpdateChecker] version marker write failed (non-fatal)");
    }

    st.removeFile(kVpkPath);
    diagnostics::log("[UpdateChecker] apply OK remote=" + ver);
    emitProgress(onProgress, ApplyStage::Done, 1, 1, "Update installed — close and reopen the app");
    return true;
}

} // namespace psvitaalive

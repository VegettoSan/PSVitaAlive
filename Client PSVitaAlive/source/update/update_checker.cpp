#include "update/update_checker.hpp"

#include "diagnostic_logger.hpp"
#include "network/http_client.hpp"

#include <psp2/json.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/clib.h>

#include <cctype>
#include <cstdlib>
#include <string>

namespace psvitaalive {
namespace {

constexpr const char* RELEASES_LATEST_URL =
    "https://api.github.com/repos/VegettoSan/PSVitaAlive/releases/latest";
constexpr const char* UPDATE_ASSET_NAME = "PSVitaAlive.vpk";

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

int compareVersions(const std::string& left, const std::string& right) {
    size_t lp = 0;
    size_t rp = 0;
    for (int part = 0; part < 3; ++part) {
        uint64_t lv = 0;
        uint64_t rv = 0;
        if (!parseVersionPart(left, lp, lv) || !parseVersionPart(right, rp, rv)) return 0;
        if (lv < rv) return -1;
        if (lv > rv) return 1;
        if (lp < left.size() && left[lp] == '.') ++lp;
        if (rp < right.size() && right[rp] == '.') ++rp;
    }
    return 0;
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
    sce::Json::InitParameter params;
    params.allocator = &allocator;
    params.userData = nullptr;
    params.bufSize = 64 * 1024;

    sce::Json::Initializer initializer;
    if (initializer.initialize(&params) < 0) {
        result.error = "Unable to initialize Vita JSON parser";
        diagnostics::log("[UpdateChecker] JSON initializer failed");
        http.shutdown();
        return result;
    }

    sce::Json::Value root;
    if (sce::Json::Parser::parse(root, body.c_str()) < 0) {
        result.error = "Invalid GitHub release JSON";
        diagnostics::log("[UpdateChecker] GitHub release JSON parse failed");
        initializer.terminate();
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

    const sce::Json::Value& assetsValue = root["assets"];
    if (assetsValue) {
        const sce::Json::Array& assets = assetsValue.getArray();
        for (SceSize i = 0; i < assets.size(); ++i) {
            const sce::Json::Value& asset = assetsValue[i];
            const std::string name = getString(asset, "name");
            if (name != UPDATE_ASSET_NAME) continue;
            result.assetName = name;
            result.downloadUrl = getString(asset, "browser_download_url");
            result.digest = getString(asset, "digest");
            result.assetSize = getUnsigned(asset, "size");
            break;
        }
    }

    if (result.remoteVersion.empty()) {
        result.error = "GitHub release does not expose a usable version";
    } else if (result.downloadUrl.empty()) {
        result.error = "GitHub release does not contain PSVitaAlive.vpk";
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
    http.shutdown();
    return result;
}

} // namespace psvitaalive

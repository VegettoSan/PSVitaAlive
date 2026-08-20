#include "update/update_checker.hpp"

#include "archive/zip_extractor.hpp"
#include "diagnostic_logger.hpp"
#include "installer/fake_package_builder.hpp"
#include "network/http_client.hpp"
#include "storage/storage_manager.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/appmgr.h>
#include <psp2/json.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/promoterutil.h>
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
constexpr const char* VERSION_MARKER = "ux0:/app/PSVAS1178/psvitaalive_version.txt";
// Same shallow promote path as HomebrewInstaller (working VPK installs).
constexpr const char* PROMOTE_DIR = "ux0:data/psva_vpk";
// Legacy intermediate path (migrated to PROMOTE_DIR before launching updater).
constexpr const char* PACKAGE_DIR = "ux0:data/psvitaalive/pkg";
constexpr const char* UPDATER_TITLE_ID = "PSVAUPDT1";
constexpr const char* UPDATER_PKG_DIR = "ux0:data/psvitaalive/updater_pkg";
constexpr const char* UPDATER_EBOOT_SRC = "app0:updater/eboot.bin";
constexpr const char* UPDATER_SFO_SRC = "app0:updater/param.sfo";
constexpr const char* UPDATE_LOG_NOTE =
    "ux0:data/psvitaalive/logs/updater.log (written by PSVAUPDT1)";

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
    return currentVersion;
}

bool isDotEntry(const char* name) {
    return name && (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0);
}

bool removeTree(const std::string& path) {
    StorageManager st;
    if (!st.exists(path)) return true;
    if (!st.isDirectory(path)) return st.removeFile(path);

    SceUID uid = sceIoDopen(path.c_str());
    if (uid < 0) return false;
    bool ok = true;
    SceIoDirent ent;
    while (sceIoDread(uid, &ent) > 0) {
        if (isDotEntry(ent.d_name)) continue;
        const std::string child = path + "/" + ent.d_name;
        const bool childIsDir = (ent.d_stat.st_mode & SCE_S_IFDIR) != 0;
        if (childIsDir) {
            if (!removeTree(child)) { ok = false; break; }
        } else if (!st.removeFile(child)) {
            ok = false;
            break;
        }
    }
    sceIoDclose(uid);
    if (!ok) return false;
    return st.removeDirectory(path);
}

bool loadPromoterModules() {
    if (sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PAF) < 0) {
        uint32_t ptr[0x100] = {0};
        ptr[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&ptr[0]));
        uint32_t scepafArgp[] = { 0x400000u, 0xEA60u, 0x40000u, 0u, 0u };
        const int r = sceSysmoduleLoadModuleInternalWithArg(
            SCE_SYSMODULE_INTERNAL_PAF,
            sizeof(scepafArgp),
            scepafArgp,
            reinterpret_cast<SceSysmoduleOpt*>(ptr)
        );
        diagnostics::log("[UpdateChecker] PAF load result=" + std::to_string(r));
        if (r < 0) return false;
    }

    if (sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL) < 0) {
        const int r = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
        diagnostics::log("[UpdateChecker] Promoter module load result=" + std::to_string(r));
        if (r < 0) return false;
    }
    return true;
}

bool fileExists(const std::string& path) {
    SceIoStat st{};
    return sceIoGetstat(path.c_str(), &st) >= 0;
}

bool copyFileSimple(const std::string& src, const std::string& dst) {
    const SceUID in = sceIoOpen(src.c_str(), SCE_O_RDONLY, 0);
    if (in < 0) return false;
    const SceUID out = sceIoOpen(dst.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (out < 0) {
        sceIoClose(in);
        return false;
    }
    // Keep buffer small: update worker used to have only 64KB stack and a
    // 64KB local buffer here overflowed it on real hardware during
    // installUpdaterHelper (crash right after "begin").
    char buf[8 * 1024];
    bool ok = true;
    while (true) {
        const int n = sceIoRead(in, buf, sizeof(buf));
        if (n < 0) { ok = false; break; }
        if (n == 0) break;
        if (sceIoWrite(out, buf, static_cast<SceSize>(n)) != n) { ok = false; break; }
    }
    sceIoClose(out);
    sceIoClose(in);
    return ok;
}

void unloadPromoterModules() {
    // VitaShell pattern: fully tear down promoter + PAF before launching another app.
    // Leaving them loaded makes sceAppMgrLaunchAppByUri intermittent (freeze/crash).
    scePromoterUtilityExit();
    if (sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL) >= 0) {
        const int r = sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
        diagnostics::log("[UpdateChecker] Promoter unload -> " + std::to_string(r));
    }
    if (sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PAF) >= 0) {
        uint32_t buf = 0;
        const int r = sceSysmoduleUnloadModuleInternalWithArg(
            SCE_SYSMODULE_INTERNAL_PAF, 0, nullptr, &buf);
        diagnostics::log("[UpdateChecker] PAF unload -> " + std::to_string(r));
    }
}

// Prefer VitaShell-style synchronous promote for the tiny PSVAUPDT1 package,
// then unload modules so LiveArea can register the bubble cleanly.
bool promoteDirSyncAndUnload(const std::string& dir) {
    diagnostics::log("[UpdateChecker] promoteDirSyncAndUnload: " + dir);
    if (!loadPromoterModules()) {
        diagnostics::log("[UpdateChecker] promoteDirSync: loadPromoterModules failed");
        return false;
    }
    const int initResult = scePromoterUtilityInit();
    diagnostics::log("[UpdateChecker] scePromoterUtilityInit -> " + std::to_string(initResult));
    if (initResult < 0) {
        unloadPromoterModules();
        return false;
    }

    // sync=1 blocks until promote finishes (VitaShell updater uses this path).
    const int promoteResult = scePromoterUtilityPromotePkg(dir.c_str(), 1);
    diagnostics::log("[UpdateChecker] PromotePkg(sync=1) " + dir + " -> " + std::to_string(promoteResult));
    if (promoteResult < 0) {
        // Fallback: async + poll (some firmwares reject sync for certain packages)
        diagnostics::log("[UpdateChecker] sync promote failed; trying async fallback");
        const int asyncR = scePromoterUtilityPromotePkg(dir.c_str(), 0);
        diagnostics::log("[UpdateChecker] PromotePkg(sync=0) -> " + std::to_string(asyncR));
        if (asyncR < 0) {
            scePromoterUtilityExit();
            unloadPromoterModules();
            return false;
        }
        int state = 1;
        int polls = 0;
        while (polls < 12000) {
            state = 0;
            if (scePromoterUtilityGetState(&state) < 0) break;
            if (state == 0) break;
            if ((polls % 200) == 0) {
                diagnostics::log("[UpdateChecker] async promote wait state=" + std::to_string(state));
            }
            sceKernelDelayThread(10 * 1000);
            ++polls;
        }
        int op = 0;
        const int gr = scePromoterUtilityGetResult(&op);
        diagnostics::log("[UpdateChecker] async GetResult call=" + std::to_string(gr) +
                         " op=" + std::to_string(op) + " state=" + std::to_string(state));
        if (state != 0) {
            scePromoterUtilityExit();
            unloadPromoterModules();
            return false;
        }
    }

    scePromoterUtilityExit();
    unloadPromoterModules();

    const bool gone = !fileExists(dir);
    diagnostics::log(std::string("[UpdateChecker] promote dir consumed=") + (gone ? "yes" : "no"));
    // Give LiveArea a moment to register the new title id.
    sceKernelDelayThread(500 * 1000);
    return true;
}

bool promoteDirAsync(const std::string& dir) {
    // Kept for any non-updater callers; updater uses promoteDirSyncAndUnload.
    return promoteDirSyncAndUnload(dir);
}

// Install/refresh the temporary PSVAUPDT1 helper from files packed in the client VPK.
bool installUpdaterHelper() {
    diagnostics::log("[UpdateChecker] installUpdaterHelper: begin");
    StorageManager st;
    diagnostics::log("[UpdateChecker] installUpdaterHelper: removeTree updater_pkg");
    removeTree(UPDATER_PKG_DIR);
    diagnostics::log("[UpdateChecker] installUpdaterHelper: createDirectories");
    if (!st.createDirectories(std::string(UPDATER_PKG_DIR) + "/sce_sys")) {
        diagnostics::log("[UpdateChecker] cannot create updater package dirs");
        return false;
    }
    diagnostics::log("[UpdateChecker] installUpdaterHelper: checking app0:updater payload");
    if (!fileExists(UPDATER_EBOOT_SRC) || !fileExists(UPDATER_SFO_SRC)) {
        diagnostics::log("[UpdateChecker] updater payload missing from app0:updater/");
        return false;
    }
    diagnostics::log("[UpdateChecker] installUpdaterHelper: copy eboot.bin");
    if (!copyFileSimple(UPDATER_EBOOT_SRC, std::string(UPDATER_PKG_DIR) + "/eboot.bin")) {
        diagnostics::log("[UpdateChecker] failed to copy updater eboot");
        return false;
    }
    diagnostics::log("[UpdateChecker] copied updater eboot.bin");
    diagnostics::log("[UpdateChecker] installUpdaterHelper: copy param.sfo");
    if (!copyFileSimple(UPDATER_SFO_SRC, std::string(UPDATER_PKG_DIR) + "/sce_sys/param.sfo")) {
        diagnostics::log("[UpdateChecker] failed to copy updater param.sfo");
        return false;
    }
    diagnostics::log("[UpdateChecker] copied updater param.sfo");
    st.createDirectories(std::string(UPDATER_PKG_DIR) + "/ui");
    if (fileExists("app0:ui/catalog_loading.png")) {
        diagnostics::log("[UpdateChecker] installUpdaterHelper: copy catalog_loading.png");
        const bool ok = copyFileSimple("app0:ui/catalog_loading.png",
                       std::string(UPDATER_PKG_DIR) + "/ui/catalog_loading.png");
        diagnostics::log(std::string("[UpdateChecker] catalog_loading.png copy ") + (ok ? "OK" : "FAIL"));
    } else {
        diagnostics::log("[UpdateChecker] catalog_loading.png not in app0:ui (UI will be plain)");
    }

    diagnostics::log("[UpdateChecker] installUpdaterHelper: FakePackageBuilder");
    FakePackageBuilder builder;
    if (!builder.build(UPDATER_PKG_DIR)) {
        diagnostics::log("[UpdateChecker] updater head.bin failed: " + builder.lastError());
        removeTree(UPDATER_PKG_DIR);
        return false;
    }
    diagnostics::log("[UpdateChecker] updater head.bin OK - promoting PSVAUPDT1");
    if (!promoteDirAsync(UPDATER_PKG_DIR)) {
        diagnostics::log("[UpdateChecker] updater promote failed");
        removeTree(UPDATER_PKG_DIR);
        return false;
    }
    removeTree(UPDATER_PKG_DIR);
    diagnostics::log("[UpdateChecker] updater helper installed (PSVAUPDT1)");
    return true;
}

bool waitForUpdaterBubble(int maxMs) {
    if (!loadPromoterModules()) {
        diagnostics::log("[UpdateChecker] waitForUpdaterBubble: cannot load promoter");
        return false;
    }
    const int initR = scePromoterUtilityInit();
    if (initR < 0) {
        diagnostics::log("[UpdateChecker] waitForUpdaterBubble: Init -> " + std::to_string(initR));
        unloadPromoterModules();
        return false;
    }
    int waited = 0;
    bool found = false;
    while (waited <= maxMs) {
        int exists = 0;
        const int cr = scePromoterUtilityCheckExist(UPDATER_TITLE_ID, &exists);
        if (cr >= 0 && exists) {
            found = true;
            diagnostics::log("[UpdateChecker] PSVAUPDT1 bubble visible after " +
                             std::to_string(waited) + " ms");
            break;
        }
        if ((waited % 500) == 0) {
            diagnostics::log("[UpdateChecker] waiting for PSVAUPDT1 bubble exists=" +
                             std::to_string(exists) + " check=" + std::to_string(cr) +
                             " t=" + std::to_string(waited));
        }
        sceKernelDelayThread(100 * 1000);
        waited += 100;
    }
    scePromoterUtilityExit();
    unloadPromoterModules();
    return found;
}

bool launchUpdaterAndExitInternal() {
    // Stabilize handoff (real Vita flakiness):
    // 1) Wait until promoter sees PSVAUPDT1.
    // 2) Unload promoter/PAF completely.
    // 3) Destroy other apps / brief delay so Shell can switch.
    // 4) Launch via URI with retries.
    diagnostics::log("[UpdateChecker] launchUpdaterAndExit: prepare handoff");

    if (!waitForUpdaterBubble(8000)) {
        diagnostics::log("[UpdateChecker] PSVAUPDT1 not visible yet; launching anyway after delay");
        sceKernelDelayThread(1500 * 1000);
    } else {
        sceKernelDelayThread(800 * 1000);
    }

    diagnostics::log("[UpdateChecker] DestroyOtherApp before updater launch");
    sceAppMgrDestroyOtherApp();
    sceKernelDelayThread(400 * 1000);

    char uri[48];
    sceClibSnprintf(uri, sizeof(uri), "psgm:play?titleid=%s", UPDATER_TITLE_ID);
    diagnostics::log(std::string("[UpdateChecker] launching updater uri=") + uri);

    int launchR = -1;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        launchR = sceAppMgrLaunchAppByUri(0xFFFFF, uri);
        diagnostics::log("[UpdateChecker] LaunchAppByUri attempt=" + std::to_string(attempt) +
                         " -> " + std::to_string(launchR));
        if (launchR >= 0) break;
        sceKernelDelayThread(700 * 1000);
    }

    // Always exit this process so Shell can focus the updater (even if launchR < 0;
    // user can still open PSVAUPDT1 manually from LiveArea if needed).
    sceKernelDelayThread(200 * 1000);
    diagnostics::log("[UpdateChecker] client exiting for updater handoff");
    sceKernelExitProcess(0);
    return true;
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
    if (statusLatest == 403 || statusLatest == 429) {
        result.error = "GitHub temporarily unavailable (HTTP 403/429) - update check skipped";
        diagnostics::log("[UpdateChecker] " + result.error);
        http.shutdown();
        return result;
    }

    if (fetchResult != HttpResult::Ok || statusLatest == 404 || body.empty()) {
        diagnostics::log("[UpdateChecker] releases/latest unavailable - falling back to releases list");
        body.clear();
        fetchResult = http.fetchToString(RELEASES_LIST_URL, body, 512 * 1024);
        usedReleaseList = true;
        logFetch("GET releases?per_page=10", http.lastStatusCode(), body.size(), fetchResult);

        const int statusList = http.lastStatusCode();
        if (statusList == 403 || statusList == 429) {
            result.error = "GitHub temporarily unavailable (HTTP 403/429) - update check skipped";
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

    http.shutdown();
    diagnostics::log("[UpdateChecker] HTTP shutdown complete (before JSON parse)");

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
    diagnostics::log("[UpdateChecker] ===== applyUpdate BEGIN =====");
    diagnostics::log(std::string("[UpdateChecker] remote=") + info.remoteVersion +
                     " tag=" + info.releaseTag +
                     " asset=" + info.assetName +
                     " url=" + info.downloadUrl +
                     " size=" + std::to_string(info.assetSize));
    diagnostics::log(std::string("[UpdateChecker] VPK path=") + kVpkPath);
    diagnostics::log(std::string("[UpdateChecker] promote path (homebrew-aligned)=") + PROMOTE_DIR);
    diagnostics::log(std::string("[UpdateChecker] updater log will be at ") + UPDATE_LOG_NOTE);

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
    removeTree(PACKAGE_DIR);
    removeTree(PROMOTE_DIR);
    removeTree(UPDATER_PKG_DIR);
    diagnostics::log("[UpdateChecker] cleaned previous staging/update dirs");

    if (shouldCancel && shouldCancel()) {
        emitProgress(onProgress, ApplyStage::Error, 0, 0, "Cancelled");
        return false;
    }

    HttpClient http;
    if (http.init() != HttpResult::Ok) {
        diagnostics::log("[UpdateChecker] HTTP init failed");
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
        diagnostics::log("[UpdateChecker] download failed: " + err +
                         " status=" + std::to_string(http.lastStatusCode()));
        st.removeFile(kVpkPath);
        http.shutdown();
        emitProgress(onProgress, ApplyStage::Error, 0, 0, err.c_str());
        return false;
    }
    http.shutdown();
    {
        SceIoStat vst{};
        const int vr = sceIoGetstat(kVpkPath, &vst);
        char buf[160];
        sceClibSnprintf(buf, sizeof(buf),
            "[UpdateChecker] VPK on disk stat=%d size=%u path=%s",
            vr, vr >= 0 ? (unsigned)vst.st_size : 0u, kVpkPath);
        diagnostics::log(buf);
    }

    // Keep the VPK on disk for manual recovery if anything below fails.
    emitProgress(onProgress, ApplyStage::Extracting, 0, 0, "Extracting update package");
    diagnostics::log(std::string("[UpdateChecker] extracting VPK -> ") + PROMOTE_DIR +
                     " (same path as homebrew VPK installs)");

    removeTree(PROMOTE_DIR);
    const ZipResult zr = ZipExtractor().extract(
        kVpkPath,
        PROMOTE_DIR,
        [&](const ZipProgress& zp) {
            emitProgress(onProgress, ApplyStage::Extracting, zp.entriesDone, zp.entriesTotal,
                         "Extracting update package");
        },
        shouldCancel
    );

    if (zr != ZipResult::Ok) {
        diagnostics::log(std::string("[UpdateChecker] extract failed: ") + toString(zr));
        removeTree(PROMOTE_DIR);
        emitProgress(
            onProgress,
            ApplyStage::Error,
            0,
            0,
            "Extract failed. Manual VPK: ux0:data/psvitaalive/update/PSVitaAlive.vpk"
        );
        return false;
    }
    diagnostics::log("[UpdateChecker] extract OK");
    diagnostics::log(std::string("[UpdateChecker] exists eboot=") +
                     (fileExists(std::string(PROMOTE_DIR) + "/eboot.bin") ? "yes" : "no") +
                     " param=" +
                     (fileExists(std::string(PROMOTE_DIR) + "/sce_sys/param.sfo") ? "yes" : "no"));

    FakePackageBuilder builder;
    if (!builder.build(PROMOTE_DIR)) {
        diagnostics::log("[UpdateChecker] head.bin failed: " + builder.lastError());
        emitProgress(
            onProgress,
            ApplyStage::Error,
            0,
            0,
            "Package prepare failed. Manual VPK: ux0:data/psvitaalive/update/PSVitaAlive.vpk"
        );
        return false;
    }
    diagnostics::log(std::string("[UpdateChecker] head.bin OK exists=") +
                     (fileExists(std::string(PROMOTE_DIR) + "/sce_sys/package/head.bin") ? "yes" : "no"));

    emitProgress(onProgress, ApplyStage::Finalizing, 0, 0, "Preparing updater...");
    diagnostics::log("[UpdateChecker] installing PSVAUPDT1 helper from app0:updater/");
    diagnostics::log(std::string("[UpdateChecker] updater eboot src exists=") +
                     (fileExists(UPDATER_EBOOT_SRC) ? "yes" : "no") +
                     " sfo=" + (fileExists(UPDATER_SFO_SRC) ? "yes" : "no"));

    if (!installUpdaterHelper()) {
        diagnostics::log("[UpdateChecker] installUpdaterHelper FAILED");
        emitProgress(
            onProgress,
            ApplyStage::Error,
            0,
            0,
            "Updater install failed. Manual VPK: ux0:data/psvitaalive/update/PSVitaAlive.vpk"
        );
        return false;
    }
    diagnostics::log("[UpdateChecker] PSVAUPDT1 helper installed OK");

    const std::string ver = info.remoteVersion.empty() ? info.releaseTag : info.remoteVersion;
    if (!writeVersionMarker(ver)) {
        diagnostics::log("[UpdateChecker] version marker write failed (non-fatal)");
    } else {
        diagnostics::log("[UpdateChecker] version marker written: " + ver);
    }

    // Do NOT launch here — applyUpdate runs on a worker thread. sceAppMgrLaunchAppByUri
    // from a non-main thread freezes/crashes on real hardware. Main thread must call
    // UpdateChecker::launchUpdaterAndExit() after this returns true.
    diagnostics::log("[UpdateChecker] helper ready — main thread must launch PSVAUPDT1");
    diagnostics::log("[UpdateChecker] after update, check ux0:data/psvitaalive/logs/updater.log");
    diagnostics::log("[UpdateChecker] ===== applyUpdate DONE (no launch from worker) =====");
    emitProgress(onProgress, ApplyStage::Done, 1, 1, "Updater ready");
    return true;
}

void UpdateChecker::launchUpdaterAndExit() {
    diagnostics::log("[UpdateChecker] launchUpdaterAndExit on MAIN thread");
    // Reuse internal handoff (wait bubble + DestroyOtherApp + retries + ExitProcess).
    launchUpdaterAndExitInternal();
}

// Remove temporary updater bubble after a successful self-update cycle.
bool UpdateChecker::cleanupUpdaterBubble() {
    if (!loadPromoterModules()) {
        diagnostics::log("[UpdateChecker] cleanupUpdater: promoter load failed");
        return false;
    }
    const int initResult = scePromoterUtilityInit();
    if (initResult < 0) {
        diagnostics::log("[UpdateChecker] cleanupUpdater: init failed " + std::to_string(initResult));
        return false;
    }
    int exists = 0;
    const int check = scePromoterUtilityCheckExist(UPDATER_TITLE_ID, &exists);
    if (check < 0) {
        // Not installed / API quirk - treat as nothing to do.
        scePromoterUtilityExit();
        return true;
    }
    diagnostics::log("[UpdateChecker] cleanupUpdater: deleting PSVAUPDT1 bubble");
    sceAppMgrDestroyOtherApp();
    const int del = scePromoterUtilityDeletePkg(UPDATER_TITLE_ID);
    diagnostics::log("[UpdateChecker] DeletePkg(PSVAUPDT1) -> " + std::to_string(del));
    int state = 1;
    int polls = 0;
    while (polls < 2000) {
        state = 0;
        if (scePromoterUtilityGetState(&state) < 0) break;
        if (state == 0) break;
        sceKernelDelayThread(10 * 1000);
        ++polls;
    }
    scePromoterUtilityExit();
    // Best-effort: also drop leftover VPK after a clean update cycle.
    StorageManager st;
    st.removeFile(kVpkPath);
    return del >= 0;
}


} // namespace psvitaalive

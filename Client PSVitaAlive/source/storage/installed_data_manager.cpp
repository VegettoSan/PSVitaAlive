#include "storage/installed_data_manager.hpp"
#include "storage/installed_data_policy.hpp"
#include "diagnostic_logger.hpp"
#include <psp2/appmgr.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/promoterutil.h>
#include <psp2/sysmodule.h>
#include <algorithm>
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>

namespace psvitaalive { namespace storage {
namespace {
constexpr uint32_t MAX_ENTRIES = 200000;
constexpr unsigned MAX_DEPTH = 24;
constexpr const char* CLIENT_TID = "PSVAS1178"; // self-protection is also by appId

uint64_t sum(uint64_t a, uint64_t b) {
    return b > UINT64_MAX - a ? UINT64_MAX : a + b;
}
bool exists(const std::string& path, SceIoStat* out=nullptr) {
    SceIoStat st{};
    if (sceIoGetstat(path.c_str(), &st) < 0) return false;
    if (out) *out = st;
    return true;
}
bool safeComponent(const std::string& n) {
    return validInstalledRelativePath(n) && n.find('/') == std::string::npos;
}
bool scanTree(const std::string& p, unsigned depth,
              uint32_t& entries, uint64_t& bytes) {
    if (depth > MAX_DEPTH || ++entries > MAX_ENTRIES ||
        !safeExternalDataPath(p)) return false;
    SceIoStat st{};
    if (!exists(p, &st)) return false;
    if (!(st.st_mode & SCE_S_IFDIR)) {
        if (st.st_size > 0) bytes = sum(bytes, static_cast<uint64_t>(st.st_size));
        return true;
    }
    const SceUID d = sceIoDopen(p.c_str());
    if (d < 0) return false;
    SceIoDirent entry{};
    int rc = 0;
    bool complete = true;
    while ((rc = sceIoDread(d, &entry)) > 0) {
        if (!safeComponent(entry.d_name) ||
            !scanTree(p + "/" + entry.d_name, depth+1, entries, bytes)) {
            complete = false;
            break;
        }
    }
    if (rc < 0) complete = false;
    sceIoDclose(d);
    return complete;
}
bool eraseTree(const std::string& path, unsigned depth,
               uint32_t& count) {
    if (!safeExternalDataPath(path) || depth > MAX_DEPTH ||
        ++count > MAX_ENTRIES) return false;
    SceIoStat st{};
    if (!exists(path, &st)) return true;
    if (!(st.st_mode & SCE_S_IFDIR)) return sceIoRemove(path.c_str()) >= 0;
    // Removing during enumeration may skip entries; reopen until empty.
    for (unsigned pass = 0; pass < 32; ++pass) {
        const SceUID d = sceIoDopen(path.c_str());
        if (d < 0) return false;
        SceIoDirent e{};
        int rc = 0;
        bool found = false;
        bool ok = true;
        while ((rc = sceIoDread(d, &e)) > 0) {
            if (!safeComponent(e.d_name)) { ok = false; break; }
            found = true;
            if (!eraseTree(path + "/" + e.d_name, depth+1, count)) {
                ok = false; break;
            }
        }
        sceIoDclose(d);
        if (rc < 0 || !ok) return false;
        if (!found) return sceIoRmdir(path.c_str()) >= 0;
    }
    return false;
}
void uniqueRoots(std::vector<std::string>& roots) {
    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    // Retain only ancestors. This avoids counting or deleting a child twice.
    std::vector<std::string> compact;
    for (const auto& p : roots) {
        bool covered = false;
        for (const auto& prior : compact)
            if (installedPathsOverlap(prior, p) &&
                prior.size() < p.size()) { covered = true; break; }
        if (!covered) compact.push_back(p);
    }
    roots.swap(compact);
}
} // namespace

InstalledDataSnapshot InstalledDataManager::scan(std::vector<InstalledDataApp> apps) {
    InstalledDataSnapshot result;
    result.apps = std::move(apps);
    // Never interpret candidate metadata as permission for a different root.
    for (auto& app : result.apps) {
        const bool titleOk = validInstalledTitleId(app.titleId);
        SceIoStat installed{};
        app.packagePresent = titleOk &&
            exists(std::string("ux0:app/") + app.titleId + "/sce_sys/param.sfo", &installed);
        if (app.appId == "psvitaalive" || app.appId == "ps-vita-alive" ||
            app.titleId == CLIENT_TID) { app.blocked = true; app.unresolved = true; }
        uniqueRoots(app.roots);
        for (const auto& path : app.roots) {
            InstalledDataPath row;
            row.path = path;
            if (!safeExternalDataPath(path) || !titleOk) {
                app.blocked = true;
                app.unresolved = true;
                row.complete = false;
            }
            app.paths.push_back(std::move(row));
        }
        if (app.unresolved) app.blocked = true;
    }
    // Cross-app path collisions are blocked regardless of whether both app
    // packages are currently installed. Catalog ownership is not exclusive.
    for (size_t i = 0; i < result.apps.size(); ++i) {
        for (size_t j = i+1; j < result.apps.size(); ++j) {
            for (auto& a : result.apps[i].paths)
                for (auto& b : result.apps[j].paths)
                    if (installedPathsOverlap(a.path, b.path)) {
                        a.shared = b.shared = true;
                        result.apps[i].blocked = result.apps[j].blocked = true;
                    }
        }
    }
    std::unordered_map<std::string, std::pair<uint64_t, bool>> measured;
    for (auto& app : result.apps) {
        for (auto& path : app.paths) {
            if (!safeExternalDataPath(path.path)) continue;
            SceIoStat st{};
            path.exists = exists(path.path, &st);
            if (!path.exists) continue;
            const auto cached = measured.find(path.path);
            if (cached != measured.end()) {
                path.bytes = cached->second.first;
                path.complete = cached->second.second;
            } else {
                uint32_t entries = 0;
                path.complete = scanTree(path.path, 0, entries, path.bytes);
                measured.emplace(path.path, std::make_pair(path.bytes, path.complete));
            }
            if (!path.complete) {
                result.complete = false;
                app.blocked = true;
            }
            app.bytes = sum(app.bytes, path.bytes);
        }
    }
    // Total uses only outermost physical roots, never downloaded ZIP sizes.
    std::vector<std::string> distinct;
    distinct.reserve(measured.size());
    for (const auto& entry : measured) distinct.push_back(entry.first);
    uniqueRoots(distinct);
    for (const auto& path : distinct)
        result.totalExternalBytes = sum(result.totalExternalBytes, measured[path].first);
    return result;
}

bool InstalledDataManager::removeExternal(const InstalledDataSnapshot& snapshot,
                                          const std::string& titleId) {
    if (!validInstalledTitleId(titleId)) return false;
    const InstalledDataApp* target = nullptr;
    for (const auto& app : snapshot.apps)
        if (app.titleId == titleId) { target = &app; break; }
    if (!target || target->blocked || target->unresolved || target->paths.empty())
        return false;
    // Don't trust only snapshot flags: validate collisions immediately before
    // deleting. Source catalog changes require a fresh UI scan by the caller.
    for (const auto& p : target->paths) {
        if (!safeExternalDataPath(p.path) || !p.complete) return false;
        for (const auto& other : snapshot.apps) {
            if (other.titleId == titleId) continue;
            for (const auto& q : other.paths)
                if (installedPathsOverlap(p.path, q.path)) return false;
        }
    }
    bool removed = true;
    for (const auto& path : target->paths) {
        if (!path.exists) continue;
        uint32_t count = 0;
        if (!eraseTree(path.path, 0, count)) {
            diagnostics::log("[InstalledData] unable to remove " + path.path);
            removed = false;
        }
    }
    return removed;
}

bool InstalledDataManager::uninstallPackage(const std::string& titleId) {
    if (!validInstalledTitleId(titleId) || titleId == CLIENT_TID) return false;
    SceIoStat st{};
    if (!exists(std::string("ux0:app/") + titleId + "/sce_sys/param.sfo", &st))
        return false;
    if (sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL) < 0 &&
        sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL) < 0)
        return false;
    if (scePromoterUtilityInit() < 0) return false;
    const int del = scePromoterUtilityDeletePkg(titleId.c_str());
    int state = 1;
    bool idle = false;
    for (unsigned i=0; i<2000 && del >= 0; ++i) {
        state = 1;
        if (scePromoterUtilityGetState(&state) < 0) break;
        if (state == 0) { idle = true; break; }
        sceKernelDelayThread(10000);
    }
    scePromoterUtilityExit();
    // Don't announce success merely because an API call was accepted.
    return del >= 0 && idle &&
        !exists(std::string("ux0:app/") + titleId + "/sce_sys/param.sfo");
}

}} // namespace psvitaalive::storage

#include "storage/installed_data_manager.hpp"
#include "storage/installed_data_policy.hpp"
#include "diagnostic_logger.hpp"
#include <psp2/appmgr.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/promoterutil.h>
#include <psp2/sysmodule.h>
#include <algorithm>
#include <cstring>
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
// Each completed directory entry carries d_stat on PS Vita, avoiding the
// previously expensive sceIoGetstat() call for every file in a large ZIP.
// Some emulators have incomplete d_stat; fall back to Getstat in that case.
struct ScanContext {
    InstalledDataScanProgress* progress = nullptr;
    uint64_t startUs = 0;
    uint64_t budgetUs = 0;
    bool stopped = false;
    bool shouldStop() {
        if (stopped) return true;
        if (progress && progress->cancelRequested.load(std::memory_order_relaxed))
            stopped = true;
        if (!stopped && budgetUs &&
            (sceKernelGetProcessTimeWide() - startUs) >= budgetUs)
            stopped = true;
        if (stopped && progress)
            progress->truncated.store(true, std::memory_order_relaxed);
        return stopped;
    }
    void fileSeen() {
        if(progress) progress->filesVisited.fetch_add(1, std::memory_order_relaxed);
    }
    void dirSeen() {
        if(progress) progress->dirsVisited.fetch_add(1, std::memory_order_relaxed);
    }
};
bool scanTree(const std::string& p, const SceIoStat& st, unsigned depth,
              uint32_t& entries, uint64_t& bytes, ScanContext& ctx) {
    if (depth > MAX_DEPTH || ++entries > MAX_ENTRIES ||
        !safeExternalDataPath(p) || ctx.shouldStop()) return false;
    if (!(st.st_mode & SCE_S_IFDIR)) {
        ctx.fileSeen();
        if (st.st_size > 0) bytes = sum(bytes, static_cast<uint64_t>(st.st_size));
        return true;
    }
    ctx.dirSeen();
    const SceUID d = sceIoDopen(p.c_str());
    if (d < 0) return false;
    SceIoDirent entry{};
    int rc = 0;
    bool complete = true;
    while ((rc = sceIoDread(d, &entry)) > 0) {
        if (std::strcmp(entry.d_name, ".") == 0 ||
            std::strcmp(entry.d_name, "..") == 0) continue;
        if (!safeComponent(entry.d_name) || entries >= MAX_ENTRIES ||
            ((entries & 63u) == 0u && ctx.shouldStop())) {
            complete = false;
            break;
        }
        // This fast path eliminates path allocations and syscalls for files.
        if (entry.d_stat.st_mode != 0 &&
            !(entry.d_stat.st_mode & SCE_S_IFDIR)) {
            ++entries;
            ctx.fileSeen();
            if (entry.d_stat.st_size > 0)
                bytes = sum(bytes, static_cast<uint64_t>(entry.d_stat.st_size));
            continue;
        }
        const std::string child = p + "/" + entry.d_name;
        SceIoStat childStat = entry.d_stat;
        if (childStat.st_mode == 0 && !exists(child, &childStat)) {
            complete = false;
            break;
        }
        if (!scanTree(child, childStat, depth+1, entries, bytes, ctx)) {
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
            if (std::strcmp(e.d_name, ".") == 0 ||
                std::strcmp(e.d_name, "..") == 0) continue;
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

InstalledDataSnapshot InstalledDataManager::scan(
    std::vector<InstalledDataApp> apps,
    const std::string& onlyTitleId,
    InstalledDataScanProgress* progress,
    uint64_t timeBudgetUs) {
    InstalledDataSnapshot result;
    result.apps = std::move(apps);
    ScanContext ctx;
    ctx.progress = progress;
    ctx.startUs = sceKernelGetProcessTimeWide();
    ctx.budgetUs = timeBudgetUs;
    if (progress) {
        // The caller resets counters *before* launching the worker.
        // Do not erase a cancellation pressed immediately after opening.
        progress->phase.store(1, std::memory_order_release);
        uint32_t count = 0;
        for (const auto& app : result.apps)
            if (onlyTitleId.empty() || app.titleId == onlyTitleId) ++count;
        progress->appsTotal.store(count, std::memory_order_relaxed);
    }
    // Phase 1: identify declared roots and package presence. We keep *all*
    // catalog claims to detect overlaps, but only touch the requested app's
    // filesystem when invoked from the application's uninstall button.
    for (auto& app : result.apps) {
        const bool selected = onlyTitleId.empty() || app.titleId == onlyTitleId;
        const bool titleOk = validInstalledTitleId(app.titleId);
        if (selected && titleOk) {
            app.packagePresent = exists(
                std::string("ux0:app/") + app.titleId + "/sce_sys/param.sfo");
        }
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
            } else if (selected && !ctx.shouldStop()) {
                row.exists = exists(path);
                if (row.exists && progress)
                    progress->rootsFound.fetch_add(1, std::memory_order_relaxed);
            } else if (selected) {
                row.complete = false;
            }
            app.paths.push_back(std::move(row));
        }
        if (app.unresolved) app.blocked = true;
    }
    // Cross-app conflicts must be checked for every declaration, including
    // uninstalled apps. Never rely solely on a filtered UI list for safety.
    for (size_t i = 0; i < result.apps.size(); ++i)
        for (size_t j = i+1; j < result.apps.size(); ++j)
            for (auto& a : result.apps[i].paths)
                for (auto& b : result.apps[j].paths)
                    if (installedPathsOverlap(a.path, b.path)) {
                        a.shared = b.shared = true;
                        result.apps[i].blocked = result.apps[j].blocked = true;
                    }

    if (progress) progress->phase.store(2, std::memory_order_release);
    // Phase 2: measure actual file bytes with a strict total time budget.
    // Directory reads never hold a UI lock or allocate one object per file.
    std::unordered_map<std::string, std::pair<uint64_t, bool>> measured;
    for (auto& app : result.apps) {
        if (!onlyTitleId.empty() && app.titleId != onlyTitleId) continue;
        if (ctx.shouldStop()) {
            result.complete = false;
            app.blocked = true;
            for (auto& path : app.paths) path.complete = false;
            continue;
        }
        for (auto& path : app.paths) {
            if (!safeExternalDataPath(path.path) || !path.exists) continue;
            const auto cached = measured.find(path.path);
            if (cached != measured.end()) {
                path.bytes = cached->second.first;
                path.complete = cached->second.second;
            } else {
                SceIoStat st{};
                if (!exists(path.path, &st)) {
                    path.complete = false;
                } else {
                    uint32_t entries = 0;
                    path.complete = scanTree(path.path, st, 0, entries, path.bytes, ctx);
                }
                measured.emplace(path.path, std::make_pair(path.bytes, path.complete));
            }
            if (!path.complete) {
                result.complete = false;
                app.blocked = true;
            }
            app.bytes = sum(app.bytes, path.bytes);
            if (ctx.stopped) break;
        }
        if (ctx.stopped) {
            result.complete = false;
            app.blocked = true;
            for (auto& path : app.paths)
                if (path.exists && measured.find(path.path)==measured.end())
                    path.complete = false;
        } else if (progress) {
            progress->appsCompleted.fetch_add(1, std::memory_order_relaxed);
        }
    }
    std::vector<std::string> distinct;
    distinct.reserve(measured.size());
    for (const auto& entry : measured) distinct.push_back(entry.first);
    uniqueRoots(distinct);
    for (const auto& path : distinct)
        result.totalExternalBytes = sum(result.totalExternalBytes, measured[path].first);
    if (ctx.stopped) result.complete = false;
    if (progress) {
        progress->truncated.store(!result.complete, std::memory_order_relaxed);
        progress->phase.store(3, std::memory_order_release);
    }
    return result;
}

uint64_t InstalledDataManager::knownExternalBytes(
    const InstalledDataSnapshot& snapshot) {
    std::vector<const InstalledDataPath*> paths;
    for (const auto& app : snapshot.apps)
        for (const auto& path : app.paths)
            if (path.exists && safeExternalDataPath(path.path))
                paths.push_back(&path);
    // Count parents before children, and count a shared root only once.
    std::sort(paths.begin(), paths.end(),
              [](const InstalledDataPath* a, const InstalledDataPath* b) {
                  return a->path.size() == b->path.size()
                      ? a->path < b->path : a->path.size() < b->path.size();
              });
    std::vector<std::string> counted;
    uint64_t total = 0;
    for (const auto* path : paths) {
        bool covered = false;
        for (const auto& root : counted)
            if (installedPathsOverlap(root, path->path)) {
                covered = true;
                break;
            }
        if (!covered) {
            counted.push_back(path->path);
            total = sum(total, path->bytes);
        }
    }
    return total;
}

bool InstalledDataManager::removeExternal(const InstalledDataSnapshot& snapshot,
                                          const std::string& titleId) {
    // Other apps may have incomplete size scans; their path *claims* are
    // still checked below. Only the target must be fully verified.
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

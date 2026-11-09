#include "storage/installed_data_manager.hpp"
#include "storage/installed_data_policy.hpp"
#include "storage/installed_directory_cache.hpp"
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
#include <unordered_set>
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
// Each child name is validated independently of the protected root policy.
// Root directory timestamp is a *hint*. A nested write may not update it.
// Therefore any cached bytes are explicitly marked historical in the UI.
uint64_t rootFingerprint(const SceIoStat& st) {
    const auto& t = st.st_mtime;
    if (!t.year) return 0;
    uint64_t value = 1469598103934665603ULL;
    const uint32_t fields[] = {t.year,t.month,t.day,t.hour,t.minute,
                               t.second,t.microsecond};
    for (uint32_t field : fields) {
        value ^= field;
        value *= 1099511628211ULL;
    }
    return value;
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
    // Read-only traversal permits one exact ux0:app/<TITLE_ID> package root.
    // eraseTree() remains strictly limited to safe ux0:data/ locations.
    // Validate only the root; validating every descendant hurts large ZIPs.
    const bool safeRoot = depth != 0 || safeExternalDataPath(p) ||
        (p.compare(0, 8, "ux0:app/") == 0 &&
         validInstalledTitleId(p.substr(8)));
    if (depth > MAX_DEPTH || ++entries > MAX_ENTRIES ||
        !safeRoot ||
        ctx.shouldStop()) return false;
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
        if (!validInstalledChildName(entry.d_name) || entries >= MAX_ENTRIES ||
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
               uint32_t& count, InstalledDataOperationProgress* progress) {
    if ((depth == 0 && !safeExternalDataPath(path)) ||
        depth > MAX_DEPTH || ++count > MAX_ENTRIES) return false;
    SceIoStat st{};
    if (!exists(path, &st)) return true;
    if (!(st.st_mode & SCE_S_IFDIR)) {
        const bool ok = sceIoRemove(path.c_str()) >= 0;
        if (ok && progress)
            progress->filesRemoved.fetch_add(1, std::memory_order_relaxed);
        return ok;
    }
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
            if (!validInstalledChildName(e.d_name)) { ok = false; break; }
            found = true;
            if (!eraseTree(path + "/" + e.d_name, depth+1, count, progress)) {
                ok = false; break;
            }
        }
        sceIoDclose(d);
        if (rc < 0 || !ok) return false;
        if (!found) {
            const bool ok = sceIoRmdir(path.c_str()) >= 0;
            if (ok && progress)
                progress->foldersRemoved.fetch_add(1, std::memory_order_relaxed);
            return ok;
        }
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
// A catalog declaration alone does not prove ownership. Only another
// installed package claiming an overlapping root blocks a destructive action.
bool validateExternalRemoval(const InstalledDataSnapshot& snapshot,
                             const std::string& titleId,
                             const InstalledDataApp*& target,
                             bool confirmedCatalogShare) {
    target = nullptr;
    if (!validInstalledTitleId(titleId)) return false;
    for (const auto& app : snapshot.apps)
        if (app.titleId == titleId) { target = &app; break; }
    // Metadata/routing security is mandatory even when the user has opted
    // into deleting a catalog-shared folder.
    if (!target || !installedDataCanOfferExternalDelete(*target)) return false;
    if (!installedDataSharedDeletePermitted(confirmedCatalogShare,
            installedDataHasCatalogShare(snapshot, *target))) {
        diagnostics::log("[InstalledData] shared root needs an informed confirmation");
        return false;
    }
    bool anyPresent = false;
    for (const auto& p : target->paths) {
        if (!safeExternalDataPath(p.path)) return false;
        if (!p.exists) continue;
        if (!exists(p.path)) return false; // snapshot may have become stale
        anyPresent = true;
    }
    return anyPresent;
}
} // namespace

InstalledDataSnapshot InstalledDataManager::scan(
    std::vector<InstalledDataApp> apps,
    const std::string& onlyTitleId,
    InstalledDataScanProgress* progress,
    uint64_t timeBudgetUs,
    bool measureSizes) {
    InstalledDataSnapshot result;
    result.apps = std::move(apps);
    ScanContext ctx;
    ctx.progress = progress;
    ctx.startUs = sceKernelGetProcessTimeWide();
    ctx.budgetUs = timeBudgetUs;
    std::vector<CachedDirectory> index;
    InstalledDirectoryCache::load(index); // absent or corrupt -> fresh lookup
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
        if (titleOk && (selected || !measureSizes)) {
            const std::string packageRoot = "ux0:app/" + app.titleId;
            // Other VPKs need a lightweight SFO check only for menu
            // discovery/refresh, never for repeated per-game deep measures.
            app.packagePresent = exists(packageRoot + "/sce_sys/param.sfo");
            SceIoStat packageStat{};
            if (selected && app.packagePresent &&
                exists(packageRoot, &packageStat)) {
                app.packageRootStamp = rootFingerprint(packageStat);
                const auto* saved = InstalledDirectoryCache::find(
                    index, app.titleId, packageRoot);
                if (saved && saved->sizeKnown && app.packageRootStamp &&
                    saved->rootStamp == app.packageRootStamp) {
                    app.packageBytes = saved->bytes;
                    app.packageSizeKnown = true;
                    app.packageSizeCached = true;
                }
            }
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
                SceIoStat st{};
                row.exists = exists(path, &st);
                if (row.exists) {
                    row.rootStamp = rootFingerprint(st);
                    // The disk index never supersedes canonical JSON paths or
                    // a fresh Getstat. Cached measurements do not grant
                    // permission to delete.
                    const auto* cached = InstalledDirectoryCache::find(
                        index, app.titleId, path);
                    if (cached && cached->sizeKnown && row.rootStamp &&
                        cached->rootStamp == row.rootStamp) {
                        row.bytes = cached->bytes;
                        row.sizeKnown = true;
                        row.sizeCached = true;
                    }
                    if (progress)
                        progress->rootsFound.fetch_add(1, std::memory_order_relaxed);
                }
            } else if (selected) {
                row.complete = false;
            }
            app.paths.push_back(std::move(row));
        }
        if (selected && app.alternateGameRoots) {
            // Alternate download destinations with one existing root have
            // an unambiguous deletion target; multiple roots need review.
            unsigned present = 0;
            for (const auto& path : app.paths) if (path.exists) ++present;
            if (installedDataAlternativesAmbiguous(app.alternateGameRoots,
                                                   present))
                app.unresolved = true;
        }
        if (app.unresolved) app.blocked = true;
    }
    // Cache the active installed-app collision hint once, not per UI frame.
    for (auto& app : result.apps)
        if (onlyTitleId.empty() || app.titleId == onlyTitleId) {
            app.activeShared = installedDataHasActiveShare(result, app);
            app.catalogShared = installedDataHasCatalogShare(result, app);
        }
    // Do not mark a Game Files directory as shared/protected simply
    // because another catalog entry declares the same path. Declarations
    // are not evidence of multiple installations. In particular this must
    // NEVER affect read-only measurement. Destructive actions have their
    // own real-installation check in removeExternal().

    if (progress) progress->phase.store(2, std::memory_order_release);
    // Phase 2 is deliberately OPTIONAL. An ordinary menu open only performs
    // bounded Getstat checks of roots; it never recursively walks game files.
    // The user must choose Measure size on an individual Homebrew.
    std::unordered_map<std::string, std::pair<uint64_t, bool>> measured;
    for (auto& app : result.apps) {
        if (!onlyTitleId.empty() && app.titleId != onlyTitleId) continue;
        if (ctx.shouldStop()) {
            result.complete = false;
            // A read-only size timeout cannot make a verified root unsafe.
            // Keep deletion safety and measurement completeness independent.
            for (auto& path : app.paths) path.complete = false;
            continue;
        }
        for (auto& path : app.paths) {
            if (!safeExternalDataPath(path.path) || !path.exists) continue;
            if (measureSizes) {
                const auto previous = measured.find(path.path);
                if (previous != measured.end()) {
                    path.bytes = previous->second.first;
                    path.complete = previous->second.second;
                } else {
                    SceIoStat st{};
                    if (!exists(path.path, &st)) {
                        path.complete = false;
                    } else {
                        uint32_t entries = 0;
                        path.bytes = 0; // never preserve a partial cached value
                        path.complete = scanTree(
                            path.path, st, 0, entries, path.bytes, ctx);
                    }
                    measured.emplace(path.path,
                                     std::make_pair(path.bytes, path.complete));
                }
                path.sizeKnown = path.complete;
                path.sizeCached = false;
            }
            if (!path.complete) result.complete = false;
            if (path.sizeKnown) app.bytes = sum(app.bytes, path.bytes);
            if (ctx.stopped) break;
        }
        // Measure the package only after its external roots: still one
        // Homebrew at a time, using the same bounded cooperative worker.
        // No recursive package walk occurs on ordinary menu entry.
        if (app.packagePresent && measureSizes && !ctx.shouldStop()) {
            const std::string packageRoot = "ux0:app/" + app.titleId;
            SceIoStat packageStat{};
            uint32_t entries = 0;
            uint64_t measuredBytes = 0;
            if (exists(packageRoot, &packageStat) &&
                scanTree(packageRoot, packageStat, 0, entries,
                         measuredBytes, ctx)) {
                app.packageBytes = measuredBytes;
                app.packageSizeKnown = true;
                app.packageSizeCached = false;
                app.packageRootStamp = rootFingerprint(packageStat);
            } else {
                app.packageBytes = 0;
                app.packageSizeKnown = false;
                app.packageSizeCached = false;
                result.complete = false;
            }
        }
        if (ctx.stopped) {
            result.complete = false;
            for (auto& path : app.paths)
                if (path.exists && measureSizes && !path.sizeKnown)
                    path.complete = false;
        } else if (progress) {
            progress->appsCompleted.fetch_add(1, std::memory_order_relaxed);
        }
    }
    // Only sum measured (or previously measured) roots and mark unknown totals
    // explicitly. A cached measurement is historical, not a current proof.
    result.totalExternalBytes = knownExternalBytes(result);
    result.totalPackageBytes = knownPackageBytes(result);
    for (const auto& app : result.apps) {
        if (!onlyTitleId.empty() && app.titleId != onlyTitleId) continue;
        for (const auto& path : app.paths)
            if (path.exists && !path.sizeKnown)
                result.totalSizeKnown = false;
    }
    // Reconcile the directory index with canonical catalog declarations.
    // Unselected apps keep prior observations; selected roots are rechecked.
    std::vector<CachedDirectory> next;
    next.reserve(index.size() + 8);
    for (const auto& app : result.apps) {
        const bool selected = onlyTitleId.empty() || app.titleId == onlyTitleId;
        if (validInstalledTitleId(app.titleId)) {
            const std::string packageRoot = "ux0:app/" + app.titleId;
            if (selected && app.packagePresent && app.packageRootStamp) {
                next.push_back({app.titleId, packageRoot, app.packageRootStamp,
                    app.packageSizeKnown ? app.packageBytes : 0,
                    app.packageSizeKnown});
            } else if (!selected) {
                const auto* saved = InstalledDirectoryCache::find(
                    index, app.titleId, packageRoot);
                if (saved) next.push_back(*saved);
            }
        }
        for (const auto& path : app.paths) {
            if (!safeExternalDataPath(path.path) ||
                !validInstalledTitleId(app.titleId)) continue;
            if (selected) {
                if (!path.exists || !path.complete) continue;
                next.push_back({app.titleId, path.path, path.rootStamp,
                    path.sizeKnown ? path.bytes : 0, path.sizeKnown});
            } else {
                const auto* saved = InstalledDirectoryCache::find(
                    index, app.titleId, path.path);
                if (saved) next.push_back(*saved);
            }
        }
    }
    if (!ctx.stopped) InstalledDirectoryCache::save(next);
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
            if (path.exists && path.sizeKnown &&
                safeExternalDataPath(path.path))
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

uint64_t InstalledDataManager::knownPackageBytes(
    const InstalledDataSnapshot& snapshot) {
    uint64_t total = 0;
    std::unordered_set<std::string> counted;
    for (const auto& app : snapshot.apps) {
        if (!app.packagePresent || !app.packageSizeKnown ||
            !validInstalledTitleId(app.titleId) ||
            !counted.insert(app.titleId).second) continue;
        total = sum(total, app.packageBytes);
    }
    return total;
}

void InstalledDataManager::invalidateCachedTitle(const std::string& titleId) {
    InstalledDirectoryCache::invalidateTitle(titleId);
}
void InstalledDataManager::invalidateCachedPackageTitle(
    const std::string& titleId) {
    InstalledDirectoryCache::invalidatePackageTitle(titleId);
}

bool InstalledDataManager::prepareExternalRemoval(
    const InstalledDataSnapshot& snapshot, const std::string& titleId,
    InstalledDataOperationProgress* progress, bool confirmedCatalogShare) {
    const InstalledDataApp* target = nullptr;
    // Show the preflight stage immediately, including when ownership checks
    // reject the request. Never falsely label this as uninstalling the APP.
    if (progress) progress->phase.store(5, std::memory_order_release);
    if (!validateExternalRemoval(snapshot, titleId, target,
                                 confirmedCatalogShare)) return false;
    uint64_t total = 0;
    for (const auto& path : target->paths) {
        if (!path.exists) continue;
        SceIoStat st{};
        if (!exists(path.path, &st)) return false;
        uint32_t entries = 0;
        uint64_t bytesIgnored = 0;
        ScanContext ctx; // no time budget for the complete pre-delete count
        if (!scanTree(path.path, st, 0, entries, bytesIgnored, ctx))
            return false; // do not start a deletion using an incomplete count
        total = sum(total, entries);
    }
    if (!total) return false;
    if (progress)
        progress->entriesTotal.store(total, std::memory_order_release);
    return true;
}

bool InstalledDataManager::removeExternal(const InstalledDataSnapshot& snapshot,
                                          const std::string& titleId,
                                          InstalledDataOperationProgress* progress,
                                          bool confirmedCatalogShare) {
    const InstalledDataApp* target = nullptr;
    // Recheck ownership and existence AFTER pre-count and package operations.
    if (!validateExternalRemoval(snapshot, titleId, target,
                                 confirmedCatalogShare)) return false;
    if (progress) {
        uint32_t roots = 0;
        for (const auto& path : target->paths)
            if (path.exists) ++roots;
        progress->phase.store(2, std::memory_order_release);
        progress->rootsTotal.store(roots, std::memory_order_release);
    }
    bool removed = true;
    // After any destructive attempt the old size must never be displayed
    // again, even when a partial filesystem deletion fails.
    invalidateCachedTitle(titleId);
    // Another Homebrew may have cached the same physical directory. If it
    // changes here, invalidate every overlapping historical measurement.
    for (const auto& other : snapshot.apps) {
        if (other.titleId == titleId) continue;
        for (const auto& q : other.paths) {
            if (!safeExternalDataPath(q.path)) continue;
            bool overlap = false;
            for (const auto& p : target->paths)
                if (p.exists && installedPathsOverlap(p.path, q.path)) {
                    overlap = true;
                    break;
                }
            if (overlap) {
                invalidateCachedTitle(other.titleId);
                break;
            }
        }
    }
    for (const auto& path : target->paths) {
        if (!path.exists) continue;
        uint32_t count = 0;
        if (!eraseTree(path.path, 0, count, progress)) {
            diagnostics::log("[InstalledData] unable to remove " + path.path);
            removed = false;
        } else if (progress) {
            progress->rootsCompleted.fetch_add(1, std::memory_order_release);
        }
    }
    return removed;
}

bool InstalledDataManager::uninstallPackage(
    const std::string& titleId, InstalledDataOperationProgress* progress) {
    if (progress) progress->phase.store(1, std::memory_order_release);
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
        if (progress)
            progress->promoterPolls.fetch_add(1, std::memory_order_relaxed);
        if (state == 0) { idle = true; break; }
        sceKernelDelayThread(10000);
    }
    if (progress) progress->phase.store(3, std::memory_order_release);
    scePromoterUtilityExit();
    // Don't announce success merely because an API call was accepted.
    const bool removed = del >= 0 && idle &&
        !exists(std::string("ux0:app/") + titleId + "/sce_sys/param.sfo");
    if (removed) InstalledDirectoryCache::invalidatePackageTitle(titleId);
    return removed;
}

}} // namespace psvitaalive::storage

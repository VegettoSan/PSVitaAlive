#include "storage/installed_data_manager.hpp"
#include "storage/installed_data_policy.hpp"
#include "storage/game_media_metadata.hpp"
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
    bool gameMedia = false;
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
        (ctx.gameMedia && safeGameMediaPath(p,(st.st_mode & SCE_S_IFMT)==SCE_S_IFDIR)) ||
        (p.compare(0, 8, "ux0:app/") == 0 &&
         validInstalledTitleId(p.substr(8)));
    if (depth > MAX_DEPTH || ++entries > MAX_ENTRIES ||
        !safeRoot ||
        ctx.shouldStop()) return false;
    if(ctx.gameMedia && (st.st_mode & SCE_S_IFMT)!=SCE_S_IFDIR &&
       (st.st_mode & SCE_S_IFMT)!=SCE_S_IFREG)return false;
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
            if(ctx.gameMedia && (entry.d_stat.st_mode & SCE_S_IFMT)!=SCE_S_IFREG){complete=false;break;}
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
               uint32_t& count, InstalledDataOperationProgress* progress,bool gameMedia=false) {
    if ((depth == 0 && !safeExternalDataPath(path) && !gameMedia) ||
        depth > MAX_DEPTH || ++count > MAX_ENTRIES) return false;
    SceIoStat st{};
    if (!exists(path, &st)) return !gameMedia;
    if(gameMedia && ((st.st_mode & SCE_S_IFMT)!=SCE_S_IFDIR && (st.st_mode & SCE_S_IFMT)!=SCE_S_IFREG))return false;
    if(gameMedia && depth==0 && !safeGameMediaPath(path,(st.st_mode & SCE_S_IFMT)==SCE_S_IFDIR))return false;
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
            if (!eraseTree(path + "/" + e.d_name, depth+1, count, progress,gameMedia)) {
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
    std::unordered_set<std::string> gamePaths;
    uint64_t mediaTotal=0;
    for (const auto& app : snapshot.apps)
        for (const auto& path : app.paths) {
            if(!path.exists || !path.sizeKnown)continue;
            if(app.mediaRow) {
                if(safeGameMediaPath(path.path,app.mediaDirectory) && gamePaths.insert(path.path).second)
                    mediaTotal=sum(mediaTotal,path.bytes);
            } else if(safeExternalDataPath(path.path))paths.push_back(&path);
        }
    // Count parents before children, and count a shared root only once.
    std::sort(paths.begin(), paths.end(),
              [](const InstalledDataPath* a, const InstalledDataPath* b) {
                  return a->path.size() == b->path.size()
                      ? a->path < b->path : a->path.size() < b->path.size();
              });
    std::vector<std::string> counted;
    uint64_t total = mediaTotal;
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

namespace {
bool regular(const SceIoStat& s) {return (s.st_mode & SCE_S_IFMT)==SCE_S_IFREG;}
bool directory(const SceIoStat& s) {return (s.st_mode & SCE_S_IFMT)==SCE_S_IFDIR;}
uint64_t mediaFingerprint(const SceIoStat& s) {
    const uint64_t stamp=rootFingerprint(s);
    return stamp?stamp ^ (static_cast<uint64_t>(s.st_size)*1099511628211ULL):0;
}
media::Metadata probeMedia(const std::string& path,const SceIoStat& st) {
    if(!regular(st)||st.st_size<=0)return {};
    const SceUID fd=sceIoOpen(path.c_str(),SCE_O_RDONLY,0);
    if(fd<0)return {};
    media::Read read=[&](uint64_t offset,void* data,size_t n) {
        return sceIoLseek(fd,static_cast<SceOff>(offset),SCE_SEEK_SET)==static_cast<SceOff>(offset) &&
            sceIoRead(fd,data,static_cast<SceSize>(n))==static_cast<int>(n);
    };
    const auto result=media::probe(read,static_cast<uint64_t>(st.st_size));
    sceIoClose(fd);return result;
}
std::string gamePbp(const std::string& path,SceIoStat& st) {
    const std::string upper=path+"/EBOOT.PBP",lower=path+"/eboot.pbp";
    if(exists(upper,&st)&&regular(st))return upper;
    if(exists(lower,&st)&&regular(st))return lower;
    return {};
}
std::string filenameId(std::string name) {
    const auto dot=name.find_last_of('.');if(dot!=std::string::npos)name.resize(dot);
    auto id=media::idOf(name);
    if(id.empty()&&name.size()>=11&&name.back()==']'&&name[name.size()-11]=='[')
        id=media::idOf(name.substr(name.size()-10,9));
    return id;
}
}
InstalledDataSnapshot InstalledDataManager::scanGames(const std::vector<GameCatalogIdentity>& catalog,
        unsigned mode,InstalledDataScanProgress* progress,uint64_t budgetUs) {
    InstalledDataSnapshot out;
    ScanContext ctx;ctx.progress=progress;ctx.startUs=sceKernelGetProcessTimeWide();ctx.budgetUs=budgetUs;
    if(progress)progress->phase.store(1,std::memory_order_release);
    // Build one lookup map; ambiguous aliases cannot identify any row.
    std::unordered_map<std::string,const GameCatalogIdentity*> byId;
    auto add=[&](const std::string& raw,const GameCatalogIdentity& app) {
        const auto id=media::idOf(raw);if(id.empty())return;
        auto inserted=byId.emplace(id,&app);
        if(!inserted.second && inserted.first->second!=&app)inserted.first->second=nullptr;
    };
    for(const auto& app:catalog) {
        add(app.titleId,app);
        size_t at=0;
        while(at<app.alternateIds.size()){
            const auto end=app.alternateIds.find('|',at);
            add(app.alternateIds.substr(at,end==std::string::npos?end:end-at),app);
            if(end==std::string::npos)break;
            at=end+1;
        }
    }
    auto lookup=[&](const std::string& id)->const GameCatalogIdentity* {
        const auto it=byId.find(id);return it==byId.end()?nullptr:it->second;
    };
    if(mode==1) {
        // Only exact catalog matches are authorized as Vita games. Do not
        // classify unrelated Homebrew/system applications by folder alone.
        std::vector<CachedDirectory> cache;InstalledDirectoryCache::load(cache);
        const SceUID d=sceIoDopen("ux0:app");
        if(d<0)out.complete=false;
        else {
            SceIoDirent e{};int rc=0;
            while(!ctx.shouldStop()&&(rc=sceIoDread(d,&e))>0) {
                const std::string id=e.d_name;
                const auto* match=lookup(id);
                if(!match||!validInstalledTitleId(id)||id==CLIENT_TID)continue;
                const auto path="ux0:app/"+id;
                SceIoStat st{},sfoStat{};
                if(!exists(path,&st)||!directory(st)||!exists(path+"/sce_sys/param.sfo",&sfoStat)||!regular(sfoStat))continue;
                InstalledDataApp row;row.appId=match->appId;row.name=match->name;row.icon=match->icon;
                row.titleId=id;row.packagePresent=true;row.packageRootStamp=rootFingerprint(st);
                const auto* saved=InstalledDirectoryCache::find(cache,id,path);
                if(saved&&saved->sizeKnown&&row.packageRootStamp&&saved->rootStamp==row.packageRootStamp) {
                    row.packageBytes=saved->bytes;row.packageSizeKnown=true;row.packageSizeCached=true;
                }
                out.apps.push_back(std::move(row));
                if(progress)progress->rootsFound.fetch_add(1,std::memory_order_relaxed);
            }
            if(rc<0||ctx.stopped)out.complete=false;
            sceIoDclose(d);
        }
    } else if(mode==2||mode==3) {
        for(const char* mount:{"ux0:","uma0:"})for(const char* section:{"ISO/","PSP/GAME/"}) {
            if(ctx.shouldStop())break;
            const std::string base=std::string(mount)+"pspemu/"+section;
            const SceUID d=sceIoDopen(base.c_str());
            if(d<0){if(exists(base))out.complete=false;continue;}
            SceIoDirent e{};int rc=0;
            while(!ctx.shouldStop()&&(rc=sceIoDread(d,&e))>0) {
                const std::string name=e.d_name;
                if(!validInstalledChildName(name))continue;
                const std::string path=base+name;
                SceIoStat st{};
                if(!exists(path,&st)||(!regular(st)&&!directory(st))||!safeGameMediaPath(path,directory(st)))continue;
                SceIoStat payload=st;
                const std::string file=directory(st)?gamePbp(path,payload):path;
                if(file.empty())continue; // not a verified game-shaped folder
                const auto m=probeMedia(file,payload);
                if((mode==2 && m.platform==media::Platform::PS1)||(mode==3&&m.platform==media::Platform::PSP))continue;
                // ISO/CSO are normally PSP; an unreadable image stays unknown.
                const auto* primary=m.platform==media::Platform::Unknown?nullptr:lookup(m.contentId.empty()?m.id:m.contentId);
                const auto* secondary=m.platform==media::Platform::Unknown?nullptr:lookup(m.id);
                const auto* match=primary?primary:secondary;
                if(primary&&secondary&&primary!=secondary)match=nullptr;
                // Filename is only a hint when no reliable internal identity exists.
                const auto hint=filenameId(name);
                if(!match && m.id.empty()&&m.contentId.empty()&&m.platform!=media::Platform::Unknown)match=lookup(hint);
                InstalledDataApp row;
                row.titleId=path;row.mediaRow=true;row.mediaDirectory=directory(st);
                row.mediaPlatform=static_cast<uint8_t>(m.platform);
                row.mediaId=!m.contentId.empty()?m.contentId:m.id;
                row.mediaIdentified=match!=nullptr;
                row.name=match?match->name:(!m.title.empty()?m.title:name);
                if(match){row.appId=match->appId;row.icon=match->icon;}
                row.mediaStamp=mediaFingerprint(st);row.mediaFileBytes=st.st_size>0?static_cast<uint64_t>(st.st_size):0;
                row.mediaPayloadStamp=mediaFingerprint(payload);
                row.mediaFormat=row.mediaDirectory?"PBP / Folder":(std::string(section)=="ISO/"?(name.size()>=4&&
                    (name[name.size()-3]=='c'||name[name.size()-3]=='C')?"CSO":"ISO"):"PBP");
                InstalledDataPath entry;entry.path=path;entry.exists=true;entry.rootStamp=row.mediaStamp;
                entry.sizeKnown=!row.mediaDirectory;entry.bytes=entry.sizeKnown?row.mediaFileBytes:0;
                row.bytes=entry.bytes;row.paths.push_back(std::move(entry));
                // Allow removing a single unrecognized ISO/PBP file, but never a
                // directory without a structurally identified PSP/PS1 payload.
                row.blocked=!row.mediaStamp || (row.mediaDirectory && (!row.mediaPayloadStamp || m.platform==media::Platform::Unknown));
                out.apps.push_back(std::move(row));
                if(progress)progress->rootsFound.fetch_add(1,std::memory_order_relaxed);
                if(out.apps.size()>=4096){ctx.stopped=true;break;}
            }
            if(rc<0||ctx.stopped)out.complete=false;
            sceIoDclose(d);
        }
    }
    out.totalExternalBytes=knownExternalBytes(out);
    out.totalPackageBytes=knownPackageBytes(out);
    for(const auto& app:out.apps)if(!installedDataListSizeKnown(app))out.totalSizeKnown=false;
    if(ctx.stopped){out.complete=false;out.totalSizeKnown=false;}
    if(progress) {
        progress->appsTotal.store(static_cast<uint32_t>(out.apps.size()),std::memory_order_relaxed);
        progress->appsCompleted.store(static_cast<uint32_t>(out.apps.size()),std::memory_order_relaxed);
        progress->truncated.store(!out.complete,std::memory_order_relaxed);
        progress->phase.store(3,std::memory_order_release);
    }
    return out;
}
InstalledDataApp InstalledDataManager::measureGame(InstalledDataApp app,InstalledDataScanProgress* progress,uint64_t budgetUs) {
    ScanContext ctx;ctx.progress=progress;ctx.startUs=sceKernelGetProcessTimeWide();ctx.budgetUs=budgetUs;ctx.gameMedia=app.mediaRow;
    if(progress){progress->appsTotal.store(1);progress->phase.store(2);}
    if(app.mediaRow && app.paths.size()==1) {
        auto& path=app.paths[0];SceIoStat st{};uint32_t count=0;uint64_t bytes=0;
        path.exists=exists(path.path,&st);
        path.sizeKnown=path.exists&&mediaFingerprint(st)==app.mediaStamp&&scanTree(path.path,st,0,count,bytes,ctx);
        path.bytes=path.sizeKnown?bytes:0;path.sizeCached=false;app.bytes=path.bytes;
        if(!path.sizeKnown)path.complete=false;
    } else if(app.packagePresent && validInstalledTitleId(app.titleId)) {
        const std::string path="ux0:app/"+app.titleId;SceIoStat st{};uint32_t count=0;uint64_t bytes=0;
        app.packageSizeKnown=exists(path,&st)&&directory(st)&&scanTree(path,st,0,count,bytes,ctx);
        app.packageBytes=app.packageSizeKnown?bytes:0;app.packageSizeCached=false;
    }
    if(progress){progress->appsCompleted.store(1);progress->phase.store(3);}
    return app;
}
bool InstalledDataManager::removeGameMedia(const InstalledDataApp& app,InstalledDataOperationProgress* progress) {
    if(progress)progress->phase.store(5,std::memory_order_release);
    if(!installedDataCanOfferExternalDelete(app)||!app.mediaRow)return false;
    const auto& path=app.paths[0].path;
    auto revalidate=[&](SceIoStat& st) {
        if(!exists(path,&st)||mediaFingerprint(st)!=app.mediaStamp||
           directory(st)!=app.mediaDirectory||(!directory(st)&&!regular(st)))return false;
        if(app.mediaDirectory){SceIoStat payload{};const auto file=gamePbp(path,payload);
            if(file.empty()||mediaFingerprint(payload)!=app.mediaPayloadStamp||
               probeMedia(file,payload).platform==media::Platform::Unknown)return false;}
        return true;
    };
    SceIoStat st{};if(!revalidate(st))return false;
    ScanContext ctx;ctx.gameMedia=true;uint32_t count=0;uint64_t bytes=0;
    if(progress)progress->currentRootIndex.store(0);
    if(!scanTree(path,st,0,count,bytes,ctx)||!count||!revalidate(st))return false;
    if(progress){progress->entriesTotal.store(count);progress->rootsTotal.store(1);progress->phase.store(2,std::memory_order_release);}
    uint32_t removed=0;
    const bool erased=eraseTree(path,0,removed,progress,true);
    // Verify via the still-accessible parent; a missing mount is not success.
    const auto slash=path.find_last_of('/');
    const SceUID parent=sceIoDopen(path.substr(0,slash).c_str());
    bool absent=parent>=0;
    if(parent>=0) {
        SceIoDirent e{};int rc=0;
        const auto name=path.substr(slash+1);
        while((rc=sceIoDread(parent,&e))>0)if(name==e.d_name){absent=false;break;}
        if(rc<0)absent=false;
        sceIoDclose(parent);
    }
    const bool ok=erased && absent;
    if(progress){if(ok)progress->rootsCompleted.store(1);progress->currentRootIndex.store(UINT32_MAX);progress->phase.store(3);}
    return ok;
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
    for (size_t i = 0; i < target->paths.size(); ++i) {
        const auto& path = target->paths[i];
        if (!path.exists) continue;
        if (progress) progress->currentRootIndex.store(static_cast<uint32_t>(i), std::memory_order_release);
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
    if (progress) {
        progress->currentRootIndex.store(UINT32_MAX, std::memory_order_release);
        progress->entriesTotal.store(total, std::memory_order_release);
    }
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
    for (size_t i = 0; i < target->paths.size(); ++i) {
        const auto& path = target->paths[i];
        if (!path.exists) continue;
        if (progress) progress->currentRootIndex.store(static_cast<uint32_t>(i), std::memory_order_release);
        uint32_t count = 0;
        if (!eraseTree(path.path, 0, count, progress)) {
            diagnostics::log("[InstalledData] unable to remove " + path.path);
            removed = false;
        } else if (progress) {
            progress->rootsCompleted.fetch_add(1, std::memory_order_release);
        }
    }
    if (progress) progress->currentRootIndex.store(UINT32_MAX, std::memory_order_release);
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

#!/usr/bin/env python3
"""Source integration regression: empty catalog recovery, no destructive reset."""
from pathlib import Path

root = Path("Client PSVitaAlive")
manager = (root / "source/catalog/catalog_manager.cpp").read_text(encoding="utf-8")
main = (root / "source/main.cpp").read_text(encoding="utf-8")
header = (root / "include/ui/full_catalog_screen.hpp").read_text(encoding="utf-8")

request = manager.split("bool CatalogManager::request(", 1)[1].split("CatalogManager::Status", 1)[0]
worker = manager.split("int CatalogManager::workerMain()", 1)[1]
hard = manager.split("bool CatalogManager::loadCatalog(", 1)[1].split("void CatalogManager::publishReady", 1)[0]
soft = manager.split("bool CatalogManager::trySoftNetworkRefresh(", 1)[1].split("bool CatalogManager::loadCatalog(", 1)[0]
promote = manager.split("bool promoteCatalogCache(", 1)[1].split("// Compare identical remote payload", 1)[0]

# Recovery bypasses all shortcuts, not just the ETag comparison.
assert "if (!forceNetwork && idx" in request
assert "forceNetworkPending_ = forceNetwork" in request
assert "if (!forceNetwork && tryLoadDiskCache" in worker
assert "if (haveCache && !forceNetwork)" in hard
assert "sceIoRemove(temp.c_str());" in hard
assert "url, temp, 0," in hard

# Only validated non-empty downloads can replace the file. Keep the old copy
# until replacement succeeds and allow the validated payload in RAM on failure.
for loader in (hard, soft):
    assert loader.index("CatalogParser::parseFile(temp, fresh) && !fresh.empty()") < loader.index("promoteCatalogCache(temp, path)")
    assert "disk save failed; using validated" in loader
    assert "outItems = std::move(fresh);" in loader
    assert "sceIoRemove(path.c_str())" not in loader
assert "sceIoRename(path.c_str(), backup.c_str())" in promote
assert "sceIoRename(backup.c_str(), path.c_str())" in promote
assert "SCE_S_IFDIR" in promote
assert "restored previous catalog after interrupted cache replacement" in manager

# One worker-level retry, no retry for forced or superseded requests.
assert worker.count("loadCatalog(catalog, loaded, true)") == 1
assert "if (!ok && !forceNetwork && !stopping_ && !isStale())" in worker
assert "if (!stopping_ && !isStale())" in worker

# The UI only recovers a truly empty catalog, never an empty search result.
assert "hasUnfilteredCatalogItems() const { return !allItems_.empty(); }" in header
assert "catalogs.takeReady(ready,readyCatalog) && !ready.empty()" in main
assert "!screen.hasUnfilteredCatalogItems() && !catalogs.isBusy()" in main
assert "!catalogs.actionBlockedByCatalog() &&" in main
assert "!installer.busy() && !installer.queueProtectionActive() && !screen.catalogWorkExcluded()" in main
assert "!emptyCatalogRecoveryRequested[recoveryIndex]" in main
assert "emptyCatalogRecoveryRequested[recoveryIndex] = true;" in main
assert "catalogs.request(recoveryCatalog, true)" in main
assert "csBusy.state == psvitaalive::CatalogManager::State::Failed" in main

# Applying a downloaded catalog must not issue another HEAD request.
# Metadata must identify the downloaded bytes, not a later server revision.
for loader in (hard, soft):
    after_download = loader.split("const HttpResult result = http.downloadToFile(", 1)[1]
    assert "fetchRemoteValidators(" not in after_download
    assert "http.lastEtag()" in after_download
    assert "http.lastModified()" in after_download
    assert "[CatalogPerf] apply catalog=" in after_download

http = (root / "source/network/http_client.cpp").read_text(encoding="utf-8")
parallel = http.split("lastSegmentedZip_ = parallel == ParallelArchiveOutcome::Segmented;", 1)[1].split("return HttpResult::Ok;", 1)[0]
assert "if (!isArchive)" in parallel
assert "lastEtag_ = rangeProbe.etag;" in parallel
assert "lastModified_ = rangeProbe.lastModified;" in parallel


# Priority uses the worker lifecycle, not the decorative progress bar.
guard = manager.split("bool CatalogManager::actionBlockedByCatalog()", 1)[1].split("bool CatalogManager::request(", 1)[0]
assert "requestPending_ || workerOperationActive_ || readyPending_" in guard
assert "sceKernelLockMutex" in guard and "sceKernelUnlockMutex" in guard
assert "setCatalogActionBlockCallback" in main
ui = (root / "source/ui/full_catalog_screen.cpp").read_text(encoding="utf-8")
for name in ("activatePrimaryAction", "applyPspLiveAreaPluginGateOrRun", "openInstallAllWizard",
             "triggerSelfUpdateAction", "installAllStartQueue"):
    body = ui.split("void FullCatalogScreen::" + name + "(", 1)[1].split("{", 1)[1]
    assert "guardCatalogAction()" in body[:220], name
launch = ui.split("void FullCatalogScreen::activatePrimaryAction(", 1)[1].split("bool FullCatalogScreen::itemSupportsInstallAll", 1)[0]
assert launch.index("guardCatalogAction()") < launch.index("sceAppMgrLaunchAppByUri")
switch = ui.split("void FullCatalogScreen::changeCatalog(", 1)[1].split("void FullCatalogScreen::", 1)[0]
# A rejected request must leave the active payload and tab unchanged.
accept = switch.index("!catalogChange_(n, previous, allItems_)")
assert switch.index("catalogWorkExcluded()") < accept
assert accept < switch.index("state_.catalog=n;")
assert accept < switch.index("std::vector<CatalogItem>().swap(allItems_)")
assert accept < switch.index("releaseTextures()")
assert "catalog_memory::catalogSwitchBlocked(" in request
assert request.index("catalog_memory::catalogSwitchBlocked(") < request.index("catalog_memory::retainForSelection(")
assert request.index("catalog_memory::catalogSwitchBlocked(") < request.index("++requestGeneration_")
assert soft.index("releaseInactiveCachesLocked();") < soft.index("http.downloadToFile(")
assert 'L("CATALOG_ACTION_WAIT"), 5600' in ui
assert "catalogPriorityToastUntilMs_" in ui
for lang in ("en", "es", "fr", "de", "it", "pt-BR", "pt-PT", "ru"):
    content = (root / "assets/lang" / (lang + ".lang")).read_text(encoding="utf-8")
    matches = [line for line in content.splitlines() if line.startswith("CATALOG_ACTION_WAIT=")]
    assert len(matches) == 1 and matches[0].split("=", 1)[1].strip(), lang

print("Catalog recovery and priority source integration checks passed")

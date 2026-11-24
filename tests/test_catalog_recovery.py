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
assert "!emptyCatalogRecoveryRequested[recoveryIndex]" in main
assert "emptyCatalogRecoveryRequested[recoveryIndex] = true;" in main
assert "catalogs.request(recoveryCatalog, true)" in main
assert "csBusy.state == psvitaalive::CatalogManager::State::Failed" in main

print("Catalog recovery source integration checks passed")

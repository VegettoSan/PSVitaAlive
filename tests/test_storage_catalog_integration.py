from pathlib import Path
root = Path("Client PSVitaAlive")
ui = (root / "source/ui/full_catalog_screen.cpp").read_text()
inspector = (root / "source/ui/installed_data_screen.inc").read_text()
manager = (root / "source/storage/installed_data_manager.cpp").read_text()
maintenance = (root / "source/storage/storage_maintenance.cpp").read_text()
catalog = (root / "source/catalog/catalog_manager.cpp").read_text()
assert "storageGameMode_=static_cast<unsigned>(state_.catalog);" in ui
assert "StorageMaintenance::scan(true, homebrew)" in ui
assert "if (includeSavedZip) scanSaved(snap);" in maintenance
assert "storageCleanupItemAllowed(static_cast<CatalogType>(self->storageGameMode_), item.savedZip)" in ui
assert "storageCatalogCleanupAction(scope, action)" in ui
assert "if (storageShowsSavedZips(static_cast<CatalogType>(storageGameMode_)))" in ui
assert "InstalledDataManager::scanGames(" in ui
assert "storageInstalledSectionKey(static_cast<CatalogType>(installedDataGameMode_))" in inspector
assert "InstalledDataManager::scanGames(" in manager
assert "DirectoryCacheScope::Vita" in manager
assert "readyItems_=std::move(items);" in catalog
assert 'publishReady(catalog, fresh, "Catalog updated", false, false);' in catalog
assert catalog.index("releaseRefreshCopyLocked(catalog);") < catalog.index("CatalogParser::parseFile(temp, fresh)")
for lang in ("en","es","de","fr","it","pt-BR","pt-PT","ru"):
    lines = (root / "assets/lang" / (lang + ".lang")).read_text().splitlines()
    for key in ("ID_SECTION_VITA","ID_SECTION_PSP","ID_SECTION_PS1","STORAGE_VISIBLE_TOTAL","STORAGE_CONFIRM_TEMP_ONLY"):
        assert sum(x.startswith(key+"=") and bool(x.split("=",1)[1]) for x in lines)==1,(lang,key)
print("Contextual storage, protected ZIP cleanup and catalog RAM integration OK")

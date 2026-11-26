from pathlib import Path
root = Path("Client PSVitaAlive")
ui = (root / "source/ui/full_catalog_screen.cpp").read_text()
inspector = (root / "source/ui/installed_data_screen.inc").read_text()
manager = (root / "source/storage/installed_data_manager.cpp").read_text()
index = (root / "include/ui/installed_game_index.hpp").read_text()
maintenance = (root / "source/storage/storage_maintenance.cpp").read_text()
assert "storageCatalog_ = state_.catalog;" in ui
assert "StorageMaintenance::scan(true, homebrew)" in ui
assert "if (includeSavedZip) scanSaved(snap);" in maintenance
assert "storageCleanupItemAllowed(self->storageCatalog_, item.savedZip)" in ui
assert "storageCatalogCleanupAction(storageCatalog_, action)" in ui
assert "index.refresh(adrenaline, adrenaline)" in ui
assert "storagePathsFor(app.mediaKind, app.titleId)" in ui
assert "alternateTitleIds" in inspector
assert "budget, deepMeasure, commercial" in inspector
assert "storageInstalledSectionKey(storageCatalog_)" in inspector
assert "storagePathMatches(target->mediaKind" in manager
assert "recognizedIsoFile(path)" in index
assert "pbpStorageKind(pbp) != kind" in index
assert "if (!ctx.stopped && !commercialScan)" in manager
for lang in ("en","es","de","fr","it","pt-BR","pt-PT","ru"):
    lines = (root / "assets/lang" / (lang + ".lang")).read_text().splitlines()
    for key in ("ID_SECTION_VITA","ID_SECTION_PSP","ID_SECTION_PS1","STORAGE_VISIBLE_TOTAL","STORAGE_CONFIRM_TEMP_ONLY","ID_GAME_TOTAL","ID_GAME_LIST_HINT","ID_GAME_NONE","ID_GAME_DELETE_PACKAGE","ID_GAME_DELETE_MEDIA","ID_GAME_MEDIA_IMPACT","ID_GAME_MEDIA_PROGRESS","ID_GAME_DISCOVERING","ID_GAME_PACKAGE_IMPACT","ID_GAME_PACKAGE_KEEP","ID_GAME_BOTH_IMPACT"):
        assert sum(x.startswith(key+"=") and bool(x.split("=",1)[1]) for x in lines)==1,(lang,key)
print("Active storage catalog, localization and deletion boundaries OK")

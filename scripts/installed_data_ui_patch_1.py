from pathlib import Path
p=Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
s=p.read_text(encoding="utf-8")
def change(a,b):
 global s
 assert s.count(a)==1, (s.count(a), a[:85])
 s=s.replace(a,b,1)

change('#include "storage/storage_manager.hpp"',
 '#include "storage/storage_manager.hpp"\n#include "storage/installed_data_policy.hpp"')
change('    installStatusCache_.clear();\n    allItems_=std::move(items);\n    sortItemsByDate(allItems_);',
 '    installStatusCache_.clear();\n    allItems_=std::move(items);\n    if(state_.catalog==CatalogType::Homebrew && !installedDataVisible_) rebuildInstalledDataCandidates();\n    sortItemsByDate(allItems_);')
change('    // Existing GPU textures stay cached: identical URLs remain visible.\n    // prepareVisibleTextures() evicts textures that become off-screen.\n    allItems_=std::move(items);\n    sortItemsByDate(allItems_);',
 '    // Existing GPU textures stay cached: identical URLs remain visible.\n    // prepareVisibleTextures() evicts textures that become off-screen.\n    allItems_=std::move(items);\n    if(state_.catalog==CatalogType::Homebrew && !installedDataVisible_) rebuildInstalledDataCandidates();\n    sortItemsByDate(allItems_);')
change('    storageRows_.push_back({2,-1}); // catalog, read-only\n    storageRows_.push_back({3,-1}); // images, read-only',
 '    storageRows_.push_back({2,-1}); // catalog, read-only\n    storageRows_.push_back({3,-1}); // images, read-only\n    storageRows_.push_back({5,-1}); // installed Homebrew data, separate page')
change('    const StorageDisplayRow row = storageRows_[storageFocus_];\n    int action=-1;',
 '    const StorageDisplayRow row = storageRows_[storageFocus_];\n    if(row.kind==5){storageConfirm_=false;storagePendingAction_=-1;openInstalledDataMenu(true,"");return;}\n    int action=-1;')
change('            const char* title=row.kind==0?::psvitaalive::L("STORAGE_TEMP"):\n                row.kind==1?::psvitaalive::L("STORAGE_ZIPS"):\n                row.kind==2?::psvitaalive::L("STORAGE_CATALOG_PROTECTED"):\n                            ::psvitaalive::L("STORAGE_IMAGES_PROTECTED");',
 '            const char* title=row.kind==0?::psvitaalive::L("STORAGE_TEMP"):\n                row.kind==1?::psvitaalive::L("STORAGE_ZIPS"):\n                row.kind==2?::psvitaalive::L("STORAGE_CATALOG_PROTECTED"):\n                row.kind==3?::psvitaalive::L("STORAGE_IMAGES_PROTECTED"):\n                            ::psvitaalive::L("ID_SECTION");')
change('            const uint64_t bytes=row.kind==0?snap.temporaryBytes:\n                row.kind==1?snap.savedZipBytes:\n                row.kind==2?snap.catalogBytes:snap.imageBytes;',
 '            const uint64_t bytes=row.kind==0?snap.temporaryBytes:\n                row.kind==1?snap.savedZipBytes:\n                row.kind==2?snap.catalogBytes:\n                row.kind==3?snap.imageBytes:0;')
change('            ::psvitaalive::ui::uiDrawText(&font_,bx+bw-145,cy+29,\n                active?ACCENT:WHITE,.86f,formatBytesShort(bytes).c_str());\n            const char* action=(row.kind>=2)?::psvitaalive::L("STORAGE_PROTECTED"):\n                bytes==0?::psvitaalive::L("STORAGE_EMPTY"):\n                ::psvitaalive::L("STORAGE_CLEAR_SECTION");\n            ::psvitaalive::ui::uiDrawText(&font_,bx+20,cy+53,\n                (row.kind<2 && bytes>0)?ACCENT:DIM,.74f,action);',
 '            if(row.kind!=5) ::psvitaalive::ui::uiDrawText(&font_,bx+bw-145,cy+29,\n                active?ACCENT:WHITE,.86f,formatBytesShort(bytes).c_str());\n            const char* action=row.kind==5?::psvitaalive::L("ID_SECTION_HINT"):\n                row.kind>=2?::psvitaalive::L("STORAGE_PROTECTED"):\n                bytes==0?::psvitaalive::L("STORAGE_EMPTY"):\n                ::psvitaalive::L("STORAGE_CLEAR_SECTION");\n            ::psvitaalive::ui::uiDrawText(&font_,bx+20,cy+53,\n                (row.kind==5||(row.kind<2 && bytes>0))?ACCENT:DIM,.74f,action);')
change('void FullCatalogScreen::shutdown(){\n    if (storageWorker_ >= 0) {',
 'void FullCatalogScreen::shutdown(){\n    if(installedDataWorker_>=0){\n        sceKernelWaitThreadEnd(installedDataWorker_,nullptr,nullptr);\n        sceKernelDeleteThread(installedDataWorker_);\n        installedDataWorker_=-1;\n    }\n    if (storageWorker_ >= 0) {')
change('    pollStorageOperation();\n    flushDeferredTextureFrees();',
 '    pollStorageOperation();\n    pollInstalledDataOperation();\n    flushDeferredTextureFrees();')
change('    if(storageVisible_) drawStorageOverlay();\n    finishFrame();',
 '    if(storageVisible_) drawStorageOverlay();\n    if(installedDataVisible_) drawInstalledDataOverlay();\n    finishFrame();')
change('void FullCatalogScreen::openStorageMenu() {\n    if (storageVisible_ || installAllPhase_',
 'void FullCatalogScreen::openStorageMenu() {\n    if (installedDataVisible_ || storageVisible_ || installAllPhase_')
change('} // namespace psvitaalive::ui\n',
 '#include "installed_data_screen.inc"\n\n} // namespace psvitaalive::ui\n')
p.write_text(s,encoding="utf-8")
print("Applied storage installed data UI patch 1")

/*
 * PSVitaAlive
 *
 * Phase 9:
 * FULL_CATALOG
 * OPENING_DETAIL
 * SPLIT_DETAIL
 * CLOSING_DETAIL
 */

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>

#include "storage/storage_manager.hpp"
#include "ui/full_catalog_screen.hpp"

int main() {
    psvitaalive::StorageManager storage;

    storage.initProjectDirs();

    psvitaalive::ui::FullCatalogScreen screen;

    if (!screen.init()) {
        sceClibPrintf(
            "[PSVitaAlive] UI initialization failed\n"
        );

        storage.writeTextFile(
            std::string(
                psvitaalive::StorageManager::TEST_DIR
            ) + "/summary_phase9.txt",
            "ui_init=0\n"
        );

        sceKernelExitProcess(1);
        return 1;
    }

    storage.writeTextFile(
        std::string(
            psvitaalive::StorageManager::TEST_DIR
        ) + "/summary_phase9.txt",
        "ui_init=1 mode=FULL_CATALOG split_detail=1 mock=1\n"
    );

    while (screen.updateAndDraw()) {
        // UI frame loop.
    }

    screen.shutdown();

    sceKernelExitProcess(0);

    return 0;
}

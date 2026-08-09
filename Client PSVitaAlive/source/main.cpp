/*
 * PSVitaAlive
 *
 * Phase 10:
 * FULL_CATALOG / SPLIT_DETAIL + asynchronous download/install orchestration.
 */

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>

#include <string>

#include "storage/storage_manager.hpp"
#include "installer/install_controller.hpp"
#include "ui/full_catalog_screen.hpp"

namespace {

std::string installStatusText(
    const psvitaalive::InstallStatus& status
) {
    using State = psvitaalive::InstallStatus::State;

    char buffer[128];

    switch (status.state) {
        case State::Downloading:
            if (status.total > 0) {
                sceClibSnprintf(
                    buffer,
                    sizeof(buffer),
                    "Downloading %llu/%llu",
                    (unsigned long long)status.current,
                    (unsigned long long)status.total
                );
                return buffer;
            }
            return "Downloading...";

        case State::Installing:
            return "Installing...";

        case State::Completed:
            return "Installed successfully";

        case State::Failed:
            return status.message.empty()
                ? "Install failed"
                : status.message;

        case State::Idle:
        default:
            return "";
    }
}

} // namespace

int main() {
    psvitaalive::StorageManager storage;
    storage.initProjectDirs();

    psvitaalive::InstallController installer;

    psvitaalive::ui::FullCatalogScreen screen;

    screen.setInstallCallbacks(
        [&installer](const psvitaalive::ui::CatalogItem& item) {
            if (item.downloadUrl.empty() || item.downloadFileName.empty()) {
                sceClibPrintf(
                    "[PSVitaAlive] No normalized Download link for %s\n",
                    item.name.c_str()
                );
                return false;
            }

            return installer.requestInstall(
                item.downloadUrl,
                item.downloadFileName
            );
        },
        [&installer]() {
            return installStatusText(installer.status());
        }
    );

    if (!screen.init()) {
        sceClibPrintf(
            "[PSVitaAlive] UI initialization failed\n"
        );

        storage.writeTextFile(
            std::string(
                psvitaalive::StorageManager::TEST_DIR
            ) + "/summary_phase10.txt",
            "ui_init=0\n"
        );

        installer.shutdown();
        sceKernelExitProcess(1);
        return 1;
    }

    storage.writeTextFile(
        std::string(
            psvitaalive::StorageManager::TEST_DIR
        ) + "/summary_phase10.txt",
        "ui_init=1 mode=FULL_CATALOG split_detail=1 async_install=1 mock=1\n"
    );

    while (screen.updateAndDraw()) {
        // UI frame loop. Network/install work runs in InstallController's worker.
    }

    screen.shutdown();
    installer.shutdown();

    sceKernelExitProcess(0);

    return 0;
}

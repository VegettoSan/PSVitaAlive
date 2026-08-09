/*
 * PSVitaAlive
 *
 * Phase 10+: asynchronous download/install pipeline.
 * Downloaded VPK/PKG/ZIP files are temporary artifacts and are removed only
 * after their requested operation succeeds.
 */

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/message_dialog.h>
#include <psp2/ime_dialog.h>

#include <cstring>
#include <string>
#include <vector>
#include <utility>

#include "storage/storage_manager.hpp"
#include "installer/install_controller.hpp"
#include "ui/full_catalog_screen.hpp"
#include "catalog/catalog_parser.hpp"
#include "network/http_client.hpp"

namespace {

bool gProgressDialogOpen = false;

const char* stateName(psvitaalive::InstallStatus::State state) {
    using State = psvitaalive::InstallStatus::State;
    switch (state) {
        case State::Downloading: return "Downloading";
        case State::Installing: return "Installing";
        case State::Completed: return "Completed";
        case State::Failed: return "Failed";
        case State::Idle: default: return "Idle";
    }
}

double bytesToMiB(uint64_t value) {
    return static_cast<double>(value) / (1024.0 * 1024.0);
}

std::string installStatusText(const psvitaalive::InstallStatus& status) {
    using State = psvitaalive::InstallStatus::State;
    if (status.state == State::Idle) return {};

    char buffer[320];
    const uint64_t percent =
        status.total > 0
            ? std::min<uint64_t>(100, (status.current * 100) / status.total)
            : 0;

    sceClibSnprintf(
        buffer,
        sizeof(buffer),
        "%s | %s | %llu%% | %.2f MiB/s",
        stateName(status.state),
        status.fileName.empty() ? "file" : status.fileName.c_str(),
        (unsigned long long)percent,
        bytesToMiB(status.bytesPerSecond)
    );
    return buffer;
}

void closeProgressDialog() {
    if (!gProgressDialogOpen) return;
    if (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_NONE) {
        sceMsgDialogClose();
        sceMsgDialogTerm();
    }
    gProgressDialogOpen = false;
}

void updateProgressDialog(const psvitaalive::InstallStatus& status) {
    using State = psvitaalive::InstallStatus::State;
    const bool shouldShow =
        status.state == State::Downloading ||
        status.state == State::Installing ||
        status.state == State::Completed ||
        status.state == State::Failed;

    if (!shouldShow) {
        closeProgressDialog();
        return;
    }

    static char dialogMessage[512];
    sceClibSnprintf(
        dialogMessage,
        sizeof(dialogMessage),
        "%s\n%s\nSpeed: %.2f MiB/s",
        stateName(status.state),
        status.fileName.empty() ? "Preparing..." : status.fileName.c_str(),
        bytesToMiB(status.bytesPerSecond)
    );

    if (!gProgressDialogOpen) {
        SceMsgDialogProgressBarParam progress = {};
        progress.barType = SCE_MSG_DIALOG_PROGRESSBAR_TYPE_PERCENTAGE;
        progress.sysMsgParam.sysMsgType = SCE_MSG_DIALOG_SYSMSG_TYPE_WAIT_SMALL;
        progress.msg = dialogMessage;

        SceMsgDialogParam param = {};
        param.mode = SCE_MSG_DIALOG_MODE_PROGRESS_BAR;
        param.progBarParam = &progress;
        param.flag = SCE_MSG_DIALOG_ENV_FLAG_DEFAULT;
        param.commonParam.magic = SCE_COMMON_DIALOG_MAGIC_NUMBER;

        if (sceMsgDialogInit(&param) < 0) {
            sceClibPrintf("[PSVitaAlive] failed to open progress dialog\n");
            return;
        }
        gProgressDialogOpen = true;
    }

    uint32_t percent = 0;
    if (status.total > 0) {
        const uint64_t value = (status.current * 100) / status.total;
        percent = static_cast<uint32_t>(value > 100 ? 100 : value);
    }

    sceMsgDialogProgressBarSetValue(
        SCE_MSG_DIALOG_PROGRESSBAR_TARGET_BAR_DEFAULT,
        percent
    );
    sceMsgDialogProgressBarSetMsg(
        SCE_MSG_DIALOG_PROGRESSBAR_TARGET_BAR_DEFAULT,
        dialogMessage
    );
}

bool asciiToWide(const std::string& text, SceWChar16* out, size_t capacity) {
    if (!out || capacity == 0) return false;
    size_t i = 0;
    for (; i + 1 < capacity && i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        out[i] = static_cast<SceWChar16>(c < 128 ? c : '?');
    }
    out[i] = 0;
    return true;
}

std::string wideToAscii(const SceWChar16* text) {
    if (!text) return {};
    std::string result;
    for (size_t i = 0; text[i] != 0 && i < 2048; ++i) {
        char c = text[i] <= 0x7F ? static_cast<char>(text[i]) : '?';
        result.push_back(c);
    }
    return result;
}

bool promptZipDestination(std::string& destination) {
    static bool imeLoaded = false;
    if (!imeLoaded) {
        const int result = sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
        if (result < 0 && result != SCE_SYSMODULE_ERROR_DUPLICATE) {
            sceClibPrintf("[PSVitaAlive] failed to load IME: 0x%08X\n", result);
            return false;
        }
        imeLoaded = true;
    }

    SceWChar16 input[256] = {};
    SceWChar16 title[128] = {};
    asciiToWide(destination.empty() ? "ux0:data/" : destination, input, 256);
    asciiToWide("ZIP extraction path", title, 128);

    SceImeDialogParam param = {};
    param.type = SCE_IME_TYPE_BASIC_LATIN;
    param.option = SCE_IME_OPTION_NO_AUTO_CAPITALIZATION;
    param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
    param.title = title;
    param.maxTextLength = 255;
    param.initialText = input;
    param.inputTextBuffer = input;
    param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH | SCE_IME_LANGUAGE_SPANISH;
    param.enterLabel = SCE_IME_ENTER_LABEL_GO;
    param.commonParam.magic = SCE_COMMON_DIALOG_MAGIC_NUMBER;

    if (sceImeDialogInit(&param) < 0) {
        sceClibPrintf("[PSVitaAlive] failed to open ZIP path IME\n");
        return false;
    }

    while (sceImeDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING) {
        sceKernelDelayThread(10 * 1000);
    }

    SceImeDialogResult result = {};
    sceImeDialogGetResult(&result);
    const bool accepted = result.button == SCE_IME_DIALOG_BUTTON_ENTER;
    if (accepted) {
        destination = wideToAscii(input);
        for (char& c : destination) {
            if (c == '\\') c = '/';
        }
        while (destination.size() > 1 && destination.back() == '/') {
            destination.pop_back();
        }
    }

    sceImeDialogTerm();
    return accepted && !destination.empty();
}

} // namespace

int main() {
    psvitaalive::StorageManager storage;
    storage.initProjectDirs();

    constexpr const char* CATALOG_URL =
        "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/catalog.json";

    const std::string catalogPath =
        std::string(psvitaalive::StorageManager::CACHE_DIR) + "/catalog.json";

    storage.createDirectories(psvitaalive::StorageManager::CACHE_DIR);

    std::vector<psvitaalive::ui::CatalogItem> catalogItems;
    psvitaalive::HttpClient catalogHttp;

    if (catalogHttp.init() == psvitaalive::HttpResult::Ok) {
        const psvitaalive::HttpResult result =
            catalogHttp.downloadToFile(CATALOG_URL, catalogPath);

        if (result == psvitaalive::HttpResult::Ok) {
            if (!psvitaalive::CatalogParser::parseFile(catalogPath, catalogItems)) {
                sceClibPrintf("[PSVitaAlive] Catalog parsing failed\n");
            }
        } else {
            sceClibPrintf(
                "[PSVitaAlive] Catalog download failed: %s\n",
                catalogHttp.lastError().c_str()
            );
        }
        catalogHttp.shutdown();
    } else {
        sceClibPrintf("[PSVitaAlive] Catalog HTTP initialization failed\n");
    }

    psvitaalive::InstallController installer;
    psvitaalive::ui::FullCatalogScreen screen;

    screen.setCatalogItems(std::move(catalogItems));

    screen.setInstallCallbacks(
        [&installer](const psvitaalive::ui::CatalogItem& item) {
            if (item.downloadUrl.empty() || item.downloadFileName.empty()) {
                sceClibPrintf(
                    "[PSVitaAlive] No normalized Download link for %s\n",
                    item.name.c_str()
                );
                return false;
            }

            std::string zipDestination;
            const std::string& name = item.downloadFileName;
            if (name.size() >= 4 &&
                (name.substr(name.size() - 4) == ".zip" ||
                 name.substr(name.size() - 4) == ".ZIP")) {
                zipDestination = "ux0:data/";
                if (!promptZipDestination(zipDestination)) {
                    sceClibPrintf("[PSVitaAlive] ZIP extraction cancelled by user\n");
                    return false;
                }
            }

            return installer.requestInstall(
                item.downloadUrl,
                item.downloadFileName,
                zipDestination
            );
        },
        [&installer]() {
            return installStatusText(installer.status());
        }
    );

    if (!screen.init()) {
        sceClibPrintf("[PSVitaAlive] UI initialization failed\n");
        storage.writeTextFile(
            std::string(psvitaalive::StorageManager::TEST_DIR) + "/summary_phase10.txt",
            "ui_init=0\n"
        );
        installer.shutdown();
        sceKernelExitProcess(1);
        return 1;
    }

    storage.writeTextFile(
        std::string(psvitaalive::StorageManager::TEST_DIR) + "/summary_phase10.txt",
        "ui_init=1 mode=FULL_CATALOG split_detail=1 async_install=1 progress_dialog=1 zip_destination=1\n"
    );

    while (screen.updateAndDraw()) {
        // The centered Vita system progress dialog is updated from the UI/main
        // thread while network/install work runs on InstallController's worker.
        updateProgressDialog(installer.status());
    }

    closeProgressDialog();
    screen.shutdown();
    installer.shutdown();

    sceKernelExitProcess(0);
    return 0;
}

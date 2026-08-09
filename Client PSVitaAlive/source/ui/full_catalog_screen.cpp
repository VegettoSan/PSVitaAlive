#include "ui/full_catalog_screen.hpp"

#include <psp2/ctrl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>

#include <algorithm>
#include <cstring>

namespace psvitaalive {
namespace ui {

namespace {

constexpr unsigned BG =
    RGBA8(0x00, 0x00, 0x00, 0xFF);

constexpr unsigned SURFACE =
    RGBA8(0x37, 0x37, 0x37, 0xFF);

constexpr unsigned SURFACE2 =
    RGBA8(0x2A, 0x2A, 0x2A, 0xFF);

constexpr unsigned BORDER =
    RGBA8(0x6E, 0x6E, 0x6E, 0xFF);

constexpr unsigned TEXT =
    RGBA8(0xAA, 0xAA, 0xAA, 0xFF);

constexpr unsigned TEXT_DIM =
    RGBA8(0x6E, 0x6E, 0x6E, 0xFF);

constexpr unsigned ACCENT =
    RGBA8(0x3B, 0xFF, 0x00, 0xFF);

constexpr unsigned WHITE =
    RGBA8(0xFF, 0xFF, 0xFF, 0xFF);

constexpr unsigned PANEL =
    RGBA8(0x20, 0x20, 0x20, 0xFF);

constexpr int FULL_CARD_H = 120;
constexpr int SPLIT_CARD_H = 82;

constexpr int DETAIL_HEADER_H = 92;
constexpr int DETAIL_LINE_H = 18;

constexpr int TRANSITION_MS = 200;

} // anonymous namespace

FullCatalogScreen::FullCatalogScreen() = default;

FullCatalogScreen::~FullCatalogScreen() {
    shutdown();
}

bool FullCatalogScreen::init() {
    vita2d_init();
    vita2d_set_clear_color(BG);

    font_ = vita2d_load_default_pgf();

    if (!font_) {
        sceClibPrintf("[UI] Failed to load default PGF font\n");
        return false;
    }

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    loadMockData();

    state_.mode = UiMode::FULL_CATALOG;
    state_.activePanel = UiPanel::Catalog;
    state_.focusIndex = 0;
    state_.catalogScrollRow = 0;
    state_.detailScroll = 0;
    state_.requestExit = false;

    ready_ = true;

    sceClibPrintf("[UI] Phase 9 initialized\n");

    return true;
}

void FullCatalogScreen::shutdown() {
    if (font_) {
        vita2d_free_pgf(font_);
        font_ = nullptr;
    }

    if (ready_) {
        vita2d_fini();
        ready_ = false;
    }
}

void FullCatalogScreen::loadMockData() {
    items_.clear();

    CatalogItem vitaShell;
    vitaShell.id = "mock_0";
    vitaShell.titleId = "VITASHELL";
    vitaShell.name = "VitaShell";
    vitaShell.author = "TheFloW";
    vitaShell.description =
        "File manager and shell environment for PlayStation Vita.";
    vitaShell.longDescription =
        "VitaShell is a file manager and shell application for the "
        "PlayStation Vita. It provides file management, FTP, USB and "
        "other useful system functions.";
    vitaShell.status = "Verified";
    vitaShell.version = "2.02";
    vitaShell.versionDate = "2024-01-01";
    vitaShell.requirements = "HENkaku / compatible Vita environment";
    vitaShell.size = "5 MB";
    vitaShell.category = "Utilities";
    vitaShell.subcategory = "File Manager";
    vitaShell.changelog =
        "- Improved stability\n"
        "- Updated system compatibility\n"
        "- Various fixes";
    vitaShell.links.push_back("Download");
    vitaShell.links.push_back("Repository");
    vitaShell.links.push_back("Issues");
    items_.push_back(vitaShell);

    CatalogItem adrenaline;
    adrenaline.id = "mock_1";
    adrenaline.titleId = "ADRENALINE";
    adrenaline.name = "Adrenaline";
    adrenaline.author = "TheFloW";
    adrenaline.description =
        "PSP emulator and enhancement environment for PS Vita.";
    adrenaline.longDescription =
        "Adrenaline is a homebrew application that exploits the "
        "built-in PSP emulator of the PlayStation Vita and provides "
        "additional PSP functionality.";
    adrenaline.status = "Verified";
    adrenaline.version = "7.1";
    adrenaline.versionDate = "2024-02-01";
    adrenaline.requirements = "PS Vita with compatible custom firmware";
    adrenaline.size = "10 MB";
    adrenaline.category = "Emulator";
    adrenaline.subcategory = "PSP";
    adrenaline.changelog =
        "- Compatibility improvements\n"
        "- Stability fixes";
    adrenaline.links.push_back("Download");
    adrenaline.links.push_back("Repository");
    items_.push_back(adrenaline);

    CatalogItem retroarch;
    retroarch.id = "mock_2";
    retroarch.titleId = "RETROARCH";
    retroarch.name = "RetroArch";
    retroarch.author = "Libretro";
    retroarch.description =
        "Frontend for multiple emulator cores.";
    retroarch.longDescription =
        "RetroArch is a frontend for emulators, game engines and "
        "games. It provides a unified interface for many different "
        "systems and platforms.";
    retroarch.status = "Verified";
    retroarch.version = "1.19.0";
    retroarch.versionDate = "2024-03-01";
    retroarch.requirements = "Additional cores may require extra files";
    retroarch.size = "25 MB";
    retroarch.category = "Emulator";
    retroarch.subcategory = "Multi-system";
    retroarch.changelog =
        "- Updated cores\n"
        "- Performance improvements\n"
        "- User interface fixes";
    retroarch.links.push_back("Download");
    retroarch.links.push_back("Repository");
    items_.push_back(retroarch);

    CatalogItem pkgj;
    pkgj.id = "mock_3";
    pkgj.titleId = "PKGJ00000";
    pkgj.name = "PKGj";
    pkgj.author = "blastrock";
    pkgj.description =
        "Homebrew application for browsing and downloading content.";
    pkgj.longDescription =
        "PKGj provides a graphical interface for browsing available "
        "content sources and managing downloads.";
    pkgj.status = "Legacy";
    pkgj.version = "0.57";
    pkgj.versionDate = "2023-01-01";
    pkgj.requirements = "Compatible Vita environment";
    pkgj.size = "4 MB";
    pkgj.category = "Utilities";
    pkgj.subcategory = "Downloader";
    pkgj.changelog =
        "- Legacy release preserved by VitaHub";
    pkgj.links.push_back("Repository");
    items_.push_back(pkgj);

    CatalogItem vitadb;
    vitadb.id = "mock_4";
    vitadb.titleId = "VITADB000";
    vitadb.name = "VitaDB";
    vitadb.author = "devnoname120";
    vitadb.description =
        "Homebrew discovery and application database.";
    vitadb.longDescription =
        "VitaDB provides information about PlayStation Vita homebrew "
        "applications and related projects.";
    vitadb.status = "Legacy";
    vitadb.version = "1.0";
    vitadb.versionDate = "2022-01-01";
    vitadb.requirements = "None";
    vitadb.size = "3 MB";
    vitadb.category = "Store";
    vitadb.subcategory = "Database";
    vitadb.changelog =
        "- Legacy catalog entry";
    vitadb.links.push_back("Website");
    items_.push_back(vitadb);

    CatalogItem saveManager;
    saveManager.id = "mock_5";
    saveManager.titleId = "SAVEMGR001";
    saveManager.name = "SaveManager";
    saveManager.author = "Community";
    saveManager.description =
        "Save management utility for PS Vita.";
    saveManager.longDescription =
        "Utility for inspecting and managing application save data.";
    saveManager.status = "Archive";
    saveManager.version = "1.0";
    saveManager.versionDate = "2020-01-01";
    saveManager.requirements = "Compatible Vita environment";
    saveManager.size = "2 MB";
    saveManager.category = "Utilities";
    saveManager.subcategory = "Save Manager";
    saveManager.changelog =
        "- Archived project";
    saveManager.links.push_back("Repository");
    items_.push_back(saveManager);

    CatalogItem battery;
    battery.id = "mock_6";
    battery.titleId = "BATTERY001";
    battery.name = "BatteryMgr";
    battery.author = "Community";
    battery.description =
        "Battery information utility.";
    battery.longDescription =
        "Displays information related to the current Vita battery "
        "and power state.";
    battery.status = "Verified";
    battery.version = "1.2";
    battery.versionDate = "2024-04-01";
    battery.requirements = "None";
    battery.size = "1 MB";
    battery.category = "Utilities";
    battery.subcategory = "System";
    battery.changelog =
        "- Improved system information";
    battery.links.push_back("Download");
    items_.push_back(battery);

    CatalogItem ftp;
    ftp.id = "mock_7";
    ftp.titleId = "FTP000001";
    ftp.name = "FTP Client";
    ftp.author = "Community";
    ftp.description =
        "FTP utility for transferring files.";
    ftp.longDescription =
        "A simple FTP-oriented utility for transferring files between "
        "a PC and the PlayStation Vita.";
    ftp.status = "Verified";
    ftp.version = "1.1";
    ftp.versionDate = "2024-05-01";
    ftp.requirements = "Network connection";
    ftp.size = "1 MB";
    ftp.category = "Utilities";
    ftp.subcategory = "Network";
    ftp.changelog =
        "- Improved connection handling";
    ftp.links.push_back("Download");
    items_.push_back(ftp);

    CatalogItem themes;
    themes.id = "mock_8";
    themes.titleId = "THEME001";
    themes.name = "Theme Manager";
    themes.author = "Community";
    themes.description =
        "Utility for managing custom themes.";
    themes.longDescription =
        "Manage and organize custom PlayStation Vita themes.";
    themes.status = "Verified";
    themes.version = "2.0";
    themes.versionDate = "2024-06-01";
    themes.requirements = "Custom theme files";
    themes.size = "2 MB";
    themes.category = "Customization";
    themes.subcategory = "Themes";
    themes.changelog =
        "- Added theme browsing\n"
        "- Various fixes";
    themes.links.push_back("Download");
    items_.push_back(themes);

    CatalogItem cheat;
    cheat.id = "mock_9";
    cheat.titleId = "CHEAT0001";
    cheat.name = "Cheat Device";
    cheat.author = "Community";
    cheat.description =
        "Cheat and debugging utility.";
    cheat.longDescription =
        "Utility for compatible homebrew and development workflows.";
    cheat.status = "Archive";
    cheat.version = "1.0";
    cheat.versionDate = "2019-01-01";
    cheat.requirements = "Compatible environment";
    cheat.size = "2 MB";
    cheat.category = "Utilities";
    cheat.subcategory = "Development";
    cheat.changelog =
        "- Archived project";
    cheat.links.push_back("Repository");
    items_.push_back(cheat);

    CatalogItem music;
    music.id = "mock_10";
    music.titleId = "MUSIC0001";
    music.name = "Music Player";
    music.author = "Community";
    music.description =
        "Simple music player.";
    music.longDescription =
        "A lightweight music playback application for the Vita.";
    music.status = "Verified";
    music.version = "1.5";
    music.versionDate = "2024-07-01";
    music.requirements = "Compatible audio files";
    music.size = "3 MB";
    music.category = "Multimedia";
    music.subcategory = "Audio";
    music.changelog =
        "- Improved playback\n"
        "- Stability fixes";
    music.links.push_back("Download");
    items_.push_back(music);

    CatalogItem browser;
    browser.id = "mock_11";
    browser.titleId = "BROWSER001";
    browser.name = "File Browser";
    browser.author = "Community";
    browser.description =
        "Lightweight file browsing application.";
    browser.longDescription =
        "Browse files and directories on supported Vita storage devices.";
    browser.status = "Verified";
    browser.version = "1.0";
    browser.versionDate = "2024-08-01";
    browser.requirements = "None";
    browser.size = "2 MB";
    browser.category = "Utilities";
    browser.subcategory = "File Manager";
    browser.changelog =
        "- Initial release";
    browser.links.push_back("Download");
    items_.push_back(browser);

    state_.focusIndex = 0;
    state_.catalogScrollRow = 0;
    state_.detailScroll = 0;
}

int FullCatalogScreen::totalRows() const {
    if (items_.empty()) {
        return 0;
    }

    return (static_cast<int>(items_.size()) + GRID_COLS - 1)
        / GRID_COLS;
}

int FullCatalogScreen::visibleRowsFull() const {
    const int gridTop =
        HEADER_H + TABS_H + GRID_PAD;

    const int gridBottom =
        SCREEN_H - FOOTER_H - GRID_PAD;

    const int gridHeight =
        gridBottom - gridTop;

    const int rows =
        gridHeight / (FULL_CARD_H + CARD_GAP);

    return std::max(1, rows);
}

int FullCatalogScreen::visibleRowsSplit() const {
    const int top =
        HEADER_H + TABS_H + GRID_PAD;

    const int bottom =
        SCREEN_H - FOOTER_H - GRID_PAD;

    const int height =
        bottom - top;

    const int rows =
        height / (SPLIT_CARD_H + CARD_GAP);

    return std::max(1, rows);
}

int FullCatalogScreen::selectedIndex() const {
    if (items_.empty()) {
        return -1;
    }

    return std::max(
        0,
        std::min(
            state_.focusIndex,
            static_cast<int>(items_.size()) - 1
        )
    );
}

void FullCatalogScreen::clampCatalogFocus() {
    if (items_.empty()) {
        state_.focusIndex = 0;
        return;
    }

    state_.focusIndex = std::max(
        0,
        std::min(
            state_.focusIndex,
            static_cast<int>(items_.size()) - 1
        )
    );
}

void FullCatalogScreen::clampCatalogScroll() {
    if (items_.empty()) {
        state_.catalogScrollRow = 0;
        return;
    }

    const int visible =
        state_.mode == UiMode::FULL_CATALOG
            ? visibleRowsFull()
            : visibleRowsSplit();

    const int focusRow =
        state_.focusIndex / GRID_COLS;

    if (state_.mode == UiMode::FULL_CATALOG) {
        if (focusRow < state_.catalogScrollRow) {
            state_.catalogScrollRow = focusRow;
        }

        if (focusRow >=
            state_.catalogScrollRow + visible) {
            state_.catalogScrollRow =
                focusRow - visible + 1;
        }
    } else {
        // En split mode el catálogo es una sola columna.
        const int maxScroll =
            std::max(
                0,
                static_cast<int>(items_.size()) - visible
            );

        state_.catalogScrollRow =
            std::max(
                0,
                std::min(
                    state_.catalogScrollRow,
                    maxScroll
                )
            );

        if (state_.focusIndex <
            state_.catalogScrollRow) {
            state_.catalogScrollRow =
                state_.focusIndex;
        }

        if (state_.focusIndex >=
            state_.catalogScrollRow + visible) {
            state_.catalogScrollRow =
                state_.focusIndex - visible + 1;
        }
    }

    const int maxScroll =
        std::max(
            0,
            totalRows() - visible
        );

    if (state_.mode == UiMode::FULL_CATALOG) {
        state_.catalogScrollRow =
            std::max(
                0,
                std::min(
                    state_.catalogScrollRow,
                    maxScroll
                )
            );
    }
}

void FullCatalogScreen::clampDetailScroll() {
    const int index = selectedIndex();

    if (index < 0) {
        state_.detailScroll = 0;
        return;
    }

    /*
     * El detalle se construye aproximadamente con:
     *
     * header
     * description
     * long description
     * requirements
     * information
     * links
     * changelog
     *
     * Limitamos el scroll a un valor seguro para el mock.
     */
    constexpr int MAX_DETAIL_SCROLL = 1200;

    state_.detailScroll =
        std::max(
            0,
            std::min(
                state_.detailScroll,
                MAX_DETAIL_SCROLL
            )
        );
}

void FullCatalogScreen::moveCatalogFocus(int direction) {
    if (items_.empty()) {
        return;
    }

    if (state_.mode == UiMode::FULL_CATALOG) {
        if (direction < 0) {
            // UP
            if (state_.focusIndex >= GRID_COLS) {
                state_.focusIndex -= GRID_COLS;
            }
        } else if (direction > 0) {
            // DOWN
            if (state_.focusIndex + GRID_COLS <
                static_cast<int>(items_.size())) {
                state_.focusIndex += GRID_COLS;
            }
        }
    } else {
        if (direction < 0) {
            if (state_.focusIndex > 0) {
                --state_.focusIndex;
            }
        } else if (direction > 0) {
            if (state_.focusIndex + 1 <
                static_cast<int>(items_.size())) {
                ++state_.focusIndex;
            }
        }
    }

    clampCatalogFocus();
    clampCatalogScroll();

    // Cambiar de app reinicia el detalle.
    state_.detailScroll = 0;
}

void FullCatalogScreen::moveDetailScroll(int direction) {
    constexpr int DETAIL_SCROLL_STEP = 24;

    if (direction < 0) {
        state_.detailScroll -= DETAIL_SCROLL_STEP;
    } else if (direction > 0) {
        state_.detailScroll += DETAIL_SCROLL_STEP;
    }

    clampDetailScroll();
}

void FullCatalogScreen::changeCatalog(int direction) {
    int catalog =
        static_cast<int>(state_.catalog);

    catalog += direction;

    const int count =
        static_cast<int>(CatalogType::Count);

    if (catalog < 0) {
        catalog = count - 1;
    }

    if (catalog >= count) {
        catalog = 0;
    }

    state_.catalog =
        static_cast<CatalogType>(catalog);

    state_.focusIndex = 0;
    state_.catalogScrollRow = 0;
    state_.detailScroll = 0;
}

bool FullCatalogScreen::isTransitioning() const {
    return state_.mode == UiMode::OPENING_DETAIL ||
           state_.mode == UiMode::CLOSING_DETAIL;
}

void FullCatalogScreen::startOpeningDetail() {
    if (state_.mode != UiMode::FULL_CATALOG) {
        return;
    }

    state_.activePanel = UiPanel::Catalog;
    state_.detailScroll = 0;

    state_.transitionStart =
        sceKernelGetProcessTimeWide();

    state_.mode = UiMode::OPENING_DETAIL;

    sceClibPrintf("[UI] Opening detail\n");
}

void FullCatalogScreen::startClosingDetail() {
    if (state_.mode != UiMode::SPLIT_DETAIL) {
        return;
    }

    state_.transitionStart =
        sceKernelGetProcessTimeWide();

    state_.mode = UiMode::CLOSING_DETAIL;

    sceClibPrintf("[UI] Closing detail\n");
}

float FullCatalogScreen::transitionProgress() const {
    if (!isTransitioning()) {
        return 1.0f;
    }

    const uint64_t now =
        sceKernelGetProcessTimeWide();

    const uint64_t elapsed =
        now - state_.transitionStart;

    const float progress =
        static_cast<float>(elapsed) /
        static_cast<float>(TRANSITION_MS * 1000);

    return std::max(
        0.0f,
        std::min(1.0f, progress)
    );
}

void FullCatalogScreen::updateTransition() {
    if (!isTransitioning()) {
        return;
    }

    if (transitionProgress() < 1.0f) {
        return;
    }

    if (state_.mode == UiMode::OPENING_DETAIL) {
        state_.mode = UiMode::SPLIT_DETAIL;
        state_.activePanel = UiPanel::Catalog;

        clampCatalogFocus();
        clampCatalogScroll();

        sceClibPrintf("[UI] SPLIT_DETAIL\n");
    } else {
        state_.mode = UiMode::FULL_CATALOG;
        state_.activePanel = UiPanel::Catalog;

        clampCatalogFocus();
        clampCatalogScroll();

        sceClibPrintf("[UI] FULL_CATALOG\n");
    }
}

void FullCatalogScreen::handleInput() {
    if (isTransitioning()) {
        return;
    }

    SceCtrlData pad;
    std::memset(&pad, 0, sizeof(pad));

    sceCtrlPeekBufferPositive(
        0,
        &pad,
        1
    );

    static uint32_t previousButtons = 0;

    const uint32_t pressed =
        pad.buttons & ~previousButtons;

    previousButtons = pad.buttons;

    if (pressed & SCE_CTRL_START) {
        state_.requestExit = true;
        return;
    }

    if (pressed & SCE_CTRL_LTRIGGER) {
        changeCatalog(-1);
        return;
    }

    if (pressed & SCE_CTRL_RTRIGGER) {
        changeCatalog(1);
        return;
    }

    if (state_.mode == UiMode::FULL_CATALOG) {

        if (pressed & SCE_CTRL_LEFT) {
            if (state_.focusIndex % GRID_COLS > 0) {
                --state_.focusIndex;
                clampCatalogScroll();
            }
        }

        if (pressed & SCE_CTRL_RIGHT) {
            if ((state_.focusIndex % GRID_COLS) <
                    GRID_COLS - 1 &&
                state_.focusIndex + 1 <
                    static_cast<int>(items_.size())) {

                ++state_.focusIndex;
                clampCatalogScroll();
            }
        }

        if (pressed & SCE_CTRL_UP) {
            moveCatalogFocus(-1);
        }

        if (pressed & SCE_CTRL_DOWN) {
            moveCatalogFocus(1);
        }

        if (pressed & SCE_CTRL_CROSS) {
            startOpeningDetail();
        }

        return;
    }

    if (state_.mode == UiMode::SPLIT_DETAIL) {

        if (pressed & SCE_CTRL_CIRCLE) {
            startClosingDetail();
            return;
        }

        // Izquierda/derecha cambia el panel activo.
        if (pressed & SCE_CTRL_LEFT) {
            state_.activePanel = UiPanel::Catalog;
        }

        if (pressed & SCE_CTRL_RIGHT) {
            state_.activePanel = UiPanel::Detail;
        }

        if (state_.activePanel == UiPanel::Catalog) {

            if (pressed & SCE_CTRL_UP) {
                moveCatalogFocus(-1);
            }

            if (pressed & SCE_CTRL_DOWN) {
                moveCatalogFocus(1);
            }

            if (pressed & SCE_CTRL_CROSS) {
                // El split ya está abierto.
                // X no lo cierra; la selección permanece activa.
                state_.detailScroll = 0;

                sceClibPrintf(
                    "[UI] Selected: %s\n",
                    items_[selectedIndex()].name.c_str()
                );
            }

        } else {

            if (pressed & SCE_CTRL_UP) {
                moveDetailScroll(-1);
            }

            if (pressed & SCE_CTRL_DOWN) {
                moveDetailScroll(1);
            }
        }

        return;
    }
}

unsigned FullCatalogScreen::colorForStatus(
    const std::string& status
) const {
    if (status == "Verified") {
        return ACCENT;
    }

    if (status == "Legacy") {
        return TEXT;
    }

    if (status == "Archive") {
        return TEXT_DIM;
    }

    return TEXT;
}

void FullCatalogScreen::drawHeader(int width) {
    vita2d_draw_rectangle(
        0,
        0,
        width,
        HEADER_H,
        SURFACE2
    );

    vita2d_pgf_draw_text(
        font_,
        16,
        32,
        ACCENT,
        1.2f,
        "PSVitaAlive"
    );

    vita2d_pgf_draw_text(
        font_,
        width - 130,
        32,
        TEXT_DIM,
        0.9f,
        "START: exit"
    );
}

void FullCatalogScreen::drawTabs(int width) {
    vita2d_draw_rectangle(
        0,
        HEADER_H,
        width,
        TABS_H,
        SURFACE
    );

    const float tabWidth =
        static_cast<float>(width) /
        static_cast<int>(CatalogType::Count);

    for (int i = 0;
         i < static_cast<int>(CatalogType::Count);
         ++i) {

        const int x =
            static_cast<int>(i * tabWidth);

        const bool active =
            static_cast<int>(state_.catalog) == i;

        if (active) {
            vita2d_draw_rectangle(
                x,
                HEADER_H + TABS_H - 3,
                static_cast<int>(tabWidth),
                3,
                ACCENT
            );
        }

        vita2d_pgf_draw_text(
            font_,
            x + 12,
            HEADER_H + 24,
            active ? ACCENT : TEXT,
            0.9f,
            catalogName(
                static_cast<CatalogType>(i)
            )
        );
    }
}

void FullCatalogScreen::drawCatalogCard(
    const CatalogItem& item,
    int index,
    int x,
    int y,
    int width,
    int height,
    bool focused
) {
    vita2d_draw_rectangle(
        x,
        y,
        width,
        height,
        SURFACE
    );

    if (focused) {
        vita2d_draw_rectangle(
            x,
            y,
            width,
            3,
            ACCENT
        );

        vita2d_draw_rectangle(
            x,
            y + height - 3,
            width,
            3,
            ACCENT
        );

        vita2d_draw_rectangle(
            x,
            y,
            3,
            height,
            ACCENT
        );

        vita2d_draw_rectangle(
            x + width - 3,
            y,
            3,
            height,
            ACCENT
        );
    } else {
        vita2d_draw_rectangle(
            x,
            y,
            width,
            1,
            BORDER
        );
    }

    const int iconSize =
        height < 100 ? 48 : 64;

    vita2d_draw_rectangle(
        x + 8,
        y + 8,
        iconSize,
        iconSize,
        SURFACE2
    );

    const int textX =
        x + iconSize + 18;

    vita2d_pgf_draw_text(
        font_,
        textX,
        y + 27,
        WHITE,
        0.95f,
        item.name.c_str()
    );

    vita2d_pgf_draw_text(
        font_,
        textX,
        y + 48,
        TEXT,
        0.78f,
        item.author.c_str()
    );

    vita2d_pgf_draw_text(
        font_,
        textX,
        y + 68,
        colorForStatus(item.status),
        0.75f,
        item.status.c_str()
    );

    if (height >= 100) {
        vita2d_pgf_draw_text(
            font_,
            x + 10,
            y + height - 18,
            TEXT_DIM,
            0.72f,
            item.version.c_str()
        );
    }

    (void)index;
}

void FullCatalogScreen::drawCatalogPanel(
    int x,
    int y,
    int width,
    int height,
    bool splitMode
) {
    vita2d_draw_rectangle(
        x,
        y,
        width,
        height,
        PANEL
    );

    const int top =
        y + GRID_PAD;

    const int bottom =
        y + height - GRID_PAD;

    if (!splitMode) {

        const int usableWidth =
            width -
            GRID_PAD * 2 -
            CARD_GAP * (GRID_COLS - 1);

        const int cardWidth =
            usableWidth / GRID_COLS;

        const int visible =
            visibleRowsFull();

        for (int row = 0;
             row < visible;
             ++row) {

            const int realRow =
                state_.catalogScrollRow + row;

            for (int col = 0;
                 col < GRID_COLS;
                 ++col) {

                const int index =
                    realRow * GRID_COLS + col;

                if (index < 0 ||
                    index >=
                    static_cast<int>(items_.size())) {
                    continue;
                }

                const int cardX =
                    x + GRID_PAD +
                    col * (cardWidth + CARD_GAP);

                const int cardY =
                    top +
                    row * (FULL_CARD_H + CARD_GAP);

                drawCatalogCard(
                    items_[index],
                    index,
                    cardX,
                    cardY,
                    cardWidth,
                    FULL_CARD_H,
                    index == state_.focusIndex
                );
            }
        }

    } else {

        const int visible =
            visibleRowsSplit();

        for (int row = 0;
             row < visible;
             ++row) {

            const int index =
                state_.catalogScrollRow + row;

            if (index < 0 ||
                index >=
                static_cast<int>(items_.size())) {
                continue;
            }

            const int cardY =
                top +
                row * (SPLIT_CARD_H + CARD_GAP);

            drawCatalogCard(
                items_[index],
                index,
                x + GRID_PAD,
                cardY,
                width - GRID_PAD * 2,
                SPLIT_CARD_H,
                index == state_.focusIndex
            );
        }
    }

    // Indicador de panel activo.
    if (state_.mode == UiMode::SPLIT_DETAIL &&
        state_.activePanel == UiPanel::Catalog) {

        vita2d_draw_rectangle(
            x,
            y,
            3,
            height,
            ACCENT
        );
    }

    (void)bottom;
}

void FullCatalogScreen::wrapText(
    const std::string& text,
    int maxChars,
    std::vector<std::string>& lines
) const {
    lines.clear();

    if (text.empty()) {
        return;
    }

    std::string current;

    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];

        if (c == '\n') {
            lines.push_back(current);
            current.clear();
            continue;
        }

        if (static_cast<int>(current.size()) >= maxChars &&
            c == ' ') {

            lines.push_back(current);
            current.clear();
            continue;
        }

        current.push_back(c);

        if (static_cast<int>(current.size()) >= maxChars) {
            lines.push_back(current);
            current.clear();
        }
    }

    if (!current.empty()) {
        lines.push_back(current);
    }
}

void FullCatalogScreen::drawTextLines(
    const std::vector<std::string>& lines,
    int x,
    int y,
    int lineHeight,
    unsigned color,
    float scale,
    int startLine,
    int maxLines
) {
    if (lines.empty()) {
        return;
    }

    const int first =
        std::max(0, startLine);

    const int last =
        std::min(
            static_cast<int>(lines.size()),
            first + maxLines
        );

    int drawY = y;

    for (int i = first; i < last; ++i) {
        vita2d_pgf_draw_text(
            font_,
            x,
            drawY,
            color,
            scale,
            lines[i].c_str()
        );

        drawY += lineHeight;
    }
}

void FullCatalogScreen::drawDetailContent(
    const CatalogItem& item,
    int x,
    int y,
    int width,
    int height
) {
    const int contentX = x + 18;
    const int contentWidth = width - 36;

    const int maxChars =
        std::max(
            20,
            contentWidth / 8
        );

    std::vector<std::string> lines;

    /*
     * El contenido se construye en una lista vertical.
     * detailScroll representa líneas desplazadas.
     */
    std::vector<std::string> allLines;

    allLines.push_back(
        "Description"
    );

    std::vector<std::string> descriptionLines;

    wrapText(
        item.description,
        maxChars,
        descriptionLines
    );

    for (const auto& line : descriptionLines) {
        allLines.push_back(line);
    }

    allLines.push_back("");

    allLines.push_back(
        "Long Description"
    );

    std::vector<std::string> longDescriptionLines;

    wrapText(
        item.longDescription,
        maxChars,
        longDescriptionLines
    );

    for (const auto& line : longDescriptionLines) {
        allLines.push_back(line);
    }

    allLines.push_back("");

    allLines.push_back(
        "Requirements"
    );

    std::vector<std::string> requirementLines;

    wrapText(
        item.requirements,
        maxChars,
        requirementLines
    );

    for (const auto& line : requirementLines) {
        allLines.push_back(line);
    }

    allLines.push_back("");

    allLines.push_back(
        "Information"
    );

    allLines.push_back(
        "Title ID: " + item.titleId
    );

    allLines.push_back(
        "Version: " + item.version
    );

    allLines.push_back(
        "Version Date: " + item.versionDate
    );

    allLines.push_back(
        "Category: " + item.category
    );

    allLines.push_back(
        "Subcategory: " + item.subcategory
    );

    allLines.push_back(
        "Size: " + item.size
    );

    allLines.push_back(
        "Status: " + item.status
    );

    allLines.push_back("");

    allLines.push_back(
        "Downloads / Links"
    );

    for (const auto& link : item.links) {
        allLines.push_back(
            "- " + link
        );
    }

    allLines.push_back("");

    allLines.push_back(
        "Changelog"
    );

    std::vector<std::string> changelogLines;

    wrapText(
        item.changelog,
        maxChars,
        changelogLines
    );

    for (const auto& line : changelogLines) {
        allLines.push_back(line);
    }

    const int visibleLines =
        std::max(
            1,
            (height - 110) / DETAIL_LINE_H
        );

    const int maxScroll =
        std::max(
            0,
            static_cast<int>(allLines.size()) -
            visibleLines
        );

    const int scroll =
        std::max(
            0,
            std::min(
                state_.detailScroll,
                maxScroll
            )
        );

    drawTextLines(
        allLines,
        contentX,
        y + 110,
        DETAIL_LINE_H,
        TEXT,
        0.78f,
        scroll,
        visibleLines
    );

    // Scroll indicator.
    if (maxScroll > 0) {
        const int trackX =
            x + width - 8;

        const int trackY =
            y + 100;

        const int trackH =
            height - 120;

        vita2d_draw_rectangle(
            trackX,
            trackY,
            3,
            trackH,
            BORDER
        );

        const int thumbH =
            std::max(
                20,
                trackH * visibleLines /
                    static_cast<int>(
                        allLines.size()
                    )
            );

        const int thumbY =
            trackY +
            (trackH - thumbH) *
            scroll /
            maxScroll;

        vita2d_draw_rectangle(
            trackX,
            thumbY,
            3,
            thumbH,
            ACCENT
        );
    }
}

void FullCatalogScreen::drawDetailPanel(
    int x,
    int y,
    int width,
    int height
) {
    vita2d_draw_rectangle(
        x,
        y,
        width,
        height,
        PANEL
    );

    const int index = selectedIndex();

    if (index < 0) {
        return;
    }

    const CatalogItem& item =
        items_[index];

    // Indicador de panel activo.
    if (state_.activePanel == UiPanel::Detail) {
        vita2d_draw_rectangle(
            x + width - 3,
            y,
            3,
            height,
            ACCENT
        );
    }

    // Header del detalle.
    vita2d_draw_rectangle(
        x,
        y,
        width,
        DETAIL_HEADER_H,
        SURFACE
    );

    // Icono placeholder.
    vita2d_draw_rectangle(
        x + 16,
        y + 14,
        64,
        64,
        SURFACE2
    );

    vita2d_pgf_draw_text(
        font_,
        x + 94,
        y + 31,
        WHITE,
        1.0f,
        item.name.c_str()
    );

    vita2d_pgf_draw_text(
        font_,
        x + 94,
        y + 52,
        TEXT,
        0.78f,
        item.author.c_str()
    );

    vita2d_pgf_draw_text(
        font_,
        x + 94,
        y + 72,
        colorForStatus(item.status),
        0.78f,
        item.status.c_str()
    );

    drawDetailContent(
        item,
        x,
        y,
        width,
        height
    );
}

void FullCatalogScreen::drawFullCatalog() {
    vita2d_start_drawing();
    vita2d_clear_screen();

    drawHeader(SCREEN_W);
    drawTabs(SCREEN_W);

    const int gridTop =
        HEADER_H + TABS_H;

    const int gridHeight =
        SCREEN_H -
        HEADER_H -
        TABS_H -
        FOOTER_H;

    drawCatalogPanel(
        0,
        gridTop,
        SCREEN_W,
        gridHeight,
        false
    );

    vita2d_draw_rectangle(
        0,
        SCREEN_H - FOOTER_H,
        SCREEN_W,
        FOOTER_H,
        SURFACE2
    );

    vita2d_pgf_draw_text(
        font_,
        12,
        SCREEN_H - 14,
        TEXT,
        0.78f,
        "D-Pad: move   X: detail   L/R: catalog   START: exit"
    );

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void FullCatalogScreen::drawSplitDetail() {
    vita2d_start_drawing();
    vita2d_clear_screen();

    drawHeader(SCREEN_W);

    const int contentTop =
        HEADER_H + TABS_H;

    const int contentHeight =
        SCREEN_H -
        HEADER_H -
        TABS_H -
        FOOTER_H;

    const int leftWidth =
        SCREEN_W / 2;

    const int rightWidth =
        SCREEN_W - leftWidth;

    drawCatalogPanel(
        0,
        contentTop,
        leftWidth,
        contentHeight,
        true
    );

    drawDetailPanel(
        leftWidth,
        contentTop,
        rightWidth,
        contentHeight
    );

    // Separador central.
    vita2d_draw_rectangle(
        leftWidth - 1,
        contentTop,
        2,
        contentHeight,
        BORDER
    );

    vita2d_draw_rectangle(
        0,
        SCREEN_H - FOOTER_H,
        SCREEN_W,
        FOOTER_H,
        SURFACE2
    );

    vita2d_pgf_draw_text(
        font_,
        12,
        SCREEN_H - 14,
        TEXT,
        0.74f,
        "D-Pad: navigate/scroll   LEFT/RIGHT: panel   O: back   L/R: catalog"
    );

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void FullCatalogScreen::drawOpeningDetail() {
    const float progress =
        transitionProgress();

    /*
     * Durante la apertura:
     *
     * catálogo:
     * 960 -> 480
     *
     * detalle:
     * 0 -> 480
     */
    const int leftWidth =
        SCREEN_W -
        static_cast<int>(
            (SCREEN_W / 2) * progress
        );

    const int rightWidth =
        SCREEN_W - leftWidth;

    vita2d_start_drawing();
    vita2d_clear_screen();

    drawHeader(SCREEN_W);

    const int contentTop =
        HEADER_H + TABS_H;

    const int contentHeight =
        SCREEN_H -
        HEADER_H -
        TABS_H -
        FOOTER_H;

    drawCatalogPanel(
        0,
        contentTop,
        leftWidth,
        contentHeight,
        true
    );

    if (rightWidth > 0) {
        drawDetailPanel(
            leftWidth,
            contentTop,
            rightWidth,
            contentHeight
        );
    }

    vita2d_draw_rectangle(
        0,
        SCREEN_H - FOOTER_H,
        SCREEN_W,
        FOOTER_H,
        SURFACE2
    );

    vita2d_pgf_draw_text(
        font_,
        12,
        SCREEN_H - 14,
        TEXT_DIM,
        0.74f,
        "Opening detail..."
    );

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void FullCatalogScreen::drawClosingDetail() {
    const float progress =
        transitionProgress();

    /*
     * Invertimos la animación.
     */
    const float remaining =
        1.0f - progress;

    const int leftWidth =
        SCREEN_W -
        static_cast<int>(
            (SCREEN_W / 2) * remaining
        );

    const int rightWidth =
        SCREEN_W - leftWidth;

    vita2d_start_drawing();
    vita2d_clear_screen();

    drawHeader(SCREEN_W);

    const int contentTop =
        HEADER_H + TABS_H;

    const int contentHeight =
        SCREEN_H -
        HEADER_H -
        TABS_H -
        FOOTER_H;

    drawCatalogPanel(
        0,
        contentTop,
        leftWidth,
        contentHeight,
        true
    );

    if (rightWidth > 0) {
        drawDetailPanel(
            leftWidth,
            contentTop,
            rightWidth,
            contentHeight
        );
    }

    vita2d_draw_rectangle(
        0,
        SCREEN_H - FOOTER_H,
        SCREEN_W,
        FOOTER_H,
        SURFACE2
    );

    vita2d_pgf_draw_text(
        font_,
        12,
        SCREEN_H - 14,
        TEXT_DIM,
        0.74f,
        "Closing detail..."
    );

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void FullCatalogScreen::draw() {
    switch (state_.mode) {
        case UiMode::FULL_CATALOG:
            drawFullCatalog();
            break;

        case UiMode::OPENING_DETAIL:
            drawOpeningDetail();
            break;

        case UiMode::SPLIT_DETAIL:
            drawSplitDetail();
            break;

        case UiMode::CLOSING_DETAIL:
            drawClosingDetail();
            break;
    }
}

bool FullCatalogScreen::updateAndDraw() {
    if (!ready_) {
        return false;
    }

    handleInput();
    updateTransition();
    draw();

    return !state_.requestExit;
}

} // namespace ui
} // namespace psvitaalive

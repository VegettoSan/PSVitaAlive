#include "ui/full_catalog_screen.hpp"

#include <psp2/ctrl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace psvitaalive {
namespace ui {

namespace {
constexpr unsigned BG = RGBA8(0x00, 0x00, 0x00, 0xFF);
constexpr unsigned SURFACE = RGBA8(0x37, 0x37, 0x37, 0xFF);
constexpr unsigned SURFACE2 = RGBA8(0x2A, 0x2A, 0x2A, 0xFF);
constexpr unsigned BORDER = RGBA8(0x6E, 0x6E, 0x6E, 0xFF);
constexpr unsigned TEXT = RGBA8(0xAA, 0xAA, 0xAA, 0xFF);
constexpr unsigned TEXT_DIM = RGBA8(0x6E, 0x6E, 0x6E, 0xFF);
constexpr unsigned ACCENT = RGBA8(0x3B, 0xFF, 0x00, 0xFF);
constexpr unsigned WHITE = RGBA8(0xFF, 0xFF, 0xFF, 0xFF);
constexpr unsigned PANEL = RGBA8(0x20, 0x20, 0x20, 0xFF);
constexpr int FULL_CARD_H = 120;
constexpr int SPLIT_CARD_H = 82;
constexpr int DETAIL_HEADER_H = 92;
constexpr int DETAIL_LINE_H = 18;
constexpr int TRANSITION_MS = 200;
}

FullCatalogScreen::FullCatalogScreen() = default;
FullCatalogScreen::~FullCatalogScreen() { shutdown(); }

void FullCatalogScreen::setCatalogItems(
    std::vector<CatalogItem> items
) {
    items_ = std::move(items);

    state_.focusIndex = 0;
    state_.catalogScrollRow = 0;
    state_.detailScroll = 0;
}

void FullCatalogScreen::setInstallCallbacks(
    InstallRequestFn requestInstall,
    InstallStatusFn statusText
) {
    installRequest_ = std::move(requestInstall);
    installStatusText_ = std::move(statusText);
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

    state_.mode = UiMode::FULL_CATALOG;
    state_.activePanel = UiPanel::Catalog;
    state_.focusIndex = 0;
    state_.catalogScrollRow = 0;
    state_.detailScroll = 0;
    state_.requestExit = false;
    ready_ = true;

    sceClibPrintf("[UI] Phase 10 initialized\n");
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

    auto add = [this](
        const char* id,
        const char* titleId,
        const char* name,
        const char* author,
        const char* description,
        const char* longDescription,
        const char* status,
        const char* version,
        const char* versionDate,
        const char* requirements,
        const char* size,
        const char* category,
        const char* subcategory,
        const char* changelog,
        const char* downloadUrl = nullptr,
        const char* downloadFileName = nullptr
    ) {
        CatalogItem item;
        item.id = id;
        item.titleId = titleId;
        item.name = name;
        item.author = author;
        item.description = description;
        item.longDescription = longDescription;
        item.status = status;
        item.version = version;
        item.versionDate = versionDate;
        item.requirements = requirements;
        item.size = size;
        item.category = category;
        item.subcategory = subcategory;
        item.changelog = changelog;
        if (downloadUrl) item.downloadUrl = downloadUrl;
        if (downloadFileName) item.downloadFileName = downloadFileName;
        items_.push_back(item);
    };

    // Mock entries deliberately keep URLs empty until real catalog parsing is added.
    add("mock_0", "VITASHELL", "VitaShell", "TheFloW",
        "File manager and shell environment for PlayStation Vita.",
        "VitaShell is a file manager and shell application for the PlayStation Vita. It provides file management, FTP, USB and other useful system functions.",
        "Verified", "2.02", "2024-01-01", "HENkaku / compatible Vita environment", "5 MB", "Utilities", "File Manager",
        "- Improved stability\n- Updated system compatibility\n- Various fixes");
    add("mock_1", "ADRENALINE", "Adrenaline", "TheFloW",
        "PSP emulator and enhancement environment for PS Vita.",
        "Adrenaline is a homebrew application that exploits the built-in PSP emulator of the PlayStation Vita and provides additional PSP functionality.",
        "Verified", "7.1", "2024-02-01", "PS Vita with compatible custom firmware", "10 MB", "Emulator", "PSP",
        "- Compatibility improvements\n- Stability fixes");
    add("mock_2", "RETROARCH", "RetroArch", "Libretro",
        "Frontend for multiple emulator cores.",
        "RetroArch is a frontend for emulators, game engines and games. It provides a unified interface for many different systems and platforms.",
        "Verified", "1.19.0", "2024-03-01", "Additional cores may require extra files", "25 MB", "Emulator", "Multi-system",
        "- Updated cores\n- Performance improvements\n- User interface fixes");
    add("mock_3", "PKGJ00000", "PKGj", "blastrock",
        "Homebrew application for browsing and downloading content.",
        "PKGj provides a graphical interface for browsing available content sources and managing downloads.",
        "Legacy", "0.57", "2023-01-01", "Compatible Vita environment", "4 MB", "Utilities", "Downloader",
        "- Legacy release preserved by VitaHub");
    add("mock_4", "VITADB000", "VitaDB", "devnoname120",
        "Homebrew discovery and application database.",
        "VitaDB provides information about PlayStation Vita homebrew applications and related projects.",
        "Legacy", "1.0", "2022-01-01", "None", "3 MB", "Store", "Database",
        "- Legacy catalog entry");
    add("mock_5", "SAVEMGR001", "SaveManager", "Community",
        "Save management utility for PS Vita.",
        "Utility for inspecting and managing application save data.",
        "Archive", "1.0", "2020-01-01", "Compatible Vita environment", "2 MB", "Utilities", "Save Manager",
        "- Archived project");
    add("mock_6", "BATTERY001", "BatteryMgr", "Community",
        "Battery information utility.",
        "Displays information related to the current Vita battery and power state.",
        "Verified", "1.2", "2024-04-01", "None", "1 MB", "Utilities", "System",
        "- Improved system information");
    add("mock_7", "FTP000001", "FTP Client", "Community",
        "FTP utility for transferring files.",
        "A simple FTP-oriented utility for transferring files between a PC and the PlayStation Vita.",
        "Verified", "1.1", "2024-05-01", "Network connection", "1 MB", "Utilities", "Network",
        "- Improved connection handling");
    add("mock_8", "THEME001", "Theme Manager", "Community",
        "Utility for managing custom themes.",
        "Manage and organize custom PlayStation Vita themes.",
        "Verified", "2.0", "2024-06-01", "Custom theme files", "2 MB", "Customization", "Themes",
        "- Added theme browsing\n- Various fixes");
    add("mock_9", "CHEAT0001", "Cheat Device", "Community",
        "Cheat and debugging utility.",
        "Utility for compatible homebrew and development workflows.",
        "Archive", "1.0", "2019-01-01", "Compatible environment", "2 MB", "Utilities", "Development",
        "- Archived project");
    add("mock_10", "MUSIC0001", "Music Player", "Community",
        "Simple music player.",
        "A lightweight music playback application for the Vita.",
        "Verified", "1.5", "2024-07-01", "Compatible audio files", "3 MB", "Multimedia", "Audio",
        "- Improved playback\n- Stability fixes");
    add("mock_11", "BROWSER001", "File Browser", "Community",
        "Lightweight file browsing application.",
        "Browse files and directories on supported Vita storage devices.",
        "Verified", "1.0", "2024-08-01", "None", "2 MB", "Utilities", "File Manager",
        "- Initial release");

    for (auto& item : items_) {
        if (item.status == "Verified") item.links.push_back("Download");
        item.links.push_back("Repository");
    }

    state_.focusIndex = 0;
    state_.catalogScrollRow = 0;
    state_.detailScroll = 0;
}

int FullCatalogScreen::totalRows() const {
    if (items_.empty()) return 0;
    return (static_cast<int>(items_.size()) + GRID_COLS - 1) / GRID_COLS;
}

int FullCatalogScreen::visibleRowsFull() const {
    const int gridHeight = SCREEN_H - HEADER_H - TABS_H - FOOTER_H - GRID_PAD * 2;
    return std::max(1, gridHeight / (FULL_CARD_H + CARD_GAP));
}

int FullCatalogScreen::visibleRowsSplit() const {
    const int height = SCREEN_H - HEADER_H - TABS_H - FOOTER_H - GRID_PAD * 2;
    return std::max(1, height / (SPLIT_CARD_H + CARD_GAP));
}

int FullCatalogScreen::selectedIndex() const {
    if (items_.empty()) return -1;
    return std::max(0, std::min(state_.focusIndex, static_cast<int>(items_.size()) - 1));
}

void FullCatalogScreen::clampCatalogFocus() {
    if (items_.empty()) {
        state_.focusIndex = 0;
        return;
    }
    state_.focusIndex = std::max(0, std::min(state_.focusIndex, static_cast<int>(items_.size()) - 1));
}

void FullCatalogScreen::clampCatalogScroll() {
    if (items_.empty()) {
        state_.catalogScrollRow = 0;
        return;
    }

    const int visible = state_.mode == UiMode::FULL_CATALOG ? visibleRowsFull() : visibleRowsSplit();

    if (state_.mode == UiMode::FULL_CATALOG) {
        const int focusRow = state_.focusIndex / GRID_COLS;
        if (focusRow < state_.catalogScrollRow) state_.catalogScrollRow = focusRow;
        if (focusRow >= state_.catalogScrollRow + visible) state_.catalogScrollRow = focusRow - visible + 1;
        state_.catalogScrollRow = std::max(0, std::min(state_.catalogScrollRow, std::max(0, totalRows() - visible)));
    } else {
        const int maxScroll = std::max(0, static_cast<int>(items_.size()) - visible);
        state_.catalogScrollRow = std::max(0, std::min(state_.catalogScrollRow, maxScroll));
        if (state_.focusIndex < state_.catalogScrollRow) state_.catalogScrollRow = state_.focusIndex;
        if (state_.focusIndex >= state_.catalogScrollRow + visible) state_.catalogScrollRow = state_.focusIndex - visible + 1;
        state_.catalogScrollRow = std::max(0, std::min(state_.catalogScrollRow, maxScroll));
    }
}

void FullCatalogScreen::clampDetailScroll() {
    state_.detailScroll = std::max(0, std::min(state_.detailScroll, 1200));
}

void FullCatalogScreen::moveCatalogFocus(int direction) {
    if (items_.empty()) return;

    if (state_.mode == UiMode::FULL_CATALOG) {
        if (direction < 0 && state_.focusIndex >= GRID_COLS) state_.focusIndex -= GRID_COLS;
        if (direction > 0 && state_.focusIndex + GRID_COLS < static_cast<int>(items_.size())) state_.focusIndex += GRID_COLS;
    } else {
        if (direction < 0 && state_.focusIndex > 0) --state_.focusIndex;
        if (direction > 0 && state_.focusIndex + 1 < static_cast<int>(items_.size())) ++state_.focusIndex;
    }

    clampCatalogFocus();
    clampCatalogScroll();
    state_.detailScroll = 0;
}

void FullCatalogScreen::moveDetailScroll(int direction) {
    constexpr int STEP = 24;
    state_.detailScroll += direction < 0 ? -STEP : STEP;
    clampDetailScroll();
}

void FullCatalogScreen::changeCatalog(int direction) {
    int catalog = static_cast<int>(state_.catalog) + direction;
    const int count = static_cast<int>(CatalogType::Count);
    if (catalog < 0) catalog = count - 1;
    if (catalog >= count) catalog = 0;
    state_.catalog = static_cast<CatalogType>(catalog);
    state_.focusIndex = 0;
    state_.catalogScrollRow = 0;
    state_.detailScroll = 0;
}

bool FullCatalogScreen::isTransitioning() const {
    return state_.mode == UiMode::OPENING_DETAIL || state_.mode == UiMode::CLOSING_DETAIL;
}

void FullCatalogScreen::startOpeningDetail() {
    if (state_.mode != UiMode::FULL_CATALOG) return;
    state_.activePanel = UiPanel::Catalog;
    state_.detailScroll = 0;
    state_.transitionStart = sceKernelGetProcessTimeWide();
    state_.mode = UiMode::OPENING_DETAIL;
    sceClibPrintf("[UI] Opening detail\n");
}

void FullCatalogScreen::startClosingDetail() {
    if (state_.mode != UiMode::SPLIT_DETAIL) return;
    state_.transitionStart = sceKernelGetProcessTimeWide();
    state_.mode = UiMode::CLOSING_DETAIL;
    sceClibPrintf("[UI] Closing detail\n");
}

float FullCatalogScreen::transitionProgress() const {
    if (!isTransitioning()) return 1.0f;
    const uint64_t elapsed = sceKernelGetProcessTimeWide() - state_.transitionStart;
    return std::max(0.0f, std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(TRANSITION_MS * 1000)));
}

void FullCatalogScreen::updateTransition() {
    if (!isTransitioning() || transitionProgress() < 1.0f) return;

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
    if (isTransitioning()) return;

    SceCtrlData pad;
    std::memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(0, &pad, 1);

    static uint32_t previousButtons = 0;
    const uint32_t pressed = pad.buttons & ~previousButtons;
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
        if (pressed & SCE_CTRL_LEFT && state_.focusIndex % GRID_COLS > 0) {
            --state_.focusIndex;
            clampCatalogScroll();
        }
        if (pressed & SCE_CTRL_RIGHT && state_.focusIndex % GRID_COLS < GRID_COLS - 1 && state_.focusIndex + 1 < static_cast<int>(items_.size())) {
            ++state_.focusIndex;
            clampCatalogScroll();
        }
        if (pressed & SCE_CTRL_UP) moveCatalogFocus(-1);
        if (pressed & SCE_CTRL_DOWN) moveCatalogFocus(1);
        if (pressed & SCE_CTRL_CROSS) startOpeningDetail();
        return;
    }

    if (state_.mode != UiMode::SPLIT_DETAIL) return;

    if (pressed & SCE_CTRL_CIRCLE) {
        startClosingDetail();
        return;
    }

    if (pressed & SCE_CTRL_LEFT) state_.activePanel = UiPanel::Catalog;
    if (pressed & SCE_CTRL_RIGHT) state_.activePanel = UiPanel::Detail;

    if (state_.activePanel == UiPanel::Catalog) {
        if (pressed & SCE_CTRL_UP) moveCatalogFocus(-1);
        if (pressed & SCE_CTRL_DOWN) moveCatalogFocus(1);
        if (pressed & SCE_CTRL_CROSS && selectedIndex() >= 0) {
            state_.detailScroll = 0;
            sceClibPrintf("[UI] Selected: %s\n", items_[selectedIndex()].name.c_str());
        }
    } else {
        if (pressed & SCE_CTRL_UP) moveDetailScroll(-1);
        if (pressed & SCE_CTRL_DOWN) moveDetailScroll(1);

        // Triangle is the Phase 10 install action in the detail panel.
        if (pressed & SCE_CTRL_TRIANGLE && selectedIndex() >= 0) {
            const CatalogItem& item = items_[selectedIndex()];
            if (installRequest_) {
                const bool accepted = installRequest_(item);
                sceClibPrintf("[UI] Install request: %s accepted=%d\n", item.name.c_str(), accepted ? 1 : 0);
            } else {
                sceClibPrintf("[UI] Install callback unavailable\n");
            }
        }
    }
}

unsigned FullCatalogScreen::colorForStatus(const std::string& status) const {
    if (status == "Verified") return ACCENT;
    if (status == "Legacy") return TEXT;
    if (status == "Archive") return TEXT_DIM;
    return TEXT;
}

void FullCatalogScreen::drawHeader(int width) {
    vita2d_draw_rectangle(0, 0, width, HEADER_H, SURFACE2);
    vita2d_pgf_draw_text(font_, 16, 32, ACCENT, 1.2f, "PSVitaAlive");
    vita2d_pgf_draw_text(font_, width - 130, 32, TEXT_DIM, 0.9f, "START: exit");
}

void FullCatalogScreen::drawTabs(int width) {
    vita2d_draw_rectangle(0, HEADER_H, width, TABS_H, SURFACE);
    const float tabWidth = static_cast<float>(width) / static_cast<int>(CatalogType::Count);
    for (int i = 0; i < static_cast<int>(CatalogType::Count); ++i) {
        const int x = static_cast<int>(i * tabWidth);
        const bool active = static_cast<int>(state_.catalog) == i;
        if (active) vita2d_draw_rectangle(x, HEADER_H + TABS_H - 3, static_cast<int>(tabWidth), 3, ACCENT);
        vita2d_pgf_draw_text(font_, x + 12, HEADER_H + 24, active ? ACCENT : TEXT, 0.9f, catalogName(static_cast<CatalogType>(i)));
    }
}

void FullCatalogScreen::drawCatalogCard(const CatalogItem& item, int index, int x, int y, int width, int height, bool focused) {
    vita2d_draw_rectangle(x, y, width, height, SURFACE);
    if (focused) {
        vita2d_draw_rectangle(x, y, width, 3, ACCENT);
        vita2d_draw_rectangle(x, y + height - 3, width, 3, ACCENT);
        vita2d_draw_rectangle(x, y, 3, height, ACCENT);
        vita2d_draw_rectangle(x + width - 3, y, 3, height, ACCENT);
    } else {
        vita2d_draw_rectangle(x, y, width, 1, BORDER);
    }

    const int iconSize = height < 100 ? 48 : 64;
    vita2d_draw_rectangle(x + 8, y + 8, iconSize, iconSize, SURFACE2);
    const int textX = x + iconSize + 18;
    vita2d_pgf_draw_text(font_, textX, y + 27, WHITE, 0.95f, item.name.c_str());
    vita2d_pgf_draw_text(font_, textX, y + 48, TEXT, 0.78f, item.author.c_str());
    vita2d_pgf_draw_text(font_, textX, y + 68, colorForStatus(item.status), 0.75f, item.status.c_str());
    if (height >= 100) vita2d_pgf_draw_text(font_, x + 10, y + height - 18, TEXT_DIM, 0.72f, item.version.c_str());
    (void)index;
}

void FullCatalogScreen::drawCatalogPanel(int x, int y, int width, int height, bool splitMode) {
    vita2d_draw_rectangle(x, y, width, height, PANEL);
    const int top = y + GRID_PAD;

    if (!splitMode) {
        const int usableWidth = width - GRID_PAD * 2 - CARD_GAP * (GRID_COLS - 1);
        const int cardWidth = usableWidth / GRID_COLS;
        const int visible = visibleRowsFull();
        for (int row = 0; row < visible; ++row) {
            const int realRow = state_.catalogScrollRow + row;
            for (int col = 0; col < GRID_COLS; ++col) {
                const int index = realRow * GRID_COLS + col;
                if (index < 0 || index >= static_cast<int>(items_.size())) continue;
                const int cardX = x + GRID_PAD + col * (cardWidth + CARD_GAP);
                const int cardY = top + row * (FULL_CARD_H + CARD_GAP);
                drawCatalogCard(items_[index], index, cardX, cardY, cardWidth, FULL_CARD_H, index == state_.focusIndex);
            }
        }
    } else {
        const int visible = visibleRowsSplit();
        for (int row = 0; row < visible; ++row) {
            const int index = state_.catalogScrollRow + row;
            if (index < 0 || index >= static_cast<int>(items_.size())) continue;
            const int cardY = top + row * (SPLIT_CARD_H + CARD_GAP);
            drawCatalogCard(items_[index], index, x + GRID_PAD, cardY, width - GRID_PAD * 2, SPLIT_CARD_H, index == state_.focusIndex);
        }
    }

    if (state_.mode == UiMode::SPLIT_DETAIL && state_.activePanel == UiPanel::Catalog) {
        vita2d_draw_rectangle(x, y, 3, height, ACCENT);
    }
}

void FullCatalogScreen::wrapText(const std::string& text, int maxChars, std::vector<std::string>& lines) const {
    lines.clear();
    if (text.empty()) return;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
            continue;
        }
        if (static_cast<int>(current.size()) >= maxChars && c == ' ') {
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
    if (!current.empty()) lines.push_back(current);
}

void FullCatalogScreen::drawTextLines(const std::vector<std::string>& lines, int x, int y, int lineHeight, unsigned color, float scale, int startLine, int maxLines) {
    if (lines.empty()) return;
    const int first = std::max(0, startLine);
    const int last = std::min(static_cast<int>(lines.size()), first + maxLines);
    int drawY = y;
    for (int i = first; i < last; ++i) {
        vita2d_pgf_draw_text(font_, x, drawY, color, scale, lines[i].c_str());
        drawY += lineHeight;
    }
}

void FullCatalogScreen::drawDetailContent(const CatalogItem& item, int x, int y, int width, int height) {
    const int contentX = x + 18;
    const int contentWidth = width - 36;
    const int maxChars = std::max(20, contentWidth / 8);
    std::vector<std::string> allLines;

    auto addWrapped = [&](const char* title, const std::string& value) {
        allLines.push_back(title);
        std::vector<std::string> lines;
        wrapText(value, maxChars, lines);
        for (const auto& line : lines) allLines.push_back(line);
        allLines.push_back("");
    };

    addWrapped("Description", item.description);
    addWrapped("Long Description", item.longDescription);
    addWrapped("Requirements", item.requirements);

    allLines.push_back("Information");
    allLines.push_back("Title ID: " + item.titleId);
    allLines.push_back("Version: " + item.version);
    allLines.push_back("Version Date: " + item.versionDate);
    allLines.push_back("Category: " + item.category);
    allLines.push_back("Subcategory: " + item.subcategory);
    allLines.push_back("Size: " + item.size);
    allLines.push_back("Status: " + item.status);
    allLines.push_back("");

    allLines.push_back("Downloads / Links");
    for (const auto& link : item.links) allLines.push_back("- " + link);
    if (!item.downloadUrl.empty()) allLines.push_back("- Install source available");
    allLines.push_back("");

    addWrapped("Changelog", item.changelog);

    const int visibleLines = std::max(1, (height - 110) / DETAIL_LINE_H);
    const int maxScroll = std::max(0, static_cast<int>(allLines.size()) - visibleLines);
    const int scroll = std::max(0, std::min(state_.detailScroll, maxScroll));

    drawTextLines(allLines, contentX, y + 110, DETAIL_LINE_H, TEXT, 0.78f, scroll, visibleLines);

    if (maxScroll > 0) {
        const int trackX = x + width - 8;
        const int trackY = y + 100;
        const int trackH = height - 120;
        vita2d_draw_rectangle(trackX, trackY, 3, trackH, BORDER);
        const int thumbH = std::max(20, trackH * visibleLines / static_cast<int>(allLines.size()));
        const int thumbY = trackY + (trackH - thumbH) * scroll / maxScroll;
        vita2d_draw_rectangle(trackX, thumbY, 3, thumbH, ACCENT);
    }
}

void FullCatalogScreen::drawDetailPanel(int x, int y, int width, int height) {
    vita2d_draw_rectangle(x, y, width, height, PANEL);
    const int index = selectedIndex();
    if (index < 0) return;
    const CatalogItem& item = items_[index];

    if (state_.activePanel == UiPanel::Detail) vita2d_draw_rectangle(x + width - 3, y, 3, height, ACCENT);
    vita2d_draw_rectangle(x, y, width, DETAIL_HEADER_H, SURFACE);
    vita2d_draw_rectangle(x + 16, y + 14, 64, 64, SURFACE2);
    vita2d_pgf_draw_text(font_, x + 94, y + 31, WHITE, 1.0f, item.name.c_str());
    vita2d_pgf_draw_text(font_, x + 94, y + 52, TEXT, 0.78f, item.author.c_str());
    vita2d_pgf_draw_text(font_, x + 94, y + 72, colorForStatus(item.status), 0.78f, item.status.c_str());

    if (state_.activePanel == UiPanel::Detail) {
        const std::string status = installStatusText_ ? installStatusText_() : std::string();
        if (!status.empty()) vita2d_pgf_draw_text(font_, x + 94, y + 87, ACCENT, 0.65f, status.c_str());
    }

    drawDetailContent(item, x, y, width, height);
}

void FullCatalogScreen::drawFullCatalog() {
    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(SCREEN_W);
    drawTabs(SCREEN_W);

    const int gridTop = HEADER_H + TABS_H;
    const int gridHeight = SCREEN_H - HEADER_H - TABS_H - FOOTER_H;
    drawCatalogPanel(0, gridTop, SCREEN_W, gridHeight, false);

    vita2d_draw_rectangle(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, SURFACE2);
    vita2d_pgf_draw_text(font_, 12, SCREEN_H - 14, TEXT, 0.78f,
        "D-Pad: move   X: detail   L/R: catalog   START: exit");
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void FullCatalogScreen::drawSplitDetail() {
    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(SCREEN_W);

    const int contentTop = HEADER_H + TABS_H;
    const int contentHeight = SCREEN_H - HEADER_H - TABS_H - FOOTER_H;
    const int leftWidth = SCREEN_W / 2;
    const int rightWidth = SCREEN_W - leftWidth;

    drawCatalogPanel(0, contentTop, leftWidth, contentHeight, true);
    drawDetailPanel(leftWidth, contentTop, rightWidth, contentHeight);
    vita2d_draw_rectangle(leftWidth - 1, contentTop, 2, contentHeight, BORDER);

    vita2d_draw_rectangle(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, SURFACE2);
    vita2d_pgf_draw_text(font_, 12, SCREEN_H - 14, TEXT, 0.70f,
        "D-Pad: navigate/scroll   LEFT/RIGHT: panel   O: back   △: install   L/R: catalog");
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void FullCatalogScreen::drawOpeningDetail() {
    const float progress = transitionProgress();
    const int leftWidth = SCREEN_W - static_cast<int>((SCREEN_W / 2) * progress);
    const int rightWidth = SCREEN_W - leftWidth;

    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(SCREEN_W);
    const int contentTop = HEADER_H + TABS_H;
    const int contentHeight = SCREEN_H - HEADER_H - TABS_H - FOOTER_H;
    drawCatalogPanel(0, contentTop, leftWidth, contentHeight, true);
    if (rightWidth > 0) drawDetailPanel(leftWidth, contentTop, rightWidth, contentHeight);
    vita2d_draw_rectangle(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, SURFACE2);
    vita2d_pgf_draw_text(font_, 12, SCREEN_H - 14, TEXT_DIM, 0.74f, "Opening detail...");
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void FullCatalogScreen::drawClosingDetail() {
    const float remaining = 1.0f - transitionProgress();
    const int leftWidth = SCREEN_W - static_cast<int>((SCREEN_W / 2) * remaining);
    const int rightWidth = SCREEN_W - leftWidth;

    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(SCREEN_W);
    const int contentTop = HEADER_H + TABS_H;
    const int contentHeight = SCREEN_H - HEADER_H - TABS_H - FOOTER_H;
    drawCatalogPanel(0, contentTop, leftWidth, contentHeight, true);
    if (rightWidth > 0) drawDetailPanel(leftWidth, contentTop, rightWidth, contentHeight);
    vita2d_draw_rectangle(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, SURFACE2);
    vita2d_pgf_draw_text(font_, 12, SCREEN_H - 14, TEXT_DIM, 0.74f, "Closing detail...");
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void FullCatalogScreen::draw() {
    switch (state_.mode) {
        case UiMode::FULL_CATALOG: drawFullCatalog(); break;
        case UiMode::OPENING_DETAIL: drawOpeningDetail(); break;
        case UiMode::SPLIT_DETAIL: drawSplitDetail(); break;
        case UiMode::CLOSING_DETAIL: drawClosingDetail(); break;
    }
}

bool FullCatalogScreen::updateAndDraw() {
    if (!ready_) return false;
    handleInput();
    updateTransition();
    draw();
    return !state_.requestExit;
}

} // namespace ui
} // namespace psvitaalive

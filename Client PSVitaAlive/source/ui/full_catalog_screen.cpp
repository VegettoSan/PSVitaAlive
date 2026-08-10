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
constexpr int FULL_CARD_H = 132;
constexpr int SPLIT_CARD_H = 82;
constexpr int DETAIL_HEADER_H = 92;
constexpr int DETAIL_LINE_H = 18;
constexpr int TRANSITION_MS = 200;

const char* imageExtension(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    return path.c_str() + dot;
}
}

FullCatalogScreen::FullCatalogScreen() = default;
FullCatalogScreen::~FullCatalogScreen() { shutdown(); }

void FullCatalogScreen::setInstallCallbacks(InstallRequestFn requestInstall, InstallStatusFn statusText) {
    installRequest_ = std::move(requestInstall);
    installStatusText_ = std::move(statusText);
}

void FullCatalogScreen::setCatalogChangeCallback(CatalogChangeFn callback) {
    catalogChange_ = std::move(callback);
}

void FullCatalogScreen::setImageCache(ImageCache* cache) {
    imageCache_ = cache;
}

void FullCatalogScreen::setCatalogItems(std::vector<CatalogItem> items) {
    items_ = std::move(items);
    state_.focusIndex = 0;
    state_.catalogScrollRow = 0;
    state_.detailScroll = 0;
    catalogLoading_ = false;
    catalogError_.clear();
}

void FullCatalogScreen::setCatalogLoading(bool loading, const std::string& label, uint64_t current, uint64_t total, const std::string& message) {
    catalogLoading_ = loading;
    catalogLoadingLabel_ = label;
    catalogLoadingCurrent_ = current;
    catalogLoadingTotal_ = total;
    catalogLoadingMessage_ = message;
    if (loading) catalogError_.clear();
}

void FullCatalogScreen::setCatalogError(const std::string& error) {
    catalogLoading_ = false;
    catalogError_ = error;
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
    sceClibPrintf("[UI] FULL_CATALOG initialized\n");
    return true;
}

void FullCatalogScreen::releaseTextures() {
    for (auto& entry : textures_) {
        if (entry.second) vita2d_free_texture(entry.second);
    }
    textures_.clear();
}

void FullCatalogScreen::shutdown() {
    releaseTextures();
    if (font_) {
        vita2d_free_pgf(font_);
        font_ = nullptr;
    }
    if (ready_) {
        vita2d_fini();
        ready_ = false;
    }
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
        if (state_.focusIndex < state_.catalogScrollRow) state_.catalogScrollRow = state_.focusIndex;
        if (state_.focusIndex >= state_.catalogScrollRow + visible) state_.catalogScrollRow = state_.focusIndex - visible + 1;
        state_.catalogScrollRow = std::max(0, std::min(state_.catalogScrollRow, maxScroll));
    }
}

void FullCatalogScreen::clampDetailScroll() {
    state_.detailScroll = std::max(0, std::min(state_.detailScroll, 1600));
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
    int value = static_cast<int>(state_.catalog) + direction;
    const int count = static_cast<int>(CatalogType::Count);
    if (value < 0) value = count - 1;
    if (value >= count) value = 0;
    const CatalogType next = static_cast<CatalogType>(value);

    if (catalogChange_) {
        if (catalogChange_(next)) {
            catalogLoading_ = next != CatalogType::Homebrew;
            catalogLoadingLabel_ = catalogName(next);
            catalogLoadingCurrent_ = 0;
            catalogLoadingTotal_ = 0;
            catalogLoadingMessage_ = "Connecting to catalog...";
            catalogError_.clear();
        }
        return;
    }

    state_.catalog = next;
    state_.focusIndex = 0;
    state_.catalogScrollRow = 0;
    state_.detailScroll = 0;
}

bool FullCatalogScreen::isTransitioning() const {
    return state_.mode == UiMode::OPENING_DETAIL || state_.mode == UiMode::CLOSING_DETAIL;
}

void FullCatalogScreen::startOpeningDetail() {
    if (state_.mode != UiMode::FULL_CATALOG || catalogLoading_ || selectedIndex() < 0) return;
    state_.activePanel = UiPanel::Catalog;
    state_.detailScroll = 0;
    state_.transitionStart = sceKernelGetProcessTimeWide();
    state_.mode = UiMode::OPENING_DETAIL;
}

void FullCatalogScreen::startClosingDetail() {
    if (state_.mode != UiMode::SPLIT_DETAIL) return;
    state_.transitionStart = sceKernelGetProcessTimeWide();
    state_.mode = UiMode::CLOSING_DETAIL;
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
    } else {
        state_.mode = UiMode::FULL_CATALOG;
    }
    state_.activePanel = UiPanel::Catalog;
    clampCatalogFocus();
    clampCatalogScroll();
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

    if (catalogLoading_) return;

    if (state_.mode == UiMode::FULL_CATALOG) {
        if (pressed & SCE_CTRL_LEFT) {
            if (state_.focusIndex % GRID_COLS > 0) --state_.focusIndex;
            clampCatalogScroll();
        }
        if (pressed & SCE_CTRL_RIGHT) {
            if (state_.focusIndex % GRID_COLS < GRID_COLS - 1 && state_.focusIndex + 1 < static_cast<int>(items_.size())) ++state_.focusIndex;
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
        if (pressed & SCE_CTRL_CROSS) state_.detailScroll = 0;
    } else {
        if (pressed & SCE_CTRL_UP) moveDetailScroll(-1);
        if (pressed & SCE_CTRL_DOWN) moveDetailScroll(1);
        if (pressed & SCE_CTRL_TRIANGLE && selectedIndex() >= 0 && installRequest_) {
            const CatalogItem& item = items_[selectedIndex()];
            const bool accepted = installRequest_(item);
            sceClibPrintf("[UI] Install request: %s accepted=%d\n", item.name.c_str(), accepted ? 1 : 0);
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
    vita2d_pgf_draw_text(font_, 16, 32, ACCENT, 1.15f, "PSVitaAlive Store");
    vita2d_pgf_draw_text(font_, width - 132, 32, TEXT_DIM, 0.78f, "START: Exit");
}

void FullCatalogScreen::drawTabs(int width) {
    vita2d_draw_rectangle(0, HEADER_H, width, TABS_H, SURFACE);
    const float tabWidth = static_cast<float>(width) / static_cast<int>(CatalogType::Count);
    for (int i = 0; i < static_cast<int>(CatalogType::Count); ++i) {
        const int x = static_cast<int>(i * tabWidth);
        const bool active = static_cast<int>(state_.catalog) == i;
        if (active) vita2d_draw_rectangle(x, HEADER_H + TABS_H - 3, static_cast<int>(tabWidth), 3, ACCENT);
        vita2d_pgf_draw_text(font_, x + 12, HEADER_H + 24, active ? ACCENT : TEXT, 0.82f, catalogName(static_cast<CatalogType>(i)));
    }
}

void FullCatalogScreen::drawImage(const std::string& url, const std::string& namespaceName, int x, int y, int width, int height) {
    vita2d_draw_rectangle(x, y, width, height, SURFACE2);
    if (!imageCache_ || url.empty()) return;

    const std::string path = imageCache_->request(url, namespaceName);
    if (!imageCache_->isReady(path)) return;

    auto it = textures_.find(path);
    if (it == textures_.end()) {
        vita2d_texture* texture = nullptr;
        const char* ext = imageExtension(path);
        if (std::strcmp(ext, ".jpg") == 0 || std::strcmp(ext, ".jpeg") == 0) {
            texture = vita2d_load_JPEG_file(path.c_str());
        } else {
            texture = vita2d_load_PNG_file(path.c_str());
        }
        if (!texture) return;
        textures_[path] = texture;
        it = textures_.find(path);
    }

    vita2d_texture* texture = it->second;
    const float tw = static_cast<float>(vita2d_texture_get_width(texture));
    const float th = static_cast<float>(vita2d_texture_get_height(texture));
    if (tw <= 0.0f || th <= 0.0f) return;

    const float scale = std::min(static_cast<float>(width) / tw, static_cast<float>(height) / th);
    const float dw = tw * scale;
    const float dh = th * scale;
    vita2d_draw_texture_scale(texture, x + (width - dw) * 0.5f, y + (height - dh) * 0.5f, scale, scale);
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

    const int iconSize = height >= 100 ? 82 : 54;
    const int iconX = x + 10;
    const int iconY = y + 10;
    const std::string image = !item.icon.empty() ? item.icon : item.cover;
    drawImage(image, "app", iconX, iconY, iconSize, iconSize);

    const int textX = iconX + iconSize + 14;
    const int textWidth = width - iconSize - 30;
    (void)textWidth;
    vita2d_pgf_draw_text(font_, textX, y + 27, WHITE, 0.86f, item.name.c_str());
    vita2d_pgf_draw_text(font_, textX, y + 47, TEXT, 0.70f, item.author.empty() ? "Unknown author" : item.author.c_str());
    vita2d_pgf_draw_text(font_, textX, y + 67, colorForStatus(item.status), 0.68f, item.status.c_str());

    const std::string version = item.version.empty() ? "" : "v" + item.version;
    const std::string meta = version + (item.size.empty() ? "" : "  " + item.size);
    if (!meta.empty()) vita2d_pgf_draw_text(font_, x + 10, y + height - 13, TEXT_DIM, 0.64f, meta.c_str());
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
    const int maxChars = std::max(18, contentWidth / 7);

    // Screenshot strip follows the web detail page hierarchy: screenshots after
    // description and before technical information. Only the first three are
    // rendered at once to keep memory and GPU cost predictable on Vita.
    const int screenshotY = y + DETAIL_HEADER_H + 12 - state_.detailScroll;
    const int shotW = std::max(80, (contentWidth - 16) / 3);
    const int shotH = 74;
    for (int i = 0; i < 3 && i < static_cast<int>(item.screenshots.size()); ++i) {
        drawImage(item.screenshots[i], "shot", contentX + i * (shotW + 8), screenshotY, shotW, shotH);
    }

    std::vector<std::string> allLines;
    auto addWrapped = [&](const char* title, const std::string& value) {
        if (value.empty()) return;
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
    allLines.push_back("Release date: " + item.versionDate);
    allLines.push_back("Category: " + item.category);
    allLines.push_back("Subcategory: " + item.subcategory);
    allLines.push_back("Size: " + item.size);
    allLines.push_back("Status: " + item.status);
    allLines.push_back("");

    if (!item.linkDetails.empty()) {
        allLines.push_back("Downloads & Links");
        for (const auto& link : item.linkDetails) {
            std::string line = link.type;
            if (!link.name.empty()) {
                if (!line.empty()) line += ": ";
                line += link.name;
            }
            if (link.recommended) line += "  [Recommended]";
            allLines.push_back("- " + line);
        }
        allLines.push_back("");
    }

    addWrapped("Changelog", item.changelog);

    const int bodyTop = y + DETAIL_HEADER_H + (item.screenshots.empty() ? 18 : 96);
    const int visibleLines = std::max(1, (height - (bodyTop - y) - 12) / DETAIL_LINE_H);
    const int maxScroll = std::max(0, static_cast<int>(allLines.size()) - visibleLines);
    const int scroll = std::max(0, std::min(state_.detailScroll, maxScroll));
    drawTextLines(allLines, contentX, bodyTop, DETAIL_LINE_H, TEXT, 0.70f, scroll, visibleLines);

    if (maxScroll > 0) {
        const int trackX = x + width - 8;
        const int trackY = y + DETAIL_HEADER_H + 8;
        const int trackH = height - DETAIL_HEADER_H - 18;
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
    drawImage(!item.icon.empty() ? item.icon : item.cover, "app", x + 12, y + 12, 68, 68);
    vita2d_pgf_draw_text(font_, x + 92, y + 29, WHITE, 0.92f, item.name.c_str());
    vita2d_pgf_draw_text(font_, x + 92, y + 50, TEXT, 0.68f, item.author.empty() ? "Unknown author" : item.author.c_str());
    vita2d_pgf_draw_text(font_, x + 92, y + 70, colorForStatus(item.status), 0.68f, item.status.c_str());

    if (state_.activePanel == UiPanel::Detail) {
        const std::string status = installStatusText_ ? installStatusText_() : std::string();
        if (!status.empty()) vita2d_pgf_draw_text(font_, x + 92, y + 86, ACCENT, 0.56f, status.c_str());
        else if (!item.downloadUrl.empty()) vita2d_pgf_draw_text(font_, width + x - 140, y + 24, ACCENT, 0.62f, "△ Install");
    }

    drawDetailContent(item, x, y, width, height);
}

void FullCatalogScreen::drawLoadingOverlay() {
    const int w = 560;
    const int h = 220;
    const int x = (SCREEN_W - w) / 2;
    const int y = (SCREEN_H - h) / 2;

    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0x00, 0x00, 0x00, 0xB8));
    vita2d_draw_rectangle(x, y, w, h, SURFACE2);
    vita2d_draw_rectangle(x, y, w, 2, ACCENT);

    vita2d_pgf_draw_text(font_, x + 28, y + 34, ACCENT, 0.70f, "PSVitaAlive");
    vita2d_pgf_draw_text(font_, x + 28, y + 70, WHITE, 1.05f, ("Loading " + catalogLoadingLabel_ + "...").c_str());
    vita2d_pgf_draw_text(font_, x + 28, y + 98, TEXT, 0.70f, catalogLoadingMessage_.c_str());

    const int barX = x + 28;
    const int barY = y + 130;
    const int barW = w - 56;
    const int barH = 12;
    vita2d_draw_rectangle(barX, barY, barW, barH, BORDER);

    uint32_t percent = 0;
    if (catalogLoadingTotal_ > 0) {
        const uint64_t value = (catalogLoadingCurrent_ * 100ULL) / catalogLoadingTotal_;
        percent = static_cast<uint32_t>(std::min<uint64_t>(100, value));
    }
    if (catalogLoadingTotal_ == 0) {
        const uint64_t phase = (sceKernelGetProcessTimeWide() / 30000ULL) % 100ULL;
        percent = static_cast<uint32_t>(phase < 10 ? 10 : phase);
    }
    vita2d_draw_rectangle(barX, barY, (barW * percent) / 100, barH, ACCENT);

    char percentText[32];
    sceClibSnprintf(percentText, sizeof(percentText), "%u%%", percent);
    vita2d_pgf_draw_text(font_, x + 28, y + 174, TEXT, 0.76f, percentText);
    vita2d_pgf_draw_text(font_, x + w - 190, y + 174, TEXT_DIM, 0.62f, "Downloading / processing");
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
    vita2d_pgf_draw_text(font_, 12, SCREEN_H - 14, TEXT, 0.66f, "D-Pad: Navigate   X: Detail   L/R: Catalog   START: Exit");
    if (catalogLoading_) drawLoadingOverlay();
    if (!catalogError_.empty()) {
        vita2d_pgf_draw_text(font_, 18, HEADER_H + TABS_H + 26, ACCENT, 0.70f, catalogError_.c_str());
    }
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void FullCatalogScreen::drawSplitDetail() {
    vita2d_start_drawing();
    vita2d_clear_screen();
    drawHeader(SCREEN_W);
    drawTabs(SCREEN_W);

    const int contentTop = HEADER_H + TABS_H;
    const int contentHeight = SCREEN_H - HEADER_H - TABS_H - FOOTER_H;
    const int leftWidth = SCREEN_W / 2;
    drawCatalogPanel(0, contentTop, leftWidth, contentHeight, true);
    drawDetailPanel(leftWidth, contentTop, SCREEN_W - leftWidth, contentHeight);
    vita2d_draw_rectangle(leftWidth - 1, contentTop, 2, contentHeight, BORDER);

    vita2d_draw_rectangle(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, SURFACE2);
    vita2d_pgf_draw_text(font_, 12, SCREEN_H - 14, TEXT, 0.60f, "D-Pad: Navigate/Scroll   LEFT/RIGHT: Panel   O: Back   △: Install   L/R: Catalog");
    if (catalogLoading_) drawLoadingOverlay();
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
    drawTabs(SCREEN_W);
    const int contentTop = HEADER_H + TABS_H;
    const int contentHeight = SCREEN_H - HEADER_H - TABS_H - FOOTER_H;
    drawCatalogPanel(0, contentTop, leftWidth, contentHeight, true);
    if (rightWidth > 0) drawDetailPanel(leftWidth, contentTop, rightWidth, contentHeight);
    vita2d_draw_rectangle(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, SURFACE2);
    vita2d_pgf_draw_text(font_, 12, SCREEN_H - 14, TEXT_DIM, 0.66f, "Opening detail...");
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
    drawTabs(SCREEN_W);
    const int contentTop = HEADER_H + TABS_H;
    const int contentHeight = SCREEN_H - HEADER_H - TABS_H - FOOTER_H;
    drawCatalogPanel(0, contentTop, leftWidth, contentHeight, true);
    if (rightWidth > 0) drawDetailPanel(leftWidth, contentTop, rightWidth, contentHeight);
    vita2d_draw_rectangle(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, SURFACE2);
    vita2d_pgf_draw_text(font_, 12, SCREEN_H - 14, TEXT_DIM, 0.66f, "Closing detail...");
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

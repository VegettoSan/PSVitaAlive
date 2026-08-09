#pragma once

#include "ui/ui_types.hpp"

#include <vita2d.h>

#include <functional>
#include <string>
#include <vector>
#include <cstdint>

namespace psvitaalive {
namespace ui {

struct InstallUiStatus {
    bool visible = false;
    bool active = false;
    bool completed = false;
    bool failed = false;
    uint64_t current = 0;
    uint64_t total = 0;
    uint64_t bytesPerSecond = 0;
    std::string fileName;
    std::string stage;
    std::string message;
};

class FullCatalogScreen {
public:
    using InstallRequestFn = std::function<bool(const CatalogItem&)>;
    using InstallStatusFn = std::function<InstallUiStatus()>;

    FullCatalogScreen();
    ~FullCatalogScreen();

    bool init();
    void shutdown();
    bool updateAndDraw();

    void setInstallCallbacks(
        InstallRequestFn requestInstall,
        InstallStatusFn status
    );

    void setCatalogItems(
        std::vector<CatalogItem> items
    );

private:
    UiState state_;
    std::vector<CatalogItem> items_;

    InstallRequestFn installRequest_;
    InstallStatusFn installStatus_;

    vita2d_pgf* font_ = nullptr;
    bool ready_ = false;

    void loadMockData();

    void handleInput();

    void draw();
    void drawFullCatalog();
    void drawOpeningDetail();
    void drawSplitDetail();
    void drawClosingDetail();
    void drawInstallOverlay();

    void drawHeader(int width);
    void drawTabs(int width);

    void drawCatalogPanel(
        int x,
        int y,
        int width,
        int height,
        bool splitMode
    );

    void drawDetailPanel(
        int x,
        int y,
        int width,
        int height
    );

    void drawCatalogCard(
        const CatalogItem& item,
        int index,
        int x,
        int y,
        int width,
        int height,
        bool focused
    );

    void drawDetailContent(
        const CatalogItem& item,
        int x,
        int y,
        int width,
        int height
    );

    void startOpeningDetail();
    void startClosingDetail();

    void updateTransition();

    void clampCatalogFocus();
    void clampCatalogScroll();
    void clampDetailScroll();

    int totalRows() const;
    int visibleRowsFull() const;
    int visibleRowsSplit() const;

    int selectedIndex() const;

    float transitionProgress() const;

    bool isTransitioning() const;

    void changeCatalog(int direction);
    void moveCatalogFocus(int direction);
    void moveDetailScroll(int direction);

    void wrapText(
        const std::string& text,
        int maxChars,
        std::vector<std::string>& lines
    ) const;

    void drawTextLines(
        const std::vector<std::string>& lines,
        int x,
        int y,
        int lineHeight,
        unsigned color,
        float scale,
        int startLine,
        int maxLines
    );

    unsigned colorForStatus(const std::string& status) const;
};

} // namespace ui
} // namespace psvitaalive

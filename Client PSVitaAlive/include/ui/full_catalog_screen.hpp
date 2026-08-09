#pragma once

#include "ui/ui_types.hpp"

#include <vita2d.h>

#include <string>
#include <vector>

namespace psvitaalive {
namespace ui {

class FullCatalogScreen {
public:
    FullCatalogScreen();
    ~FullCatalogScreen();

    bool init();
    void shutdown();

    // Procesa una iteración de la UI.
    // Devuelve false cuando el usuario solicita salir.
    bool updateAndDraw();

private:
    UiState state_;
    std::vector<CatalogItem> items_;

    vita2d_pgf* font_ = nullptr;
    bool ready_ = false;

    void loadMockData();

    void handleInput();

    void draw();
    void drawFullCatalog();
    void drawOpeningDetail();
    void drawSplitDetail();
    void drawClosingDetail();

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

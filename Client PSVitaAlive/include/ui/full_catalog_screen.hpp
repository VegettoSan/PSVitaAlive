#pragma once

#include "ui/ui_types.hpp"
#include "ui/image_cache.hpp"

#include <vita2d.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace psvitaalive {
namespace ui {

class FullCatalogScreen {
public:
    using InstallRequestFn = std::function<bool(const CatalogItem&)>;
    using InstallStatusFn = std::function<std::string()>;
    using CatalogChangeFn = std::function<bool(CatalogType)>;

    FullCatalogScreen();
    ~FullCatalogScreen();

    bool init();
    void shutdown();
    bool updateAndDraw();

    void setInstallCallbacks(InstallRequestFn requestInstall, InstallStatusFn statusText);
    void setCatalogChangeCallback(CatalogChangeFn callback);
    void setImageCache(ImageCache* cache);
    void setCatalogItems(std::vector<CatalogItem> items);
    void setActiveCatalog(CatalogType catalog);

    void setCatalogLoading(bool loading, const std::string& label, uint64_t current, uint64_t total, const std::string& message);
    void setCatalogError(const std::string& error);

private:
    UiState state_;
    std::vector<CatalogItem> items_;
    InstallRequestFn installRequest_;
    InstallStatusFn installStatusText_;
    CatalogChangeFn catalogChange_;
    ImageCache* imageCache_ = nullptr;

    vita2d_pgf* font_ = nullptr;
    bool ready_ = false;

    bool catalogLoading_ = false;
    std::string catalogLoadingLabel_;
    std::string catalogLoadingMessage_;
    uint64_t catalogLoadingCurrent_ = 0;
    uint64_t catalogLoadingTotal_ = 0;
    std::string catalogError_;

    std::unordered_map<std::string, vita2d_texture*> textures_;

    void handleInput();
    void draw();
    void drawFullCatalog();
    void drawOpeningDetail();
    void drawSplitDetail();
    void drawClosingDetail();
    void drawHeader(int width);
    void drawTabs(int width);
    void drawCatalogPanel(int x, int y, int width, int height, bool splitMode);
    void drawDetailPanel(int x, int y, int width, int height);
    void drawCatalogCard(const CatalogItem& item, int index, int x, int y, int width, int height, bool focused);
    void drawDetailContent(const CatalogItem& item, int x, int y, int width, int height);
    void drawLoadingOverlay();
    void drawImage(const std::string& url, const std::string& namespaceName, int x, int y, int width, int height);
    void releaseTextures();

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
    void wrapText(const std::string& text, int maxChars, std::vector<std::string>& lines) const;
    void drawTextLines(const std::vector<std::string>& lines, int x, int y, int lineHeight, unsigned color, float scale, int startLine, int maxLines);
    unsigned colorForStatus(const std::string& status) const;
};

} // namespace ui
} // namespace psvitaalive

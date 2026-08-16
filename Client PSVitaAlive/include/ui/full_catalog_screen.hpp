#pragma once

#include "ui/ui_types.hpp"
#include "ui/image_cache.hpp"

#include <vita2d.h>
#include <psp2/io/stat.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace psvitaalive {
namespace ui {

class FullCatalogScreen {
public:
    using InstallRequestFn = std::function<bool(const CatalogItem&)>;
    using InstallStatusFn = std::function<std::string()>;
    using InstallCancelFn = std::function<void()>;
    using InstallAcknowledgeFn = std::function<void()>;
    using CatalogChangeFn = std::function<bool(CatalogType)>;
    using SearchRequestFn = std::function<std::string(const std::string&)>;
    using LinkActionFn = std::function<bool(const CatalogItem&, const CatalogLink&)>;

    FullCatalogScreen();
    ~FullCatalogScreen();
    bool init();
    void shutdown();
    bool updateAndDraw();

    void setInstallCallbacks(InstallRequestFn requestInstall, InstallStatusFn statusText);
    void setInstallCancelCallback(InstallCancelFn callback);
    void setInstallAcknowledgeCallback(InstallAcknowledgeFn callback);
    void setCatalogChangeCallback(CatalogChangeFn callback);
    void setSearchCallback(SearchRequestFn callback);
    void setLinkActionCallback(LinkActionFn callback);
    void setImageCache(ImageCache* cache);
    void setCatalogItems(std::vector<CatalogItem> items);
    void setActiveCatalog(CatalogType catalog);
    void setCatalogLoading(bool loading, const std::string& label, uint64_t current, uint64_t total, const std::string& message);
    void setCatalogError(const std::string& error);
    // outcome: 0 = progress, 1 = success, 2 = error
    void setInstallProgress(bool active, uint64_t current, uint64_t total, uint64_t bytesPerSecond,
                            const std::string& stage, const std::string& fileName,
                            const std::string& message,
                            int outcome = 0,
                            bool liveAreaOk = false,
                            const std::string& installPath = std::string(),
                            const std::string& titleId = std::string());

private:
    UiState state_;
    std::vector<CatalogItem> allItems_;
    std::vector<CatalogItem> items_;
    InstallRequestFn installRequest_;
    InstallStatusFn installStatusText_;
    InstallCancelFn installCancel_;
    InstallAcknowledgeFn installAcknowledge_;
    CatalogChangeFn catalogChange_;
    SearchRequestFn searchRequest_;
    LinkActionFn linkAction_;
    ImageCache* imageCache_ = nullptr;
    vita2d_pgf* font_ = nullptr;
    bool ready_ = false;

    std::string searchQuery_;

    bool catalogLoading_ = false;
    std::string catalogLoadingLabel_;
    std::string catalogLoadingMessage_;
    uint64_t catalogLoadingCurrent_ = 0;
    uint64_t catalogLoadingTotal_ = 0;
    std::string catalogError_;

    bool installProgressActive_ = false;
    uint64_t installProgressCurrent_ = 0;
    uint64_t installProgressTotal_ = 0;
    uint64_t installProgressSpeed_ = 0;
    std::string installProgressStage_;
    std::string installProgressFile_;
    std::string installProgressMessage_;
    int installOutcome_ = 0; // 0 progress, 1 success, 2 error
    bool installLiveAreaOk_ = false;
    std::string installResultPath_;
    std::string installResultTitleId_;

    // Preserves the normal detail position while the temporary link-navigation
    // viewport is active.
    int detailScrollBeforeLinkMode_ = 0;

    std::unordered_map<std::string, vita2d_texture*> textures_;
    std::vector<std::string> textureOrder_;
    /** Freed one frame later so the GPU is done with the previous swap. */
    std::vector<vita2d_texture*> deferredFreeTextures_;
    int catalogSwitchCooldownFrames_ = 0;
    uint64_t lastCatalogSwitchMs_ = 0;
    // Smooth motion / feedback
    float visualCatalogScroll_ = 0.f;
    float visualDetailScroll_ = 0.f;
    float visualFocusIndex_ = 0.f;
    float detailCrossfade_ = 1.f;
    int detailCrossfadeFrom_ = -1;
    float tabIndicatorX_ = 0.f;
    float tabIndicatorReady_ = 0.f;
    float contentFade_ = 1.f;
    std::string toastMessage_;
    uint64_t toastExpiresMs_ = 0;
    uint64_t toastShownMs_ = 0;

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
    void drawScrollFades(int x, int y, int width, int height) const;
    void drawActivePanelFrame(int x, int y, int width, int height, const char* label) const;
    float focusPulse() const;
    float easeInOut(float t) const;
    void showToast(const std::string& message, uint64_t durationMs = 1400);
    void updateAnimations();
    void drawToast() const;
    void drawDetailContent(const CatalogItem& item, int x, int y, int width, int height);
    void drawDetailLinks(const CatalogItem& item, int x, int y, int width, int& heightOut);
    void drawLoadingOverlay();
    void prepareImageTexture(const std::string& url, const std::string& namespaceName);
    void prepareVisibleTextures();
    void drawImage(const std::string& url, const std::string& namespaceName, int x, int y, int width, int height);
    void drawImageLoadingPlaceholder(const std::string& url, const std::string& namespaceName, int x, int y, int width, int height);
    void releaseTextures();
    void releaseScreenshotTextures();
    void scheduleTextureFree(vita2d_texture* texture);
    void flushDeferredTextureFrees();
    /** Free GPU textures whose disk path is not in keep (visible set). */
    void releaseTexturesNotIn(const std::unordered_set<std::string>& keep);
    void touchTexture(const std::string& path);
    void evictTextureIfNeeded(const std::string& namespaceName);

    void startOpeningDetail();
    void startClosingDetail();
    void updateTransition();
    void clampCatalogFocus();
    void clampCatalogScroll();
    void clampDetailScroll();
    int detailLinkScrollLimit(const CatalogItem& item, int width, int height) const;
    void enterLinkNavigation();
    void exitLinkNavigation();
    int totalRows() const;
    int visibleRowsFull() const;
    int visibleRowsSplit() const;
    int selectedIndex() const;
    float transitionProgress() const;
    bool isTransitioning() const;
    void changeCatalog(int direction);
    void moveCatalogFocus(int direction);
    void moveDetailScroll(int direction);
    void moveLinkFocus(int dx, int dy);
    void activateFocusedLink();
    void applySearch(const std::string& query);
    bool matchesSearch(const CatalogItem& item, const std::string& query) const;
    void sortItemsByDate(std::vector<CatalogItem>& items) const;
    int detailContentHeight(const CatalogItem& item, int width) const;
    void wrapText(const std::string& text, int maxChars, std::vector<std::string>& lines) const;
    void drawTextLines(const std::vector<std::string>& lines, int x, int y, int lineHeight, unsigned color, float scale,
                       int startLine, int maxLines, int clipTop, int clipBottom);
    unsigned colorForStatus(const std::string& status) const;
};

} // namespace ui
} // namespace psvitaalive

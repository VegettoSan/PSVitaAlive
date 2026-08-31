#pragma once

#include "ui/ui_types.hpp"
#include "ui/image_cache.hpp"
#include "installer/app_settings.hpp"
#include "installer/plugin_detector.hpp"
#include "update/update_checker.hpp"

#include <vita2d.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace psvitaalive {
namespace ui {

enum class LocalInstallState {
    Unknown = 0,
    NotInstalled,
    Installed,
    UpdateAvailable
};

struct LocalInstallInfo {
    LocalInstallState state = LocalInstallState::Unknown;
    std::string installedVersion;
};

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
    void showToast(const std::string& message, uint64_t durationMs = 1400);
    void setAppSettings(const ::psvitaalive::AppSettingsData& settings);
    void setPluginStatus(const ::psvitaalive::PluginStatus& plugins);
    using SettingsSaveFn = std::function<void(const ::psvitaalive::AppSettingsData&)>;
    void setSettingsSaveCallback(SettingsSaveFn callback);
    void openSettings();
    void closeSettings(bool save);
    /** Fetch news.txt after startup (or reopen from footer). Non-blocking failure. */
    void runNewsCheck(bool forceShow);
    bool isNewsVisible() const { return newsVisible_; }
    bool isNewsCheckDone() const { return newsCheckedOnce_; }
    // outcome: 0 = progress, 1 = success, 2 = error
    void setInstallProgress(bool active, uint64_t current, uint64_t total, uint64_t bytesPerSecond,
                            const std::string& stage, const std::string& fileName,
                            const std::string& message,
                            int outcome = 0,
                            bool liveAreaOk = false,
                            const std::string& installPath = std::string(),
                            const std::string& titleId = std::string(),
                            uint64_t resultAutoCloseRemainingMs = 0);

private:
    UiState state_;
    std::vector<CatalogItem> allItems_;
    std::vector<CatalogItem> items_; // filtered only when searchQuery_ non-empty
    /** Browse list: allItems_ when not searching (avoids 2x RAM on large catalogs). */
    const std::vector<CatalogItem>& catalogView() const {
        return (searchQuery_.empty() && !dataFilesFilter_) ? allItems_ : items_;
    }
    void rebuildFilteredItems();
    void setDataFilesFilter(bool enabled);
    bool dataFilesFilter() const { return dataFilesFilter_; }
    InstallRequestFn installRequest_;
    InstallStatusFn installStatusText_;
    InstallCancelFn installCancel_;
    InstallAcknowledgeFn installAcknowledge_;
    CatalogChangeFn catalogChange_;
    SearchRequestFn searchRequest_;
    LinkActionFn linkAction_;
    ImageCache* imageCache_ = nullptr;
    vita2d_pgf* font_ = nullptr;
    /** Full-screen splash while catalogs download/load at startup (app0:ui/catalog_loading.png). */
    vita2d_texture* catalogLoadingTex_ = nullptr;
    /** Header brand image (app0:ui/PSVitaAlive_Store_logo_text.png). */
    vita2d_texture* headerLogoTex_ = nullptr;
    /** 1 = show splash, animates to 0 when catalog load ends. */
    float catalogSplashAlpha_ = 0.f;
    bool ready_ = false;

    std::string searchQuery_;
    /** When true, catalogView only includes apps with Game Files and/or Data Files. */
    bool dataFilesFilter_ = false;

    bool catalogLoading_ = false;
    std::unordered_map<std::string, LocalInstallInfo> installStatusCache_;
    std::string catalogLoadingLabel_;
    std::string catalogLoadingMessage_;
    uint64_t catalogLoadingCurrent_ = 0;
    uint64_t catalogLoadingTotal_ = 0;
    // Do not touch ux0:app while catalogs/splash are settling.
    uint64_t installStatusWarmupUntilMs_ = 0;
    std::string catalogError_;

    bool installProgressActive_ = false;
    uint64_t installProgressCurrent_ = 0;
    uint64_t installProgressTotal_ = 0;
    uint64_t installProgressSpeed_ = 0;
    std::string installProgressStage_;
    std::string installProgressFile_;
    std::string installProgressMessage_;
    int installOutcome_ = 0;
    uint64_t installResultAutoCloseMs_ = 0; // 0 progress, 1 success, 2 error
    bool installLiveAreaOk_ = false;
    std::string installResultPath_;
    std::string installResultTitleId_;

    // Preserves the normal detail position while the temporary link-navigation
    // viewport is active.
    int detailScrollBeforeLinkMode_ = 0;

    /** One rendered line of the News modal (Markdown-lite). */
    struct NewsDrawLine {
        std::string text;
        float scale = 0.55f;
        unsigned color = 0;
        int indentPx = 0;
        int heightPx = 22;
        bool isHr = false;
        bool isBlank = false;
        bool emphasize = false;
    };

    // News modal state.
    bool newsVisible_ = false;
    bool newsCheckedOnce_ = false;
    int newsFetchAttempts_ = 0;
    bool newsMarkSeenOnClose_ = false;
    std::string newsId_;
    std::string newsTitle_;
    std::string newsBody_;
    std::vector<NewsDrawLine> newsLines_;
    int newsScrollLine_ = 0;
    float visualNewsScroll_ = 0.f;

    std::unordered_map<std::string, vita2d_texture*> textures_;
    std::vector<std::string> textureOrder_;
    /** Freed one frame later so the GPU is done with the previous swap. */
    std::vector<vita2d_texture*> deferredFreeTextures_;
    int catalogSwitchCooldownFrames_ = 0;
    uint64_t lastCatalogSwitchMs_ = 0;
    // Front-touch navigation (works alongside buttons)
    bool touchDown_ = false;
    int touchStartX_ = 0;
    int touchStartY_ = 0;
    int touchLastY_ = 0;
    bool touchMoved_ = false;
    uint64_t touchDownMs_ = 0;
    float touchAccumY_ = 0.f; // residual drag for less-sensitive scroll
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
    void handleTouch();
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
    LocalInstallInfo queryLocalInstall(const CatalogItem& item);
    void invalidateInstallStatus(const std::string& titleId = {});
    void drawInstallBadge(int x, int y, const LocalInstallInfo& info, bool compact);
    void drawScrollFades(int x, int y, int width, int height) const;
    void drawActivePanelFrame(int x, int y, int width, int height, const char* label) const;
    float focusPulse() const;
    float easeInOut(float t) const;
    void updateAnimations();
    void drawToast() const;
    void drawDetailContent(const CatalogItem& item, int x, int y, int width, int height);
    void drawDetailLinks(const CatalogItem& item, int x, int y, int width, int& heightOut);
    void drawInstallAllOverlay();
    void openInstallAllWizard();
    void closeInstallAllWizard(bool cancel);
    void installAllAdvancePick();
    void installAllStartQueue();
    void installAllTryAdvanceFromProgress(int outcome);
    bool itemSupportsInstallAll(const CatalogItem& item) const;
    void collectInstallAllOptions(const CatalogItem& item, const char* kind, std::vector<int>& out) const;
    void drawLoadingOverlay();
    void drawNewsChip();
    void drawNewsOverlay();
    void closeNewsModal(bool markSeen);
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
    void drawSettings();
    void handleSettingsInput(uint32_t pressed, uint32_t nav);
    void cycleSettingsOption(int row, int delta);
    void triggerSelfUpdateAction();
    void pollSelfUpdateProgress();
    static int selfUpdateWorkerEntry(SceSize args, void* argp);

    ::psvitaalive::AppSettingsData settingsEdit_{};
    ::psvitaalive::PluginStatus pluginsStatus_{};
    SettingsSaveFn settingsSave_;
    int settingsFocus_ = 0;
    UiMode settingsReturnMode_ = UiMode::FULL_CATALOG;
    float settingsEnter_ = 1.f;   // 0..1 open transition
    float settingsFocusY_ = 0.f;  // animated highlight Y
    float settingsScrollY_ = 0.f; // adaptive list scroll (px)

    // Discord error report UI (webhook)
    bool reportConfirmVisible_ = false;
    int reportUiState_ = 0;          // 0 idle, 1 sending, 2 sent, 3 failed
    uint64_t reportUiUntilMs_ = 0;
    char reportUiMsg_[48] = {};
    std::string reportTitle_;
    std::string reportContext_;
    std::string reportAppName_;
    std::string reportAppTitleId_;
    std::string reportAppVersion_;
    std::string reportFileName_;
    std::atomic<bool> reportBusy_{false};
    std::atomic<bool> reportDone_{false};
    std::atomic<bool> reportOk_{false};
    char reportResultMsg_[64] = {};
    SceUID reportThread_ = -1;
    static int reportWorkerEntry(SceSize args, void* argp);
    void trySendErrorReport(const std::string& title, const std::string& context);
    void pollReportWorker();
    void drawReportChip();
    void drawReportConfirmOverlay();
    void openReportConfirm();
    void closeReportConfirm();

    // Request Data/Game Files (Discord webhook, detail panel)
    bool dataRequestConfirmVisible_ = false;
    std::atomic<bool> dataRequestBusy_{false};
    std::atomic<bool> dataRequestDone_{false};
    std::atomic<bool> dataRequestOk_{false};
    char dataRequestResultMsg_[64] = {};
    SceUID dataRequestThread_ = -1;
    std::string dataReqName_;
    std::string dataReqTitleId_;
    std::string dataReqVersion_;
    std::string dataReqDate_;
    static int dataRequestWorkerEntry(SceSize args, void* argp);
    void openDataRequestConfirm();
    void closeDataRequestConfirm();
    void drawDataRequestConfirmOverlay();
    void trySendDataRequest();
    void pollDataRequestWorker();
    bool itemEligibleForDataRequest(const CatalogItem& item) const;

    // Install All (VPK + Game Files + Data Files) wizard + sequential queue
    enum class InstallAllPhase {
        Hidden = 0,
        Confirm,
        PickDownload,
        PickGameFiles,
        PickDataFiles,
        Running
    };
    InstallAllPhase installAllPhase_ = InstallAllPhase::Hidden;
    int installAllFocus_ = 0;              // focus inside current wizard list / buttons
    int installAllItemIndex_ = -1;         // index into catalogView() when wizard opened
    std::vector<int> installAllOptions_;   // linkDetail indices for current pick phase
    int installAllChosenDownload_ = -1;
    int installAllChosenGameFiles_ = -1;
    int installAllChosenDataFiles_ = -1;
    std::vector<CatalogLink> installAllQueue_;
    std::vector<std::string> installAllQueueLabels_;
    size_t installAllQueueIndex_ = 0;
    int installAllLastOutcome_ = -1;       // edge-detect Completed/Failed
    bool installAllFinishedToast_ = false;

    // Self-update (GitHub Releases → in-place extract to ux0:app/TITLEID)
    ::psvitaalive::UpdateChecker::Result selfUpdateInfo_{};
    bool selfUpdateChecked_ = false;
    std::atomic<bool> selfUpdateBusy_{false};
    std::atomic<bool> selfUpdateDone_{false};
    std::atomic<bool> selfUpdateOk_{false};
    std::atomic<uint64_t> selfUpdateCur_{0};
    std::atomic<uint64_t> selfUpdateTot_{0};
    char selfUpdateMsg_[160] = {};
    SceUID selfUpdateThread_ = -1;
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

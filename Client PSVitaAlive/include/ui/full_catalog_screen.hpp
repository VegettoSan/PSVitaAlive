#pragma once

#include "ui/ui_types.hpp"
#include "ui/image_cache.hpp"
#include "ui/search_keyboard.hpp"
#include "installer/app_settings.hpp"
#include "installer/plugin_detector.hpp"
#include "storage/storage_maintenance.hpp"
#include "ui/install_failure_presentation.hpp"
#include "update/update_checker.hpp"

#include <vita2d.h>
#include "ui/ui_font.hpp"
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace psvitaalive {
namespace ui {

enum class LocalInstallState {
    Unknown = 0,
    NotInstalled,
    Installed,
    UpdateAvailable,
    /** Present on disk but release identity not proven (nightly/fork/unknown SFO). */
    InstalledUnknown
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
    using InstallPauseFn = std::function<bool()>;
    using InstallAcknowledgeFn = std::function<void()>;
    using InstallZipRecoveryFn = std::function<bool(bool keep, std::string& savedPath)>;
    using DeleteSavedZipFn = std::function<bool(const std::string& appId,
        const std::string& titleId, const std::string& archivePath)>;
    using CatalogChangeFn = std::function<bool(CatalogType)>;
    using SearchRequestFn = std::function<void(const std::string&)>;
    using LinkActionFn = std::function<bool(const CatalogItem&, const CatalogLink&)>;
    struct SavedZipOption {
        std::string fileName;
        std::string archivePath;
        unsigned partCount = 1;
    };
    using SavedZipListFn = std::function<std::vector<SavedZipOption>(const CatalogItem&)>;
    using SavedZipRetryFn = std::function<bool(const CatalogItem&, const std::string&)>;

    FullCatalogScreen();
    ~FullCatalogScreen();
    bool init();
    void shutdown();
    /** Nonblocking: request a one-off refresh after a user download/install completes. */
    void requestUx0SpaceRefresh();
    bool updateAndDraw();

    void setInstallCallbacks(InstallRequestFn requestInstall, InstallStatusFn statusText);
    void setInstallCancelCallback(InstallCancelFn callback);
    void setInstallPauseCallbacks(InstallPauseFn pause, InstallPauseFn resume) {
        installPause_ = std::move(pause); installResume_ = std::move(resume);
    }
    void setStorageBusyCallback(std::function<bool()> callback) { storageInstallerBusy_ = std::move(callback); }
    void setInstallAcknowledgeCallback(InstallAcknowledgeFn callback);
    void setInstallZipRecoveryCallback(InstallZipRecoveryFn callback) { installZipRecovery_ = callback; }
    void setDeleteSavedZipCallback(DeleteSavedZipFn callback) { deleteSavedZip_ = callback; }
    void setCatalogChangeCallback(CatalogChangeFn callback);
    void setSearchCallback(SearchRequestFn callback);
    /** Native vita2d QWERTY search and ZIP path entry (no Sony IME). */
    void completeSearch(const std::string& query);
    void openSearchKeyboard(const std::string& query);
    bool keyboardVisible() const { return keyboard_.active(); }
    bool promptTextBlocking(const std::string& initial, const std::string& title,
                            std::string& output);
    void setLinkActionCallback(LinkActionFn callback);
    void setSavedZipCallbacks(SavedZipListFn list, SavedZipRetryFn retry) {
        savedZipList_ = std::move(list); savedZipRetry_ = std::move(retry);
        savedZipProbeAtUs_ = 0;
    }
    void setImageCache(ImageCache* cache);
    /** Free catalog icons/covers/screenshots before starting a heavy install worker. UI thread only. */
    void prepareForHeavyWork();
    void setCatalogItems(std::vector<CatalogItem> items);
    void setActiveCatalog(CatalogType catalog);
    CatalogType activeCatalog() const { return state_.catalog; }
    /** Refresh the active catalog without losing user selection, search or scroll. */
    void refreshCatalogItems(std::vector<CatalogItem> items);
    void setCatalogLoading(bool loading, const std::string& label, uint64_t current, uint64_t total, const std::string& message);
    /** Thin theme-colored progress rule under the existing catalog tabs. */
    void setCatalogSyncProgress(bool active, float progress, bool indeterminate = false);
    void setCatalogError(const std::string& error);
    void showToast(const std::string& message, uint64_t durationMs = 2600);
    // Queue the Archive.org warning. It opens only when startup/install
    // modals are no longer visible, never over an error/recovery dialog.
    void queueArchiveServiceWarning(bool fromDownload);
    void openArchiveServiceWarningIfReady();
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
    /** First-run color theme picker (before News). */
    void openThemeSetupIfNeeded();
    void openThemePicker();
    bool isThemeSetupVisible() const { return themeSetupVisible_; }
    bool isEssentialPluginsVisible() const { return essentialPluginsModal_; }
    bool isEssentialPluginsInstalling() const { return essentialInstallRunning_; }
    bool essentialPluginsPromptDone() const { return essentialPluginsPromptDone_; }
    bool isPluginRebootModal() const { return pluginRebootModal_; }
    /** After theme + news: offer missing kubridge / fd_fix / libshacccg if needed. */
    void tryShowEssentialPluginsPrompt();
    // outcome: 0 = progress, 1 = success, 2 = error
    void setInstallProgress(bool active, uint64_t current, uint64_t total, uint64_t bytesPerSecond,
                            const std::string& stage, const std::string& fileName,
                            const std::string& message,
                            int outcome = 0,
                            bool liveAreaOk = false,
                            const std::string& installPath = std::string(),
                            const std::string& titleId = std::string(),
                            uint64_t resultAutoCloseRemainingMs = 0,
                            bool needsReboot = false,
                            bool zipRecoveryAvailable = false,
                            const std::string& zipRecoveryPath = std::string(),
                            const std::string& zipRecoveryExtractPath = std::string(),
                            bool zipRecoveryMayBeCorrupt = false,
                            bool archiveSourceUnavailable = false,
                            bool pauseSupported = false);

private:
    UiState state_;
    std::vector<CatalogItem> allItems_;
    std::vector<CatalogItem> items_; // existing filtered view (RAM optimization deferred)
    enum class ContentFilter {
        All, Installed, Updates, Unrecognized, DataOrGame, Dlc, Category, Genre, Heading
    };
    struct FilterOption {
        ContentFilter mode;
        const char* category;
        const char* labelKey;
    };
    /** Browsing without search/filter reuses allItems_, avoiding a second list. */
    const std::vector<CatalogItem>& catalogView() const {
        return (searchQuery_.empty() && filterMode_ == ContentFilter::All) ? allItems_ : items_;
    }
    void rebuildFilteredItems();
    bool isInstallStateFilter() const;
    void advanceInstallFilterScan();
    bool itemMatchesCurrentFilter(const CatalogItem& item);
    void applyContentFilter(ContentFilter mode, const char* category = "");
    bool canShowFilterButton() const;
    const FilterOption* filterOptionsForCatalog(int& count) const;
    void openFilterMenu();
    void closeFilterMenu();
    void moveFilterFocus(int direction);
    void selectFilterOption(int index);
    const char* filterOptionLabel(const FilterOption& option) const;
    void drawFilterOverlay();
    InstallRequestFn installRequest_;
    InstallStatusFn installStatusText_;
    InstallCancelFn installCancel_;
    InstallPauseFn installPause_;
    InstallPauseFn installResume_;
    InstallAcknowledgeFn installAcknowledge_;
    InstallZipRecoveryFn installZipRecovery_;
    DeleteSavedZipFn deleteSavedZip_;
    CatalogChangeFn catalogChange_;
    SearchRequestFn searchRequest_;
    LinkActionFn linkAction_;
    SavedZipListFn savedZipList_;
    SavedZipRetryFn savedZipRetry_;
    std::string savedZipProbeKey_;
    uint64_t savedZipProbeAtUs_ = 0;
    std::vector<SavedZipOption> savedZipProbeResult_;
    bool savedZipActionVisible_ = false;
    bool savedZipActionAwaitRelease_ = false;
    bool savedZipActionDeleteConfirm_ = false;
    int savedZipActionFocus_ = 0; // retry, delete, cancel
    std::string savedZipActionAppId_, savedZipActionTitleId_;
    std::string savedZipActionPath_, savedZipActionName_;
    unsigned savedZipActionParts_ = 1;
    void resolveSavedZipAction(int action);
    void drawSavedZipActionOverlay();
    const std::vector<SavedZipOption>& savedZipsFor(const CatalogItem& item);
    bool hasSavedZipFor(const CatalogItem& item);
    int savedZipCountFor(const CatalogItem& item);
    int savedZipBlockHeight(const CatalogItem& item);
    ImageCache* imageCache_ = nullptr;
    ::psvitaalive::ui::UiFont font_;
    /** Full-screen splash while catalogs download/load at startup (app0:ui/catalog_loading.png). */
    vita2d_texture* catalogLoadingTex_ = nullptr;
    /** Monochrome splash for non-brand theme tinting. */
    vita2d_texture* catalogLoadingMonoTex_ = nullptr;
    /** Header brand image (app0:ui/PSVitaAlive_Store_logo_text.png). */
    vita2d_texture* headerLogoTex_ = nullptr;
    /** Monochrome header logo for non-brand theme tinting. */
    vita2d_texture* headerLogoMonoTex_ = nullptr;
    /** 1 = show splash, animates to 0 when catalog load ends. */
    float catalogSplashAlpha_ = 0.f;
    bool ready_ = false;

    std::string searchQuery_;
    SearchKeyboard keyboard_;
    ContentFilter filterMode_ = ContentFilter::All;
    const char* filterCategory_ = ""; // points into compiled-in menu table (no allocation)
    size_t installFilterScanNext_ = 0; // status filters scan progressively
    bool filterModalOpen_ = false;
    int filterFocus_ = 0;
    int filterScrollRow_ = 0;

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
    /** After Plugin install(s): force reboot dialog (blocks LiveArea exit). */
    bool pluginRebootModal_ = false;
    // Confirmation for a single catalog Plugin link (independent of Install All).
    bool pluginChoiceModal_ = false;
    bool pluginChoiceWasInstalled_ = false; // installed does not establish version
    bool pluginChoiceAwaitRelease_ = false; // prevent held X/O from activating underlying UI
    int pluginChoiceItemIndex_ = -1;
    int pluginChoiceLinkIndex_ = -1;
    int pluginChoiceFocus_ = 0; // 0 Install, 1 Skip
    // Only catalog Plugin links use these read-only compatibility notices.
    std::vector<std::string> pluginChoiceConflicts_;
    int pluginChoiceScrollLine_ = 0;
    int pluginChoiceMaxScrollLine_ = 0;

    bool archiveWarningVisible_ = false;
    bool archiveWarningPending_ = false;
    bool archiveWarningDownload_ = false;
    // QR/Web handoff: only the displayed symbol occupies VRAM.
    bool webQrVisible_ = false;
    std::string webQrUrl_;
    std::string webQrPath_;
    vita2d_texture* webQrTexture_ = nullptr;
    struct WebQrHelpLine {
        std::string text;
        bool heading = false;
        int advance = 22; // vertical line advance, already wrapped for the right pane
    };
    std::vector<WebQrHelpLine> webQrHelpLines_;
    std::vector<std::string> webQrUrlLines_;
    int webQrHelpScroll_ = 0;
    int webQrHelpMaxScroll_ = 0;
    int webQrUrlScrollLine_ = 0;
    /** True only after an Install All plugin job actually completes successfully. */
    bool installAllPluginInstalled_ = false;
    /** Install All summary owns its reboot button when a plugin succeeds. */
    bool installAllRebootPending_ = false;

    // Startup: recommend missing critical plugins (kubridge, fd_fix, libshacccg)
    bool essentialPluginsModal_ = false;
    bool essentialPluginsPromptDone_ = false;
    int essentialPluginsFocus_ = 0; // 0 = Install, 1 = Remind later

    // First PSP/PS1 download: install method wizard (once).
    bool pspSetupModal_ = false;
    int pspSetupFocus_ = 0; // 0 LiveArea, 1 Adrenaline, 2 Folder, 3 ISO, 4 ux0, 5 uma0, 6 Confirm, 7 Cancel
    ::psvitaalive::PspTarget pspSetupTarget_ = ::psvitaalive::PspTarget::Adrenaline;
    ::psvitaalive::PspMediaFormat pspSetupMedia_ = ::psvitaalive::PspMediaFormat::Folder;
    ::psvitaalive::PspStorage pspSetupStorage_ = ::psvitaalive::PspStorage::Ux0;
    bool pspSetupPendingInstallAll_ = false;
    int pspSetupPendingItemIndex_ = -1;
    int pspSetupPendingLinkIndex_ = -1; // index into item.linkDetails
    struct EssentialPluginSpec {
        std::string name;
        std::string desc;
        std::string url;
        std::string extractPath;
        std::string section; // "*KERNEL" or "none"
        std::string line;    // config line / dest basename hint
        std::vector<std::string> checkPaths;
    };
    std::vector<EssentialPluginSpec> essentialMissing_;
    std::vector<EssentialPluginSpec> essentialInstallQueue_;
    size_t essentialInstallIndex_ = 0;
    bool essentialInstallRunning_ = false;
    int essentialInstallLastOutcome_ = -1;

    uint64_t installProgressCurrent_ = 0;
    uint64_t installProgressTotal_ = 0;
    uint64_t installProgressSpeed_ = 0;
    bool installPauseSupported_ = false;
    // UI-only clock for indeterminate Promoter phases (elapsed, not guessed ETA).
    uint64_t installPhaseStartedUs_ = 0;
    std::string installProgressStage_;
    std::string installProgressFile_;
    std::string installProgressMessage_;
    int installOutcome_ = 0;
    uint64_t installResultAutoCloseMs_ = 0; // 0 progress, 1 success, 2 error
    bool installLiveAreaOk_ = false;
    // Structured failure source: never infer connectivity from localized text.
    bool installArchiveSourceUnavailable_ = false;
    InstallFailureKind installFailureKind_ = InstallFailureKind::Other;
    bool installFailureNetworkLinked_ = false; // Captured once per failure, not polled per frame.
    std::string installResultPath_;
    std::string installResultTitleId_;
    bool installZipRecoveryAvailable_ = false;
    bool installZipRecoveryMayBeCorrupt_ = false;
    std::string installZipRecoveryPath_;
    std::string installZipRecoveryExtractPath_;

    // Preserves the normal detail position while the temporary link-navigation
    // viewport is active.
    int detailScrollBeforeLinkMode_ = 0;
    // Snapshot plugin link statuses outside the render loop (indexed by linkDetails).
    std::vector<bool> detailPluginInstalled_;

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

    // First-run theme setup modal (once, before News).
    bool themeSetupVisible_ = false;
    bool themeSetupChecked_ = false;
    int themeSetupFocus_ = 0;          // 0..Count-1 themes, Count = Save
    int themeSetupScrollRow_ = 0;
    int themeSetupAppliedFocus_ = -1; // last previewed theme index (-1 = none)
    float visualThemeSetupScroll_ = 0.f;

    std::unordered_map<std::string, vita2d_texture*> textures_;
    /** True while install/extract/download work requires catalog media GPU memory. */
    bool mediaTexturesSuspended_ = false;
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
    // Image scheduler: physical vertical navigation remains busy even between D-pad repeat steps.
    bool verticalNavHeld_ = false;
    bool imageNavigationCancelIssued_ = false;
    uint64_t imageWorkResumeAfterUs_ = 0;
    // Smooth motion / feedback
    float visualCatalogScroll_ = 0.f;
    float visualDetailScroll_ = 0.f;
    float visualFocusIndex_ = 0.f;
    float detailCrossfade_ = 1.f;
    int detailCrossfadeFrom_ = -1;
    float tabIndicatorX_ = 0.f;
    float tabIndicatorReady_ = 0.f;
    bool catalogSyncActive_ = false;
    bool catalogSyncIndeterminate_ = false;
    float catalogSyncProgress_ = 0.f;
    float contentFade_ = 1.f;
    std::string toastMessage_;
    uint64_t toastExpiresMs_ = 0;
    uint64_t toastShownMs_ = 0;

    void handleInput();
    void handleTouch();
    void draw();
    void finishUiFrame();  // in-frame keyboard overlay, then a single buffer swap
    void drawFullCatalog();
    void drawOpeningDetail();
    void drawSplitDetail();
    void drawClosingDetail();
    void drawHeader(int width);
    void drawTabs(int width);
    void drawCatalogPanel(int x, int y, int width, int height, bool splitMode);
    void drawDetailPanel(int x, int y, int width, int height);
    void drawCatalogCard(const CatalogItem& item, int index, int x, int y, int width, int height, bool focused, int clipLeft, int clipTop, int clipRight, int clipBottom);
    LocalInstallInfo queryLocalInstall(const CatalogItem& item);
    void invalidateInstallStatus(const std::string& titleId = {});
    void drawInstallBadge(int x, int y, const LocalInstallInfo& info, bool compact,
                          int clipLeft, int clipTop, int clipRight, int clipBottom);
    void drawScrollFades(int x, int y, int width, int height) const;
    void drawActivePanelFrame(int x, int y, int width, int height, const char* label) const;
    float focusPulse() const;
    float easeInOut(float t) const;
    void updateAnimations();
    void drawToast() const;
    void drawDetailContent(const CatalogItem& item, int x, int y, int width, int height);
    void drawDetailLinks(const CatalogItem& item, int x, int y, int width, int& heightOut);
    void openWebQrModal();
    void closeWebQrModal();
    void drawWebQrOverlay();
    void scrollWebQrHelp(int direction);
    void drawInstallAllOverlay();
    void drawPluginChoiceOverlay();
    void refreshPluginChoiceConflicts(const CatalogLink& link);
    void resolveIndividualPluginChoice(bool install);
    void installAllAdvancePluginPick(bool install);
    void drawPluginRebootOverlay();
    void drawArchiveServiceOverlay();
    void drawEssentialPluginsOverlay();
    void closeEssentialPluginsPrompt(bool install);
    void drawPspSetupOverlay();
    void openPspSetupWizard(bool forInstallAll, int itemIndex, int linkDetailIndex);
    void closePspSetupWizard(bool confirm);
    bool guardPspPs1SetupOrProceed(bool forInstallAll, int itemIndex, int linkDetailIndex);
    void applyPspLiveAreaPluginGateOrRun(bool forInstallAll, int itemIndex, int linkDetailIndex);
    void kickNextEssentialPluginInstall();
    void essentialPluginsTryAdvanceFromProgress(int outcome);

    enum class PrimaryAction { None, InstallAll, Update, Open };
    /** Action is derived from the existing installed-release detector. */
    PrimaryAction primaryActionFor(const CatalogItem& item);
    int preferredVpkDownloadIndex(const CatalogItem& item) const;
    void activatePrimaryAction(const CatalogItem& item);
    void openInstallAllWizard();
    void closeInstallAllWizard(bool cancel);
    void installAllAdvancePick();
    void installAllStartQueue();
    void installAllStartNextStep();
    void installAllFinishReport(bool aborted = false);
    void installAllResolveRecovery(bool keep);
    void installAllSummaryZipChoice(bool keep);
    void installAllTryAdvanceFromProgress(int outcome, bool needsReboot);
    void drawInstallAllSummary();
    bool itemSupportsInstallAll(const CatalogItem& item) const;
    void collectInstallAllOptions(const CatalogItem& item, const char* kind, std::vector<int>& out) const;
    void drawLoadingOverlay();
    void drawNewsChip();
    void drawNewsOverlay();
    void closeNewsModal(bool markSeen);
    void drawThemeSetupOverlay();
    void closeThemeSetup(bool save);
    void applyThemeSetupFocus();
    void prepareImageTexture(const std::string& url, const std::string& namespaceName);
    void prepareVisibleTextures();
    void drawImage(const std::string& url, const std::string& namespaceName, int x, int y, int width, int height);
    void drawImageLoadingPlaceholder(const std::string& url, const std::string& namespaceName, int x, int y, int width, int height);
    void releaseTextures();
    void updateMediaTextureSuspension();
    void releaseScreenshotTextures();
    void scheduleTextureFree(vita2d_texture* texture);
    void flushDeferredTextureFrees();
    /** Free off-screen screenshot textures; app/icon textures remain in the bounded LRU. */
    void releaseTexturesNotIn(const std::unordered_set<std::string>& keep);
    void touchTexture(const std::string& path);
    void evictTextureIfNeeded(const std::string& namespaceName);

    void refreshDetailPluginStatus();
    void startOpeningDetail();
    void startClosingDetail();
    void updateTransition();
    void clampCatalogFocus();
    void clampCatalogScroll();
    void clampDetailScroll();
    int detailLinkScrollLimit(const CatalogItem& item, int width, int height);
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
    // Snapshot expensive filesystem-backed plugin checks once when Settings opens.
    bool settingsKubridgeOk_ = false;
    bool settingsRepatchOk_ = false;
    bool settingsFdFixOk_ = false;
    bool settingsLibshacccgOk_ = false;
    SettingsSaveFn settingsSave_;
    int settingsFocus_ = 0;
    UiMode settingsReturnMode_ = UiMode::FULL_CATALOG;
    float settingsEnter_ = 1.f;   // 0..1 open transition
    float settingsFocusY_ = 0.f;  // animated highlight Y
    float settingsScrollY_ = 0.f; // adaptive list scroll (px)
    float settingsInfoScrollY_ = 0.f; // INFO panel text scroll (px)
    float settingsInfoMaxScroll_ = 0.f;

    // Footer UX0 storage management: worker-owned scan/delete, never on render thread.
    std::function<bool()> storageInstallerBusy_;
    bool storageVisible_ = false;
    bool storageConfirm_ = false;
    int storagePendingAction_ = -1; // double-confirm the exact cleanup action
    struct StorageDisplayRow {
        // 0: temporaries, 1: saved ZIP, 2: catalog, 3: images, 4: file
        int kind = 0;
        int itemIndex = -1;
    };
    std::vector<StorageDisplayRow> storageRows_;
    int storageFocus_ = 0; // index into storageRows_ (always includes four categories)
    int storageScrollY_ = 0; // pixel scroll, shared by touch and controller
    int storageAction_ = -1; // -1 scan, -2 all, -3 temp, -4 saved ZIP, >=0 file index
    std::atomic<bool> storageWorkerDone_{false};
    std::atomic<bool> storageWorkerOk_{false};
    SceUID storageWorker_ = -1;
    ::psvitaalive::storage::UsageSnapshot storageSnapshot_;
    void openStorageMenu();
    void startStorageOperation(int action);
    void pollStorageOperation();
    void rebuildStorageRows();
    int storageContentHeight() const;
    void clampStorageScroll();
    void ensureStorageFocusVisible();
    void executeStorageSelection();
    void executeStorageAll();
    void drawStorageOverlay();
    static int storageWorkerEntry(SceSize args, void* argp);

    // Discord error report UI (webhook)
    bool reportConfirmVisible_ = false;
    // User-written feedback from the footer Report chip, isolated from
    // automatic diagnostic reporting and its log-collection pipeline.
    std::string feedbackTitle_;
    std::string feedbackBody_;
    std::vector<std::string> feedbackWrapped_;
    bool feedbackSuggestion_ = false;
    bool feedbackSending_ = false;
    int feedbackFocus_ = 1; // 0 kind, 1 title, 2 description, 3 send, 4 close
    int feedbackKeyboardField_ = 0; // 0 search, 1 title, 2 description
    int feedbackScrollY_ = 0;
    void openFeedbackKeyboard(int field);
    void refreshFeedbackWrap();
    void sendManualFeedbackFromForm();
    void activateFeedbackFocus();
    void clampFeedbackScroll();

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
    struct ReportTargetOverrides {
        std::string appName, titleId, version, fileName;
    };
    void trySendErrorReport(const std::string& title, const std::string& context,
                            const ReportTargetOverrides* target = nullptr);
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
        PickPlugin,
        SpaceWarning,  // unified pre-download capacity error (no job started)
        Running,
        Summary
    };
    InstallAllPhase installAllPhase_ = InstallAllPhase::Hidden;
    uint64_t installAllRequiredBytes_ = 0;
    uint64_t installAllFreeBytes_ = 0;
    bool installAllSpaceProbeFailed_ = false;
    int installAllFocus_ = 0;              // focus inside current wizard list / buttons
    int installAllItemIndex_ = -1;         // index into catalogView() when wizard opened
    std::vector<int> installAllOptions_;   // linkDetail indices for current pick phase
    int installAllChosenDownload_ = -1;
    int installAllChosenGameFiles_ = -1;
    int installAllChosenDataFiles_ = -1;
    // Ask about every catalog Plugin link, even when already installed.
    std::vector<int> installAllPluginCandidates_;
    std::vector<bool> installAllPluginCandidateInstalled_;
    std::vector<int> installAllChosenPlugins_;
    size_t installAllPluginPickIndex_ = 0;
    /** Stable app metadata for the active batch if the installed/updates filter changes. */
    CatalogItem installAllJobItem_;
    std::vector<CatalogLink> installAllQueue_;
    std::vector<std::string> installAllQueueLabels_;
    size_t installAllQueueIndex_ = 0;
    int installAllLastOutcome_ = -1;       // edge-detect terminal jobs
    bool installAllWaitingRecovery_ = false;
    bool installAllRecoveryResolved_ = false;
    bool installAllRecoveryAutoAttempted_ = false;
    bool installAllRecoveryAutoFailed_ = false;
    uint64_t installAllRecoveryStartUs_ = 0;
    static constexpr uint64_t INSTALL_ALL_ZIP_KEEP_TIMEOUT_US = 10000000ULL;
    enum class InstallAllResultState { Pending, Completed, Verified, Failed, Kept, Deleted,
                                       Skipped, AlreadyInstalled, Cancelled, NotAttempted };
    struct InstallAllResult {
        std::string name;
        std::string detail;
        std::string failureReason; // never overwritten by ZIP keep/delete decision
        std::string fileName;      // failing stage's file, not current catalog selection
        bool reported = false;
        std::string savedZipPath; // validated persistent manual ZIP path (not displayed message)
        InstallAllResultState state;
        bool autoKept = false;
        bool keepConfirmed = false;
        InstallAllResult(const std::string& n, const std::string& d, InstallAllResultState s)
            : name(n), detail(d), state(s) {}
    };
    std::vector<InstallAllResult> installAllResults_;
    std::vector<size_t> installAllQueueResultIndices_;
    int installAllSummarySelected_ = 0;
    int installAllReportSendingIndex_ = -1;
    uint32_t installAllReportGeneration_ = 0;
    uint32_t installAllReportSendingGeneration_ = 0;
    std::string installAllReportAppName_;
    std::string installAllReportAppVersion_;
    bool installAllSummaryDeleteConfirm_ = false;
    int installAllSummaryDeleteFocus_ = 1; // 0 Delete ZIP, 1 Cancel (safe default)
    std::string installAllReportAppId_;
    std::string installAllReportTitleId_;
    bool installAllFinishedToast_ = false;
    bool canReportInstallAllResult(int index) const;
    void sendInstallAllResultReport(int index);

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
    int detailContentHeight(const CatalogItem& item, int width);
    void wrapText(const std::string& text, int maxChars, std::vector<std::string>& lines) const;
    void drawTextLines(const std::vector<std::string>& lines, int x, int y, int lineHeight, unsigned color, float scale,
                       int startLine, int maxLines, int clipTop, int clipBottom);
    unsigned colorForStatus(const std::string& status) const;
};

} // namespace ui
} // namespace psvitaalive

#pragma once

#include "installer/app_settings.hpp"
#include "localization/language.hpp"

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace psvitaalive {

enum class TextId {
    Search = 0,
    Settings,
    Download,
    Install,
    Cancel,
    Loading,
    Language,
    SystemAutomatic,
    English,
    Spanish,
    ColorTheme,
    InstallMethod,
    PspPs1Target,
    PspMediaAdrenaline,
    WarnMissingPlugins,
    
    CheckForUpdates,
    Auto,
    Direct,
    Adrenaline,
    LiveArea,
    Folder,
    Iso,
    Yes,
    No,
    SectionInstall,
    SectionInterface,
    SectionCatalog,
    SectionUpdates,
    Info,
    System,
    SettingsSaved,
    SettingsFooter,
    SettingsSaveBack,
    HintInstallMethod,
    HintPspTarget,
    HintPspMedia,
    HintLanguage,
    HintColorTheme,
    HintWarnPlugins,
    
    HintSelfUpdate,
    CatalogHomebrew,
    CatalogVitaGames,
    CatalogPsp,
    CatalogPs1,
    CatalogUnknown,
    FooterCatalog,
    FooterDetailList,
    FooterDetailPanel,
    SearchPlaceholder,
    FilterGdOnly,
    FilterDlcOnly,
    FilterCleared,
    LoadingCatalog,
    ChangingCatalog,
    // Settings INFO panel bodies (3 lines each)
    InfoInstallMethod1, InfoInstallMethod2, InfoInstallMethod3,
    InfoPspTarget1, InfoPspTarget2, InfoPspTarget3,
    InfoPspMedia1, InfoPspMedia2, InfoPspMedia3,
    InfoLanguage1, InfoLanguage2, InfoLanguage3,
    InfoColorTheme1, InfoColorTheme2, InfoColorTheme3,
    InfoWarnPlugins1, InfoWarnPlugins2, InfoWarnPlugins3,
    
    InfoSelfUpdate1, InfoSelfUpdate2, InfoSelfUpdate3,
    StatusOk,
    StatusMissing,
    LocalVersion,
    RemoteVersion,
    RemoteUpToDate,
    RemoteCheckFailed,
    UpdateWorking,
    UpdateInstallPrefix,
    UpdateUpToDate,
    UpdateCheckFailed,
    // Detail / links
    SectionDownloads,
    SectionDataFiles,
    SectionGameFiles,
    SectionMods,
    SectionDlc,
    SectionUpdatesLinks,
    SectionPkg,
    SectionPlugins,
    SectionOther,
    MetaDownload,
    MetaDataFiles,
    MetaGameFiles,
    MetaMod,
    MetaDlc,
    MetaUpdate,
    MetaPkg,
    MetaPlugin,
    DetailInformation,
    InstallAll,
    AlreadyInstalled,
    PspDlcBlocked,
    InstallAllHeader,
    PrimaryUpdate,
    PrimaryOpen,
    PrimaryUpdateSubtitle,
    PrimaryOpenSubtitle,
    PrimaryVpkUnavailable,
    PrimaryLaunchFailed,
    InstallAllSubtitle,
    BadgeRecommended,
    BadgeInstalled,
    MetaAlreadyInstalled,
    MetaInstalled,
    MetaXInstall,
    MetaX,
    SectionDescription,
    SectionLongDescription,
    SectionScreenshots,
    SectionRequirements,
    SectionChangelog,
    MetaTitleId,
    MetaVersion,
    MetaInstall,
    MetaReleased,
    MetaCategory,
    MetaSubcategory,
    MetaSize,
    MetaStatus,
    InstallStateInstalled,
    InstallStateUpdateAvailable,
    InstallStateNotInstalled,
    InstallStateInstalledUnknown,
    BtnContinue,
    BtnCancel,
    InstallAllConfirm1,
    InstallAllConfirm2,
    InstallAllConfirm3,
    InstallAllConfirm4,
    InstallAllNavHint,
    ChooseDownload,
    ChooseGameFiles,
    ChooseDataFiles,
    ChooseMirrorHint,
    InstallComplete,
    DownloadCancelled,
    InstallFailed,
    PanelDetail,
    SelectLinks,
    ExitLinkMode,
    RequestData,
    ChipNews,
    ChipReport,
    ChipSent,
    ChipFail,
    ReportTitle,
    ReportSubtitle,
    ReportBody1,
    ReportBody2,
    BtnOCancel,
    BtnXReport,
    BtnOClose,
    NewsNavHint,
    LockedFinishJob,
    LockedStillRunning,
    LockedCannotExit,
    LockedCannotSwitchCatalog,
    ZipExtractComplete,
    ExtractedTo,
    OContinue,
    StageDownloading,
    StageInstalling,
    StagePreparingDownload,
    StagePreparing,
    LabelFile,
    LabelEta,
    HintRetryConnection,
    HintRetryStep,
    HintExtracting,
    HintInstalling,
    HintConnecting,
    HintDownloadSpeed,
    HintPleaseWait,
    LockedBanner1,
    LockedBanner2,
    CircleCancelDownload,
    ProgressFooterHint,
    LabelReason,
    UnknownError,
    FreeSpaceHint1,
    FreeSpaceHint2,
    CheckLogsHint1,
    CheckLogsHint2,
    CancelledNoError,
    CancelledFriendlyMsg,
    SquareReportHint,
    CircleCloseHint,
    NoLiveAreaFilesOnly,
    LiveAreaOk,
    LiveAreaNotConfirmed,
    LiveAreaNa,
    LabelPath,
    MetaXSelect,
    ToastAllInstalled,
    UiFont,
    FontDefault,
    FontSerif,
    FontSans,
    FontSerifBold,
    FontSansBold,
    HintUiFont,
    InfoFont1,
    InfoFont2,
    InfoFont3,
    UiFontSize,
    HintUiFontSize,
    InfoFontSize1,
    InfoFontSize2,
    InfoFontSize3,
    FontFallbackToast,
    ThemeSetupTitle,
    ThemeSetupBody1,
    ThemeSetupBody2,
    ThemeSetupNavHint,
    ThemeSavedToast,
    ThemePreviewToast,
    ThemeAppliedPrefix,
    BtnSave,
    // Essential plugins + reboot after plugin install
    EssentialPluginsTitle,
    EssentialPluginsSubtitle,
    EssentialInstallPlugins,
    EssentialRemindLater,
    EssentialNavHint,
    EssentialPluginKubridgeDesc,
    EssentialPluginFdFixDesc,
    EssentialPluginShacccgDesc,
    EssentialRemindToast,
    InstallerNotReady,
    CouldNotStartPrefix,
    PluginRebootTitle,
    PluginRebootLine1,
    PluginRebootLine2,
    PluginRebootLine3,
    PluginRebootLine4,
    PluginRebootButton,
    PluginRebootFooter,
    // Remaining UI toasts / overlays
    ToastNoNews,
    ToastRequestInProgress,
    ToastCouldNotStartRequest,
    ToastSendingRequest,
    ToastRequestSent,
    ToastRequestFailed,
    ToastReportSent,
    ToastReportFailed,
    ToastReportFailedStart,
    ToastUpdateThreadFailed,
    ToastCheckingUpdates,
    ToastUpdateAvailablePrefix,
    ToastUpdateAvailableSuffix,
    ToastUpToDatePrefix,
    ToastUpToDateSuffix,
    ToastPleaseWait,
    ToastWaitOperation,
    ToastNothingToInstall,
    ToastInstallUnavailable,
    ToastCouldNotStartInstall,
    ToastInstallAllStopped,
    NewsTitle,
    DataRequestTitle,
    DataRequestBody1,
    DataRequestBody2,
    DataRequestBody3,
    DataRequestBody4,
    DataRequestSend,
    FilterActiveLabel,
    FilterClearHint,
    UnknownAuthor,
    LoadingCatalogFallback,
    PreparingFallback,
    PleaseWaitFallback,
    LoadingCatalogLocalCache,
    LoadingHomebrewCatalogMsg,
    CheckingCatalogCache,
    CheckingCatalogOfflineOk,
    CatalogReadyMsg,
    CatalogUnavailableMsg,
    UnableToLoadCatalog,
    UnableToStartCatalogCheck,
    CatalogsReadyMsg,
    StartupPhaseLabel,
    StartupUpdateLabel,
    CheckingForAppUpdates,
    SelfUpdateLabel,
    StartingUpdaterMsg,
    UpdatingApplicationMsg,
    UpdateStartupUnavailable,
    UpdateUnavailableContinue,
    LabelAppVpk,
    SelfUpdateStage,
    SelfUpdateStarting,
    SelfUpdateThreadFailed,
    // Installer / progress (user-visible)
    ToastLockedCircleOnly,
    ToastFiltersCleared,
    InstMsgReady,
    InstMsgStopped,
    InstMsgDownloading,
    InstMsgStartingDownload,
    InstMsgCancellingDownload,
    InstMsgNoInternet,
    InstMsgNotEnoughSpace,
    InstMsgPreparingLicense,
    InstMsgQueuingBgdl,
    InstMsgCouldNotCreateJob,
    InstMsgCouldNotCreateThread,
    InstMsgCouldNotStartThread,
    InstMsgDownloadCancelled,
    InstMsgDownloadFailed,
    InstMsgFinalizingStorage,
    InstMsgPreparingInstall,
    InstMsgInstallingPlugin,
    InstMsgCannotCreatePluginDir,
    InstMsgCannotOpenPlugin,
    InstMsgCannotWritePlugin,
    InstMsgPluginCopyRead,
    InstMsgPluginCopyWrite,
    InstMsgPluginInstalled,
    InstMsgRetryIncomplete,
    InstMsgRetryGeneric,
    InstMsgRetryDownload,
    InstMsgInstalledAt,
    InstMsgInstallComplete,
    InstMsgZipExtractedTo,
    InstMsgInstalledRefreshHint,
    InstMsgQueuedBgdl,
    InstMsgBgdlLicenseFailed,
    InstMsgPromoteFailed,
    InstMsgPromoterTimeout,
    InstMsgCancelled,
    InstMsgExtractCancelled,
    InstMsgExtractFailed,
    InstMsgPackagePrepFailed,
    InstMsgInvalidVpkLayout,
    InstMsgVpkNotFound,
    InstMsgEmptyPath,
    InstMsgFileNotFound,
    InstMsgUnsupportedFormat,
    InstMsgCannotCreateDir,
    InstMsgCannotOpenSource,
    InstMsgCannotOpenDest,
    InstMsgReadFailed,
    InstMsgWriteFailed,
    InstMsgNotPbp,
    InstMsgNotIsoCso,
    InstMsgNotPkg,
    InstMsgPkgNotFound,
    InstMsgRifNotFound,
    InstMsgInstallationFailed,
    StageRetrying,
    StageNetwork,
    StageSpace,
    StagePlugin,
    StageCompleted,
    StageCancelled,
    StageError,
    StageExtracting,
    PspLiveAreaNeedsNoPspEmuDrm,
    NoPspEmuDrmMissingToast,
    // First-time PSP/PS1 install method wizard
    PspSetupTitle,
    PspSetupIntro,
    PspSetupLiveAreaTitle,
    PspSetupLiveAreaDesc,
    PspSetupAdrenalineTitle,
    PspSetupAdrenalineDesc,
    PspSetupFolderTitle,
    PspSetupFolderDesc,
    PspSetupIsoTitle,
    PspSetupIsoDesc,
    PspSetupSettingsHint,
    PspSetupConfirm,
    PspSetupCancel,
    PspSetupNavHint,
    PspSetupLiveAreaBlocked,
    // Catalog card install/update badges (appended to preserve existing TextId ordinals)
    BadgeUpdate,
    Count
};

class LocalizationManager {
public:
    static LocalizationManager& instance();

    bool initialize(const AppSettingsData& settings);
    bool setMode(LanguageMode mode, const std::string& languageCode = "en");

    Language currentLanguage() const { return currentLanguage_; }
    LanguageMode mode() const { return mode_; }
    const char* currentLanguageName() const { return languageName(currentLanguage_); }

    const char* get(TextId id) const;
    const char* get(const char* key) const;
    bool isLanguageAvailable(Language language) const;
    std::vector<Language> availableLanguages() const;

private:
    LocalizationManager() = default;

    bool loadTable(Language language, std::unordered_map<std::string, std::string>& table) const;
    void selectLanguage(Language language);
    static const char* keyFor(TextId id);

    LanguageMode mode_ = LanguageMode::System;
    Language currentLanguage_ = Language::English;
    std::unordered_map<std::string, std::string> active_;
    std::unordered_map<std::string, std::string> english_;
};

inline const char* L(TextId id) { return LocalizationManager::instance().get(id); }

// The startup image-cache maintenance runs before normal catalog loading. Keep
// these four messages localized even on current language packs that predate the
// feature. A future .lang entry wins automatically because L(key) first asks
// LocalizationManager::get(); this table is only a compatibility fallback.
inline const char* startupImageCacheTextFallback(Language language, const char* key) {
    if (!key) return nullptr;
    int text = -1;
    if (std::strcmp(key, "IMAGE_CACHE_LABEL") == 0) text = 0;
    else if (std::strcmp(key, "IMAGE_CACHE_CHECKING") == 0) text = 1;
    else if (std::strcmp(key, "IMAGE_CACHE_CLEANING") == 0) text = 2;
    else if (std::strcmp(key, "IMAGE_CACHE_READY") == 0) text = 3;
    if (text < 0) return nullptr;

    switch (language) {
        case Language::Spanish: {
            static const char* const v[] = {
                "Caché de imágenes", "Comprobando caché de imágenes...",
                "Limpiando imágenes antiguas de la caché...", "Caché de imágenes lista"
            };
            return v[text];
        }
        case Language::French: {
            static const char* const v[] = {
                "Cache d'images", "Vérification du cache d'images...",
                "Nettoyage des anciennes images en cache...", "Cache d'images prêt"
            };
            return v[text];
        }
        case Language::German: {
            static const char* const v[] = {
                "Bild-Cache", "Bild-Cache wird überprüft...",
                "Alte zwischengespeicherte Bilder werden bereinigt...", "Bild-Cache bereit"
            };
            return v[text];
        }
        case Language::Italian: {
            static const char* const v[] = {
                "Cache immagini", "Controllo della cache immagini...",
                "Pulizia delle vecchie immagini nella cache...", "Cache immagini pronta"
            };
            return v[text];
        }
        case Language::PortugueseBrazil: {
            static const char* const v[] = {
                "Cache de imagens", "Verificando o cache de imagens...",
                "Limpando imagens antigas do cache...", "Cache de imagens pronto"
            };
            return v[text];
        }
        case Language::PortuguesePortugal: {
            static const char* const v[] = {
                "Cache de imagens", "A verificar a cache de imagens...",
                "A limpar imagens antigas da cache...", "Cache de imagens pronta"
            };
            return v[text];
        }
        case Language::Russian: {
            static const char* const v[] = {
                "Кэш изображений", "Проверка кэша изображений...",
                "Очистка старых изображений из кэша...", "Кэш изображений готов"
            };
            return v[text];
        }
        case Language::English:
        default: {
            static const char* const v[] = {
                "Image cache", "Checking image cache...",
                "Cleaning old cached images...", "Image cache ready"
            };
            return v[text];
        }
    }
}

inline const char* L(const char* key) {
    LocalizationManager& manager = LocalizationManager::instance();
    const char* resolved = manager.get(key);
    if (!key || !resolved || std::strcmp(resolved, key) != 0) return resolved;
    const char* startupFallback = startupImageCacheTextFallback(manager.currentLanguage(), key);
    return startupFallback ? startupFallback : resolved;
}

} // namespace psvitaalive

#pragma once

#include "installer/app_settings.hpp"
#include "localization/language.hpp"

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
    PromptImageDownload,
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
    HintImageWarmup,
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
    InfoImageWarmup1, InfoImageWarmup2, InfoImageWarmup3,
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
inline const char* L(const char* key) { return LocalizationManager::instance().get(key); }

} // namespace psvitaalive

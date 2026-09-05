#include "localization/localization.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/apputil.h>
#include <psp2/system_param.h>

#include <cstdio>
#include <cstring>

namespace psvitaalive {
namespace {

const char* const kLanguageRoot = "app0:lang/";

bool readFile(const char* path, std::string& out) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return false;
    char buffer[4096];
    out.clear();
    for (;;) {
        const int n = sceIoRead(fd, buffer, sizeof(buffer));
        if (n < 0) { sceIoClose(fd); return false; }
        if (n == 0) break;
        out.append(buffer, static_cast<size_t>(n));
    }
    sceIoClose(fd);
    return true;
}

void trim(std::string& s) {
    size_t first = 0;
    while (first < s.size() && (s[first] == ' ' || s[first] == '\t' || s[first] == '\r')) ++first;
    size_t last = s.size();
    while (last > first && (s[last - 1] == ' ' || s[last - 1] == '\t' || s[last - 1] == '\r')) --last;
    s = s.substr(first, last - first);
}

bool parseTable(const std::string& content, std::unordered_map<std::string, std::string>& table) {
    table.clear();
    size_t start = 0;
    while (start <= content.size()) {
        size_t end = content.find('\n', start);
        if (end == std::string::npos) end = content.size();
        std::string line = content.substr(start, end - start);
        trim(line);
        if (!line.empty() && line[0] != '#') {
            const size_t eq = line.find('=');
            if (eq != std::string::npos && eq > 0) {
                std::string key = line.substr(0, eq);
                std::string value = line.substr(eq + 1);
                trim(key); trim(value);
                if (!key.empty()) table[key] = value;
            }
        }
        if (end == content.size()) break;
        start = end + 1;
    }
    return true;
}

} // namespace

LocalizationManager& LocalizationManager::instance() {
    static LocalizationManager manager;
    return manager;
}

bool LocalizationManager::loadTable(Language language, std::unordered_map<std::string, std::string>& table) const {
    char path[64];
    sceClibSnprintf(path, sizeof(path), "%s%s.lang", kLanguageRoot, languageCode(language));
    std::string content;
    if (!readFile(path, content)) return false;
    return parseTable(content, table);
}

void LocalizationManager::selectLanguage(Language language) {
    std::unordered_map<std::string, std::string> selected;
    if (language == Language::English || !loadTable(language, selected)) {
        currentLanguage_ = Language::English;
        active_ = english_;
        return;
    }
    currentLanguage_ = language;
    active_.swap(selected);
}

bool LocalizationManager::initialize(const AppSettingsData& settings) {
    english_.clear();
    if (!loadTable(Language::English, english_)) {
        sceClibPrintf("[Localization] English table missing: using key fallback\n");
    }

    mode_ = settings.languageMode;
    Language requested = Language::English;
    if (mode_ == LanguageMode::Manual) {
        if (!languageFromCode(settings.language, requested) || !isLanguageAvailable(requested))
            requested = Language::English;
    } else {
        int systemLanguage = 1;
        if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &systemLanguage) < 0)
            systemLanguage = 1;
        requested = languageFromSystemValue(systemLanguage);
    }
    selectLanguage(requested);
    sceClibPrintf("[Localization] mode=%s language=%s\n", AppSettings::toString(mode_), languageCode(currentLanguage_));
    return !english_.empty();
}

bool LocalizationManager::setMode(LanguageMode mode, const std::string& languageCodeValue) {
    mode_ = mode;
    if (mode_ == LanguageMode::Manual) {
        Language requested;
        if (languageFromCode(languageCodeValue, requested) && isLanguageAvailable(requested)) {
            selectLanguage(requested);
            return true;
        }
        mode_ = LanguageMode::System;
    }
    int systemLanguage = 1;
    if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &systemLanguage) < 0)
        systemLanguage = 1;
    selectLanguage(languageFromSystemValue(systemLanguage));
    return true;
}

const char* LocalizationManager::get(TextId id) const { return get(keyFor(id)); }

const char* LocalizationManager::get(const char* key) const {
    if (!key) return "";
    const auto it = active_.find(key);
    if (it != active_.end()) return it->second.c_str();
    const auto en = english_.find(key);
    if (en != english_.end()) return en->second.c_str();
    return key;
}

bool LocalizationManager::isLanguageAvailable(Language language) const {
    if (language == Language::English) return !english_.empty();
    std::unordered_map<std::string, std::string> probe;
    return loadTable(language, probe);
}

std::vector<Language> LocalizationManager::availableLanguages() const {
    std::vector<Language> result;
    for (int i = static_cast<int>(Language::English); i <= static_cast<int>(Language::Turkish); ++i) {
        const Language language = static_cast<Language>(i);
        if (isLanguageAvailable(language)) result.push_back(language);
    }
    return result;
}

const char* LocalizationManager::keyFor(TextId id) {
    switch (id) {
        case TextId::Search: return "SEARCH";
        case TextId::Settings: return "SETTINGS";
        case TextId::Download: return "DOWNLOAD";
        case TextId::Install: return "INSTALL";
        case TextId::Cancel: return "CANCEL";
        case TextId::Loading: return "LOADING";
        case TextId::Language: return "LANGUAGE";
        case TextId::SystemAutomatic: return "SYSTEM_AUTOMATIC";
        case TextId::English: return "ENGLISH";
        case TextId::Spanish: return "SPANISH";
        case TextId::ColorTheme: return "COLOR_THEME";
        case TextId::InstallMethod: return "INSTALL_METHOD";
        case TextId::PspPs1Target: return "PSP_PS1_TARGET";
        case TextId::PspMediaAdrenaline: return "PSP_MEDIA_ADRENALINE";
        case TextId::WarnMissingPlugins: return "WARN_MISSING_PLUGINS";
        case TextId::PromptImageDownload: return "PROMPT_IMAGE_DOWNLOAD";
        case TextId::CheckForUpdates: return "CHECK_FOR_UPDATES";
        case TextId::Auto: return "AUTO";
        case TextId::Direct: return "DIRECT";
        case TextId::Adrenaline: return "ADRENALINE";
        case TextId::LiveArea: return "LIVEAREA";
        case TextId::Folder: return "FOLDER";
        case TextId::Iso: return "ISO";
        case TextId::Yes: return "YES";
        case TextId::No: return "NO";
        case TextId::SectionInstall: return "SECTION_INSTALL";
        case TextId::SectionInterface: return "SECTION_INTERFACE";
        case TextId::SectionCatalog: return "SECTION_CATALOG";
        case TextId::SectionUpdates: return "SECTION_UPDATES";
        case TextId::Info: return "INFO";
        case TextId::System: return "SYSTEM";
        case TextId::SettingsSaved: return "SETTINGS_SAVED";
        case TextId::SettingsFooter: return "SETTINGS_FOOTER";
        case TextId::SettingsSaveBack: return "SETTINGS_SAVE_BACK";
        case TextId::HintInstallMethod: return "HINT_INSTALL_METHOD";
        case TextId::HintPspTarget: return "HINT_PSP_TARGET";
        case TextId::HintPspMedia: return "HINT_PSP_MEDIA";
        case TextId::HintLanguage: return "HINT_LANGUAGE";
        case TextId::HintColorTheme: return "HINT_COLOR_THEME";
        case TextId::HintWarnPlugins: return "HINT_WARN_PLUGINS";
        case TextId::HintImageWarmup: return "HINT_IMAGE_WARMUP";
        case TextId::HintSelfUpdate: return "HINT_SELF_UPDATE";
        case TextId::CatalogHomebrew: return "CATALOG_HOMEBREW";
        case TextId::CatalogVitaGames: return "CATALOG_VITA_GAMES";
        case TextId::CatalogPsp: return "CATALOG_PSP";
        case TextId::CatalogPs1: return "CATALOG_PS1";
        case TextId::CatalogUnknown: return "CATALOG_UNKNOWN";
        case TextId::FooterCatalog: return "FOOTER_CATALOG";
        case TextId::FooterDetailList: return "FOOTER_DETAIL_LIST";
        case TextId::FooterDetailPanel: return "FOOTER_DETAIL_PANEL";
        case TextId::SearchPlaceholder: return "SEARCH_PLACEHOLDER";
        case TextId::FilterGdOnly: return "FILTER_GD_ONLY";
        case TextId::FilterCleared: return "FILTER_CLEARED";
        case TextId::LoadingCatalog: return "LOADING_CATALOG";
        case TextId::ChangingCatalog: return "CHANGING_CATALOG";
        case TextId::InfoInstallMethod1: return "INFO_INSTALL_METHOD_1";
        case TextId::InfoInstallMethod2: return "INFO_INSTALL_METHOD_2";
        case TextId::InfoInstallMethod3: return "INFO_INSTALL_METHOD_3";
        case TextId::InfoPspTarget1: return "INFO_PSP_TARGET_1";
        case TextId::InfoPspTarget2: return "INFO_PSP_TARGET_2";
        case TextId::InfoPspTarget3: return "INFO_PSP_TARGET_3";
        case TextId::InfoPspMedia1: return "INFO_PSP_MEDIA_1";
        case TextId::InfoPspMedia2: return "INFO_PSP_MEDIA_2";
        case TextId::InfoPspMedia3: return "INFO_PSP_MEDIA_3";
        case TextId::InfoLanguage1: return "INFO_LANGUAGE_1";
        case TextId::InfoLanguage2: return "INFO_LANGUAGE_2";
        case TextId::InfoLanguage3: return "INFO_LANGUAGE_3";
        case TextId::InfoColorTheme1: return "INFO_COLOR_THEME_1";
        case TextId::InfoColorTheme2: return "INFO_COLOR_THEME_2";
        case TextId::InfoColorTheme3: return "INFO_COLOR_THEME_3";
        case TextId::InfoWarnPlugins1: return "INFO_WARN_PLUGINS_1";
        case TextId::InfoWarnPlugins2: return "INFO_WARN_PLUGINS_2";
        case TextId::InfoWarnPlugins3: return "INFO_WARN_PLUGINS_3";
        case TextId::InfoImageWarmup1: return "INFO_IMAGE_WARMUP_1";
        case TextId::InfoImageWarmup2: return "INFO_IMAGE_WARMUP_2";
        case TextId::InfoImageWarmup3: return "INFO_IMAGE_WARMUP_3";
        case TextId::InfoSelfUpdate1: return "INFO_SELF_UPDATE_1";
        case TextId::InfoSelfUpdate2: return "INFO_SELF_UPDATE_2";
        case TextId::InfoSelfUpdate3: return "INFO_SELF_UPDATE_3";
        case TextId::StatusOk: return "STATUS_OK";
        case TextId::StatusMissing: return "STATUS_MISSING";
        case TextId::LocalVersion: return "LOCAL_VERSION";
        case TextId::RemoteVersion: return "REMOTE_VERSION";
        case TextId::RemoteUpToDate: return "REMOTE_UP_TO_DATE";
        case TextId::RemoteCheckFailed: return "REMOTE_CHECK_FAILED";
        case TextId::UpdateWorking: return "UPDATE_WORKING";
        case TextId::UpdateInstallPrefix: return "UPDATE_INSTALL_PREFIX";
        case TextId::UpdateUpToDate: return "UPDATE_UP_TO_DATE";
        case TextId::UpdateCheckFailed: return "UPDATE_CHECK_FAILED";
        case TextId::SectionDownloads: return "SECTION_DOWNLOADS";
        case TextId::SectionDataFiles: return "SECTION_DATA_FILES";
        case TextId::SectionGameFiles: return "SECTION_GAME_FILES";
        case TextId::SectionMods: return "SECTION_MODS";
        case TextId::SectionDlc: return "SECTION_DLC";
        case TextId::SectionUpdatesLinks: return "SECTION_UPDATES_LINKS";
        case TextId::SectionPkg: return "SECTION_PKG";
        case TextId::SectionPlugins: return "SECTION_PLUGINS";
        case TextId::SectionOther: return "SECTION_OTHER";
        case TextId::MetaDownload: return "META_DOWNLOAD";
        case TextId::MetaDataFiles: return "META_DATA_FILES";
        case TextId::MetaGameFiles: return "META_GAME_FILES";
        case TextId::MetaMod: return "META_MOD";
        case TextId::MetaDlc: return "META_DLC";
        case TextId::MetaUpdate: return "META_UPDATE";
        case TextId::MetaPkg: return "META_PKG";
        case TextId::MetaPlugin: return "META_PLUGIN";
        case TextId::DetailInformation: return "DETAIL_INFORMATION";
        case TextId::InstallAll: return "INSTALL_ALL";
        case TextId::AlreadyInstalled: return "ALREADY_INSTALLED";
        case TextId::PspDlcBlocked: return "PSP_DLC_BLOCKED";
        case TextId::InstallAllHeader: return "INSTALL_ALL_HEADER";
        case TextId::InstallAllSubtitle: return "INSTALL_ALL_SUBTITLE";
        case TextId::BadgeRecommended: return "BADGE_RECOMMENDED";
        case TextId::BadgeInstalled: return "BADGE_INSTALLED";
        case TextId::MetaAlreadyInstalled: return "META_ALREADY_INSTALLED";
        case TextId::MetaInstalled: return "META_INSTALLED";
        case TextId::MetaXInstall: return "META_X_INSTALL";
        case TextId::MetaX: return "META_X";
        case TextId::SectionDescription: return "SECTION_DESCRIPTION";
        case TextId::SectionLongDescription: return "SECTION_LONG_DESCRIPTION";
        case TextId::SectionScreenshots: return "SECTION_SCREENSHOTS";
        case TextId::SectionRequirements: return "SECTION_REQUIREMENTS";
        case TextId::SectionChangelog: return "SECTION_CHANGELOG";
        case TextId::MetaTitleId: return "META_TITLE_ID";
        case TextId::MetaVersion: return "META_VERSION";
        case TextId::MetaInstall: return "META_INSTALL";
        case TextId::MetaReleased: return "META_RELEASED";
        case TextId::MetaCategory: return "META_CATEGORY";
        case TextId::MetaSubcategory: return "META_SUBCATEGORY";
        case TextId::MetaSize: return "META_SIZE";
        case TextId::MetaStatus: return "META_STATUS";
        case TextId::InstallStateInstalled: return "INSTALL_STATE_INSTALLED";
        case TextId::InstallStateUpdateAvailable: return "INSTALL_STATE_UPDATE_AVAILABLE";
        case TextId::InstallStateNotInstalled: return "INSTALL_STATE_NOT_INSTALLED";
        case TextId::BtnContinue: return "BTN_CONTINUE";
        case TextId::BtnCancel: return "BTN_CANCEL";
        case TextId::InstallAllConfirm1: return "INSTALL_ALL_CONFIRM_1";
        case TextId::InstallAllConfirm2: return "INSTALL_ALL_CONFIRM_2";
        case TextId::InstallAllConfirm3: return "INSTALL_ALL_CONFIRM_3";
        case TextId::InstallAllConfirm4: return "INSTALL_ALL_CONFIRM_4";
        case TextId::InstallAllNavHint: return "INSTALL_ALL_NAV_HINT";
        case TextId::ChooseDownload: return "CHOOSE_DOWNLOAD";
        case TextId::ChooseGameFiles: return "CHOOSE_GAME_FILES";
        case TextId::ChooseDataFiles: return "CHOOSE_DATA_FILES";
        case TextId::ChooseMirrorHint: return "CHOOSE_MIRROR_HINT";
        case TextId::InstallComplete: return "INSTALL_COMPLETE";
        case TextId::DownloadCancelled: return "DOWNLOAD_CANCELLED";
        case TextId::InstallFailed: return "INSTALL_FAILED";
        case TextId::PanelDetail: return "PANEL_DETAIL";
        case TextId::SelectLinks: return "SELECT_LINKS";
        case TextId::ExitLinkMode: return "EXIT_LINK_MODE";
        case TextId::RequestData: return "REQUEST_DATA";
        case TextId::ChipNews: return "CHIP_NEWS";
        case TextId::ChipReport: return "CHIP_REPORT";
        case TextId::ChipSent: return "CHIP_SENT";
        case TextId::ChipFail: return "CHIP_FAIL";
        case TextId::ReportTitle: return "REPORT_TITLE";
        case TextId::ReportSubtitle: return "REPORT_SUBTITLE";
        case TextId::ReportBody1: return "REPORT_BODY_1";
        case TextId::ReportBody2: return "REPORT_BODY_2";
        case TextId::BtnOCancel: return "BTN_O_CANCEL";
        case TextId::BtnXReport: return "BTN_X_REPORT";
        case TextId::BtnOClose: return "BTN_O_CLOSE";
        case TextId::NewsNavHint: return "NEWS_NAV_HINT";
        case TextId::LockedFinishJob: return "LOCKED_FINISH_JOB";
        case TextId::LockedStillRunning: return "LOCKED_STILL_RUNNING";
        case TextId::LockedCannotExit: return "LOCKED_CANNOT_EXIT";
        case TextId::LockedCannotSwitchCatalog: return "LOCKED_CANNOT_SWITCH_CATALOG";
        case TextId::ZipExtractComplete: return "ZIP_EXTRACT_COMPLETE";
        case TextId::ExtractedTo: return "EXTRACTED_TO";
        case TextId::OContinue: return "O_CONTINUE";
        case TextId::StageDownloading: return "STAGE_DOWNLOADING";
        case TextId::StageInstalling: return "STAGE_INSTALLING";
        case TextId::StagePreparingDownload: return "STAGE_PREPARING_DOWNLOAD";
        case TextId::StagePreparing: return "STAGE_PREPARING";
        case TextId::LabelFile: return "LABEL_FILE";
        case TextId::LabelEta: return "LABEL_ETA";
        case TextId::HintRetryConnection: return "HINT_RETRY_CONNECTION";
        case TextId::HintRetryStep: return "HINT_RETRY_STEP";
        case TextId::HintExtracting: return "HINT_EXTRACTING";
        case TextId::HintInstalling: return "HINT_INSTALLING";
        case TextId::HintConnecting: return "HINT_CONNECTING";
        case TextId::HintDownloadSpeed: return "HINT_DOWNLOAD_SPEED";
        case TextId::HintPleaseWait: return "HINT_PLEASE_WAIT";
        case TextId::LockedBanner1: return "LOCKED_BANNER_1";
        case TextId::LockedBanner2: return "LOCKED_BANNER_2";
        case TextId::CircleCancelDownload: return "CIRCLE_CANCEL_DOWNLOAD";
        case TextId::ProgressFooterHint: return "PROGRESS_FOOTER_HINT";
        default: return "";
    }
}

} // namespace psvitaalive

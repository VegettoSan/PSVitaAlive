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
    Spanish
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

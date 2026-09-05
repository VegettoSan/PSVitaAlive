#include "localization/localization.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/appmgr.h>

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
                trim(key);
                trim(value);
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
        sceClibPrintf("[Localization] English table missing: using built-in key fallback\n");
    }

    mode_ = settings.languageMode;
    Language requested = Language::English;
    if (mode_ == LanguageMode::Manual) {
        if (!languageFromCode(settings.language, requested)) requested = Language::English;
    } else {
        int systemLanguage = 1;
        if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &systemLanguage) < 0)
            systemLanguage = 1;
        requested = languageFromSystemValue(systemLanguage);
    }
    selectLanguage(requested);
    sceClibPrintf("[Localization] mode=%s language=%s available=%d\n", AppSettings::toString(mode_), languageCode(currentLanguage_), isLanguageAvailable(currentLanguage_) ? 1 : 0);
    return !english_.empty();
}

bool LocalizationManager::setMode(LanguageMode mode, const std::string& languageCodeValue) {
    mode_ = mode;
    if (mode_ == LanguageMode::Manual) {
        Language requested;
        if (!languageFromCode(languageCodeValue, requested) || !isLanguageAvailable(requested)) {
            mode_ = LanguageMode::System;
        } else {
            selectLanguage(requested);
            return true;
        }
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
        default: return "";
    }
}

} // namespace psvitaalive

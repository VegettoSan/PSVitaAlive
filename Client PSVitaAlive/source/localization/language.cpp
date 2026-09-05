#include "localization/language.hpp"

namespace psvitaalive {

const char* languageCode(Language language) {
    switch (language) {
        case Language::Spanish: return "es";
        case Language::French: return "fr";
        case Language::German: return "de";
        case Language::Italian: return "it";
        case Language::Dutch: return "nl";
        case Language::PortuguesePortugal: return "pt-PT";
        case Language::PortugueseBrazil: return "pt-BR";
        case Language::Russian: return "ru";
        case Language::Korean: return "ko";
        case Language::ChineseTraditional: return "zh-TW";
        case Language::ChineseSimplified: return "zh-CN";
        case Language::Finnish: return "fi";
        case Language::Swedish: return "sv";
        case Language::Danish: return "da";
        case Language::Norwegian: return "no";
        case Language::Polish: return "pl";
        case Language::Turkish: return "tr";
        case Language::English:
        default: return "en";
    }
}

const char* languageName(Language language) {
    switch (language) {
        case Language::Spanish: return "Español";
        case Language::French: return "Français";
        case Language::German: return "Deutsch";
        case Language::Italian: return "Italiano";
        case Language::Dutch: return "Nederlands";
        case Language::PortuguesePortugal: return "Português (Portugal)";
        case Language::PortugueseBrazil: return "Português (Brasil)";
        case Language::Russian: return "Русский";
        case Language::Korean: return "한국어";
        case Language::ChineseTraditional: return "繁體中文";
        case Language::ChineseSimplified: return "简体中文";
        case Language::Finnish: return "Suomi";
        case Language::Swedish: return "Svenska";
        case Language::Danish: return "Dansk";
        case Language::Norwegian: return "Norsk";
        case Language::Polish: return "Polski";
        case Language::Turkish: return "Türkçe";
        case Language::English:
        default: return "English";
    }
}

bool languageFromCode(const std::string& code, Language& out) {
    for (int i = static_cast<int>(Language::English); i <= static_cast<int>(Language::Turkish); ++i) {
        const Language candidate = static_cast<Language>(i);
        if (code == languageCode(candidate)) {
            out = candidate;
            return true;
        }
    }
    return false;
}

Language languageFromSystemValue(int systemLanguage) {
    // SceSystemParamLang values verified against the documented Vita system-language enum.
    switch (systemLanguage) {
        case 1:  // American English
        case 18: // British English
            return Language::English;
        case 3: return Language::Spanish;
        case 2: return Language::French;
        case 4: return Language::German;
        case 5: return Language::Italian;
        case 6: return Language::Dutch;
        case 7: return Language::PortuguesePortugal;
        case 17: return Language::PortugueseBrazil;
        case 8: return Language::Russian;
        case 9: return Language::Korean;
        case 10: return Language::ChineseTraditional;
        case 11: return Language::ChineseSimplified;
        case 12: return Language::Finnish;
        case 13: return Language::Swedish;
        case 14: return Language::Danish;
        case 15: return Language::Norwegian;
        case 16: return Language::Polish;
        case 19: return Language::Turkish;
        default: return Language::English;
    }
}

} // namespace psvitaalive

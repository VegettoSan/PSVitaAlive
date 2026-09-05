#pragma once

#include <string>

namespace psvitaalive {

enum class Language {
    English = 0,
    Spanish,
    French,
    German,
    Italian,
    Dutch,
    PortuguesePortugal,
    PortugueseBrazil,
    Russian,
    Korean,
    ChineseTraditional,
    ChineseSimplified,
    Finnish,
    Swedish,
    Danish,
    Norwegian,
    Polish,
    Turkish
};

const char* languageCode(Language language);
const char* languageName(Language language);
bool languageFromCode(const std::string& code, Language& out);
Language languageFromSystemValue(int systemLanguage);

} // namespace psvitaalive

#pragma once

#include <string>

namespace psvitaalive {

// Discord Forum thread names allow at most 100 Unicode code points.
// Prefix app context only when a detail page was open. Preserve both app name
// and user title, removing controls and truncating on UTF-8 boundaries.
inline std::string feedbackForumClean(const std::string& text) {
    std::string clean;
    clean.reserve(text.size());
    bool previousSpace = false;
    for (unsigned char ch : text) {
        const bool whitespace = (ch <= 0x20 || ch == 0x7F);
        if (whitespace) {
            if (!clean.empty() && !previousSpace) {
                clean.push_back(' ');
                previousSpace = true;
            }
        } else {
            clean.push_back(static_cast<char>(ch));
            previousSpace = false;
        }
    }
    if (!clean.empty() && clean.back() == ' ') clean.pop_back();
    return clean;
}

inline std::string feedbackForumLimitUtf8(const std::string& value, size_t maxChars) {
    size_t chars = 0;
    size_t end = 0;
    while (end < value.size()) {
        // A continuation byte must not be counted as a new character.
        if ((static_cast<unsigned char>(value[end]) & 0xC0) != 0x80) {
            if (chars == maxChars) break;
            ++chars;
        }
        ++end;
    }
    return value.substr(0, end);
}

inline size_t feedbackForumUtf8Chars(const std::string& value) {
    size_t count = 0;
    for (unsigned char ch : value)
        if ((ch & 0xC0) != 0x80) ++count;
    return count;
}

inline std::string feedbackForumThreadName(
    const std::string& title, bool suggestion, const std::string& appName = std::string()) {
    const std::string cleanTitle = feedbackForumClean(title);
    const std::string cleanApp = feedbackForumLimitUtf8(feedbackForumClean(appName), 44);
    // Emoji and one separator space use two code points.
    constexpr size_t kTextLimit = 98;
    std::string combined;
    if (!cleanApp.empty() && !cleanTitle.empty()) {
        combined = cleanApp + ": ";
        const size_t prefixChars = feedbackForumUtf8Chars(combined);
        combined += feedbackForumLimitUtf8(cleanTitle, kTextLimit - prefixChars);
    } else if (!cleanApp.empty()) {
        combined = cleanApp;
    } else {
        combined = feedbackForumLimitUtf8(cleanTitle, kTextLimit);
    }
    const char* prefix = suggestion ? "💡 " : "🐛 ";
    return std::string(prefix) + (combined.empty() ? "PSVitaAlive Feedback" : combined);
}

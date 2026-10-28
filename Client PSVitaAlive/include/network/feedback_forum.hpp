#pragma once

#include <string>

namespace psvitaalive {

// Discord forum posts require a nonempty thread_name (maximum 100 Unicode
// characters). The PSVitaAlive form already limits titles to 64 characters.
// Strip ASCII controls without breaking UTF-8 multibyte sequences.
inline std::string feedbackForumThreadName(const std::string& title, bool suggestion) {
    std::string clean;
    clean.reserve(title.size() + 4);
    bool previousSpace = false;
    for (unsigned char ch : title) {
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
    const char* prefix = suggestion ? "💡 " : "🐛 ";
    return std::string(prefix) + (clean.empty() ? "PSVitaAlive Feedback" : clean);
}

} // namespace psvitaalive

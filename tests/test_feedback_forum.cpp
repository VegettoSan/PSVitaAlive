#include "network/feedback_forum.hpp"

#include <cassert>
#include <string>

int main() {
    using psvitaalive::feedbackForumThreadName;
    assert(feedbackForumThreadName("Download crashes", false) == "🐛 Download crashes");
    assert(feedbackForumThreadName("Add favorites", true) == "💡 Add favorites");
    assert(feedbackForumThreadName("\n  Broken\r\tinstall\n", false) == "🐛 Broken install");
    assert(feedbackForumThreadName("Añadir idiomas 日本語", true) == "💡 Añadir idiomas 日本語");
    assert(feedbackForumThreadName(" \t ", true) == "💡 PSVitaAlive Feedback");
    assert(feedbackForumThreadName("Install failed", false, "VitaDeck") == "🐛 VitaDeck: Install failed");
    assert(feedbackForumThreadName("Update suggestion", true, "日本語 Port") == "💡 日本語 Port: Update suggestion");
    assert(feedbackForumThreadName("Install failed", false, "") == "🐛 Install failed");
    const std::string longName(120, 'A');
    const std::string longTitle(120, 'B');
    const std::string longThread = feedbackForumThreadName(longTitle, false, longName);
    assert(psvitaalive::feedbackForumUtf8Chars(longThread) <= 100);
    assert(longThread.find(": ") != std::string::npos);
    assert(longThread.find("BBB") != std::string::npos);

    return 0;
}

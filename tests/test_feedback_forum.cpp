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
    return 0;
}

#pragma once

#include <string>

namespace psvitaalive {

/** Remote news / announcement (plain text from GitHub). */
struct NewsItem {
    std::string id;
    std::string title;
    std::string body;
    bool enabled = true;
    bool valid = false;
};

/**
 * Fetch and track "seen" news from news.txt on GitHub.
 * Does not touch install/download pipelines.
 */
class NewsManager {
public:
    static constexpr const char* kRemoteUrl =
        "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/news.txt";
    static constexpr const char* kCachePath = "ux0:data/psvitaalive/cache/news.txt";
    static constexpr const char* kSeenPath = "ux0:data/psvitaalive/news_seen_id.txt";

    /** GET remote news.txt (small). On failure returns invalid item. */
    static NewsItem fetchRemote();

    /** Load last successfully cached news (offline / footer). */
    static NewsItem loadCached();

    /** Parse news.txt format (id/title/enabled + --- body). */
    static NewsItem parseText(const std::string& text);

    static std::string loadSeenId();
    static void saveSeenId(const std::string& id);
    static void saveCache(const std::string& rawText);

    /** True if item is valid, enabled, and id differs from last seen. */
    static bool shouldAutoShow(const NewsItem& item);
};

} // namespace psvitaalive

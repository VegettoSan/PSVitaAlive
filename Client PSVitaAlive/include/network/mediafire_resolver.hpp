#pragma once

#include "network/http_client.hpp"

#include <string>

namespace psvitaalive {

/** True if URL hosts on mediafire.com (share page or download CDN). */
bool isMediaFireUrl(const std::string& url);

/**
 * Resolve a MediaFire share/view page into a direct CDN download URL.
 * Safe no-op helper: on failure leaves directOut empty and returns false.
 * Does not download the payload file — only fetches the HTML page (size-capped).
 */
bool resolveMediaFireDirectUrl(
    HttpClient& http,
    const std::string& pageUrl,
    std::string& directOut,
    std::string& errorOut
);

} // namespace psvitaalive

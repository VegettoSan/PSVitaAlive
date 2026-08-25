#pragma once

#include "network/http_client.hpp"

#include <cstdint>
#include <string>

namespace psvitaalive {

/** True if URL hosts on mediafire.com (share page or download CDN). */
bool isMediaFireUrl(const std::string& url);

/**
 * Resolve a MediaFire share/view page into a direct CDN download URL.
 * Optionally fills sizeBytesOut when the share page exposes a file size
 * (e.g. "Download (1.47GB)"). sizeBytesOut may stay 0 if unknown.
 * Does not download the payload file — only fetches the HTML page (size-capped).
 */
bool resolveMediaFireDirectUrl(
    HttpClient& http,
    const std::string& pageUrl,
    std::string& directOut,
    std::string& errorOut,
    uint64_t* sizeBytesOut = nullptr
);

} // namespace psvitaalive

#pragma once

// Vita SDK / libcurl lifecycle proxy used only by HttpClient.
// The real libcurl header is still included; only the global init entry point
// is redirected for the HttpClient compilation unit. There is intentionally
// no global cleanup macro: worker shutdown is not process shutdown.
#include_next <curl/curl.h>

#ifdef PSVITAALIVE_CURL_LIFECYCLE_PROXY
extern "C" CURLcode psvitaalive_curl_global_init(long flags);
#define curl_global_init(flags) psvitaalive_curl_global_init(flags)
#endif

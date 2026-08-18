#pragma once

// Vita SDK / libcurl lifecycle proxy used only by HttpClient.
// The real libcurl header is still included; only the global init entry point
// and easy-perform call are redirected for the HttpClient compilation unit.
#include_next <curl/curl.h>

#ifdef PSVITAALIVE_CURL_LIFECYCLE_PROXY
extern "C" CURLcode psvitaalive_curl_global_init(long flags);
extern "C" CURLcode psvitaalive_curl_easy_perform(CURL* curl);
#define curl_global_init(flags) psvitaalive_curl_global_init(flags)
#define curl_easy_perform(curl) psvitaalive_curl_easy_perform(curl)
#endif

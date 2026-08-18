#pragma once

// Vita SDK / libcurl lifecycle proxy used only by HttpClient.
// The real libcurl header is still included; only the global init/cleanup
// entry points are redirected for the HttpClient compilation unit.
#include_next <curl/curl.h>

#ifdef PSVITAALIVE_CURL_LIFECYCLE_PROXY
extern "C" CURLcode psvitaalive_curl_global_init(long flags);
extern "C" void psvitaalive_curl_global_cleanup(void);
#define curl_global_init(flags) psvitaalive_curl_global_init(flags)
#define curl_global_cleanup() psvitaalive_curl_global_cleanup()
#endif

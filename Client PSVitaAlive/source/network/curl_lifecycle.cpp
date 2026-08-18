#include <curl/curl.h>

#include <mutex>

namespace {
std::mutex gCurlLifecycleMutex;
bool gCurlInitialized = false;
}

// libcurl global state belongs to the complete PSVitaAlive process.
// It must be initialized before any easy handle is used and must remain alive
// while CatalogManager, ImageCache, Installer and UpdateChecker can still issue
// HTTP requests. We intentionally do NOT expose a cleanup path here: worker
// shutdown is not process shutdown, and curl_global_cleanup() is unsafe while
// another worker may still be using libcurl internals.
extern "C" CURLcode psvitaalive_curl_global_init(long flags) {
    std::lock_guard<std::mutex> lock(gCurlLifecycleMutex);

    if (gCurlInitialized) return CURLE_OK;

    const CURLcode result = curl_global_init(static_cast<long>(flags));
    if (result != CURLE_OK) return result;

    gCurlInitialized = true;
    return CURLE_OK;
}

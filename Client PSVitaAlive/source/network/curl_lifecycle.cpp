#include <curl/curl.h>

#include <mutex>

namespace {
std::mutex gCurlLifecycleMutex;
unsigned gCurlUsers = 0;
}

extern "C" CURLcode psvitaalive_curl_global_init(long flags) {
    std::lock_guard<std::mutex> lock(gCurlLifecycleMutex);
    if (gCurlUsers == 0) {
        const CURLcode result = curl_global_init(static_cast<long>(flags));
        if (result != CURLE_OK) return result;
    }
    ++gCurlUsers;
    return CURLE_OK;
}

extern "C" void psvitaalive_curl_global_cleanup(void) {
    std::lock_guard<std::mutex> lock(gCurlLifecycleMutex);
    if (gCurlUsers == 0) return;
    --gCurlUsers;
    if (gCurlUsers == 0) curl_global_cleanup();
}

#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace psvitaalive {

enum class HttpResult {
    Ok = 0,
    NotInitialized,
    NetworkError,
    HttpError,
    SslError,
    IoError,
    Cancelled,
    InvalidArgument
};

struct HttpProgress {
    uint64_t downloaded = 0;
    uint64_t total = 0;
    uint64_t absoluteDownloaded = 0;
    uint64_t bytesPerSecond = 0;
};

using HttpProgressFn = std::function<void(const HttpProgress&)>;
using HttpCancelFn = std::function<bool()>;

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    HttpResult init();
    void shutdown();
    bool isInitialized() const { return initialized_; }

    HttpResult downloadToFile(
        const std::string& url,
        const std::string& destinationPath,
        uint64_t resumeOffset = 0,
        HttpProgressFn onProgress = nullptr,
        HttpCancelFn shouldCancel = nullptr,
        int maxAttemptsOverride = 0, // 0 = default (archive 10 / other 5); images can pass 4
        const std::string& ifRangeValidator = {} // persisted ETag or Last-Modified for safe resume
    );

    /**
     * GET body into memory (capped). Used for MediaFire HTML resolve, etc.
     * Not for large file payloads.
     */
    HttpResult fetchToString(
        const std::string& url,
        std::string& outBody,
        size_t maxBytes = 512 * 1024
    );

    // Performs a lightweight HEAD request and returns the validators exposed by
    // the remote server. These are used by CatalogManager to avoid downloading
    // an unchanged catalog body.
    HttpResult fetchRemoteValidators(
        const std::string& url,
        std::string& etag,
        std::string& lastModified
    );

    /**
     * POST a JSON body (Content-Type: application/json).
     * Used for Discord error reports. Does not affect file downloads.
     * Treats HTTP 2xx (including 204) as success.
     */
    HttpResult postJson(
        const std::string& url,
        const std::string& jsonBody,
        size_t maxResponseBytes = 4096
    );

    int lastStatusCode() const { return lastStatus_; }
    const std::string& lastError() const { return lastError_; }
    bool lastRangeAccepted() const { return lastRangeAccepted_; }
    const std::string& lastEtag() const { return lastEtag_; }
    const std::string& lastModified() const { return lastModified_; }

private:
    bool initialized_ = false;
    int lastStatus_ = 0;
    bool lastRangeAccepted_ = false;
    std::string lastError_;
    std::string lastEtag_;
    std::string lastModified_;

    int tplHttp_ = -1;
    int tplSsl_ = -1;

    void setError(const std::string& msg);
};

const char* toString(HttpResult r);

} // namespace psvitaalive

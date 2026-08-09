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
    uint64_t downloaded = 0; // bytes written in this session (not including resume offset)
    uint64_t total = 0;      // full content length if known (may include offset context)
    uint64_t absoluteDownloaded = 0; // offset + session downloaded
};

using HttpProgressFn = std::function<void(const HttpProgress&)>;
using HttpCancelFn = std::function<bool()>; // return true to cancel

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    HttpResult init();
    void shutdown();
    bool isInitialized() const { return initialized_; }

    /**
     * Download URL to file.
     * @param resumeOffset if > 0, sends Range: bytes=offset- and appends to file
     */
    HttpResult downloadToFile(
        const std::string& url,
        const std::string& destinationPath,
        uint64_t resumeOffset = 0,
        HttpProgressFn onProgress = nullptr,
        HttpCancelFn shouldCancel = nullptr
    );

    int lastStatusCode() const { return lastStatus_; }
    const std::string& lastError() const { return lastError_; }
    bool lastRangeAccepted() const { return lastRangeAccepted_; }

private:
    bool initialized_ = false;
    int lastStatus_ = 0;
    bool lastRangeAccepted_ = false;
    std::string lastError_;

    int tplHttp_ = -1;
    int tplSsl_ = -1;

    void setError(const std::string& msg);
};

const char* toString(HttpResult r);

} // namespace psvitaalive

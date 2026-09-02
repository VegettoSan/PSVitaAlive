#include "network/http_client.hpp"

#include <curl/curl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <cstdio>
#include <cstring>
#include <strings.h>
#include <utility>
#include <atomic>

namespace psvitaalive {

namespace {
constexpr size_t DOWNLOAD_BUFFER_SIZE = 512 * 1024;
constexpr long CONNECT_TIMEOUT_SECONDS = 25;
// archive.org: fail-fast on early attempts so users are not stuck minutes on a dead edge;
// later attempts get a bit more patience without the old 90s hang.
constexpr long CONNECT_TIMEOUT_ARCHIVE_FAST = 18;
constexpr long CONNECT_TIMEOUT_ARCHIVE_SLOW = 40;
constexpr long LOW_SPEED_LIMIT = 1;
constexpr long LOW_SPEED_TIME_SECONDS = 120;
constexpr long LOW_SPEED_TIME_ARCHIVE_SECONDS = 150;

// Catalog etag checks are HEAD-only — never wait minutes on a stuck GitHub edge.
constexpr long VALIDATOR_CONNECT_TIMEOUT_SECONDS = 12;
constexpr long VALIDATOR_TOTAL_TIMEOUT_SECONDS = 20;
constexpr const char* DIAG_LOG = "ux0:data/psvitaalive/logs/session.log";
// Primary UA identifies the app (IA bot guidelines). CDN fallback is a mainstream browser UA.
constexpr const char* UA_APP =
    "PSVitaAlive/1.14 (PlayStation Vita; +https://github.com/VegettoSan/PSVitaAlive)";
constexpr const char* UA_CDN_FALLBACK =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";


// libcurl global state belongs to the whole process, not to individual HttpClient objects.
// Multiple PSVitaAlive workers can own HttpClient instances at the same time.
std::atomic<int> gCurlGlobalState{0}; // 0=not initialized, 1=initializing, 2=ready

void httpDiagnostic(const char* message) {
    if (!message) return;
    sceIoMkdir("ux0:data/psvitaalive", 0777);
    sceIoMkdir("ux0:data/psvitaalive/logs", 0777);
    SceUID fd = sceIoOpen(DIAG_LOG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd < 0) return;
    char line[1200];
    const uint64_t ms = sceKernelGetProcessTimeWide() / 1000ULL;
    sceClibSnprintf(line, sizeof(line), "[%llu ms] HTTP %s\n", (unsigned long long)ms, message);
    sceIoWrite(fd, line, std::strlen(line));
    sceIoClose(fd);
}

struct TransferContext {
    CURL* curl = nullptr;
    SceUID fd = -1;
    uint64_t resumeOffset = 0;
    uint64_t downloaded = 0;
    uint64_t total = 0;
    uint64_t lastProgressTick = 0;
    uint64_t lastProgressBytes = 0;
    uint64_t bytesPerSecond = 0;
    bool firstWrite = true;
    bool cancelled = false;
    bool ioError = false;
    bool restartedFromZero = false;
    bool totalFromContentRange = false;
    int retryAfterSeconds = 0; // from Retry-After header (429/503)
    std::string etag;
    std::string lastModified;
    HttpProgressFn onProgress;
    HttpCancelFn shouldCancel;
    std::string path;
};

static const char* findHeaderIgnoreCase(const char* buffer, const char* header) {
    if (!buffer || !header) return nullptr;
    const size_t headerLength = std::strlen(header);
    if (headerLength == 0) return buffer;
    for (const char* p = buffer; *p != '\0'; ++p) {
        size_t i = 0;
        while (i < headerLength && p[i] != '\0') {
            char a = p[i];
            char b = header[i];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            if (a != b) break;
            ++i;
        }
        if (i == headerLength) return p;
    }
    return nullptr;
}

static std::string headerValue(const char* line, const char* header) {
    const char* p = findHeaderIgnoreCase(line, header);
    if (!p) return {};
    p += std::strlen(header);
    while (*p == ' ' || *p == '\t') ++p;
    std::string value(p);
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) value.pop_back();
    return value;
}

static void updateSpeed(TransferContext* ctx) {
    if (!ctx) return;
    const uint64_t now = sceKernelGetProcessTimeWide();
    if (ctx->lastProgressTick == 0) {
        ctx->lastProgressTick = now;
        ctx->lastProgressBytes = ctx->downloaded;
        ctx->bytesPerSecond = 0;
        return;
    }
    const uint64_t elapsedUs = now - ctx->lastProgressTick;
    if (elapsedUs >= 250000) {
        const uint64_t delta = ctx->downloaded >= ctx->lastProgressBytes ? ctx->downloaded - ctx->lastProgressBytes : 0;
        ctx->bytesPerSecond = elapsedUs > 0 ? (delta * 1000000ULL) / elapsedUs : 0;
        ctx->lastProgressTick = now;
        ctx->lastProgressBytes = ctx->downloaded;
    }
}

static bool parseContentRangeTotal(const std::string& value, uint64_t& startOut, uint64_t& endOut, uint64_t& totalOut) {
    unsigned long long start = 0;
    unsigned long long end = 0;
    unsigned long long total = 0;
    if (std::sscanf(value.c_str(), "bytes %llu-%llu/%llu", &start, &end, &total) != 3)
        return false;
    if (end < start || total == 0 || end >= total)
        return false;
    startOut = static_cast<uint64_t>(start);
    endOut = static_cast<uint64_t>(end);
    totalOut = static_cast<uint64_t>(total);
    return true;
}

static size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    TransferContext* ctx = static_cast<TransferContext*>(userdata);
    const size_t bytes = size * nitems;
    if (!ctx || bytes == 0) return bytes;

    const char* contentLength = findHeaderIgnoreCase(buffer, "Content-Length:");
    if (contentLength) {
        unsigned long long value = 0;
        if (std::sscanf(contentLength, "Content-Length: %llu", &value) == 1) ctx->total = static_cast<uint64_t>(value);
    }
    const std::string contentRange = headerValue(buffer, "Content-Range:");
    if (!contentRange.empty()) {
        uint64_t rangeStart = 0;
        uint64_t rangeEnd = 0;
        uint64_t rangeTotal = 0;
        if (parseContentRangeTotal(contentRange, rangeStart, rangeEnd, rangeTotal)) {
            ctx->total = rangeTotal;
            ctx->totalFromContentRange = true;
            char rangeMsg[220];
            sceClibSnprintf(rangeMsg, sizeof(rangeMsg),
                "content-range start=%llu end=%llu total=%llu",
                (unsigned long long)rangeStart,
                (unsigned long long)rangeEnd,
                (unsigned long long)rangeTotal);
            httpDiagnostic(rangeMsg);
        } else {
            // HTTP 416 commonly uses: Content-Range: bytes */TOTAL
            unsigned long long rangeTotal416 = 0;
            if (std::sscanf(contentRange.c_str(), "bytes */%llu", &rangeTotal416) == 1 &&
                rangeTotal416 > 0) {
                ctx->total = static_cast<uint64_t>(rangeTotal416);
                ctx->totalFromContentRange = true;
                char rangeMsg[180];
                sceClibSnprintf(rangeMsg, sizeof(rangeMsg),
                    "content-range unsatisfied total=%llu",
                    (unsigned long long)rangeTotal416);
                httpDiagnostic(rangeMsg);
            }
        }
    }
    const std::string etag = headerValue(buffer, "ETag:");
    if (!etag.empty()) ctx->etag = etag;
    const std::string modified = headerValue(buffer, "Last-Modified:");
    if (!modified.empty()) ctx->lastModified = modified;
    // RFC 7231 Retry-After: delta-seconds (HTTP-date rarely used by IA; ignore date form).
    const std::string retryAfter = headerValue(buffer, "Retry-After:");
    if (!retryAfter.empty()) {
        int sec = 0;
        if (std::sscanf(retryAfter.c_str(), "%d", &sec) == 1 && sec > 0) {
            if (sec > 30) sec = 30; // hard cap so a bad header cannot freeze the UI for minutes
            ctx->retryAfterSeconds = sec;
        }
    }
    return bytes;
}

static size_t writeCallback(void* ptr, size_t size, size_t nmemb, void* userdata) {
    TransferContext* ctx = static_cast<TransferContext*>(userdata);
    const size_t bytes = size * nmemb;
    if (!ctx || bytes == 0) return bytes;

    if (ctx->shouldCancel && ctx->shouldCancel()) {
        ctx->cancelled = true;
        return 0;
    }

    if (ctx->firstWrite) {
        ctx->firstWrite = false;
        long responseCode = 0;
        curl_easy_getinfo(ctx->curl, CURLINFO_RESPONSE_CODE, &responseCode);
        char first[180];
        sceClibSnprintf(first, sizeof(first), "first-write status=%ld resume=%llu total=%llu", responseCode, (unsigned long long)ctx->resumeOffset, (unsigned long long)ctx->total);
        httpDiagnostic(first);
        if (ctx->resumeOffset > 0 && responseCode == 200) {
            sceIoClose(ctx->fd);
            ctx->fd = sceIoOpen(ctx->path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
            if (ctx->fd < 0) {
                ctx->ioError = true;
                return 0;
            }
            ctx->resumeOffset = 0;
            ctx->downloaded = 0;
            ctx->restartedFromZero = true;
            httpDiagnostic("server ignored Range; restarted download from zero");
        }
    }

    size_t written = 0;
    while (written < bytes) {
        const int result = sceIoWrite(ctx->fd, static_cast<const char*>(ptr) + written, static_cast<unsigned int>(bytes - written));
        if (result <= 0) {
            ctx->ioError = true;
            return 0;
        }
        written += static_cast<size_t>(result);
    }

    ctx->downloaded += static_cast<uint64_t>(bytes);
    updateSpeed(ctx);
    if (ctx->onProgress) {
        HttpProgress progress;
        progress.downloaded = ctx->downloaded;
        progress.absoluteDownloaded = ctx->resumeOffset + ctx->downloaded;
        progress.total = ctx->total;
        progress.bytesPerSecond = ctx->bytesPerSecond;
        if (progress.total > 0 &&
            ctx->resumeOffset > 0 &&
            !ctx->restartedFromZero &&
            !ctx->totalFromContentRange) {
            progress.total += ctx->resumeOffset;
        }
        ctx->onProgress(progress);
    }
    return bytes;
}

} // namespace

const char* toString(HttpResult r) {
    switch (r) {
        case HttpResult::Ok: return "Ok";
        case HttpResult::NotInitialized: return "NotInitialized";
        case HttpResult::NetworkError: return "NetworkError";
        case HttpResult::HttpError: return "HttpError";
        case HttpResult::SslError: return "SslError";
        case HttpResult::IoError: return "IoError";
        case HttpResult::Cancelled: return "Cancelled";
        case HttpResult::InvalidArgument: return "InvalidArgument";
        default: return "Unknown";
    }
}

HttpClient::HttpClient() = default;
HttpClient::~HttpClient() { shutdown(); }

void HttpClient::setError(const std::string& msg) {
    lastError_ = msg;
    sceClibPrintf("[HttpClient] %s\n", msg.c_str());
    httpDiagnostic((std::string("ERROR ") + msg).c_str());
}

HttpResult HttpClient::init() {
    if (initialized_) return HttpResult::Ok;
    lastError_.clear();
    lastStatus_ = 0;
    lastRangeAccepted_ = false;

    int state = gCurlGlobalState.load(std::memory_order_acquire);
    if (state != 2) {
        int expected = 0;
        if (gCurlGlobalState.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
            const CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
            if (result != CURLE_OK) {
                gCurlGlobalState.store(0, std::memory_order_release);
                setError(curl_easy_strerror(result));
                return HttpResult::NetworkError;
            }
            gCurlGlobalState.store(2, std::memory_order_release);
            httpDiagnostic("libcurl global init");
        } else {
            // Another thread is initializing libcurl. Wait briefly without touching curl.
            int spins = 0;
            while (gCurlGlobalState.load(std::memory_order_acquire) == 1 && spins++ < 500) {
                sceKernelDelayThread(1000);
            }
            if (gCurlGlobalState.load(std::memory_order_acquire) != 2) {
                setError("libcurl global initialization did not complete");
                return HttpResult::NetworkError;
            }
        }
    }

    initialized_ = true;
    sceClibPrintf("[HttpClient] libcurl initialized (process-global)\n");
    httpDiagnostic("libcurl initialized (process-global)");
    return HttpResult::Ok;
}

void HttpClient::shutdown() {
    if (!initialized_) return;
    // IMPORTANT: never call curl_global_cleanup() here. Other HttpClient instances
    // may still be active on ImageCache/CatalogManager/other worker threads.
    // libcurl global state remains alive until the Vita process terminates.
    initialized_ = false;
    sceClibPrintf("[HttpClient] libcurl shutdown (global state kept alive)\n");
    httpDiagnostic("libcurl shutdown (global state kept alive)");
}


namespace {
struct StringWriteCtx {
    std::string* body = nullptr;
    size_t maxBytes = 0;
    bool overflow = false;
};

size_t stringWriteCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<StringWriteCtx*>(userdata);
    if (!ctx || !ctx->body) return 0;
    const size_t n = size * nmemb;
    if (ctx->body->size() + n > ctx->maxBytes) {
        ctx->overflow = true;
        return 0; // abort
    }
    ctx->body->append(ptr, n);
    return n;
}
} // namespace

HttpResult HttpClient::fetchToString(
    const std::string& url,
    std::string& outBody,
    size_t maxBytes
) {
    lastStatus_ = 0;
    lastError_.clear();
    outBody.clear();
    if (!initialized_) {
        setError("not initialized");
        return HttpResult::NotInitialized;
    }
    if (url.empty()) {
        setError("empty url");
        return HttpResult::InvalidArgument;
    }
    if (maxBytes < 1024) maxBytes = 1024;

    CURL* curl = curl_easy_init();
    if (!curl) {
        setError("curl_easy_init failed");
        return HttpResult::NetworkError;
    }

    StringWriteCtx ctx;
    ctx.body = &outBody;
    ctx.maxBytes = maxBytes;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
    // Keep existing browser UA (not changed). GitHub requires a non-empty User-Agent.
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stringWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

    // GitHub REST recommends Accept: application/vnd.github+json for API hosts.
    // Do not alter User-Agent here. Extra headers only for api.github.com.
    struct curl_slist* headers = nullptr;
    if (url.find("api.github.com") != std::string::npos) {
        headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    const CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    lastStatus_ = static_cast<int>(status);
    if (headers) {
        curl_slist_free_all(headers);
        headers = nullptr;
    }
    curl_easy_cleanup(curl);

    if (ctx.overflow) {
        setError("response larger than maxBytes");
        outBody.clear();
        return HttpResult::IoError;
    }
    if (rc != CURLE_OK) {
        setError(std::string("curl: ") + curl_easy_strerror(rc));
        outBody.clear();
        if (rc == CURLE_SSL_CONNECT_ERROR || rc == CURLE_SSL_CERTPROBLEM)
            return HttpResult::SslError;
        return HttpResult::NetworkError;
    }
    if (status < 200 || status >= 400) {
        char m[96];
        sceClibSnprintf(m, sizeof(m), "HTTP %ld", status);
        setError(m);
        outBody.clear();
        return HttpResult::HttpError;
    }
    return HttpResult::Ok;
}

HttpResult HttpClient::postJson(
    const std::string& url,
    const std::string& jsonBody,
    size_t maxResponseBytes
) {
    lastStatus_ = 0;
    lastError_.clear();
    if (!initialized_) {
        setError("not initialized");
        return HttpResult::NotInitialized;
    }
    if (url.empty() || jsonBody.empty()) {
        setError("empty url or body");
        return HttpResult::InvalidArgument;
    }
    if (maxResponseBytes < 256) maxResponseBytes = 256;

    CURL* curl = curl_easy_init();
    if (!curl) {
        setError("curl_easy_init failed");
        return HttpResult::NetworkError;
    }

    std::string response;
    StringWriteCtx ctx;
    ctx.body = &response;
    ctx.maxBytes = maxResponseBytes;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(jsonBody.size()));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "PSVitaAlive/ErrorReporter");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stringWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    const CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    lastStatus_ = static_cast<int>(status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        setError(std::string("curl: ") + curl_easy_strerror(rc));
        if (rc == CURLE_SSL_CONNECT_ERROR || rc == CURLE_SSL_CERTPROBLEM)
            return HttpResult::SslError;
        return HttpResult::NetworkError;
    }
    // Discord webhooks often return 204 No Content
    if (status >= 200 && status < 300) {
        return HttpResult::Ok;
    }
    char m[96];
    sceClibSnprintf(m, sizeof(m), "HTTP %ld", status);
    setError(m);
    return HttpResult::HttpError;
}

HttpResult HttpClient::fetchRemoteValidators(const std::string& url, std::string& etag, std::string& lastModified) {
    etag.clear();
    lastModified.clear();
    lastStatus_ = 0;
    lastError_.clear();
    if (!initialized_) {
        setError("not initialized");
        return HttpResult::NotInitialized;
    }
    if (url.empty()) {
        setError("empty url");
        return HttpResult::InvalidArgument;
    }
    CURL* curl = curl_easy_init();
    if (!curl) {
        setError("curl_easy_init failed");
        return HttpResult::NetworkError;
    }
    TransferContext ctx;
    ctx.curl = curl;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, UA_APP);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    // DEFAULT negotiates best TLS; forcing 1.2 alone fails on some hosts (Vita3K/GitHub).
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_DEFAULT);
#if defined(CURLSSLOPT_NO_REVOKE)
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NO_REVOKE);
#endif
    curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    // Hard ceiling: catalog checks must not hang the splash on PS1/PSP/Vita Games.
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, VALIDATOR_CONNECT_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, VALIDATOR_TOTAL_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 0L); // disable low-speed abort for tiny HEAD
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ctx);
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: */*");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    const CURLcode result = curl_easy_perform(curl);
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    lastStatus_ = static_cast<int>(responseCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    etag = ctx.etag;
    lastModified = ctx.lastModified;
    if (result != CURLE_OK) {
        char message[180];
        sceClibSnprintf(message, sizeof(message), "curl validator error %d: %s", static_cast<int>(result), curl_easy_strerror(result));
        setError(message);
        return (result == CURLE_SSL_CONNECT_ERROR || result == CURLE_PEER_FAILED_VERIFICATION) ? HttpResult::SslError : HttpResult::NetworkError;
    }
    if (responseCode < 200 || responseCode >= 400) {
        char message[96];
        sceClibSnprintf(message, sizeof(message), "validator HTTP status %ld", responseCode);
        setError(message);
        return HttpResult::HttpError;
    }
    char message[320];
    sceClibSnprintf(message, sizeof(message), "VALIDATORS status=%ld etag=%s modified=%s", responseCode, etag.empty() ? "-" : etag.c_str(), lastModified.empty() ? "-" : lastModified.c_str());
    httpDiagnostic(message);
    return HttpResult::Ok;
}

HttpResult HttpClient::downloadToFile(
    const std::string& url,
    const std::string& destinationPath,
    uint64_t resumeOffset,
    HttpProgressFn onProgress,
    HttpCancelFn shouldCancel,
    int maxAttemptsOverride
) {
    lastStatus_ = 0;
    lastRangeAccepted_ = false;
    lastError_.clear();
    if (!initialized_) {
        setError("not initialized");
        return HttpResult::NotInitialized;
    }
    if (url.empty() || destinationPath.empty()) {
        setError("empty url or path");
        return HttpResult::InvalidArgument;
    }
    const bool isHttps = url.rfind("https://", 0) == 0;
    const bool isHttp = url.rfind("http://", 0) == 0;
    if (!isHttps && !isHttp) {
        setError("url must start with http:// or https://");
        return HttpResult::InvalidArgument;
    }

    char begin[900];
    sceClibSnprintf(begin, sizeof(begin), "BEGIN url=%s destination=%s resume=%llu", url.c_str(), destinationPath.c_str(), (unsigned long long)resumeOffset);
    httpDiagnostic(begin);

    CURL* curl = curl_easy_init();
    if (!curl) {
        setError("curl_easy_init failed");
        return HttpResult::NetworkError;
    }

    TransferContext ctx;
    ctx.curl = curl;
    ctx.resumeOffset = resumeOffset;
    ctx.onProgress = std::move(onProgress);
    ctx.shouldCancel = std::move(shouldCancel);
    ctx.path = destinationPath;

    int flags = SCE_O_WRONLY | SCE_O_CREAT;
    flags |= (resumeOffset > 0) ? SCE_O_APPEND : SCE_O_TRUNC;
    ctx.fd = sceIoOpen(destinationPath.c_str(), flags, 0777);
    if (ctx.fd < 0) {
        curl_easy_cleanup(curl);
        setError("cannot open destination file");
        return HttpResult::IoError;
    }

    // Keep libcurl's detailed per-transfer error text for diagnostics and final errors.
    char curlError[CURL_ERROR_SIZE];
    std::memset(curlError, 0, sizeof(curlError));
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlError);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    // Browser-like UA helps some CDNs (GitLab package registry, etc.)
    curl_easy_setopt(curl, CURLOPT_USERAGENT, UA_APP);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_DEFAULT);
#if defined(CURLSSLOPT_NO_REVOKE)
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NO_REVOKE);
#endif
    curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    // Prefer IPv4 — dual-stack SSL handshakes often fail on Vita/Vita3K.
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    // Do not set CURLOPT_SSL_CIPHER_LIST: Vita libcurl often returns CURLE_SSL_CIPHER (59)
    // for OpenSSL "SECLEVEL" syntax. Leave cipher negotiation to the TLS backend.
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, LOW_SPEED_LIMIT);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, LOW_SPEED_TIME_SECONDS);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, static_cast<long>(DOWNLOAD_BUFFER_SIZE));

    if (resumeOffset > 0) {
        // CURLOPT_RESUME_FROM takes a long (32-bit on Vita) and breaks past ~2GB.
        // Prefer the 64-bit LARGE variant for multi-GB downloads (Game Files, etc.).
#if defined(CURLOPT_RESUME_FROM_LARGE)
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(resumeOffset));
#else
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM, static_cast<long>(resumeOffset > 0x7FFFFFFFULL ? 0x7FFFFFFFULL : resumeOffset));
#endif
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ctx);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: */*");
    headers = curl_slist_append(headers, "Accept-Encoding: identity");
    headers = curl_slist_append(headers, "Connection: keep-alive");
    // Mildly improves first-byte reliability on some Internet Archive edges.
    if (url.find("archive.org") != std::string::npos) {
        headers = curl_slist_append(headers, "Referer: https://archive.org/");
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 12L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode result = CURLE_OK;
    const bool isGitlab = url.find("gitlab.com") != std::string::npos
        || url.find("gitlab.io") != std::string::npos;
    const bool isArchive = url.find("archive.org") != std::string::npos;
    const bool isGithub = url.find("github.com") != std::string::npos
        || url.find("githubusercontent.com") != std::string::npos;

    // archive.org: mid-transfer stalls still need patience; connect uses fail-fast below.
    if (isArchive) {
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, LOW_SPEED_TIME_ARCHIVE_SECONDS);
    }

    // TLS 1.2 first (archive.org + most CDNs). Avoid burning early attempts on TLS 1.1.
    const long sslAttempts[] = {
        CURL_SSLVERSION_TLSv1_2,
        CURL_SSLVERSION_DEFAULT,
        CURL_SSLVERSION_TLSv1_2,
        CURL_SSLVERSION_DEFAULT,
    };
    // Keep retries, but make early attempts cheap (fail-fast) so start is seconds not minutes.
    const int kMaxAttempts = (maxAttemptsOverride > 0)
        ? maxAttemptsOverride
        : (isArchive ? 10 : 5);
    long responseCode = 0;
    CURLcode lastFail = CURLE_OK;
    // One-shot guard: if a Range request fails (curl 33), truncate and retry as full GET.
    bool rangeFallbackUsed = false;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        const int sslIdx = attempt < 4 ? attempt : (attempt % 4);
        curl_easy_setopt(curl, CURLOPT_SSLVERSION, sslAttempts[sslIdx]);
        // Fail-fast connect: short on first tries, slightly longer later.
        if (isArchive) {
            const long ct = (attempt < 3) ? CONNECT_TIMEOUT_ARCHIVE_FAST : CONNECT_TIMEOUT_ARCHIVE_SLOW;
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, ct);
        } else if (attempt > 0) {
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_SECONDS);
        }
        // Only force a brand-new TCP/TLS after serious failures (not every retry).
        const bool seriousFail =
            lastFail == CURLE_SSL_CONNECT_ERROR ||
            lastFail == CURLE_PEER_FAILED_VERIFICATION ||
            lastFail == CURLE_SSL_CERTPROBLEM ||
            lastFail == CURLE_COULDNT_CONNECT ||
            lastFail == CURLE_COULDNT_RESOLVE_HOST ||
            lastFail == CURLE_OPERATION_TIMEDOUT ||
            lastFail == CURLE_RECV_ERROR ||
            lastFail == CURLE_GOT_NOTHING ||
            lastFail == CURLE_SEND_ERROR;
        const bool needFresh = (attempt > 0 && seriousFail);
        curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, needFresh ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, needFresh ? 1L : 0L);
        // Drop TLS session reuse after SSL failures (stale sessions can stick on Vita).
        if (attempt > 0 && (lastFail == CURLE_SSL_CONNECT_ERROR ||
                            lastFail == CURLE_PEER_FAILED_VERIFICATION ||
                            lastFail == CURLE_SSL_CERTPROBLEM)) {
            curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 0L);
        }
        // UA: app identity first; after connect/SSL trouble try CDN-friendly browser UA.
        if (attempt > 0 && seriousFail) {
            curl_easy_setopt(curl, CURLOPT_USERAGENT, UA_CDN_FALLBACK);
        } else {
            curl_easy_setopt(curl, CURLOPT_USERAGENT, UA_APP);
        }
        if (attempt > 0) {
            char retryMsg[160];
            sceClibSnprintf(retryMsg, sizeof(retryMsg),
                "download retry %d/%d hostHints=gitlab:%d archive:%d github:%d lastCurl=%d fresh=%d",
                attempt + 1, kMaxAttempts, isGitlab ? 1 : 0, isArchive ? 1 : 0, isGithub ? 1 : 0,
                static_cast<int>(lastFail), needFresh ? 1 : 0);
            httpDiagnostic(retryMsg);
            // Short early backoff; grow only after several failures.
            int delayMs = 250 * (attempt <= 3 ? attempt : 3);
            if (isArchive) delayMs = 350 * (attempt <= 4 ? attempt : 4);
            if (lastFail == CURLE_SSL_CONNECT_ERROR ||
                lastFail == CURLE_PEER_FAILED_VERIFICATION ||
                lastFail == CURLE_SSL_CERTPROBLEM) {
                const int sslExtra = isArchive ? (600 * (attempt <= 5 ? attempt : 5)) : (500 * attempt);
                if (sslExtra > delayMs) delayMs = sslExtra;
                if (delayMs > 4000) delayMs = 4000;
            }
            // Honor Retry-After from previous 429/503 when present.
            if (ctx.retryAfterSeconds > 0) {
                const int raMs = ctx.retryAfterSeconds * 1000;
                if (raMs > delayMs) delayMs = raMs;
                ctx.retryAfterSeconds = 0;
            }
            if (delayMs < 200) delayMs = 200;
            if (delayMs > 30000) delayMs = 30000;
            sceKernelDelayThread(delayMs * 1000);
            ctx.cancelled = false;
            // CRITICAL: after a mid-transfer drop (e.g. curl 56), the FD is at EOF and
            // CURLOPT_RESUME_FROM is still 0. A plain re-perform would GET from byte 0
            // while writing at EOF → file grows past Content-Length → size-limit abort.
            // Resume from the absolute bytes already on disk instead.
            if (ctx.downloaded > 0) {
                const uint64_t absPos = ctx.resumeOffset + ctx.downloaded;
                ctx.resumeOffset = absPos;
                ctx.downloaded = 0;
                ctx.lastProgressBytes = 0;
                ctx.firstWrite = true;
                ctx.restartedFromZero = false;
#if defined(CURLOPT_RESUME_FROM_LARGE)
                curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(absPos));
#else
                curl_easy_setopt(curl, CURLOPT_RESUME_FROM,
                    static_cast<long>(absPos > 0x7FFFFFFFULL ? 0x7FFFFFFFULL : absPos));
#endif
                if (ctx.fd >= 0)
                    sceIoLseek(ctx.fd, 0, SCE_SEEK_END);
                char resumeMsg[140];
                sceClibSnprintf(resumeMsg, sizeof(resumeMsg),
                    "retry resume from absolute=%llu", (unsigned long long)absPos);
                httpDiagnostic(resumeMsg);
            } else if (resumeOffset == 0 && ctx.fd >= 0) {
                sceIoLseek(ctx.fd, 0, SCE_SEEK_SET);
            }
        }

        // Response headers belong to this attempt. Clear range-derived state so a
        // previous retry/redirect cannot leak an old total into the next response.
        ctx.total = 0;
        ctx.totalFromContentRange = false;
        curlError[0] = '\0';
        result = curl_easy_perform(curl);
        responseCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

        if (ctx.cancelled) break;

        // A 416 can mean the partial file is already exactly complete. HTTP servers
        // commonly report the resource size as "Content-Range: bytes */TOTAL".
        if (result == CURLE_OK &&
            responseCode == 416 &&
            ctx.resumeOffset > 0 &&
            ctx.downloaded == 0 &&
            ctx.totalFromContentRange &&
            ctx.resumeOffset == ctx.total) {
            httpDiagnostic("HTTP 416 but local partial matches remote total; treating as complete");
            break;
        }

        // An invalid/stale resume offset should not permanently fail the job. Restart
        // once from zero, just like the curl 33 Range fallback.
        if (result == CURLE_OK &&
            responseCode == 416 &&
            ctx.resumeOffset > 0 &&
            ctx.totalFromContentRange &&
            !rangeFallbackUsed) {
            rangeFallbackUsed = true;
            char rangeMsg[220];
            sceClibSnprintf(rangeMsg, sizeof(rangeMsg),
                "HTTP 416 range fallback offset=%llu remote_total=%llu (full GET next)",
                (unsigned long long)ctx.resumeOffset,
                (unsigned long long)ctx.total);
            httpDiagnostic(rangeMsg);

            if (ctx.fd >= 0) {
                sceIoClose(ctx.fd);
                ctx.fd = -1;
            }
            ctx.fd = sceIoOpen(
                destinationPath.c_str(),
                SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
                0777);
            if (ctx.fd < 0) {
                ctx.ioError = true;
                break;
            }

            ctx.resumeOffset = 0;
            ctx.downloaded = 0;
            ctx.total = 0;
            ctx.totalFromContentRange = false;
            ctx.lastProgressTick = 0;
            ctx.lastProgressBytes = 0;
            ctx.bytesPerSecond = 0;
            ctx.firstWrite = true;
            ctx.restartedFromZero = true;
#if defined(CURLOPT_RESUME_FROM_LARGE)
            curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(0));
#else
            curl_easy_setopt(curl, CURLOPT_RESUME_FROM, 0L);
#endif
            continue;
        }

        if (result == CURLE_OK) {
            const bool transientHttp =
                responseCode == 429 || responseCode == 502 || responseCode == 503 ||
                responseCode == 504 || responseCode == 520 || responseCode == 522 ||
                responseCode == 524;
            if (!transientHttp) break;
            char httpRetry[120];
            sceClibSnprintf(httpRetry, sizeof(httpRetry),
                "attempt %d HTTP %ld transient — will retry (Retry-After=%d)",
                attempt + 1, responseCode, ctx.retryAfterSeconds);
            httpDiagnostic(httpRetry);
            lastFail = CURLE_HTTP_RETURNED_ERROR;
            if (resumeOffset == 0 && ctx.fd >= 0 && ctx.downloaded > 0) {
                sceIoClose(ctx.fd);
                ctx.fd = sceIoOpen(destinationPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
                ctx.downloaded = 0;
            }
            continue;
        }

        // CDN/host rejected HTTP Range (common on MediaFire and some mirrors).
        // Truncate the partial, clear CURLOPT resume, and retry once as a full GET.
        // Use ctx.resumeOffset (not the original parameter) so internal mid-transfer
        // retries that promoted an absolute offset are also covered.
        if (result == CURLE_RANGE_ERROR &&
            ctx.resumeOffset > 0 &&
            !rangeFallbackUsed) {
            rangeFallbackUsed = true;
            char rangeMsg[180];
            sceClibSnprintf(
                rangeMsg, sizeof(rangeMsg),
                "range fallback curl=%d offset=%llu restarted=0 (full GET next)",
                static_cast<int>(result),
                (unsigned long long)ctx.resumeOffset);
            httpDiagnostic(rangeMsg);

            if (ctx.fd >= 0) {
                sceIoClose(ctx.fd);
                ctx.fd = -1;
            }
            ctx.fd = sceIoOpen(
                destinationPath.c_str(),
                SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
                0777);
            if (ctx.fd < 0) {
                ctx.ioError = true;
                break;
            }

            ctx.resumeOffset = 0;
            ctx.downloaded = 0;
            ctx.total = 0;
            ctx.lastProgressTick = 0;
            ctx.lastProgressBytes = 0;
            ctx.bytesPerSecond = 0;
            ctx.firstWrite = true;
            ctx.restartedFromZero = true;

#if defined(CURLOPT_RESUME_FROM_LARGE)
            curl_easy_setopt(
                curl,
                CURLOPT_RESUME_FROM_LARGE,
                static_cast<curl_off_t>(0));
#else
            curl_easy_setopt(curl, CURLOPT_RESUME_FROM, 0L);
#endif
            lastFail = result;
            continue;
        }

        const bool retryable =
            result == CURLE_SSL_CONNECT_ERROR ||
            result == CURLE_PEER_FAILED_VERIFICATION ||
            result == CURLE_SSL_CERTPROBLEM ||
#ifdef CURLE_SSL_CIPHER
            result == CURLE_SSL_CIPHER ||
#endif
            result == CURLE_COULDNT_CONNECT ||
            result == CURLE_COULDNT_RESOLVE_HOST ||
            result == CURLE_OPERATION_TIMEDOUT ||
            result == CURLE_RECV_ERROR ||
            result == CURLE_SEND_ERROR ||
            result == CURLE_GOT_NOTHING ||
            result == CURLE_PARTIAL_FILE ||
            result == CURLE_HTTP_RETURNED_ERROR;
        char failMsg[420];
        sceClibSnprintf(failMsg, sizeof(failMsg),
            "attempt %d failed curl=%d %s retryable=%d detail=%s",
            attempt + 1,
            static_cast<int>(result),
            curl_easy_strerror(result),
            retryable ? 1 : 0,
            curlError[0] ? curlError : "-");
        httpDiagnostic(failMsg);
        lastFail = result;
        if (!retryable) break;
    }
    const char* effectiveUrl = nullptr;
    long redirectCount = 0;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effectiveUrl);
    curl_easy_getinfo(curl, CURLINFO_REDIRECT_COUNT, &redirectCount);

    char effectiveUrlCopy[900];
    effectiveUrlCopy[0] = '\0';
    if (effectiveUrl && *effectiveUrl) {
        sceClibSnprintf(effectiveUrlCopy, sizeof(effectiveUrlCopy), "%s", effectiveUrl);
    }

    lastStatus_ = static_cast<int>(responseCode);
    if (ctx.resumeOffset > 0 && responseCode == 206) lastRangeAccepted_ = true;

    curl_slist_free_all(headers);
    sceIoClose(ctx.fd);
    ctx.fd = -1;
    curl_easy_cleanup(curl);

    char resultMsg[1200];
    sceClibSnprintf(
        resultMsg,
        sizeof(resultMsg),
        "RESULT curl=%d status=%ld bytes=%llu absolute=%llu total=%llu speed=%llu range=%d restarted=%d redirects=%ld effective_url=%s curl_detail=%s",
        static_cast<int>(result),
        responseCode,
        (unsigned long long)ctx.downloaded,
        (unsigned long long)(ctx.resumeOffset + ctx.downloaded),
        (unsigned long long)ctx.total,
        (unsigned long long)ctx.bytesPerSecond,
        lastRangeAccepted_ ? 1 : 0,
        ctx.restartedFromZero ? 1 : 0,
        redirectCount,
        effectiveUrlCopy[0] ? effectiveUrlCopy : "-",
        curlError[0] ? curlError : "-");
    httpDiagnostic(resultMsg);

    if (ctx.cancelled) {
        setError("cancelled");
        return HttpResult::Cancelled;
    }
    if (result == CURLE_ABORTED_BY_CALLBACK) {
        setError("transfer aborted");
        return HttpResult::NetworkError;
    }

    if (ctx.ioError) {
        setError("sceIoWrite failed");
        return HttpResult::IoError;
    }
    if (result != CURLE_OK) {
        char message[420];
        sceClibSnprintf(
            message,
            sizeof(message),
            "curl error %d: %s%s%s",
            static_cast<int>(result),
            curl_easy_strerror(result),
            curlError[0] ? " | " : "",
            curlError[0] ? curlError : "");
        setError(message);
        return (result == CURLE_SSL_CONNECT_ERROR || result == CURLE_PEER_FAILED_VERIFICATION) ? HttpResult::SslError : HttpResult::NetworkError;
    }
    const bool rangeAlreadyComplete =
        responseCode == 416 &&
        ctx.resumeOffset > 0 &&
        ctx.downloaded == 0 &&
        ctx.totalFromContentRange &&
        ctx.resumeOffset == ctx.total;
    if (!rangeAlreadyComplete && responseCode != 200 && responseCode != 206) {
        char message[96];
        sceClibSnprintf(message, sizeof(message), "HTTP status %ld", responseCode);
        setError(message);
        return HttpResult::HttpError;
    }

    if (rangeAlreadyComplete) {
        httpDiagnostic("download already complete according to HTTP 416 Content-Range");
    }

    sceClibPrintf("[HttpClient] done status=%ld downloaded=%llu absolute=%llu range=%d speed=%llu B/s redirects=%ld effective=%s\n",
        responseCode,
        (unsigned long long)ctx.downloaded,
        (unsigned long long)(ctx.resumeOffset + ctx.downloaded),
        lastRangeAccepted_ ? 1 : 0,
        (unsigned long long)ctx.bytesPerSecond,
        redirectCount,
        effectiveUrlCopy[0] ? effectiveUrlCopy : "-");
    return HttpResult::Ok;
}

} // namespace psvitaalive

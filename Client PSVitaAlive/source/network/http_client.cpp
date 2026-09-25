#include "network/http_client.hpp"
#include "diagnostic_logger.hpp"

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
#include <vector>

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

void archiveNodeDiagnostic(const std::string& message) {
    diagnostics::archiveNodeLog(message);
}


// VitaSDK ships OpenSSL 1.0.2-era TLS. Re-assert insecure-verify defaults every
// attempt so a sticky handle never silently re-enables peer verification, and
// clear build-time CA paths that do not exist on device.
static void applyVitaSslDefaults(CURL* curl) {
    if (!curl) return;
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, nullptr);
    curl_easy_setopt(curl, CURLOPT_CAPATH, nullptr);
#if defined(CURLSSLOPT_NO_REVOKE)
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NO_REVOKE);
#endif
}

static char asciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

static bool containsAsciiNoCase(const char* haystack, const char* needle) {
    if (!haystack || !needle || !needle[0]) return false;
    const size_t needleLen = std::strlen(needle);
    for (const char* p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < needleLen && p[i] && asciiLower(p[i]) == asciiLower(needle[i])) ++i;
        if (i == needleLen) return true;
    }
    return false;
}

static bool curlDetailLooksTls(const char* detail) {
    if (!detail || !detail[0]) return false;
    return containsAsciiNoCase(detail, "ssl") ||
           containsAsciiNoCase(detail, "tls") ||
           containsAsciiNoCase(detail, "certificate") ||
           containsAsciiNoCase(detail, "openssl") ||
           containsAsciiNoCase(detail, "x509");
}

static bool hostLooksLikeBadArchiveEdge(const char* url) {
    if (!url || !url[0]) return false;
    // Historical risk marker only: dn*/.ca nodes are no longer hard-filtered.
    // The selector probes them and lets real Vita-side results decide.
    if (containsAsciiNoCase(url, ".ca.archive.org")) return true;
    const char* host = std::strstr(url, "://");
    host = host ? host + 3 : url;
    if (asciiLower(host[0]) == 'd' && asciiLower(host[1]) == 'n' &&
        containsAsciiNoCase(host, "archive.org")) return true;
    return false;
}

static std::string archiveUrlHostLower(const std::string& url) {
    if (url.empty()) return {};
    size_t start = url.find("://");
    start = (start == std::string::npos) ? 0 : start + 3;
    if (start >= url.size()) return {};
    size_t end = start;
    while (end < url.size() && url[end] != '/' && url[end] != ':' &&
           url[end] != '?' && url[end] != '#') {
        ++end;
    }
    if (end <= start) return {};
    std::string host = url.substr(start, end - start);
    for (char& c : host) c = asciiLower(c);
    return host;
}

static bool archiveEffectiveUrlUsable(const std::string& url) {
    const std::string host = archiveUrlHostLower(url);
    if (host == "archive.org") return true;
    static const char* suffix = ".archive.org";
    const size_t suffixLen = std::strlen(suffix);
    return host.size() > suffixLen &&
           host.compare(host.size() - suffixLen, suffixLen, suffix) == 0;
}

static bool parseArchiveDownloadParts(const std::string& url, std::string& identifier, std::string& fileName) {
    identifier.clear();
    fileName.clear();
    const char* marker = "/download/";
    const char* p = std::strstr(url.c_str(), marker);
    if (!p) return false;
    p += std::strlen(marker);
    const char* slash = std::strchr(p, '/');
    if (!slash || slash == p) return false;
    identifier.assign(p, slash - p);
    const char* rest = slash + 1;
    const char* q = std::strchr(rest, '?');
    fileName.assign(rest, q ? (q - rest) : std::strlen(rest));
    // Strip trailing slash noise.
    while (!fileName.empty() && (fileName.back() == '/' || fileName.back() == ' ')) fileName.pop_back();
    return !identifier.empty() && !fileName.empty();
}

static bool extractJsonStringField(const std::string& json, const char* key, std::string& out) {
    out.clear();
    if (!key || !*key) return false;
    std::string pat = std::string("\"") + key + "\"";
    size_t pos = 0;
    while ((pos = json.find(pat, pos)) != std::string::npos) {
        size_t colon = json.find(':', pos + pat.size());
        if (colon == std::string::npos) return false;
        size_t i = colon + 1;
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) ++i;
        if (i >= json.size() || json[i] != '"') {
            pos += pat.size();
            continue;
        }
        ++i;
        std::string value;
        while (i < json.size() && json[i] != '"') {
            if (json[i] == '\\' && i + 1 < json.size()) {
                value.push_back(json[i + 1]);
                i += 2;
                continue;
            }
            value.push_back(json[i]);
            ++i;
        }
        out = value;
        return !out.empty();
    }
    return false;
}

// Lightweight metadata fetch (separate easy handle) to locate alternate IA storage hosts.
static bool fetchArchiveMetadataJson(const std::string& identifier, std::string& jsonOut) {
    jsonOut.clear();
    if (identifier.empty()) return false;
    const std::string metaUrl = "https://archive.org/metadata/" + identifier;
    CURL* m = curl_easy_init();
    if (!m) return false;
    std::string body;
    body.reserve(64 * 1024);
    auto writeMeta = [](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
        std::string* out = static_cast<std::string*>(userdata);
        const size_t n = size * nmemb;
        if (out->size() + n > 512 * 1024) return 0;
        out->append(ptr, n);
        return n;
    };
    char err[CURL_ERROR_SIZE];
    std::memset(err, 0, sizeof(err));
    curl_easy_setopt(m, CURLOPT_URL, metaUrl.c_str());
    curl_easy_setopt(m, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(m, CURLOPT_USERAGENT, UA_APP);
    applyVitaSslDefaults(m);
    curl_easy_setopt(m, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(m, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(m, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(m, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(m, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(m, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(m, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(m, CURLOPT_ERRORBUFFER, err);
    curl_easy_setopt(m, CURLOPT_WRITEFUNCTION, +writeMeta);
    curl_easy_setopt(m, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(m, CURLOPT_NOSIGNAL, 1L);
    archiveNodeDiagnostic(std::string("METADATA_REQUEST id=") + identifier + " url=" + metaUrl);
    const CURLcode rc = curl_easy_perform(m);
    long status = 0;
    curl_easy_getinfo(m, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(m);
    if (rc != CURLE_OK || status != 200 || body.empty()) {
        char msg[320];
        sceClibSnprintf(msg, sizeof(msg), "archive metadata failed id=%s curl=%d status=%ld bytes=%llu",
            identifier.c_str(), static_cast<int>(rc), status, (unsigned long long)body.size());
        httpDiagnostic(msg);
        archiveNodeDiagnostic(std::string("METADATA_RESULT ok=0 ") + msg);
        return false;
    }
    {
        char msg[320];
        sceClibSnprintf(msg, sizeof(msg), "METADATA_RESULT ok=1 id=%s curl=%d HTTP=%ld bytes=%llu",
            identifier.c_str(), static_cast<int>(rc), status, (unsigned long long)body.size());
        archiveNodeDiagnostic(msg);
    }
    jsonOut.swap(body);
    return true;
}

static void pushUniqueHost(std::vector<std::string>& hosts, const std::string& host) {
    if (host.empty()) return;
    for (const auto& h : hosts) if (h == host) return;
    hosts.push_back(host);
}

// Keep dn*/.ca Archive edges as lower-priority risk candidates instead of
// filtering them out. Large payloads benchmark every working candidate, so a
// risky edge can win when it is actually faster from the user's own network.
static bool buildArchiveAlternateUrls(const std::string& originalUrl, std::vector<std::string>& outUrls) {
    outUrls.clear();
    std::string id, file;
    if (!parseArchiveDownloadParts(originalUrl, id, file)) {
        archiveNodeDiagnostic(std::string("CANDIDATE_BUILD parse_failed canonical=") + originalUrl);
        return false;
    }
    archiveNodeDiagnostic("------------------------------------------------------------");
    archiveNodeDiagnostic(std::string("CANDIDATE_BUILD begin identifier=") + id + " file=" + file);
    archiveNodeDiagnostic(std::string("CANONICAL_URL ") + originalUrl);
    std::string meta;
    if (!fetchArchiveMetadataJson(id, meta)) return false;
    std::string server, d1, d2, dir;
    extractJsonStringField(meta, "server", server);
    extractJsonStringField(meta, "d1", d1);
    extractJsonStringField(meta, "d2", d2);
    extractJsonStringField(meta, "dir", dir);
    archiveNodeDiagnostic(std::string("METADATA_FIELDS server=") + (server.empty() ? "-" : server) +
        " d1=" + (d1.empty() ? "-" : d1) +
        " d2=" + (d2.empty() ? "-" : d2) +
        " dir=" + (dir.empty() ? "-" : dir));
    if (!server.empty() && hostLooksLikeBadArchiveEdge(server.c_str())) archiveNodeDiagnostic(std::string("METADATA_HOST_RISKY host=") + server + " reason=dn_or_ca_edge allowed=1");
    if (!d1.empty() && hostLooksLikeBadArchiveEdge(d1.c_str())) archiveNodeDiagnostic(std::string("METADATA_HOST_RISKY host=") + d1 + " reason=dn_or_ca_edge allowed=1");
    if (!d2.empty() && hostLooksLikeBadArchiveEdge(d2.c_str())) archiveNodeDiagnostic(std::string("METADATA_HOST_RISKY host=") + d2 + " reason=dn_or_ca_edge allowed=1");
    if (dir.empty()) {
        // Fallback path used by many IA items when dir is absent from the top-level object.
        dir = std::string("/0/items/") + id;
    }
    if (!dir.empty() && dir[0] != '/') dir.insert(dir.begin(), '/');

    std::vector<std::string> hosts;
    // Prioritize non-dn / non-ca hosts first.
    auto prefer = [&](const std::string& h) {
        if (h.empty()) return;
        if (hostLooksLikeBadArchiveEdge(h.c_str())) return;
        pushUniqueHost(hosts, h);
    };
    auto defer = [&](const std::string& h) {
        if (h.empty()) return;
        pushUniqueHost(hosts, h);
    };
    prefer(server);
    prefer(d1);
    prefer(d2);
    defer(server);
    defer(d1);
    defer(d2);

    for (const auto& host : hosts) {
        // Risky dn*/.ca nodes stay after normal ia* candidates in discovery order,
        // but they are allowed into the probe set and may win a large-file benchmark.
        std::string u = "https://" + host + dir + "/" + file;
        outUrls.push_back(u);
    }
    if (outUrls.empty()) {
        httpDiagnostic("archive failover: no usable alternate hosts in metadata");
        archiveNodeDiagnostic("CANDIDATE_BUILD result=none; canonical Archive URL will remain active");
        return false;
    }
    {
        char countMsg[160];
        sceClibSnprintf(countMsg, sizeof(countMsg), "CANDIDATE_BUILD result=%d usable_direct_nodes", (int)outUrls.size());
        archiveNodeDiagnostic(countMsg);
        for (size_t i = 0; i < outUrls.size(); ++i) {
            char candidateMsg[760];
            sceClibSnprintf(candidateMsg, sizeof(candidateMsg), "CANDIDATE[%u] risk=%s url=%s",
                (unsigned int)i,
                hostLooksLikeBadArchiveEdge(outUrls[i].c_str()) ? "dn_or_ca" : "normal",
                outUrls[i].c_str());
            archiveNodeDiagnostic(candidateMsg);
        }
    }
    char msg[280];
    sceClibSnprintf(msg, sizeof(msg), "archive failover built %d alternate URL(s) for id=%s",
        (int)outUrls.size(), id.c_str());
    httpDiagnostic(msg);
    return true;
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
    // Content-Range from the *current* response only (reset each attempt).
    bool rangeValid = false;
    uint64_t rangeStart = 0;
    uint64_t rangeEnd = 0;
    bool rangeMismatch = false; // 206 with start != requested resume
    int retryAfterSeconds = 0; // from Retry-After header (429/503)
    long responseCode = 0; // current HTTP response, reset on each status line
    bool discardedErrorBody = false;
    std::string etag;
    std::string lastModified;
    HttpProgressFn onProgress;
    HttpCancelFn shouldCancel;
    std::string path;
};

static bool startsWithAsciiNoCase(const char* buffer, size_t bytes, const char* prefix) {
    if (!buffer || !prefix) return false;
    const size_t n = std::strlen(prefix);
    if (bytes < n) return false;
    for (size_t i = 0; i < n; ++i) {
        char a = buffer[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

static std::string headerValue(const char* buffer, size_t bytes, const char* header) {
    if (!buffer || !header || !startsWithAsciiNoCase(buffer, bytes, header)) return {};
    size_t p = std::strlen(header);
    while (p < bytes && (buffer[p] == ' ' || buffer[p] == '\t')) ++p;
    size_t end = bytes;
    while (end > p && (buffer[end - 1] == '\r' || buffer[end - 1] == '\n' ||
                       buffer[end - 1] == ' ' || buffer[end - 1] == '\t')) --end;
    return std::string(buffer + p, end - p);
}

static bool parseHttpStatusLine(const char* buffer, size_t bytes, long& statusOut) {
    statusOut = 0;
    if (!startsWithAsciiNoCase(buffer, bytes, "HTTP/")) return false;
    size_t p = 5;
    while (p < bytes && buffer[p] != ' ') ++p;
    while (p < bytes && buffer[p] == ' ') ++p;
    if (p + 2 >= bytes || buffer[p] < '0' || buffer[p] > '9' ||
        buffer[p + 1] < '0' || buffer[p + 1] > '9' ||
        buffer[p + 2] < '0' || buffer[p + 2] > '9') return false;
    statusOut = (buffer[p] - '0') * 100L + (buffer[p + 1] - '0') * 10L + (buffer[p + 2] - '0');
    return true;
}

// Small sequential probes let fresh, large Internet Archive payloads avoid a
// technically-working but slow storage edge. Probes never write to disk and are
// intentionally bounded so selection cannot turn into a second full download.
constexpr uint64_t ARCHIVE_SELECTOR_MIN_BYTES = 16ULL * 1024ULL * 1024ULL;
constexpr uint64_t ARCHIVE_SELECTOR_PROBE_BYTES = 256ULL * 1024ULL;
constexpr long ARCHIVE_SELECTOR_CONNECT_TIMEOUT = 8L;
constexpr long ARCHIVE_SELECTOR_TOTAL_TIMEOUT = 12L;

struct ArchiveProbeContext {
    uint64_t bytes = 0;
    uint64_t maxBytes = 0;
    uint64_t total = 0;
    long responseCode = 0;
    bool capped = false;
    const HttpCancelFn* shouldCancel = nullptr;
};

struct ArchiveProbeResult {
    std::string url;
    std::string effectiveUrl;
    std::string primaryIp;
    bool ok = false;
    uint64_t bytes = 0;
    uint64_t total = 0;
    uint64_t bytesPerSecond = 0;      // whole request, including TTFB/redirect latency
    uint64_t bodyBytesPerSecond = 0;  // payload body only: bytes / (elapsed - TTFB)
    uint64_t elapsedUs = 0;
    uint64_t ttfbUs = 0;
    long status = 0;
    long redirectCount = 0;
    CURLcode curlCode = CURLE_OK;
};

static size_t archiveProbeHeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    ArchiveProbeContext* ctx = static_cast<ArchiveProbeContext*>(userdata);
    const size_t bytes = size * nitems;
    if (!ctx || bytes == 0) return bytes;

    long status = 0;
    if (parseHttpStatusLine(buffer, bytes, status)) {
        ctx->responseCode = status;
        return bytes;
    }

    const std::string contentRange = headerValue(buffer, bytes, "Content-Range:");
    if (!contentRange.empty()) {
        unsigned long long start = 0, end = 0, total = 0;
        if (std::sscanf(contentRange.c_str(), "bytes %llu-%llu/%llu", &start, &end, &total) == 3 && total > 0) {
            ctx->total = static_cast<uint64_t>(total);
        } else if (std::sscanf(contentRange.c_str(), "bytes */%llu", &total) == 1 && total > 0) {
            ctx->total = static_cast<uint64_t>(total);
        }
    }
    return bytes;
}

static size_t archiveProbeWriteCallback(char*, size_t size, size_t nmemb, void* userdata) {
    ArchiveProbeContext* ctx = static_cast<ArchiveProbeContext*>(userdata);
    const size_t bytes = size * nmemb;
    if (!ctx || bytes == 0) return bytes;
    if (ctx->shouldCancel && *ctx->shouldCancel && (*ctx->shouldCancel)()) return 0;

    if (ctx->maxBytes > 0 && ctx->bytes + bytes > ctx->maxBytes) {
        const uint64_t remaining = ctx->maxBytes > ctx->bytes ? ctx->maxBytes - ctx->bytes : 0;
        ctx->bytes += remaining;
        ctx->capped = true;
        // Abort if a server ignores Range instead of consuming a potentially huge body.
        return 0;
    }
    ctx->bytes += static_cast<uint64_t>(bytes);
    return bytes;
}

static int archiveProbeProgressCallback(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    ArchiveProbeContext* ctx = static_cast<ArchiveProbeContext*>(userdata);
    if (ctx && ctx->shouldCancel && *ctx->shouldCancel && (*ctx->shouldCancel)()) return 1;
    return 0;
}

static bool probeArchiveStorageUrl(
    const std::string& url,
    uint64_t requestedBytes,
    const HttpCancelFn& shouldCancel,
    ArchiveProbeResult& out
) {
    out = ArchiveProbeResult{};
    out.url = url;
    if (url.empty() || requestedBytes == 0) return false;

    CURL* p = curl_easy_init();
    if (!p) return false;

    ArchiveProbeContext ctx;
    ctx.maxBytes = requestedBytes;
    ctx.shouldCancel = &shouldCancel;

    char range[64];
    sceClibSnprintf(range, sizeof(range), "0-%llu", (unsigned long long)(requestedBytes - 1));
    char error[CURL_ERROR_SIZE];
    std::memset(error, 0, sizeof(error));

    curl_easy_setopt(p, CURLOPT_URL, url.c_str());
    curl_easy_setopt(p, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(p, CURLOPT_RANGE, range);
    curl_easy_setopt(p, CURLOPT_USERAGENT, UA_APP);
    applyVitaSslDefaults(p);
    curl_easy_setopt(p, CURLOPT_SSLVERSION, CURL_SSLVERSION_DEFAULT);
    curl_easy_setopt(p, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    curl_easy_setopt(p, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(p, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(p, CURLOPT_CONNECTTIMEOUT, ARCHIVE_SELECTOR_CONNECT_TIMEOUT);
    curl_easy_setopt(p, CURLOPT_TIMEOUT, ARCHIVE_SELECTOR_TOTAL_TIMEOUT);
    curl_easy_setopt(p, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(p, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(p, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(p, CURLOPT_ERRORBUFFER, error);
    curl_easy_setopt(p, CURLOPT_WRITEFUNCTION, archiveProbeWriteCallback);
    curl_easy_setopt(p, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(p, CURLOPT_HEADERFUNCTION, archiveProbeHeaderCallback);
    curl_easy_setopt(p, CURLOPT_HEADERDATA, &ctx);
    curl_easy_setopt(p, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(p, CURLOPT_XFERINFOFUNCTION, archiveProbeProgressCallback);
    curl_easy_setopt(p, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(p, CURLOPT_BUFFERSIZE, 64L * 1024L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: */*");
    headers = curl_slist_append(headers, "Accept-Encoding: identity");
    headers = curl_slist_append(headers, "Referer: https://archive.org/");
    curl_easy_setopt(p, CURLOPT_HTTPHEADER, headers);

    const uint64_t started = sceKernelGetProcessTimeWide();
    const CURLcode rc = curl_easy_perform(p);
    const uint64_t elapsedUs = sceKernelGetProcessTimeWide() - started;
    long status = 0;
    curl_easy_getinfo(p, CURLINFO_RESPONSE_CODE, &status);
    const char* probeEffective = nullptr;
    const char* probeIp = nullptr;
    long probeRedirects = 0;
    double probeTtfbSec = 0.0;
    curl_easy_getinfo(p, CURLINFO_EFFECTIVE_URL, &probeEffective);
    curl_easy_getinfo(p, CURLINFO_PRIMARY_IP, &probeIp);
    curl_easy_getinfo(p, CURLINFO_REDIRECT_COUNT, &probeRedirects);
    curl_easy_getinfo(p, CURLINFO_STARTTRANSFER_TIME, &probeTtfbSec);
    const std::string probeEffectiveCopy = (probeEffective && *probeEffective) ? probeEffective : "";
    const std::string probeIpCopy = (probeIp && *probeIp) ? probeIp : "";

    curl_slist_free_all(headers);
    curl_easy_cleanup(p);

    out.bytes = ctx.bytes;
    out.total = ctx.total;
    out.elapsedUs = elapsedUs;
    out.status = status ? status : ctx.responseCode;
    out.curlCode = rc;
    out.effectiveUrl = probeEffectiveCopy;
    out.primaryIp = probeIpCopy;
    out.redirectCount = probeRedirects;
    out.ttfbUs = probeTtfbSec > 0.0 ? static_cast<uint64_t>(probeTtfbSec * 1000000.0) : 0ULL;
    if (elapsedUs > 0 && ctx.bytes > 0) {
        out.bytesPerSecond = (ctx.bytes * 1000000ULL) / elapsedUs;
    }
    const uint64_t bodyUs =
        (out.ttfbUs > 0 && out.elapsedUs > out.ttfbUs) ? (out.elapsedUs - out.ttfbUs) : out.elapsedUs;
    if (bodyUs > 0 && ctx.bytes > 0) {
        out.bodyBytesPerSecond = (ctx.bytes * 1000000ULL) / bodyUs;
    }

    // IA direct storage nodes should honor Range with 206. If a node ignores it,
    // do not select it proactively; the normal download/failover path remains available.
    out.ok = (out.status == 206 && out.bytes > 0 &&
              (rc == CURLE_OK || (rc == CURLE_WRITE_ERROR && ctx.capped)));

    char msg[520];
    sceClibSnprintf(msg, sizeof(msg),
        "archive selector probe ok=%d curl=%d HTTP=%ld bytes=%llu total=%llu us=%llu total_speed=%llu body_speed=%llu url=%s",
        out.ok ? 1 : 0,
        static_cast<int>(rc),
        out.status,
        (unsigned long long)out.bytes,
        (unsigned long long)out.total,
        (unsigned long long)out.elapsedUs,
        (unsigned long long)out.bytesPerSecond,
        (unsigned long long)out.bodyBytesPerSecond,
        url.c_str());
    httpDiagnostic(msg);
    {
        const uint64_t totalKibPerSecond = out.bytesPerSecond / 1024ULL;
        const uint64_t totalMibHundredths = (out.bytesPerSecond * 100ULL) / (1024ULL * 1024ULL);
        const uint64_t bodyKibPerSecond = out.bodyBytesPerSecond / 1024ULL;
        const uint64_t bodyMibHundredths = (out.bodyBytesPerSecond * 100ULL) / (1024ULL * 1024ULL);
        const uint64_t bodyUs =
            (out.ttfbUs > 0 && out.elapsedUs > out.ttfbUs) ? (out.elapsedUs - out.ttfbUs) : out.elapsedUs;
        const char* requestedRisk = hostLooksLikeBadArchiveEdge(url.c_str()) ? "dn_or_ca" : "normal";
        const char* effectiveRisk = (!out.effectiveUrl.empty() && hostLooksLikeBadArchiveEdge(out.effectiveUrl.c_str())) ? "dn_or_ca" : "normal";
        char detailed[1800];
        sceClibSnprintf(detailed, sizeof(detailed),
            "PROBE requested=%llu ok=%d curl=%d curl_text=%s HTTP=%ld bytes=%llu remote_total=%llu elapsed_ms=%llu ttfb_ms=%llu body_ms=%llu total_speed_Bps=%llu total_speed_KiBps=%llu total_speed_MiBps=%llu.%02llu body_speed_Bps=%llu body_speed_KiBps=%llu body_speed_MiBps=%llu.%02llu redirects=%ld ip=%s risk_requested=%s risk_effective=%s requested_url=%s effective_url=%s detail=%s",
            (unsigned long long)requestedBytes, out.ok ? 1 : 0,
            static_cast<int>(rc), curl_easy_strerror(rc), out.status,
            (unsigned long long)out.bytes, (unsigned long long)out.total,
            (unsigned long long)(out.elapsedUs / 1000ULL),
            (unsigned long long)(out.ttfbUs / 1000ULL),
            (unsigned long long)(bodyUs / 1000ULL),
            (unsigned long long)out.bytesPerSecond,
            (unsigned long long)totalKibPerSecond,
            (unsigned long long)(totalMibHundredths / 100ULL),
            (unsigned long long)(totalMibHundredths % 100ULL),
            (unsigned long long)out.bodyBytesPerSecond,
            (unsigned long long)bodyKibPerSecond,
            (unsigned long long)(bodyMibHundredths / 100ULL),
            (unsigned long long)(bodyMibHundredths % 100ULL),
            out.redirectCount,
            out.primaryIp.empty() ? "-" : out.primaryIp.c_str(),
            requestedRisk,
            effectiveRisk,
            url.c_str(),
            out.effectiveUrl.empty() ? "-" : out.effectiveUrl.c_str(),
            error[0] ? error : "-");
        archiveNodeDiagnostic(detailed);
    }
    return out.ok;
}

// Returns true when a direct Archive storage node was validated and moved to
// urls.front(). Successful large-file probes are ranked fastest-first so existing
// failover automatically tries the next-best measured node after a failure.
static bool rankArchiveAlternateUrls(
    std::vector<std::string>& urls,
    const HttpCancelFn& shouldCancel,
    uint64_t sizeHint
) {
    if (urls.empty()) return false;
    {
        char beginMsg[280];
        sceClibSnprintf(beginMsg, sizeof(beginMsg),
            "SELECTOR_BEGIN candidates=%u size_hint=%llu threshold=%llu probe_bytes=%llu",
            (unsigned int)urls.size(),
            (unsigned long long)sizeHint,
            (unsigned long long)ARCHIVE_SELECTOR_MIN_BYTES,
            (unsigned long long)ARCHIVE_SELECTOR_PROBE_BYTES);
        archiveNodeDiagnostic(beginMsg);
    }

    // The catalog/link size is intentionally only a threshold hint.
    // becomes authoritative transfer metadata and is never used for integrity.
    // When absent, preserve the previous one-byte Range probe as the fallback
    // for deciding whether the payload crosses the 16 MiB benchmark threshold.
    uint64_t decisionSize = sizeHint;
    ArchiveProbeResult sizeProbe;
    size_t responsiveIndex = urls.size();
    if (decisionSize == 0) {
        for (size_t i = 0; i < urls.size(); ++i) {
            if (shouldCancel && shouldCancel()) return false;
            if (probeArchiveStorageUrl(urls[i], 1, shouldCancel, sizeProbe)) {
                responsiveIndex = i;
                break;
            }
        }
        if (responsiveIndex == urls.size()) {
            httpDiagnostic("archive selector: no direct node passed size probe; keep normal archive.org path");
            return false;
        }
        decisionSize = sizeProbe.total;
        if (decisionSize == 0) {
            httpDiagnostic("archive selector: remote total unknown; keep normal archive.org path");
            return false;
        }
        char sourceMsg[180];
        sceClibSnprintf(sourceMsg, sizeof(sourceMsg),
            "archive selector threshold source=range size=%llu",
            (unsigned long long)decisionSize);
        httpDiagnostic(sourceMsg);
        archiveNodeDiagnostic(sourceMsg);
    } else {
        char sourceMsg[180];
        sceClibSnprintf(sourceMsg, sizeof(sourceMsg),
            "archive selector threshold source=catalog-hint size=%llu",
            (unsigned long long)decisionSize);
        httpDiagnostic(sourceMsg);
        archiveNodeDiagnostic(sourceMsg);
    }

    if (decisionSize < ARCHIVE_SELECTOR_MIN_BYTES) {
        // A catalog hint can decide that benchmarking is unnecessary, but we
        // still validate one direct node before replacing the canonical URL.
        if (responsiveIndex == urls.size()) {
            for (size_t i = 0; i < urls.size(); ++i) {
                if (shouldCancel && shouldCancel()) return false;
                if (probeArchiveStorageUrl(urls[i], 1, shouldCancel, sizeProbe)) {
                    responsiveIndex = i;
                    break;
                }
            }
        }
        if (responsiveIndex == urls.size()) {
            httpDiagnostic("archive selector: no direct node passed validation probe; keep normal archive.org path");
            return false;
        }
        if (archiveEffectiveUrlUsable(sizeProbe.effectiveUrl)) {
            archiveNodeDiagnostic(std::string("SMALL_FILE_EFFECTIVE_URL requested=") + urls[responsiveIndex] +
                " effective=" + sizeProbe.effectiveUrl);
            urls[responsiveIndex] = sizeProbe.effectiveUrl;
        }
        if (responsiveIndex != 0) std::swap(urls[0], urls[responsiveIndex]);
        char msg[320];
        sceClibSnprintf(msg, sizeof(msg),
            "archive selector: small file decision=%llu; use responsive direct node without speed benchmark -> %s",
            (unsigned long long)decisionSize, urls.front().c_str());
        httpDiagnostic(msg);
        archiveNodeDiagnostic(std::string("BENCHMARK_SKIPPED reason=below_16MiB ") + msg);
        return true;
    }

    archiveNodeDiagnostic("BENCHMARK_BEGIN mode=sequential each_probe=256KiB");
    std::vector<ArchiveProbeResult> probes;
    probes.reserve(urls.size());
    for (const auto& candidate : urls) {
        if (shouldCancel && shouldCancel()) return false;
        ArchiveProbeResult probe;
        probeArchiveStorageUrl(candidate, ARCHIVE_SELECTOR_PROBE_BYTES, shouldCancel, probe);
        probes.push_back(probe);
    }

    bool anyOk = false;
    for (const auto& p : probes) if (p.ok) { anyOk = true; break; }
    if (!anyOk) {
        httpDiagnostic("archive selector: speed probes failed; keep normal archive.org path");
        return false;
    }

    // Tiny candidate count (normally two or three): successful nodes first,
    // then payload-body throughput. TTFB is only a tie-breaker so a slow first byte
    // cannot make a fast sustained storage edge look artificially bad on large files.
    for (size_t i = 0; i < probes.size(); ++i) {
        for (size_t j = i + 1; j < probes.size(); ++j) {
            const bool bothOk = probes[j].ok && probes[i].ok;
            const bool jBetter =
                (probes[j].ok && !probes[i].ok) ||
                (bothOk && probes[j].bodyBytesPerSecond > probes[i].bodyBytesPerSecond) ||
                (bothOk && probes[j].bodyBytesPerSecond == probes[i].bodyBytesPerSecond &&
                 probes[j].ttfbUs < probes[i].ttfbUs);
            if (jBetter) std::swap(probes[i], probes[j]);
        }
    }

    archiveNodeDiagnostic("RANKING_BEGIN fastest successful candidate first");
    for (size_t i = 0; i < probes.size(); ++i) {
        const auto& p = probes[i];
        const uint64_t totalKibPerSecond = p.bytesPerSecond / 1024ULL;
        const uint64_t totalMibHundredths = (p.bytesPerSecond * 100ULL) / (1024ULL * 1024ULL);
        const uint64_t bodyKibPerSecond = p.bodyBytesPerSecond / 1024ULL;
        const uint64_t bodyMibHundredths = (p.bodyBytesPerSecond * 100ULL) / (1024ULL * 1024ULL);
        const std::string rankedEffective = archiveEffectiveUrlUsable(p.effectiveUrl) ? p.effectiveUrl : p.url;
        char ranked[1500];
        sceClibSnprintf(ranked, sizeof(ranked),
            "RANK[%u] ok=%d body_speed_Bps=%llu body_speed_KiBps=%llu body_speed_MiBps=%llu.%02llu total_speed_Bps=%llu total_speed_KiBps=%llu total_speed_MiBps=%llu.%02llu HTTP=%ld curl=%d ttfb_ms=%llu elapsed_ms=%llu redirects=%ld ip=%s risk=%s requested=%s effective=%s",
            (unsigned int)(i + 1), p.ok ? 1 : 0,
            (unsigned long long)p.bodyBytesPerSecond,
            (unsigned long long)bodyKibPerSecond,
            (unsigned long long)(bodyMibHundredths / 100ULL),
            (unsigned long long)(bodyMibHundredths % 100ULL),
            (unsigned long long)p.bytesPerSecond,
            (unsigned long long)totalKibPerSecond,
            (unsigned long long)(totalMibHundredths / 100ULL),
            (unsigned long long)(totalMibHundredths % 100ULL),
            p.status, static_cast<int>(p.curlCode),
            (unsigned long long)(p.ttfbUs / 1000ULL),
            (unsigned long long)(p.elapsedUs / 1000ULL),
            p.redirectCount,
            p.primaryIp.empty() ? "-" : p.primaryIp.c_str(),
            hostLooksLikeBadArchiveEdge(rankedEffective.c_str()) ? "dn_or_ca" : "normal",
            p.url.c_str(),
            p.effectiveUrl.empty() ? "-" : p.effectiveUrl.c_str());
        archiveNodeDiagnostic(ranked);
    }
    archiveNodeDiagnostic("RANKING_END");

    // Use what libcurl actually reached, not merely the requested storage URL.
    // Archive can redirect ia601 -> ia801 (and vice versa); dedupe by effective host
    // so the failover list does not contain multiple aliases for the same real node.
    urls.clear();
    std::vector<std::string> seenEffectiveHosts;
    for (const auto& p : probes) {
        std::string chosenUrl = p.url;
        if (p.ok && archiveEffectiveUrlUsable(p.effectiveUrl)) chosenUrl = p.effectiveUrl;
        const std::string chosenHost = archiveUrlHostLower(chosenUrl);
        bool duplicateHost = false;
        if (!chosenHost.empty()) {
            for (const auto& seen : seenEffectiveHosts) {
                if (seen == chosenHost) {
                    duplicateHost = true;
                    break;
                }
            }
        }
        if (duplicateHost) {
            archiveNodeDiagnostic(std::string("EFFECTIVE_DUPLICATE_SKIPPED host=") + chosenHost +
                " requested=" + p.url + " effective=" + (p.effectiveUrl.empty() ? "-" : p.effectiveUrl));
            continue;
        }
        if (!chosenHost.empty()) seenEffectiveHosts.push_back(chosenHost);
        urls.push_back(chosenUrl);
    }

    if (urls.empty()) {
        httpDiagnostic("archive selector: effective candidate list unexpectedly empty; keep normal archive.org path");
        return false;
    }

    char chosen[900];
    sceClibSnprintf(chosen, sizeof(chosen),
        "archive selector chose body_speed=%llu B/s total_speed=%llu B/s ttfb_ms=%llu total=%llu requested=%s effective=%s selected=%s",
        (unsigned long long)probes.front().bodyBytesPerSecond,
        (unsigned long long)probes.front().bytesPerSecond,
        (unsigned long long)(probes.front().ttfbUs / 1000ULL),
        (unsigned long long)probes.front().total,
        probes.front().url.c_str(),
        probes.front().effectiveUrl.empty() ? "-" : probes.front().effectiveUrl.c_str(),
        urls.front().c_str());
    httpDiagnostic(chosen);
    archiveNodeDiagnostic(std::string("SELECTED ") + chosen);
    return probes.front().ok;
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

    // libcurl explicitly does NOT NUL-terminate header lines and invokes this callback
    // for every response in a redirect/auth chain. Treat status lines as response
    // boundaries so Content-Length/ETag from a 30x/401 cannot leak into the final body.
    long status = 0;
    if (parseHttpStatusLine(buffer, bytes, status)) {
        ctx->responseCode = status;
        ctx->total = 0;
        ctx->totalFromContentRange = false;
        ctx->rangeValid = false;
        ctx->rangeStart = 0;
        ctx->rangeEnd = 0;
        ctx->rangeMismatch = false;
        ctx->retryAfterSeconds = 0;
        ctx->etag.clear();
        ctx->lastModified.clear();
        ctx->firstWrite = true;
        char msg[96];
        sceClibSnprintf(msg, sizeof(msg), "response status=%ld", status);
        httpDiagnostic(msg);
        return bytes;
    }

    const std::string contentLength = headerValue(buffer, bytes, "Content-Length:");
    if (!contentLength.empty()) {
        unsigned long long value = 0;
        if (std::sscanf(contentLength.c_str(), "%llu", &value) == 1)
            ctx->total = static_cast<uint64_t>(value);
    }

    const std::string contentRange = headerValue(buffer, bytes, "Content-Range:");
    if (!contentRange.empty()) {
        uint64_t rangeStart = 0;
        uint64_t rangeEnd = 0;
        uint64_t rangeTotal = 0;
        if (parseContentRangeTotal(contentRange, rangeStart, rangeEnd, rangeTotal)) {
            ctx->total = rangeTotal;
            ctx->totalFromContentRange = true;
            ctx->rangeValid = true;
            ctx->rangeStart = rangeStart;
            ctx->rangeEnd = rangeEnd;
            char rangeMsg[240];
            sceClibSnprintf(rangeMsg, sizeof(rangeMsg),
                "content-range start=%llu end=%llu total=%llu resume=%llu",
                (unsigned long long)rangeStart,
                (unsigned long long)rangeEnd,
                (unsigned long long)rangeTotal,
                (unsigned long long)ctx->resumeOffset);
            httpDiagnostic(rangeMsg);
            if (ctx->resumeOffset > 0 && rangeStart != ctx->resumeOffset) {
                ctx->rangeMismatch = true;
                char mm[220];
                sceClibSnprintf(mm, sizeof(mm),
                    "content-range MISMATCH start=%llu requested=%llu — will abort append",
                    (unsigned long long)rangeStart,
                    (unsigned long long)ctx->resumeOffset);
                httpDiagnostic(mm);
            }
        } else {
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
            } else {
                httpDiagnostic("content-range present but unparseable");
            }
        }
    }

    const std::string etag = headerValue(buffer, bytes, "ETag:");
    if (!etag.empty()) ctx->etag = etag;
    const std::string modified = headerValue(buffer, bytes, "Last-Modified:");
    if (!modified.empty()) ctx->lastModified = modified;
    const std::string retryAfter = headerValue(buffer, bytes, "Retry-After:");
    if (!retryAfter.empty()) {
        int sec = 0;
        if (std::sscanf(retryAfter.c_str(), "%d", &sec) == 1 && sec > 0) {
            if (sec > 30) sec = 30;
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

    long responseCode = ctx->responseCode;
    if (responseCode == 0)
        curl_easy_getinfo(ctx->curl, CURLINFO_RESPONSE_CODE, &responseCode);

    // Never write a final HTTP error page into payload.part. 4xx/5xx bodies are
    // still consumed so curl_easy_perform can return CURLE_OK and the caller can
    // classify/retry by HTTP status without corrupting an existing partial file.
    if (responseCode != 200 && responseCode != 206) {
        if (!ctx->discardedErrorBody) {
            char msg[128];
            sceClibSnprintf(msg, sizeof(msg), "discarding non-payload response body status=%ld", responseCode);
            httpDiagnostic(msg);
            ctx->discardedErrorBody = true;
        }
        return bytes;
    }

    if (ctx->firstWrite) {
        ctx->firstWrite = false;
        char first[260];
        sceClibSnprintf(first, sizeof(first),
            "first-write status=%ld resume=%llu total=%llu rangeValid=%d rangeStart=%llu mismatch=%d",
            responseCode,
            (unsigned long long)ctx->resumeOffset,
            (unsigned long long)ctx->total,
            ctx->rangeValid ? 1 : 0,
            (unsigned long long)ctx->rangeStart,
            ctx->rangeMismatch ? 1 : 0);
        httpDiagnostic(first);
        if (ctx->resumeOffset > 0 && responseCode == 200) {
            // Server ignored Range — never append a full body onto a partial.
            sceIoClose(ctx->fd);
            ctx->fd = sceIoOpen(ctx->path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
            if (ctx->fd < 0) {
                ctx->ioError = true;
                return 0;
            }
            ctx->resumeOffset = 0;
            ctx->downloaded = 0;
            ctx->restartedFromZero = true;
            httpDiagnostic("server ignored Range (HTTP 200); restarted download from zero");
        } else if (ctx->resumeOffset > 0 && responseCode == 206) {
            // Require a valid Content-Range that starts exactly at resumeOffset.
            if (ctx->rangeMismatch || !ctx->rangeValid) {
                httpDiagnostic("HTTP 206 with invalid/mismatched Content-Range; aborting body to avoid corruption");
                ctx->rangeMismatch = true;
                return 0; // abort this transfer; caller will clean-restart
            }
        }
    }

    if (ctx->rangeMismatch) {
        return 0;
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

// CURLOPT_XFERINFOFUNCTION keeps cancellation responsive even while a server
// is connected but not delivering body data. writeCallback alone cannot see
// cancellation during a low-speed/stalled period.
static int transferProgressCallback(
    void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t
) {
    TransferContext* ctx = static_cast<TransferContext*>(userdata);
    if (!ctx) return 0;
    if (ctx->shouldCancel && ctx->shouldCancel()) {
        ctx->cancelled = true;
        return 1; // libcurl -> CURLE_ABORTED_BY_CALLBACK
    }
    return 0;
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
    applyVitaSslDefaults(curl);
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
        if (rc == CURLE_SSL_CONNECT_ERROR || rc == CURLE_SSL_CERTPROBLEM ||
            rc == CURLE_PEER_FAILED_VERIFICATION)
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
    applyVitaSslDefaults(curl);
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
        if (rc == CURLE_SSL_CONNECT_ERROR || rc == CURLE_SSL_CERTPROBLEM ||
            rc == CURLE_PEER_FAILED_VERIFICATION)
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
    applyVitaSslDefaults(curl);
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
        return (result == CURLE_SSL_CONNECT_ERROR || result == CURLE_PEER_FAILED_VERIFICATION ||
                result == CURLE_SSL_CERTPROBLEM) ? HttpResult::SslError : HttpResult::NetworkError;
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
    int maxAttemptsOverride,
    const std::string& ifRangeValidator,
    uint64_t archiveSelectionSizeHint
) {
    lastStatus_ = 0;
    lastRangeAccepted_ = false;
    lastError_.clear();
    lastEtag_.clear();
    lastModified_.clear();
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
    applyVitaSslDefaults(curl);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_DEFAULT);
    curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    // Prefer IPv4 — dual-stack SSL handshakes often fail on Vita/Vita3K.
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
#if LIBCURL_VERSION_NUM >= 0x071900
    // Detect half-open Wi-Fi/NAT connections sooner during multi-gigabyte transfers.
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);
#endif
    // Do not set CURLOPT_SSL_CIPHER_LIST: Vita libcurl often returns CURLE_SSL_CIPHER (59)
    // for OpenSSL "SECLEVEL" syntax. Leave cipher negotiation to the TLS backend.
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, LOW_SPEED_LIMIT);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, LOW_SPEED_TIME_SECONDS);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
#if LIBCURL_VERSION_NUM >= 0x075500
    // Catalog URLs are external input. Keep both the initial transfer and all
    // redirects on HTTP(S); modern libcurl otherwise supports many schemes.
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transferProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, static_cast<long>(DOWNLOAD_BUFFER_SIZE));

    if (resumeOffset > 0) {
        // CURLOPT_RESUME_FROM takes a long (32-bit on Vita) and breaks past ~2GB.
        // Prefer the 64-bit LARGE variant for multi-GB downloads (Game Files, etc.).
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(resumeOffset));
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ctx);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: */*");
    headers = curl_slist_append(headers, "Accept-Encoding: identity");
    headers = curl_slist_append(headers, "Connection: keep-alive");
    // If-Range makes persisted partial files safe across sessions: a validator
    // match permits 206 resume; a changed resource is sent as a full 200 body.
    if (resumeOffset > 0 && !ifRangeValidator.empty()) {
        const std::string ifRangeHeader = std::string("If-Range: ") + ifRangeValidator;
        headers = curl_slist_append(headers, ifRangeHeader.c_str());
        httpDiagnostic("resume validator attached via If-Range");
    }
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
    long lastSslVerifyResult = 0;
    CURLcode lastFail = CURLE_OK;
    bool lastTlsLikeFail = false;
    // One-shot guard: if a Range request fails (curl 33), truncate and retry as full GET.
    bool rangeFallbackUsed = false;
    // archive.org: when a 302 lands on dn*/ca edges, OpenSSL 1.0.2 often fails TLS.
    // Build alternate item URLs from metadata once and rotate through them.
    std::vector<std::string> archiveAltUrls;
    size_t archiveAltIndex = 0;
    bool archiveMetaTried = false;
    std::string activeUrl = url;

    if (isArchive) {
        std::string archiveId, archiveFile;
        parseArchiveDownloadParts(url, archiveId, archiveFile);
        archiveNodeDiagnostic("============================================================");
        char requestMsg[1500];
        sceClibSnprintf(requestMsg, sizeof(requestMsg),
            "DOWNLOAD_REQUEST identifier=%s file=%s resume=%llu size_hint=%llu max_attempt_override=%d destination=%s canonical=%s",
            archiveId.empty() ? "-" : archiveId.c_str(),
            archiveFile.empty() ? "-" : archiveFile.c_str(),
            (unsigned long long)resumeOffset,
            (unsigned long long)archiveSelectionSizeHint,
            maxAttemptsOverride,
            destinationPath.c_str(), url.c_str());
        archiveNodeDiagnostic(requestMsg);
    }

    // Fresh payload downloads can avoid a slow Archive redirect before writing any
    // bytes. maxAttemptsOverride != 0 is used by small image/cache requests, which
    // deliberately skip this selector to avoid metadata/probe overhead. Resumes also
    // keep the established URL first so validator/range semantics are unchanged.
    if (isArchive && resumeOffset == 0 && maxAttemptsOverride == 0) {
        if (buildArchiveAlternateUrls(url, archiveAltUrls)) {
            archiveMetaTried = true;
            if (rankArchiveAlternateUrls(archiveAltUrls, ctx.shouldCancel, archiveSelectionSizeHint) && !archiveAltUrls.empty()) {
                activeUrl = archiveAltUrls.front();
                archiveAltIndex = 1;
                char selected[420];
                sceClibSnprintf(selected, sizeof(selected),
                    "archive selector start -> %s", activeUrl.c_str());
                httpDiagnostic(selected);
                archiveNodeDiagnostic(std::string("REAL_DOWNLOAD_SELECTED url=") + activeUrl +
                    " fallback_candidates_remaining=" + std::to_string(archiveAltUrls.size() > 1 ? archiveAltUrls.size() - 1 : 0));
            } else {
                archiveNodeDiagnostic("SELECTOR_RESULT no proactive winner; canonical URL starts first");
                // Metadata candidates remain available for the existing failure-driven
                // failover, but the first real attempt stays on the canonical URL.
                archiveAltIndex = 0;
            }
        }
    } else if (isArchive) {
        if (resumeOffset > 0) archiveNodeDiagnostic("SELECTOR_SKIPPED reason=resume");
        else if (maxAttemptsOverride != 0) archiveNodeDiagnostic("SELECTOR_SKIPPED reason=explicit_retry_override");
    }

    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        applyVitaSslDefaults(curl);
        const int sslIdx = attempt < 4 ? attempt : (attempt % 4);
        curl_easy_setopt(curl, CURLOPT_SSLVERSION, sslAttempts[sslIdx]);
        curl_easy_setopt(curl, CURLOPT_URL, activeUrl.c_str());
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
        // Broken TLS implementations can fail when a cached SSL session is reused.
        // Disable session reuse after both direct SSL errors and transport errors that
        // libcurl/OpenSSL explicitly diagnosed as TLS/certificate related.
        if (attempt > 0 && lastTlsLikeFail) {
            curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 0L);
        }
        // UA: app identity first; after connect/SSL trouble try CDN-friendly browser UA.
        if (attempt > 0 && seriousFail) {
            curl_easy_setopt(curl, CURLOPT_USERAGENT, UA_CDN_FALLBACK);
        } else {
            curl_easy_setopt(curl, CURLOPT_USERAGENT, UA_APP);
        }
        if (attempt > 0) {
            char retryMsg[192];
            sceClibSnprintf(retryMsg, sizeof(retryMsg),
                "download retry %d/%d hostHints=gitlab:%d archive:%d github:%d lastCurl=%d fresh=%d tlsLike=%d",
                attempt + 1, kMaxAttempts, isGitlab ? 1 : 0, isArchive ? 1 : 0, isGithub ? 1 : 0,
                static_cast<int>(lastFail), needFresh ? 1 : 0, lastTlsLikeFail ? 1 : 0);
            httpDiagnostic(retryMsg);
            // Short early backoff; grow only after several failures.
            int delayMs = 250 * (attempt <= 3 ? attempt : 3);
            if (isArchive) delayMs = 350 * (attempt <= 4 ? attempt : 4);
            if (lastTlsLikeFail) {
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
                ctx.rangeValid = false;
                ctx.rangeStart = 0;
                ctx.rangeEnd = 0;
                ctx.rangeMismatch = false;
                ctx.totalFromContentRange = false;
                ctx.etag.clear();
                ctx.lastModified.clear();
                // Always use 64-bit resume (CURLOPT_RESUME_FROM is long and breaks past ~2 GiB on Vita).
                curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(absPos));
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

        if (isArchive) {
            char attemptBegin[1200];
            sceClibSnprintf(attemptBegin, sizeof(attemptBegin),
                "TRANSFER_ATTEMPT_BEGIN attempt=%d/%d resume=%llu ssl_mode=%ld fresh=%d active_url=%s",
                attempt + 1, kMaxAttempts,
                (unsigned long long)ctx.resumeOffset,
                sslAttempts[sslIdx], needFresh ? 1 : 0,
                activeUrl.c_str());
            archiveNodeDiagnostic(attemptBegin);
        }

        // Response headers belong to this attempt. Clear range-derived state so a
        // previous retry/redirect cannot leak an old total into the next response.
        ctx.total = 0;
        ctx.totalFromContentRange = false;
        ctx.rangeValid = false;
        ctx.rangeStart = 0;
        ctx.rangeEnd = 0;
        ctx.rangeMismatch = false;
        ctx.responseCode = 0;
        ctx.discardedErrorBody = false;
        ctx.firstWrite = true;
        ctx.etag.clear();
        ctx.lastModified.clear();
        ctx.retryAfterSeconds = 0;
        curlError[0] = '\0';
        result = curl_easy_perform(curl);
        responseCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
        long sslVerifyResult = 0;
        if (curl_easy_getinfo(curl, CURLINFO_SSL_VERIFYRESULT, &sslVerifyResult) == CURLE_OK)
            lastSslVerifyResult = sslVerifyResult;
        else
            lastSslVerifyResult = 0;

        if (isArchive) {
            const char* attemptEffective = nullptr;
            const char* attemptIp = nullptr;
            long attemptRedirects = 0;
            double attemptTotalSec = 0.0;
            double attemptTtfbSec = 0.0;
            curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &attemptEffective);
            curl_easy_getinfo(curl, CURLINFO_PRIMARY_IP, &attemptIp);
            curl_easy_getinfo(curl, CURLINFO_REDIRECT_COUNT, &attemptRedirects);
            curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &attemptTotalSec);
            curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME, &attemptTtfbSec);
            const uint64_t kibPerSecond = ctx.bytesPerSecond / 1024ULL;
            const uint64_t mibHundredths = (ctx.bytesPerSecond * 100ULL) / (1024ULL * 1024ULL);
            char attemptEnd[1700];
            sceClibSnprintf(attemptEnd, sizeof(attemptEnd),
                "TRANSFER_ATTEMPT_RESULT attempt=%d/%d curl=%d curl_text=%s HTTP=%ld bytes_this_attempt=%llu absolute=%llu remote_total=%llu sampled_speed_Bps=%llu sampled_speed_KiBps=%llu sampled_speed_MiBps=%llu.%02llu total_ms=%llu ttfb_ms=%llu redirects=%ld ssl_verify=%ld ip=%s active_url=%s effective_url=%s detail=%s",
                attempt + 1, kMaxAttempts,
                static_cast<int>(result), curl_easy_strerror(result), responseCode,
                (unsigned long long)ctx.downloaded,
                (unsigned long long)(ctx.resumeOffset + ctx.downloaded),
                (unsigned long long)ctx.total,
                (unsigned long long)ctx.bytesPerSecond,
                (unsigned long long)kibPerSecond,
                (unsigned long long)(mibHundredths / 100ULL),
                (unsigned long long)(mibHundredths % 100ULL),
                (unsigned long long)(attemptTotalSec * 1000.0),
                (unsigned long long)(attemptTtfbSec * 1000.0),
                attemptRedirects, lastSslVerifyResult,
                (attemptIp && *attemptIp) ? attemptIp : "-",
                activeUrl.c_str(),
                (attemptEffective && *attemptEffective) ? attemptEffective : "-",
                curlError[0] ? curlError : "-");
            archiveNodeDiagnostic(attemptEnd);
        }

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

        // Content-Range mismatch on 206 (or aborted write): never keep corrupted partial.
        // Clean-restart from zero at most once.
        if (ctx.rangeMismatch && !rangeFallbackUsed) {
            rangeFallbackUsed = true;
            char mm[240];
            sceClibSnprintf(mm, sizeof(mm),
                "range mismatch clean-restart from 0 (was resume=%llu local+=%llu)",
                (unsigned long long)ctx.resumeOffset,
                (unsigned long long)ctx.downloaded);
            httpDiagnostic(mm);
            if (ctx.fd >= 0) {
                sceIoClose(ctx.fd);
                ctx.fd = -1;
            }
            sceIoRemove(destinationPath.c_str());
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
            ctx.rangeValid = false;
            ctx.rangeStart = 0;
            ctx.rangeEnd = 0;
            ctx.rangeMismatch = false;
            ctx.lastProgressTick = 0;
            ctx.lastProgressBytes = 0;
            ctx.bytesPerSecond = 0;
            ctx.firstWrite = true;
            ctx.restartedFromZero = true;
            ctx.etag.clear();
            ctx.lastModified.clear();
            curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(0));
            lastTlsLikeFail = false;
            continue;
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
            ctx.rangeValid = false;
            ctx.rangeStart = 0;
            ctx.rangeEnd = 0;
            ctx.rangeMismatch = false;
            ctx.lastProgressTick = 0;
            ctx.lastProgressBytes = 0;
            ctx.bytesPerSecond = 0;
            ctx.firstWrite = true;
            ctx.restartedFromZero = true;
            ctx.etag.clear();
            ctx.lastModified.clear();
            curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(0));
            lastTlsLikeFail = false;
            continue;
        }

        if (result == CURLE_OK) {
            const bool transientHttp =
                responseCode == 408 || responseCode == 425 || responseCode == 429 ||
                responseCode == 500 || responseCode == 502 || responseCode == 503 ||
                responseCode == 504 || responseCode == 520 || responseCode == 521 ||
                responseCode == 522 || responseCode == 523 || responseCode == 524;
            if (!transientHttp) break;
            char httpRetry[120];
            sceClibSnprintf(httpRetry, sizeof(httpRetry),
                "attempt %d HTTP %ld transient — will retry (Retry-After=%d)",
                attempt + 1, responseCode, ctx.retryAfterSeconds);
            httpDiagnostic(httpRetry);
            lastFail = CURLE_HTTP_RETURNED_ERROR;
            lastTlsLikeFail = false;
            if (resumeOffset == 0 && ctx.fd >= 0 && ctx.downloaded > 0) {
                sceIoClose(ctx.fd);
                ctx.fd = sceIoOpen(destinationPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
                ctx.downloaded = 0;
            }

            // 5xx from archive.org is commonly edge-specific. Resolve the item metadata
            // once and rotate to another storage node before spending more retries on
            // the same failing edge. 408/425/429 keep the normal retry path.
            if (isArchive && responseCode >= 500) {
                if (!archiveMetaTried) {
                    archiveMetaTried = true;
                    buildArchiveAlternateUrls(url, archiveAltUrls);
                    archiveAltIndex = 0;
                }
                if (archiveAltIndex < archiveAltUrls.size()) {
                    const std::string previousArchiveUrl = activeUrl;
                    activeUrl = archiveAltUrls[archiveAltIndex++];
                    char sw[360];
                    sceClibSnprintf(sw, sizeof(sw),
                        "archive failover switch HTTP=%ld -> %s",
                        responseCode, activeUrl.c_str());
                    httpDiagnostic(sw);
                    char detailedSwitch[1500];
                    sceClibSnprintf(detailedSwitch, sizeof(detailedSwitch),
                        "FAILOVER reason=http_%ld from=%s to=%s next_index=%u remaining=%u",
                        responseCode, previousArchiveUrl.c_str(), activeUrl.c_str(),
                        (unsigned int)archiveAltIndex,
                        (unsigned int)(archiveAltUrls.size() > archiveAltIndex ? archiveAltUrls.size() - archiveAltIndex : 0));
                    archiveNodeDiagnostic(detailedSwitch);
                    curl_easy_setopt(curl, CURLOPT_URL, activeUrl.c_str());
                    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
                    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
                    curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 0L);
                }
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

            curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(0));
            lastFail = result;
            lastTlsLikeFail = false;
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

        const bool directSslFailure =
            result == CURLE_SSL_CONNECT_ERROR ||
            result == CURLE_PEER_FAILED_VERIFICATION ||
            result == CURLE_SSL_CERTPROBLEM ||
#ifdef CURLE_SSL_CIPHER
            result == CURLE_SSL_CIPHER ||
#endif
            false;
        const bool transportTlsDetail =
            (result == CURLE_RECV_ERROR || result == CURLE_SEND_ERROR ||
             result == CURLE_GOT_NOTHING || result == CURLE_PARTIAL_FILE) &&
            curlDetailLooksTls(curlError);
        // OpenSSL can expose a non-zero verification result even when peer verification
        // is disabled. On Archive.org, combine that signal with CURLE_RECV_ERROR (56)
        // to detect broken TLS storage edges like the reported self-signed-chain case.
        const bool archiveVerifyRecv =
            isArchive && result == CURLE_RECV_ERROR && lastSslVerifyResult != 0;
        const bool tlsLikeFailure = directSslFailure || transportTlsDetail || archiveVerifyRecv;

        char failMsg[520];
        sceClibSnprintf(failMsg, sizeof(failMsg),
            "attempt %d failed curl=%d %s retryable=%d tls_like=%d ssl_verify=%ld detail=%s",
            attempt + 1,
            static_cast<int>(result),
            curl_easy_strerror(result),
            retryable ? 1 : 0,
            tlsLikeFailure ? 1 : 0,
            lastSslVerifyResult,
            curlError[0] ? curlError : "-");
        httpDiagnostic(failMsg);
        lastFail = result;
        lastTlsLikeFail = tlsLikeFailure;

        // archive.org edge failover: storage nodes can fail as TLS errors, plain
        // receive/send failures, timeouts, or half-open connections. Rotate away
        // from the edge for all transport-like failures, not only certificate text.
        const bool archiveTransportFailure =
            tlsLikeFailure ||
            result == CURLE_COULDNT_CONNECT ||
            result == CURLE_OPERATION_TIMEDOUT ||
            result == CURLE_RECV_ERROR ||
            result == CURLE_SEND_ERROR ||
            result == CURLE_GOT_NOTHING ||
            result == CURLE_PARTIAL_FILE;
        if (isArchive && archiveTransportFailure) {
            const char* eff = nullptr;
            curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff);
            const bool badEdge = hostLooksLikeBadArchiveEdge(eff) || hostLooksLikeBadArchiveEdge(activeUrl.c_str());
            if (badEdge || !archiveMetaTried) {
                if (!archiveMetaTried) {
                    archiveMetaTried = true;
                    buildArchiveAlternateUrls(url, archiveAltUrls);
                    archiveAltIndex = 0;
                }
                if (archiveAltIndex < archiveAltUrls.size()) {
                    const std::string previousArchiveUrl = activeUrl;
                    activeUrl = archiveAltUrls[archiveAltIndex++];
                    char sw[360];
                    sceClibSnprintf(sw, sizeof(sw),
                        "archive failover switch curl=%d ssl_verify=%ld -> %s",
                        static_cast<int>(result), lastSslVerifyResult, activeUrl.c_str());
                    httpDiagnostic(sw);
                    char detailedSwitch[1700];
                    sceClibSnprintf(detailedSwitch, sizeof(detailedSwitch),
                        "FAILOVER reason=transport curl=%d curl_text=%s tls_like=%d ssl_verify=%ld bad_edge=%d from=%s to=%s effective_before=%s detail=%s next_index=%u remaining=%u",
                        static_cast<int>(result), curl_easy_strerror(result), tlsLikeFailure ? 1 : 0,
                        lastSslVerifyResult, badEdge ? 1 : 0,
                        previousArchiveUrl.c_str(), activeUrl.c_str(), eff && *eff ? eff : "-",
                        curlError[0] ? curlError : "-",
                        (unsigned int)archiveAltIndex,
                        (unsigned int)(archiveAltUrls.size() > archiveAltIndex ? archiveAltUrls.size() - archiveAltIndex : 0));
                    archiveNodeDiagnostic(detailedSwitch);
                    curl_easy_setopt(curl, CURLOPT_URL, activeUrl.c_str());
                    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
                    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
                    curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 0L);
                    // Keep going even if this result was going to break non-retryable.
                    continue;
                }
            }
        }

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
    lastEtag_ = ctx.etag;
    lastModified_ = ctx.lastModified;
    if (ctx.resumeOffset > 0 && responseCode == 206) lastRangeAccepted_ = true;

    curl_slist_free_all(headers);
    sceIoClose(ctx.fd);
    ctx.fd = -1;
    curl_easy_cleanup(curl);

    char resultMsg[1200];
    sceClibSnprintf(
        resultMsg,
        sizeof(resultMsg),
        "RESULT curl=%d status=%ld bytes=%llu absolute=%llu total=%llu speed=%llu range=%d restarted=%d redirects=%ld ssl_verify=%ld tls_like=%d effective_url=%s curl_detail=%s",
        static_cast<int>(result),
        responseCode,
        (unsigned long long)ctx.downloaded,
        (unsigned long long)(ctx.resumeOffset + ctx.downloaded),
        (unsigned long long)ctx.total,
        (unsigned long long)ctx.bytesPerSecond,
        lastRangeAccepted_ ? 1 : 0,
        ctx.restartedFromZero ? 1 : 0,
        redirectCount,
        lastSslVerifyResult,
        lastTlsLikeFail ? 1 : 0,
        effectiveUrlCopy[0] ? effectiveUrlCopy : "-",
        curlError[0] ? curlError : "-");
    httpDiagnostic(resultMsg);
    if (isArchive) archiveNodeDiagnostic(std::string("FINAL_TRANSFER ") + resultMsg);

    if (ctx.cancelled) {
        if (isArchive) archiveNodeDiagnostic("OUTCOME cancelled_by_user_or_callback");
        setError("cancelled");
        return HttpResult::Cancelled;
    }
    if (result == CURLE_ABORTED_BY_CALLBACK) {
        if (isArchive) archiveNodeDiagnostic("OUTCOME error=transfer_aborted_by_callback");
        setError("transfer aborted");
        return HttpResult::NetworkError;
    }

    if (ctx.ioError) {
        if (isArchive) archiveNodeDiagnostic("OUTCOME error=sceIoWrite_failed");
        setError("sceIoWrite failed");
        return HttpResult::IoError;
    }
    if (result != CURLE_OK) {
        if (isArchive) archiveNodeDiagnostic(std::string("OUTCOME error=curl_") + std::to_string(static_cast<int>(result)) + " " + curl_easy_strerror(result));
        char message[460];
        sceClibSnprintf(
            message,
            sizeof(message),
            "curl error %d: %s%s%s",
            static_cast<int>(result),
            curl_easy_strerror(result),
            curlError[0] ? " | " : "",
            curlError[0] ? curlError : "");
        setError(message);
        return lastTlsLikeFail ? HttpResult::SslError : HttpResult::NetworkError;
    }
    const bool rangeAlreadyComplete =
        responseCode == 416 &&
        ctx.resumeOffset > 0 &&
        ctx.downloaded == 0 &&
        ctx.totalFromContentRange &&
        ctx.resumeOffset == ctx.total;
    if (!rangeAlreadyComplete && responseCode != 200 && responseCode != 206) {
        if (isArchive) archiveNodeDiagnostic(std::string("OUTCOME error=http_status_") + std::to_string(responseCode));
        char message[96];
        sceClibSnprintf(message, sizeof(message), "HTTP status %ld", responseCode);
        setError(message);
        return HttpResult::HttpError;
    }

    if (rangeAlreadyComplete) {
        httpDiagnostic("download already complete according to HTTP 416 Content-Range");
    }

    // Finalize FD before size check / rename by caller.
    if (ctx.fd >= 0) {
        sceIoClose(ctx.fd);
        ctx.fd = -1;
    }

    // Strict size check against authoritative remote total (Content-Range TOTAL or Content-Length).
    {
        SceIoStat st{};
        const int stRes = sceIoGetstat(destinationPath.c_str(), &st);
        const uint64_t localSize = (stRes >= 0) ? static_cast<uint64_t>(st.st_size) : 0ULL;
        const uint64_t absoluteDownloaded = rangeAlreadyComplete
            ? ctx.resumeOffset
            : (ctx.resumeOffset + ctx.downloaded);

        char sizeMsg[280];
        sceClibSnprintf(sizeMsg, sizeof(sizeMsg),
            "finalize local=%llu absolute=%llu remoteTotal=%llu fromRange=%d status=%ld",
            (unsigned long long)localSize,
            (unsigned long long)absoluteDownloaded,
            (unsigned long long)ctx.total,
            ctx.totalFromContentRange ? 1 : 0,
            responseCode);
        httpDiagnostic(sizeMsg);

        if (ctx.total > 0 && localSize != ctx.total) {
            char err[220];
            sceClibSnprintf(err, sizeof(err),
                "size mismatch after download local=%llu remote=%llu — refusing complete",
                (unsigned long long)localSize,
                (unsigned long long)ctx.total);
            setError(err);
            httpDiagnostic(err);
            if (isArchive) archiveNodeDiagnostic(std::string("OUTCOME error=size_mismatch ") + err);
            // Leave .part in place for diagnostics; caller must not treat as success.
            return HttpResult::NetworkError;
        }
    }

    if (isArchive) {
        char success[1500];
        const uint64_t finalKib = ctx.bytesPerSecond / 1024ULL;
        const uint64_t finalMibHundredths = (ctx.bytesPerSecond * 100ULL) / (1024ULL * 1024ULL);
        sceClibSnprintf(success, sizeof(success),
            "OUTCOME success HTTP=%ld absolute=%llu remote_total=%llu sampled_final_speed_Bps=%llu sampled_final_speed_KiBps=%llu sampled_final_speed_MiBps=%llu.%02llu redirects=%ld selected_or_active_url=%s effective_url=%s",
            responseCode,
            (unsigned long long)(ctx.resumeOffset + ctx.downloaded),
            (unsigned long long)ctx.total,
            (unsigned long long)ctx.bytesPerSecond,
            (unsigned long long)finalKib,
            (unsigned long long)(finalMibHundredths / 100ULL),
            (unsigned long long)(finalMibHundredths % 100ULL),
            redirectCount, activeUrl.c_str(),
            effectiveUrlCopy[0] ? effectiveUrlCopy : "-");
        archiveNodeDiagnostic(success);
        archiveNodeDiagnostic("============================================================");
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

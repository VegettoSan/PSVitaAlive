#include "network/parallel_archive_download.hpp"

#include "diagnostic_logger.hpp"
#include "storage/storage_manager.hpp"

#include <curl/curl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <strings.h>
#ifndef PSVITAALIVE_CURL_MBEDTLS
#include <openssl/sha.h>
#endif

namespace psvitaalive {
namespace {

constexpr size_t kParts = 4;
constexpr uint64_t kMinimumBytes = 4ULL * 1024ULL * 1024ULL;
constexpr uint64_t kDiskReserve = 32ULL * 1024ULL * 1024ULL;
// Large files were spending extra time joining four sequential part files.
// 256 KiB reduces read/write syscalls, without allocating the archive in RAM.
constexpr size_t kJoinBuffer = 256 * 1024;
constexpr const char* kUserAgent =
    "PSVitaAlive/1.14 (PlayStation Vita; +https://github.com/VegettoSan/PSVitaAlive)";

struct Session {
    const HttpProgressFn* progress = nullptr;
    const HttpCancelFn* cancel = nullptr;
    uint64_t size = 0;
    uint64_t received = 0;
    uint64_t startUs = 0;
    uint64_t lastReportUs = 0;
    bool stopped = false;
};

struct Part {
    Session* session = nullptr;
    CURL* easy = nullptr;
    curl_slist* headers = nullptr;
    SceUID fd = -1;
    std::string path;
    std::string range;
    std::string etag;
    std::string lastModified;
    uint64_t start = 0;
    uint64_t end = 0;
    uint64_t received = 0;
    long status = 0;
    bool validRange = false;
    bool ioError = false;
    bool rangeError = false;
    bool added = false;
    bool done = false;
    CURLcode code = CURLE_OK;
    char error[CURL_ERROR_SIZE] = {};
};

static uint64_t monotonicUs() {
    return sceKernelGetProcessTimeWide();
}

static bool isCancelled(const Session& s) {
    return s.cancel && *s.cancel && (*s.cancel)();
}

static bool startsWithHeader(const char* buf, size_t bytes, const char* prefix) {
    const size_t len = std::strlen(prefix);
    return bytes >= len && strncasecmp(buf, prefix, len) == 0;
}

static std::string headerText(const char* buf, size_t bytes, size_t prefix) {
    size_t from = prefix;
    while (from < bytes && (buf[from] == ' ' || buf[from] == '\t')) ++from;
    size_t end = bytes;
    while (end > from && (buf[end - 1] == '\r' || buf[end - 1] == '\n' ||
                           buf[end - 1] == ' ' || buf[end - 1] == '\t')) --end;
    return std::string(buf + from, end - from);
}

static size_t onHeader(char* ptr, size_t size, size_t nmemb, void* userdata) {
    Part* p = static_cast<Part*>(userdata);
    const size_t n = size * nmemb;
    if (!p || !n) return n;
    if (startsWithHeader(ptr, n, "HTTP/")) {
        p->status = 0;
        p->validRange = false;
        p->etag.clear();
        p->lastModified.clear();
        const std::string line(ptr, n);
        long code = 0;
        if (std::sscanf(line.c_str(), "HTTP/%*s %ld", &code) == 1)
            p->status = code;
    } else if (startsWithHeader(ptr, n, "Content-Range:")) {
        const std::string value = headerText(ptr, n, std::strlen("Content-Range:"));
        unsigned long long begin = 0, end = 0, total = 0;
        if (std::sscanf(value.c_str(), "bytes %llu-%llu/%llu", &begin, &end, &total) == 3) {
            p->validRange = begin == p->start && end == p->end &&
                            total == p->session->size;
        }
    } else if (startsWithHeader(ptr, n, "ETag:")) {
        p->etag = headerText(ptr, n, std::strlen("ETag:"));
    } else if (startsWithHeader(ptr, n, "Last-Modified:")) {
        p->lastModified = headerText(ptr, n, std::strlen("Last-Modified:"));
    }
    return n;
}

static void sendProgress(Session& s, bool finished = false) {
    if (!s.progress || !*s.progress) return;
    const uint64_t now = monotonicUs();
    if (!finished && now - s.lastReportUs < 250000ULL) return;
    s.lastReportUs = now;
    HttpProgress p;
    // Hold at 99% until the part files have been assembled and checked.
    p.absoluteDownloaded = finished ? s.size
        : (s.received >= s.size ? s.size - 1 : s.received);
    p.downloaded = p.absoluteDownloaded;
    p.total = s.size;
    const uint64_t elapsed = now > s.startUs ? now - s.startUs : 1;
    p.bytesPerSecond = (s.received / (elapsed / 1000ULL + 1)) * 1000ULL;
    (*s.progress)(p);
}

static size_t onWrite(char* data, size_t size, size_t nmemb, void* userdata) {
    Part* p = static_cast<Part*>(userdata);
    const size_t n = size * nmemb;
    if (!p || !n) return n;
    Session& s = *p->session;
    if (isCancelled(s)) {
        s.stopped = true;
        return 0;
    }
    // Never accept a full 200 response to a Range request or an error page.
    const uint64_t length = p->end - p->start + 1;
    if (p->status != 206 || !p->validRange ||
        p->received > length || n > length - p->received) {
        p->rangeError = true;
        return 0;
    }
    size_t written = 0;
    while (written < n) {
        const int rc = sceIoWrite(p->fd, data + written,
                                  static_cast<unsigned int>(n - written));
        if (rc <= 0) {
            p->ioError = true;
            return 0;
        }
        written += static_cast<size_t>(rc);
    }
    p->received += static_cast<uint64_t>(n);
    s.received += static_cast<uint64_t>(n);
    sendProgress(s);
    return n;
}

static int onTransfer(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    Part* p = static_cast<Part*>(userdata);
    if (p && isCancelled(*p->session)) {
        p->session->stopped = true;
        return 1;
    }
    return 0;
}

static std::string partPath(const std::string& base, size_t part) {
    return part == 0 ? base : base + ".range" + std::to_string(part);
}

static void clearPartFiles(const std::string& base, bool includeFirst) {
    for (size_t i = includeFirst ? 0 : 1; i < kParts; ++i)
        sceIoRemove(partPath(base, i).c_str());
}

static bool writeAll(SceUID fd, const unsigned char* bytes, size_t n) {
    size_t pos = 0;
    while (pos < n) {
        const int wrote = sceIoWrite(fd, bytes + pos, static_cast<unsigned int>(n - pos));
        if (wrote <= 0) return false;
        pos += static_cast<size_t>(wrote);
    }
    return true;
}

static bool joinParts(const std::string& base, uint64_t total) {
    std::unique_ptr<unsigned char[]> buf(new (std::nothrow) unsigned char[kJoinBuffer]);
    if (!buf) return false;
    SceUID dst = sceIoOpen(base.c_str(), SCE_O_WRONLY | SCE_O_APPEND, 0777);
    if (dst < 0) return false;
    bool ok = true;
    for (size_t i = 1; i < kParts && ok; ++i) {
        const std::string file = partPath(base, i);
        SceUID src = sceIoOpen(file.c_str(), SCE_O_RDONLY, 0);
        if (src < 0) { ok = false; break; }
        for (;;) {
            const int count = sceIoRead(src, buf.get(), static_cast<unsigned int>(kJoinBuffer));
            if (count < 0) { ok = false; break; }
            if (count == 0) break;
            if (!writeAll(dst, buf.get(), static_cast<size_t>(count))) {
                ok = false;
                break;
            }
        }
        sceIoClose(src);
        if (ok) sceIoRemove(file.c_str());
    }
    sceIoClose(dst);
    SceIoStat stat{};
    if (!ok || sceIoGetstat(base.c_str(), &stat) < 0 ||
        static_cast<uint64_t>(stat.st_size) != total) return false;
    return true;
}

// A checksum prevents accidentally installing corrupted HTTP payloads.
// It is not proof of authenticity if the metadata source itself is untrusted.
static bool matchesSha1(const std::string& path, const std::string& expected) {
#ifdef PSVITAALIVE_CURL_MBEDTLS
    (void)path;
    (void)expected;
    return false; // Keep the HTTPS-only fallback for the optional TLS backend.
#else
    if (expected.size() != SHA_DIGEST_LENGTH * 2) return false;
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) return false;
    std::unique_ptr<unsigned char[]> buffer(new (std::nothrow) unsigned char[64 * 1024]);
    if (!buffer) { sceIoClose(fd); return false; }
    SHA_CTX sha{};
    SHA1_Init(&sha);
    bool ok = true;
    for (;;) {
        const int n = sceIoRead(fd, buffer.get(), 64 * 1024);
        if (n < 0) { ok = false; break; }
        if (n == 0) break;
        SHA1_Update(&sha, buffer.get(), static_cast<size_t>(n));
    }
    sceIoClose(fd);
    if (!ok) return false;
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1_Final(digest, &sha);
    static const char* hex = "0123456789abcdef";
    for (size_t i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        const unsigned char a = static_cast<unsigned char>(expected[i * 2]);
        const unsigned char b = static_cast<unsigned char>(expected[i * 2 + 1]);
        const unsigned char actualA = hex[(digest[i] >> 4) & 15];
        const unsigned char actualB = hex[digest[i] & 15];
        auto asciiLower = [](unsigned char x) -> unsigned char {
            return (x >= 'A' && x <= 'F') ? static_cast<unsigned char>(x + 32) : x;
        };
        if (asciiLower(a) != actualA || asciiLower(b) != actualB) return false;
    }
    return true;
#endif
}

static bool isDirectHttp(const std::string& url) {
    return url.compare(0, 7, "http://") == 0;
}

// Redirected parallel parts must remain on Archive's own storage hosts.
static bool usableArchiveFinalUrl(const char* rawUrl, bool requireHttp) {
    if (!rawUrl) return false;
    const std::string url(rawUrl);
    const size_t offset = url.compare(0, 7, "http://") == 0 ? 7 :
        (url.compare(0, 8, "https://") == 0 ? 8 : 0);
    if (!offset || (requireHttp && offset != 7)) return false;
    const size_t end = url.find_first_of("/?#:", offset);
    const std::string host = url.substr(offset, end == std::string::npos ?
        std::string::npos : end - offset);
    if (host == "archive.org") return true;
    static const std::string suffix = ".archive.org";
    return host.size() > suffix.size() &&
           host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static void closeParts(CURLM* multi, std::array<Part, kParts>& parts) {
    for (Part& p : parts) {
        if (p.easy) {
            if (multi && p.added) curl_multi_remove_handle(multi, p.easy);
            curl_easy_cleanup(p.easy);
            p.easy = nullptr;
        }
        if (p.headers) curl_slist_free_all(p.headers);
        p.headers = nullptr;
        if (p.fd >= 0) sceIoClose(p.fd);
        p.fd = -1;
    }
    if (multi) curl_multi_cleanup(multi);
}

} // namespace

ParallelArchiveOutcome downloadArchiveRanges(
    const std::string& url,
    const std::string& destination,
    uint64_t authoritativeSize,
    const HttpProgressFn& onProgress,
    const HttpCancelFn& shouldCancel,
    const std::string& expectedSha1
) {
    if (authoritativeSize < kMinimumBytes || url.empty()) return ParallelArchiveOutcome::Skipped;
    // Never accept a plain HTTP payload without an exact expected checksum.
    if (isDirectHttp(url) && expectedSha1.size() != 40)
        return ParallelArchiveOutcome::Skipped;

    // Leave headroom for part assembly and installation. If storage is
    // tight, retain the existing low-space-guarded single-stream path.
    uint64_t freeBytes = 0, capacity = 0;
    const uint64_t extra = authoritativeSize - authoritativeSize / kParts;
    if (!StorageManager::queryUx0Space(freeBytes, capacity) ||
        extra > UINT64_MAX - kDiskReserve ||
        authoritativeSize > UINT64_MAX - extra - kDiskReserve ||
        freeBytes < authoritativeSize + extra + kDiskReserve) {
        diagnostics::log("[HTTP] range download skipped: insufficient staging space");
        return ParallelArchiveOutcome::Skipped;
    }

    CURLM* multi = curl_multi_init();
    if (!multi) return ParallelArchiveOutcome::Skipped;
    std::array<Part, kParts> parts{};
    Session session;
    session.progress = &onProgress;
    session.cancel = &shouldCancel;
    session.size = authoritativeSize;
    session.startUs = monotonicUs();
    bool setupOk = true;
    bool setupIoError = false;
    const uint64_t chunk = authoritativeSize / kParts;
    for (size_t i = 0; i < kParts; ++i) {
        Part& p = parts[i];
        p.session = &session;
        p.start = i * chunk;
        p.end = (i + 1 == kParts) ? authoritativeSize - 1 : ((i + 1) * chunk - 1);
        p.path = partPath(destination, i);
        p.range = std::to_string(p.start) + "-" + std::to_string(p.end);
        p.fd = sceIoOpen(p.path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        if (p.fd < 0) { setupIoError = true; setupOk = false; break; }
        p.easy = curl_easy_init();
        if (!p.easy) { setupOk = false; break; }
        p.headers = curl_slist_append(p.headers, "Accept: */*");
        p.headers = curl_slist_append(p.headers, "Accept-Encoding: identity");
        p.headers = curl_slist_append(p.headers, "Referer: https://archive.org/");
        if (!p.headers) { setupOk = false; break; }
        curl_easy_setopt(p.easy, CURLOPT_URL, url.c_str());
        curl_easy_setopt(p.easy, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(p.easy, CURLOPT_RANGE, p.range.c_str());
        curl_easy_setopt(p.easy, CURLOPT_HTTPHEADER, p.headers);
        curl_easy_setopt(p.easy, CURLOPT_USERAGENT, kUserAgent);
        curl_easy_setopt(p.easy, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(p.easy, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(p.easy, CURLOPT_SSLVERSION, CURL_SSLVERSION_DEFAULT);
        curl_easy_setopt(p.easy, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
        curl_easy_setopt(p.easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
        curl_easy_setopt(p.easy, CURLOPT_CONNECTTIMEOUT, 20L);
        curl_easy_setopt(p.easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(p.easy, CURLOPT_LOW_SPEED_TIME, 60L);
        curl_easy_setopt(p.easy, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(p.easy, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(p.easy, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(p.easy, CURLOPT_BUFFERSIZE, 64L * 1024L);
#if LIBCURL_VERSION_NUM >= 0x075500
        curl_easy_setopt(p.easy, CURLOPT_PROTOCOLS_STR, "http,https");
        curl_easy_setopt(p.easy, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
        curl_easy_setopt(p.easy, CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
        curl_easy_setopt(p.easy, CURLOPT_REDIR_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
        curl_easy_setopt(p.easy, CURLOPT_WRITEFUNCTION, onWrite);
        curl_easy_setopt(p.easy, CURLOPT_WRITEDATA, &p);
        curl_easy_setopt(p.easy, CURLOPT_HEADERFUNCTION, onHeader);
        curl_easy_setopt(p.easy, CURLOPT_HEADERDATA, &p);
        curl_easy_setopt(p.easy, CURLOPT_ERRORBUFFER, p.error);
        curl_easy_setopt(p.easy, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(p.easy, CURLOPT_XFERINFOFUNCTION, onTransfer);
        curl_easy_setopt(p.easy, CURLOPT_XFERINFODATA, &p);
        if (curl_multi_add_handle(multi, p.easy) != CURLM_OK) {
            setupOk = false;
            break;
        }
        p.added = true;
    }

    bool networkOk = setupOk;
    if (networkOk) {
        diagnostics::log("[HTTP] starting 4-range parallel payload download");
        int active = 0;
        CURLMcode mr;
        do { mr = curl_multi_perform(multi, &active); }
        while (mr == CURLM_CALL_MULTI_PERFORM);
        if (mr != CURLM_OK) networkOk = false;

        while (networkOk && active > 0) {
            if (isCancelled(session)) { session.stopped = true; break; }
            // Short bounded sleeps keep cancellation responsive without a
            // platform-specific select/poll assumption in VitaSDK's libcurl.
            sceKernelDelayThread(25000);
            do { mr = curl_multi_perform(multi, &active); }
            while (mr == CURLM_CALL_MULTI_PERFORM);
            if (mr != CURLM_OK) networkOk = false;
        }

        int messages = 0;
        CURLMsg* msg = nullptr;
        while ((msg = curl_multi_info_read(multi, &messages)) != nullptr) {
            if (msg->msg != CURLMSG_DONE) continue;
            for (Part& p : parts) {
                if (p.easy == msg->easy_handle) {
                    p.done = true;
                    p.code = msg->data.result;
                    const char* reached = nullptr;
                    curl_easy_getinfo(p.easy, CURLINFO_EFFECTIVE_URL, &reached);
                    if (!usableArchiveFinalUrl(reached, isDirectHttp(url)))
                        p.rangeError = true;
                    break;
                }
            }
        }
        for (const Part& p : parts) {
            if (!p.done || p.code != CURLE_OK || p.status != 206 ||
                !p.validRange || p.rangeError || p.ioError ||
                p.received != p.end - p.start + 1) {
                networkOk = false;
            }
        }
        // Reject inconsistent replicas or an object replaced mid-download.
        if (networkOk) {
            for (size_t i = 1; i < kParts; ++i) {
                if (parts[i].etag != parts[0].etag ||
                    parts[i].lastModified != parts[0].lastModified) {
                    networkOk = false;
                    break;
                }
            }
        }
    }

    bool ioError = setupIoError;
    for (const Part& p : parts) ioError = ioError || p.ioError;
    closeParts(multi, parts);
    if (session.stopped || isCancelled(session)) {
        clearPartFiles(destination, true);
        diagnostics::log("[HTTP] parallel download cancelled");
        return ParallelArchiveOutcome::Cancelled;
    }
    if (ioError) {
        clearPartFiles(destination, true);
        diagnostics::log("[HTTP] parallel download storage error");
        return ParallelArchiveOutcome::IoError;
    }
    if (!networkOk) {
        clearPartFiles(destination, true);
        diagnostics::log("[HTTP] parallel ranges unavailable; falling back to serial");
        return ParallelArchiveOutcome::RetrySerial;
    }

    if (!joinParts(destination, authoritativeSize)) {
        clearPartFiles(destination, true);
        diagnostics::log("[HTTP] parallel merge failed");
        return ParallelArchiveOutcome::IoError;
    }
    // Check the actual merged payload (not just the lengths of the ranges).
    // If it disagrees with the metadata checksum, force a fresh HTTPS download.
    if (isDirectHttp(url) && !matchesSha1(destination, expectedSha1)) {
        clearPartFiles(destination, true);
        diagnostics::log("[HTTP] direct HTTP checksum mismatch; retry using HTTPS");
        return ParallelArchiveOutcome::RetrySerial;
    }
    sendProgress(session, true);
    diagnostics::log("[HTTP] parallel payload validated and joined");
    return ParallelArchiveOutcome::Complete;
}

} // namespace psvitaalive

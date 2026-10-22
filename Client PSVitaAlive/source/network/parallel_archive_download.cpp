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

namespace psvitaalive {
namespace {

constexpr size_t kParts = 4;
// Files under 1 MiB are cheaper to download serially than to split/join.
constexpr uint64_t kMinimumBytes = 1ULL * 1024ULL * 1024ULL;
constexpr uint64_t kFourWayBytes = 8ULL * 1024ULL * 1024ULL;
constexpr uint64_t kDiskReserve = 32ULL * 1024ULL * 1024ULL;
// Large files were spending extra time joining four sequential part files.
// Larger sequential assembly batches reduce ux0 read/write syscalls for
// multi-gigabyte archives. The file is never loaded fully into RAM.
constexpr size_t kJoinBuffer = 1024 * 1024;
// Sequential per-range writes with a bounded buffer reduce costly ux0 I/O.
// A 256 KiB buffer per stream uses at most 1 MiB additional heap RAM.
constexpr size_t kPartWriteBuffer = 256 * 1024;
// Tiny cooperative yields during sustained ux0 disk writes, without
// reducing the parallel connection count or changing transfer integrity.
constexpr uint64_t kIoYieldStep = 4ULL * 1024ULL * 1024ULL;
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
    std::unique_ptr<unsigned char[]> writeBuffer;
    size_t buffered = 0;
    uint64_t bytesSinceYield = 0;
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

static void sendProgress(Session& s, bool finished = false, bool finalizing = false) {
    if (!s.progress || !*s.progress) return;
    const uint64_t now = monotonicUs();
    if (!finished && !finalizing && now - s.lastReportUs < 250000ULL) return;
    s.lastReportUs = now;
    HttpProgress p;
    // Hold at 99% only when assembly is required; direct ZIP/VPK skips it.
    p.absoluteDownloaded = finished ? s.size
        : (s.received >= s.size ? s.size - 1 : s.received);
    p.downloaded = p.absoluteDownloaded;
    p.total = s.size;
    const uint64_t elapsed = now > s.startUs ? now - s.startUs : 1;
    p.bytesPerSecond = finalizing ? 0 : (s.received / (elapsed / 1000ULL + 1)) * 1000ULL;
    p.finalizing = finalizing;
    (*s.progress)(p);
}

static bool flushPartBuffer(Part& p) {
    size_t offset = 0;
    while (offset < p.buffered) {
        const int wrote = sceIoWrite(p.fd, p.writeBuffer.get() + offset,
                                    static_cast<unsigned int>(p.buffered - offset));
        if (wrote <= 0) { p.ioError = true; return false; }
        offset += static_cast<size_t>(wrote);
    }
    p.bytesSinceYield += p.buffered;
    p.buffered = 0;
    if (p.bytesSinceYield >= kIoYieldStep) {
        p.bytesSinceYield = 0;
        sceKernelDelayThread(1000);
    }
    return true;
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
    size_t copied = 0;
    while (copied < n) {
        if (p->buffered == kPartWriteBuffer && !flushPartBuffer(*p))
            return 0;
        const size_t available = kPartWriteBuffer - p->buffered;
        const size_t take = (n - copied < available) ? (n - copied) : available;
        std::memcpy(p->writeBuffer.get() + p->buffered, data + copied, take);
        p->buffered += take;
        copied += take;
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

// Assemble sequentially when a non-ZIP payload still needs a single file.
static bool joinParts(
    const std::string& base,
    uint64_t total,
    size_t partCount,
    bool preservePartsUntilComplete
) {
    size_t joinChunk = kJoinBuffer;
    std::unique_ptr<unsigned char[]> buf(new (std::nothrow) unsigned char[joinChunk]);
    if (!buf) {
        // RAM is tight on some Vita configurations; preserve the legacy
        // 256 KiB assembly path rather than failing a valid 2 GB download.
        joinChunk = 256 * 1024;
        buf.reset(new (std::nothrow) unsigned char[joinChunk]);
    }
    if (!buf) return false;

    SceUID dst = sceIoOpen(base.c_str(), SCE_O_WRONLY | SCE_O_APPEND, 0777);
    if (dst < 0) return false;
    bool ok = true;
    const uint64_t joinStartedUs = sceKernelGetProcessTimeWide();
    uint64_t joinedSinceYield = 0;
    for (size_t i = 1; i < partCount && ok; ++i) {
        const std::string file = partPath(base, i);
        SceUID src = sceIoOpen(file.c_str(), SCE_O_RDONLY, 0);
        if (src < 0) { ok = false; break; }
        for (;;) {
            const int count = sceIoRead(src, buf.get(), static_cast<unsigned int>(joinChunk));
            if (count < 0) { ok = false; break; }
            if (count == 0) break;
            if (!writeAll(dst, buf.get(), static_cast<size_t>(count))) {
                ok = false;
                break;
            }
            joinedSinceYield += static_cast<uint64_t>(count);
            if (joinedSinceYield >= kIoYieldStep) {
                joinedSinceYield = 0;
                sceKernelDelayThread(1000);
            }
        }
        sceIoClose(src);
        if (ok && !preservePartsUntilComplete) sceIoRemove(file.c_str());
    }
    sceIoClose(dst);
    diagnostics::log(std::string("[HTTP] ranges appended elapsed_ms=") +
                     std::to_string((sceKernelGetProcessTimeWide() - joinStartedUs) / 1000ULL));
    SceIoStat stat{};
    if (!ok || sceIoGetstat(base.c_str(), &stat) < 0 ||
        static_cast<uint64_t>(stat.st_size) != total) return false;
    // Recovery must never delete a range before the complete archive exists.
    if (preservePartsUntilComplete) {
        for (size_t i = 1; i < partCount; ++i)
            sceIoRemove(partPath(base, i).c_str());
    }

    return true;
}


// Accept range responses only over HTTPS and from the expected Archive family.
static bool usableArchiveFinalUrl(const char* rawUrl, bool archiveOnly) {
    if (!rawUrl) return false;
    const std::string url(rawUrl);
    if (url.compare(0, 8, "https://") != 0) return false;
    if (!archiveOnly) return true;
    const size_t offset = 8;
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
    const std::string& expectedEtag,
    const std::string& expectedModified,
    bool keepZipParts
) {
    if (authoritativeSize < kMinimumBytes || url.empty()) return ParallelArchiveOutcome::Skipped;
    const bool archiveOnly = url.find("archive.org/") != std::string::npos;
    const size_t partCount = authoritativeSize < kFourWayBytes ? 2 : kParts;
    // Without a whole-file digest, never use unencrypted HTTP for payloads.
    if (url.compare(0, 8, "https://") != 0) return ParallelArchiveOutcome::Skipped;

    // Directly extracted ZIP/VPK containers do not need space for an
    // assembled duplicate. Joined formats keep the original conservative
    // staging budget; low-space cases fall back to the serial download path.
    uint64_t freeBytes = 0, capacity = 0;
    const uint64_t extra = keepZipParts
        ? 0 : (authoritativeSize - authoritativeSize / partCount);
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
    const uint64_t chunk = authoritativeSize / partCount;
    for (size_t i = 0; i < partCount; ++i) {
        Part& p = parts[i];
        p.session = &session;
        p.start = i * chunk;
        p.end = (i + 1 == partCount) ? authoritativeSize - 1 : ((i + 1) * chunk - 1);
        p.path = partPath(destination, i);
        p.range = std::to_string(p.start) + "-" + std::to_string(p.end);
        p.fd = sceIoOpen(p.path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        if (p.fd < 0) { setupIoError = true; setupOk = false; break; }
        p.writeBuffer.reset(new (std::nothrow) unsigned char[kPartWriteBuffer]);
        if (!p.writeBuffer) { setupOk = false; break; }
        p.easy = curl_easy_init();
        if (!p.easy) { setupOk = false; break; }
        p.headers = curl_slist_append(p.headers, "Accept: */*");
        p.headers = curl_slist_append(p.headers, "Accept-Encoding: identity");
        if (archiveOnly) p.headers = curl_slist_append(p.headers, "Referer: https://archive.org/");
        if (!p.headers) { setupOk = false; break; }
        curl_easy_setopt(p.easy, CURLOPT_URL, url.c_str());
        curl_easy_setopt(p.easy, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(p.easy, CURLOPT_RANGE, p.range.c_str());
        curl_easy_setopt(p.easy, CURLOPT_HTTPHEADER, p.headers);
        curl_easy_setopt(p.easy, CURLOPT_USERAGENT, kUserAgent);
        curl_easy_setopt(p.easy, CURLOPT_SSL_VERIFYPEER, 0L); // Legacy Vita TLS policy; separate hardening needed
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
        curl_easy_setopt(p.easy, CURLOPT_PROTOCOLS_STR, "https");
        curl_easy_setopt(p.easy, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
        curl_easy_setopt(p.easy, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
        curl_easy_setopt(p.easy, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
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
        diagnostics::log(std::string("[HTTP] starting ") + std::to_string(partCount) + "-range parallel payload download");
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
                    if (!usableArchiveFinalUrl(reached, archiveOnly))
                        p.rangeError = true;
                    break;
                }
            }
        }
        for (size_t i = 0; i < partCount; ++i) {
            const Part& p = parts[i];
            if (!p.done || p.code != CURLE_OK || p.status != 206 ||
                !p.validRange || p.rangeError || p.ioError ||
                p.received != p.end - p.start + 1) {
                networkOk = false;
            }
        }
        // Only commit buffered tails after all four byte ranges have finished.
        // Any incomplete response is discarded instead of being assembled.
        if (networkOk) {
            for (size_t i = 0; i < partCount; ++i) {
                Part& p = parts[i];
                if (!flushPartBuffer(p)) {
                    networkOk = false;
                    break;
                }
            }
        }
        // Generic HTTPS ranges must match the validator from the initial
        // successful 206 probe; never combine versions of a changing file.
        if (networkOk && (!expectedEtag.empty() || !expectedModified.empty())) {
            for (size_t i = 0; i < partCount; ++i) {
                if ((!expectedEtag.empty() && parts[i].etag != expectedEtag) ||
                    (!expectedModified.empty() &&
                     parts[i].lastModified != expectedModified)) {
                    networkOk = false;
                    break;
                }
            }
        }
        // Reject inconsistent replicas or an object replaced mid-download.
        if (networkOk) {
            for (size_t i = 1; i < partCount; ++i) {
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

    // Compressed ZIP/VPKs are consumed directly from their validated 206
    // ranges. Do not assemble or re-read the archive after download.
    if (keepZipParts) {
        diagnostics::log("[HTTP] segmented ZIP/VPK range lengths verified; join skipped");
        sendProgress(session, true);
        return ParallelArchiveOutcome::Segmented;
    }

    // Non-archive payloads may still require one contiguous file.
    sendProgress(session, false, true);
    diagnostics::log("[HTTP] parallel transfer complete; joining required non-ZIP payload");
    if (!joinParts(destination, authoritativeSize, partCount, false)) {
        clearPartFiles(destination, true);
        diagnostics::log("[HTTP] parallel merge failed");
        return ParallelArchiveOutcome::IoError;
    }
    sendProgress(session, true);
    diagnostics::log("[HTTP] parallel payload joined; file length verified");
    return ParallelArchiveOutcome::Complete;
}

bool materializeSegmentedZip(const std::string& base, uint64_t expectedSize) {
    SceIoStat stat{};
    const size_t partCount = sceIoGetstat(partPath(base, 3).c_str(), &stat) >= 0 ? 4 : 2;
    if (sceIoGetstat(partPath(base, 1).c_str(), &stat) < 0) return false;
    const bool ok = joinParts(base, expectedSize, partCount, true);
    diagnostics::log(ok ? "[HTTP] recovery ZIP materialized"
                        : "[HTTP] recovery ZIP materialization failed");
    return ok;
}

} // namespace psvitaalive

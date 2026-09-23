#!/usr/bin/env python3
"""Apply proactive Internet Archive storage-node selection to the Vita client.

This migration script is intentionally one-shot. The temporary workflow that runs it
removes both itself and this script after the client build succeeds.
"""
from pathlib import Path

CPP = Path("Client PSVitaAlive/source/network/http_client.cpp")
NETWORK_DOC = Path("docs/NETWORK_TLS.md")
RESILIENCE_DOC = Path("docs/DOWNLOAD_RESILIENCE.md")
CLIENT_README = Path("Client PSVitaAlive/README.md")
ROOT_README = Path("README.md")

cpp = CPP.read_text(encoding="utf-8")

helper_anchor = "static void updateSpeed(TransferContext* ctx) {"
if helper_anchor not in cpp:
    raise SystemExit("http_client.cpp: updateSpeed anchor not found")

helper = r'''// Small sequential probes let fresh, large Internet Archive payloads avoid a
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
    bool ok = false;
    uint64_t bytes = 0;
    uint64_t total = 0;
    uint64_t bytesPerSecond = 0;
    uint64_t elapsedUs = 0;
    long status = 0;
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

    curl_slist_free_all(headers);
    curl_easy_cleanup(p);

    out.bytes = ctx.bytes;
    out.total = ctx.total;
    out.elapsedUs = elapsedUs;
    out.status = status ? status : ctx.responseCode;
    out.curlCode = rc;
    if (elapsedUs > 0 && ctx.bytes > 0) {
        out.bytesPerSecond = (ctx.bytes * 1000000ULL) / elapsedUs;
    }

    // IA direct storage nodes should honor Range with 206. If a node ignores it,
    // do not select it proactively; the normal download/failover path remains available.
    out.ok = (out.status == 206 && out.bytes > 0 &&
              (rc == CURLE_OK || (rc == CURLE_WRITE_ERROR && ctx.capped)));

    char msg[520];
    sceClibSnprintf(msg, sizeof(msg),
        "archive selector probe ok=%d curl=%d HTTP=%ld bytes=%llu total=%llu us=%llu speed=%llu url=%s",
        out.ok ? 1 : 0,
        static_cast<int>(rc),
        out.status,
        (unsigned long long)out.bytes,
        (unsigned long long)out.total,
        (unsigned long long)out.elapsedUs,
        (unsigned long long)out.bytesPerSecond,
        url.c_str());
    httpDiagnostic(msg);
    return out.ok;
}

// Returns true when a direct Archive storage node was validated and moved to
// urls.front(). Successful large-file probes are ranked fastest-first so existing
// failover automatically tries the next-best measured node after a failure.
static bool rankArchiveAlternateUrls(
    std::vector<std::string>& urls,
    const HttpCancelFn& shouldCancel
) {
    if (urls.empty()) return false;

    // First spend only one byte to learn the authoritative total. Try another
    // candidate if the first storage node is unavailable.
    ArchiveProbeResult sizeProbe;
    size_t responsiveIndex = urls.size();
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

    if (sizeProbe.total > 0 && sizeProbe.total < ARCHIVE_SELECTOR_MIN_BYTES) {
        if (responsiveIndex != 0) std::swap(urls[0], urls[responsiveIndex]);
        char msg[320];
        sceClibSnprintf(msg, sizeof(msg),
            "archive selector: small file total=%llu; use responsive direct node without speed benchmark -> %s",
            (unsigned long long)sizeProbe.total, urls.front().c_str());
        httpDiagnostic(msg);
        return true;
    }

    if (sizeProbe.total == 0) {
        httpDiagnostic("archive selector: remote total unknown; keep normal archive.org path");
        return false;
    }

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

    // Tiny candidate count (normally two): simple stable ordering keeps code small
    // and avoids adding another dependency. Successful nodes come first, then speed.
    for (size_t i = 0; i < probes.size(); ++i) {
        for (size_t j = i + 1; j < probes.size(); ++j) {
            const bool jBetter =
                (probes[j].ok && !probes[i].ok) ||
                (probes[j].ok == probes[i].ok && probes[j].bytesPerSecond > probes[i].bytesPerSecond);
            if (jBetter) std::swap(probes[i], probes[j]);
        }
    }

    urls.clear();
    for (const auto& p : probes) urls.push_back(p.url);

    char chosen[420];
    sceClibSnprintf(chosen, sizeof(chosen),
        "archive selector chose speed=%llu B/s total=%llu -> %s",
        (unsigned long long)probes.front().bytesPerSecond,
        (unsigned long long)probes.front().total,
        probes.front().url.c_str());
    httpDiagnostic(chosen);
    return probes.front().ok;
}

'''

if "rankArchiveAlternateUrls(" not in cpp:
    cpp = cpp.replace(helper_anchor, helper + helper_anchor, 1)

old_state = '''    std::vector<std::string> archiveAltUrls;\n    size_t archiveAltIndex = 0;\n    bool archiveMetaTried = false;\n    std::string activeUrl = url;\n    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {'''
new_state = '''    std::vector<std::string> archiveAltUrls;\n    size_t archiveAltIndex = 0;\n    bool archiveMetaTried = false;\n    std::string activeUrl = url;\n\n    // Fresh payload downloads can avoid a slow Archive redirect before writing any\n    // bytes. maxAttemptsOverride != 0 is used by small image/cache requests, which\n    // deliberately skip this selector to avoid metadata/probe overhead. Resumes also\n    // keep the established URL first so validator/range semantics are unchanged.\n    if (isArchive && resumeOffset == 0 && maxAttemptsOverride == 0) {\n        if (buildArchiveAlternateUrls(url, archiveAltUrls)) {\n            archiveMetaTried = true;\n            if (rankArchiveAlternateUrls(archiveAltUrls, ctx.shouldCancel) && !archiveAltUrls.empty()) {\n                activeUrl = archiveAltUrls.front();\n                archiveAltIndex = 1;\n                char selected[420];\n                sceClibSnprintf(selected, sizeof(selected),\n                    "archive selector start -> %s", activeUrl.c_str());\n                httpDiagnostic(selected);\n            } else {\n                // Metadata candidates remain available for the existing failure-driven\n                // failover, but the first real attempt stays on the canonical URL.\n                archiveAltIndex = 0;\n            }\n        }\n    }\n\n    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {'''

if old_state not in cpp:
    if "archive selector start ->" not in cpp:
        raise SystemExit("http_client.cpp: archive retry-state anchor not found")
else:
    cpp = cpp.replace(old_state, new_state, 1)

CPP.write_text(cpp, encoding="utf-8")

# NETWORK_TLS.md
network = NETWORK_DOC.read_text(encoding="utf-8")
old_network = '''When possible, the client prefers storage nodes that do not look like problematic `dn*` / `.ca` edges.\n\n### Failover triggers'''
new_network = '''When possible, the client prefers storage nodes that do not look like problematic `dn*` / `.ca` edges.\n\n### Proactive node selection for fresh payloads\n\nFor a **fresh payload download** handled by the normal installer path, Archive.org is no longer treated only as failure-driven failover:\n\n1. resolve `server` / `d1` / `d2` once from item metadata;\n2. issue a one-byte Range probe to a direct storage node to learn the authoritative file total;\n3. for files smaller than **16 MiB**, use the first responsive direct node without a speed benchmark;\n4. for files of **16 MiB or larger**, probe each direct candidate **sequentially** with a bounded **256 KiB** Range request;\n5. rank successful candidates by measured bytes/second and start the real transfer on the fastest one;\n6. keep the remaining ranked candidates as the existing failover order.\n\nThe selector is intentionally skipped for resumed downloads and for small image/cache requests that use an explicit retry override. This avoids invalidating resume validators and prevents catalog artwork from paying metadata/benchmark overhead.\n\nEach probe has short connect/total timeouts and never writes probe bytes to the destination file. If metadata, the size probe or all speed probes fail, the canonical `archive.org/download/...` URL remains the first real transfer and the existing failover logic is preserved.\n\n### Failover triggers'''
if old_network not in network:
    if "### Proactive node selection for fresh payloads" not in network:
        raise SystemExit("NETWORK_TLS.md anchor not found")
else:
    network = network.replace(old_network, new_network, 1)
network = network.replace(
    '''archive failover built N alternate URL(s)\narchive failover switch HTTP=... -> https://...''',
    '''archive failover built N alternate URL(s)\narchive selector probe ok=... HTTP=... bytes=... total=... speed=... url=...\narchive selector chose speed=... -> https://...\narchive selector start -> https://...\narchive failover switch HTTP=... -> https://...''',
    1,
)
NETWORK_DOC.write_text(network, encoding="utf-8")

# DOWNLOAD_RESILIENCE.md
resilience = RESILIENCE_DOC.read_text(encoding="utf-8")
old_resilience = '''Archive.org is handled as a provider with multiple storage edges.\n\nOn relevant failures the client can query:\n\n```text\nhttps://archive.org/metadata/<identifier>\n```\n\nand build alternate download URLs from the metadata `server` / `d1` / `d2` fields.\n\nNon-`dn` / non-`.ca` storage nodes are preferred when possible because some Vita OpenSSL 1.0.2 combinations fail against particular Archive.org edges.\n\nFailover can be triggered by more than explicit certificate errors. It also covers transport-like failures such as:'''
new_resilience = '''Archive.org is handled as a provider with multiple storage edges.\n\nFor a fresh normal payload download, the client can query:\n\n```text\nhttps://archive.org/metadata/<identifier>\n```\n\nand build alternate download URLs from the metadata `server` / `d1` / `d2` fields. Non-`dn` / non-`.ca` storage nodes are preferred when possible because some Vita OpenSSL 1.0.2 combinations fail against particular Archive.org edges.\n\nBefore a large fresh payload starts, the client performs bounded **sequential** Range probes rather than accepting whichever edge the public `archive.org` redirect happens to choose:\n\n```text\n1 byte      -> discover authoritative total\n< 16 MiB    -> use first responsive direct node; no speed benchmark\n>= 16 MiB   -> 256 KiB probe per candidate -> rank by measured B/s\nreal file   -> fastest measured node\nfailure     -> next ranked node -> remaining failover\n```\n\nProbe bytes are discarded and never enter `payload.part`. Resumed jobs and image/cache requests skip proactive benchmarking. If selection cannot establish a usable direct node, the canonical Archive URL and the previous failure-driven recovery remain intact.\n\nFailover can still be triggered by more than explicit certificate errors. It also covers transport-like failures such as:'''
if old_resilience not in resilience:
    if "256 KiB probe per candidate" not in resilience:
        raise SystemExit("DOWNLOAD_RESILIENCE.md anchor not found")
else:
    resilience = resilience.replace(old_resilience, new_resilience, 1)
RESILIENCE_DOC.write_text(resilience, encoding="utf-8")

# Client README: replace the older TLS-error-only description with current policy.
client_readme = CLIENT_README.read_text(encoding="utf-8")
old_client = '''Many storage edges (`dn*.ca.archive.org`) fail TLS handshakes on this stack. On `CURLE_SSL_CONNECT_ERROR` for `archive.org/download/...` URLs the client:\n\n1. Fetches `https://archive.org/metadata/<identifier>`\n2. Retries on alternate hosts from metadata (`server` / `d1` / `d2`), preferring non-`dn` / non-`.ca` nodes (`ia*.us.archive.org`, etc.)\n\nLogs: `archive failover built N alternate URL(s)` and `archive failover switch -> https://...`.'''
new_client = '''Some Archive storage edges (`dn*.ca.archive.org`) are slow or unreliable on the Vita TLS stack. For fresh installer payloads the client now:\n\n1. fetches `https://archive.org/metadata/<identifier>`;\n2. builds direct candidates from `server` / `d1` / `d2`, preferring non-`dn` / non-`.ca` nodes;\n3. learns file size with a one-byte Range probe;\n4. for files >= 16 MiB, benchmarks candidates sequentially with 256 KiB Range probes and starts on the fastest measured node;\n5. keeps the remaining candidates as failover for TLS, transport and `5xx` failures.\n\nSmall payloads use the first responsive direct node without the speed benchmark. Resumes and image/cache requests skip proactive selection. If probing fails, the canonical Archive URL remains the fallback. Logs include `archive selector probe`, `archive selector chose`, `archive selector start` and the existing `archive failover` markers.'''
if old_client not in client_readme:
    if "archive selector probe" not in client_readme:
        raise SystemExit("Client README Archive section anchor not found")
else:
    client_readme = client_readme.replace(old_client, new_client, 1)
CLIENT_README.write_text(client_readme, encoding="utf-8")

# Root README one-line feature summary.
root = ROOT_README.read_text(encoding="utf-8")
root = root.replace(
    "Downloads (MediaFire CDN resolution, Archive.org edge failover, GitHub, …) with retries on slow networks and SSL connect errors",
    "Downloads (MediaFire CDN resolution, Archive.org fastest-edge selection + failover, GitHub, …) with retries on slow networks and SSL connect errors",
    1,
)
ROOT_README.write_text(root, encoding="utf-8")

print("Archive fastest-node selector + documentation applied")

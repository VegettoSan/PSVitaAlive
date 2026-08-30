#!/usr/bin/env python3
"""IA polish: descriptive UA + CDN fallback, Retry-After, smart FRESH_CONNECT, image micro-delay."""
from __future__ import annotations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HC = ROOT / "Client PSVitaAlive/source/network/http_client.cpp"
IC = ROOT / "Client PSVitaAlive/source/ui/image_cache.cpp"


def patch_hc() -> None:
    text = HC.read_text(encoding="utf-8")
    if "retryAfterSeconds" in text and "UA_APP" in text:
        print("http_client: already polished")
        return

    old_ctx = """    bool restartedFromZero = false;
    std::string etag;
    std::string lastModified;
"""
    new_ctx = """    bool restartedFromZero = false;
    int retryAfterSeconds = 0; // from Retry-After header (429/503)
    std::string etag;
    std::string lastModified;
"""
    if old_ctx not in text:
        raise SystemExit("TransferContext block not found")
    text = text.replace(old_ctx, new_ctx, 1)

    old_hdr = """    const std::string modified = headerValue(buffer, "Last-Modified:");
    if (!modified.empty()) ctx->lastModified = modified;
    return bytes;
}
"""
    new_hdr = """    const std::string modified = headerValue(buffer, "Last-Modified:");
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
"""
    if old_hdr not in text:
        raise SystemExit("headerCallback tail not found")
    text = text.replace(old_hdr, new_hdr, 1)

    old_diag = 'constexpr const char* DIAG_LOG = "ux0:data/psvitaalive/logs/session.log";'
    new_diag = '''constexpr const char* DIAG_LOG = "ux0:data/psvitaalive/logs/session.log";
// Primary UA identifies the app (IA bot guidelines). CDN fallback is a mainstream browser UA.
constexpr const char* UA_APP =
    "PSVitaAlive/1.14 (PlayStation Vita; +https://github.com/VegettoSan/PSVitaAlive)";
constexpr const char* UA_CDN_FALLBACK =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
'''
    if old_diag not in text:
        raise SystemExit("DIAG_LOG not found")
    text = text.replace(old_diag, new_diag, 1)

    chrome_block = '''// Prefer a mainstream browser UA — some CDNs (incl. IA) treat Vita UA poorly.\n    curl_easy_setopt(curl, CURLOPT_USERAGENT,\n        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");'''
    app_ua_block = '''curl_easy_setopt(curl, CURLOPT_USERAGENT, UA_APP);'''
    if chrome_block in text:
        text = text.replace(chrome_block, app_ua_block)
        print("http_client: UA set to UA_APP")
    else:
        old_one = 'curl_easy_setopt(curl, CURLOPT_USERAGENT,\n        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");'
        if old_one in text:
            text = text.replace(old_one, 'curl_easy_setopt(curl, CURLOPT_USERAGENT, UA_APP);')
            print("http_client: UA set to UA_APP (alt)")
        elif 'CURLOPT_USERAGENT, UA_APP' in text:
            print("http_client: UA_APP already used")
        else:
            raise SystemExit("Chrome UA block not found for replace")

    old_loop = """    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        const int sslIdx = attempt < 4 ? attempt : (attempt % 4);
        curl_easy_setopt(curl, CURLOPT_SSLVERSION, sslAttempts[sslIdx]);
        // Fail-fast connect: short on first tries, slightly longer later.
        if (isArchive) {
            const long ct = (attempt < 3) ? CONNECT_TIMEOUT_ARCHIVE_FAST : CONNECT_TIMEOUT_ARCHIVE_SLOW;
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, ct);
        } else if (attempt > 0) {
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_SECONDS);
        }
        curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, attempt > 0 ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, attempt > 0 ? 1L : 0L);
        // Drop TLS session reuse after SSL failures (stale sessions can stick on Vita).
        if (attempt > 0 && (lastFail == CURLE_SSL_CONNECT_ERROR ||
                            lastFail == CURLE_PEER_FAILED_VERIFICATION ||
                            lastFail == CURLE_SSL_CERTPROBLEM)) {
            curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 0L);
        }
        if (attempt > 0) {
            char retryMsg[160];
            sceClibSnprintf(retryMsg, sizeof(retryMsg),
                "download retry %d/%d hostHints=gitlab:%d archive:%d github:%d lastCurl=%d",
                attempt + 1, kMaxAttempts, isGitlab ? 1 : 0, isArchive ? 1 : 0, isGithub ? 1 : 0,
                static_cast<int>(lastFail));
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
            if (delayMs < 200) delayMs = 200;
            sceKernelDelayThread(delayMs * 1000);
            ctx.cancelled = false;
"""

    new_loop = """    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
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
"""

    if old_loop not in text:
        raise SystemExit("retry loop head not found")
    text = text.replace(old_loop, new_loop, 1)

    old_tr = """            sceClibSnprintf(httpRetry, sizeof(httpRetry),
                "attempt %d HTTP %ld transient — will retry", attempt + 1, responseCode);
            httpDiagnostic(httpRetry);
            if (resumeOffset == 0 && ctx.fd >= 0 && ctx.downloaded > 0) {
                sceIoClose(ctx.fd);
                ctx.fd = sceIoOpen(destinationPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
                ctx.downloaded = 0;
            }
            continue;
"""
    new_tr = """            sceClibSnprintf(httpRetry, sizeof(httpRetry),
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
"""
    if old_tr not in text:
        raise SystemExit("transient HTTP block not found")
    text = text.replace(old_tr, new_tr, 1)

    HC.write_text(text, encoding="utf-8")
    print("http_client: polished")


def patch_ic() -> None:
    text = IC.read_text(encoding="utf-8")
    if "archive.org image spacing" in text:
        print("image_cache: micro-delay already present")
        return

    old = "HttpResult r=http.downloadToFile(job.url,job.path,0,onProgress,shouldCancel);"
    if old not in text:
        raise SystemExit("image downloadToFile call not found")
    new = (
        "HttpResult r=http.downloadToFile(job.url,job.path,0,onProgress,shouldCancel);"
        "if(job.url.find(\"archive.org\")!=std::string::npos)"
        "{/* archive.org image spacing */sceKernelDelayThread(150*1000);}"
    )
    text = text.replace(old, new, 1)
    IC.write_text(text, encoding="utf-8")
    print("image_cache: archive.org micro-delay 150ms")


def main() -> int:
    patch_hc()
    patch_ic()
    print("OK: IA polish applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

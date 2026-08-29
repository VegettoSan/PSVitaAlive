#!/usr/bin/env python3
"""Faster download start: fail-fast connect, TLS1.2 first, shorter early backoff."""
from __future__ import annotations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HC = ROOT / "Client PSVitaAlive/source/network/http_client.cpp"
DM = ROOT / "Client PSVitaAlive/source/network/download_manager.cpp"


def main() -> int:
    text = HC.read_text(encoding="utf-8")
    if "CONNECT_TIMEOUT_ARCHIVE_FAST" in text:
        print("http_client: already fast-start")
    else:
        old_c = """constexpr long CONNECT_TIMEOUT_SECONDS = 45;
constexpr long CONNECT_TIMEOUT_ARCHIVE_SECONDS = 90; // archive.org often slow to accept
constexpr long LOW_SPEED_LIMIT = 1;
constexpr long LOW_SPEED_TIME_SECONDS = 120;
constexpr long LOW_SPEED_TIME_ARCHIVE_SECONDS = 180; // allow longer stalls on IA
"""
        new_c = """constexpr long CONNECT_TIMEOUT_SECONDS = 25;
// archive.org: fail-fast on early attempts so users are not stuck minutes on a dead edge;
// later attempts get a bit more patience without the old 90s hang.
constexpr long CONNECT_TIMEOUT_ARCHIVE_FAST = 18;
constexpr long CONNECT_TIMEOUT_ARCHIVE_SLOW = 40;
constexpr long LOW_SPEED_LIMIT = 1;
constexpr long LOW_SPEED_TIME_SECONDS = 120;
constexpr long LOW_SPEED_TIME_ARCHIVE_SECONDS = 150;
"""
        if old_c not in text:
            raise SystemExit("constants block not found")
        text = text.replace(old_c, new_c, 1)

        old_ua = 'curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (PlayStation Vita) PSVitaAlive/1.0");'
        new_ua = '''// Prefer a mainstream browser UA — some CDNs (incl. IA) treat Vita UA poorly.
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");'''
        if old_ua not in text:
            if "Chrome/120" in text:
                print("UA already browser-like")
            else:
                raise SystemExit("UA line not found")
        else:
            text = text.replace(old_ua, new_ua)

        old_arch = """    // archive.org: slower connect, more patience on stalls, more attempts.
    if (isArchive) {
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_ARCHIVE_SECONDS);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, LOW_SPEED_TIME_ARCHIVE_SECONDS);
    }

    // Host-aware TLS order + generic network retries (fresh connection each try).
    const long sslAttempts[] = {
        isGitlab ? CURL_SSLVERSION_TLSv1_2 : CURL_SSLVERSION_DEFAULT,
        CURL_SSLVERSION_TLSv1_2,
        CURL_SSLVERSION_DEFAULT,
        CURL_SSLVERSION_TLSv1_1,
    };
    // archive.org SSL handshakes fail often on Vita (curl 35) — more attempts + backoff.
    const int kMaxAttempts = isArchive ? 10 : 5;
    long responseCode = 0;
    CURLcode lastFail = CURLE_OK;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        const int sslIdx = attempt < 4 ? attempt : (attempt % 4);
        curl_easy_setopt(curl, CURLOPT_SSLVERSION, sslAttempts[sslIdx]);
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
            int delayMs = 500 * (attempt <= 4 ? attempt : 4);
            if (isArchive) delayMs = 1200 * (attempt <= 6 ? attempt : 6);
            // Extra wait after SSL connect errors — IA edges often need a breath.
            if (lastFail == CURLE_SSL_CONNECT_ERROR ||
                lastFail == CURLE_PEER_FAILED_VERIFICATION ||
                lastFail == CURLE_SSL_CERTPROBLEM) {
                const int sslExtra = isArchive ? (2500 * (attempt <= 4 ? attempt : 4)) : (1500 * attempt);
                if (sslExtra > delayMs) delayMs = sslExtra;
                if (delayMs > 12000) delayMs = 12000;
            }
            if (delayMs < 400) delayMs = 400;
            sceKernelDelayThread(delayMs * 1000);
            ctx.cancelled = false;
"""

        new_arch = """    // archive.org: mid-transfer stalls still need patience; connect uses fail-fast below.
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
    const int kMaxAttempts = isArchive ? 10 : 5;
    long responseCode = 0;
    CURLcode lastFail = CURLE_OK;
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

        if old_arch not in text:
            raise SystemExit("archive retry block not found")
        text = text.replace(old_arch, new_arch, 1)
        HC.write_text(text, encoding="utf-8")
        print("http_client: fast-start applied")

    dm = DM.read_text(encoding="utf-8")
    old_d = "            const int delayMs = isArchiveUrl ? (3000 * outer) : 800;"
    new_d = "            // Outer gap kept small — inner loop already retried with fail-fast.\n            const int delayMs = isArchiveUrl ? (1000 * outer) : 500;"
    if "1000 * outer" in dm:
        print("download_manager: delay already reduced")
    elif old_d not in dm:
        raise SystemExit("download_manager delay not found")
    else:
        dm = dm.replace(old_d, new_d, 1)
        DM.write_text(dm, encoding="utf-8")
        print("download_manager: outer delay reduced")

    print("OK: fast download start")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

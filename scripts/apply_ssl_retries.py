#!/usr/bin/env python3
"""More archive.org SSL retries + longer backoff on curl 35."""
from __future__ import annotations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HC = ROOT / "Client PSVitaAlive/source/network/http_client.cpp"
DM = ROOT / "Client PSVitaAlive/source/network/download_manager.cpp"


def patch_hc() -> None:
    text = HC.read_text(encoding="utf-8")
    if "lastFail = CURLE_OK" in text and "isArchive ? 10" in text:
        print("http_client: already hardened")
    else:
        old = '''    const int kMaxAttempts = isArchive ? 6 : 4;
    long responseCode = 0;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        const int sslIdx = attempt < 4 ? attempt : (attempt % 4);
        curl_easy_setopt(curl, CURLOPT_SSLVERSION, sslAttempts[sslIdx]);
        curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, attempt > 0 ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, attempt > 0 ? 1L : 0L);
        if (attempt > 0) {
            char retryMsg[160];
            sceClibSnprintf(retryMsg, sizeof(retryMsg),
                "download retry %d/%d hostHints=gitlab:%d archive:%d github:%d",
                attempt + 1, kMaxAttempts, isGitlab ? 1 : 0, isArchive ? 1 : 0, isGithub ? 1 : 0);
            httpDiagnostic(retryMsg);
            int delayMs = 500 * (attempt <= 4 ? attempt : 4);
            if (isArchive) delayMs = 1000 * (attempt <= 5 ? attempt : 5);
            if (delayMs < 400) delayMs = 400;
            sceKernelDelayThread(delayMs * 1000);
            ctx.cancelled = false;'''

        new = '''    // archive.org SSL handshakes fail often on Vita (curl 35) — more attempts + backoff.
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
            ctx.cancelled = false;'''

        if old not in text:
            raise SystemExit("http_client: retry loop head not found")
        text = text.replace(old, new, 1)
        print("http_client: more attempts + SSL backoff")

    old_fail = '''        char failMsg[160];
        sceClibSnprintf(failMsg, sizeof(failMsg), "attempt %d failed curl=%d %s retryable=%d",
            attempt + 1, static_cast<int>(result), curl_easy_strerror(result), retryable ? 1 : 0);
        httpDiagnostic(failMsg);
        if (!retryable) break;
    }'''
    new_fail = '''        char failMsg[160];
        sceClibSnprintf(failMsg, sizeof(failMsg), "attempt %d failed curl=%d %s retryable=%d",
            attempt + 1, static_cast<int>(result), curl_easy_strerror(result), retryable ? 1 : 0);
        httpDiagnostic(failMsg);
        lastFail = result;
        if (!retryable) break;
    }'''
    if "lastFail = result" in text:
        print("http_client: lastFail already tracked")
    elif old_fail not in text:
        raise SystemExit("http_client: failMsg block not found")
    else:
        text = text.replace(old_fail, new_fail, 1)
        print("http_client: lastFail tracked")

    old_ret = '''        const bool retryable =
            result == CURLE_SSL_CONNECT_ERROR ||
            result == CURLE_PEER_FAILED_VERIFICATION ||
            result == CURLE_COULDNT_CONNECT ||
            result == CURLE_COULDNT_RESOLVE_HOST ||
            result == CURLE_OPERATION_TIMEDOUT ||
            result == CURLE_RECV_ERROR ||
            result == CURLE_SEND_ERROR ||
            result == CURLE_GOT_NOTHING ||
            result == CURLE_PARTIAL_FILE ||
            result == CURLE_HTTP_RETURNED_ERROR;'''
    new_ret = '''        const bool retryable =
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
            result == CURLE_HTTP_RETURNED_ERROR;'''
    if "CURLE_SSL_CIPHER" in text and "const bool retryable" in text:
        chunk = text.split("const bool retryable")[1][:500]
        if "CURLE_SSL_CIPHER" in chunk:
            print("http_client: retryable SSL list already extended")
        elif old_ret in text:
            text = text.replace(old_ret, new_ret, 1)
            print("http_client: extended retryable SSL codes")
        else:
            raise SystemExit("http_client: retryable block not found")
    elif old_ret in text:
        text = text.replace(old_ret, new_ret, 1)
        print("http_client: extended retryable SSL codes")
    else:
        raise SystemExit("http_client: retryable block not found")

    HC.write_text(text, encoding="utf-8")


def patch_dm() -> None:
    text = DM.read_text(encoding="utf-8")
    old = "    const int outerAttempts = isArchiveUrl ? 3 : 2;"
    new = "    const int outerAttempts = isArchiveUrl ? 4 : 2;"
    if "outerAttempts = isArchiveUrl ? 4" in text:
        print("download_manager: outer attempts already 4")
    elif old not in text:
        raise SystemExit("download_manager: outerAttempts not found")
    else:
        text = text.replace(old, new, 1)
        print("download_manager: outerAttempts=4 for archive")

    old_d = "            const int delayMs = isArchiveUrl ? (2000 * outer) : 800;"
    new_d = "            const int delayMs = isArchiveUrl ? (3000 * outer) : 800;"
    if old_d in text:
        text = text.replace(old_d, new_d, 1)
        print("download_manager: longer outer delay")
    DM.write_text(text, encoding="utf-8")


def main() -> int:
    patch_hc()
    patch_dm()
    print("OK: SSL/archive retry hardening applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

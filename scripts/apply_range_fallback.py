#!/usr/bin/env python3
"""Apply CURLE_RANGE_ERROR fallback in HttpClient::downloadToFile."""
from pathlib import Path

CPP = Path("Client PSVitaAlive/source/network/http_client.cpp")
cpp = CPP.read_text(encoding="utf-8")

# 1) Flag next to lastFail
old_flags = """    long responseCode = 0;
    CURLcode lastFail = CURLE_OK;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
"""
new_flags = """    long responseCode = 0;
    CURLcode lastFail = CURLE_OK;
    // One-shot guard: if a Range request fails (curl 33), truncate and retry as full GET.
    bool rangeFallbackUsed = false;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
"""
if old_flags not in cpp:
    raise SystemExit("flags insert point not found")
cpp = cpp.replace(old_flags, new_flags, 1)

# 2) Special branch after CURLE_OK block, before retryable
old_retry = """        const bool retryable =
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
"""

new_range = """        // CDN/host rejected HTTP Range (common on MediaFire and some mirrors).
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

""" + old_retry

if old_retry not in cpp:
    raise SystemExit("retryable block not found")
cpp = cpp.replace(old_retry, new_range, 1)

# 3) lastRangeAccepted_ should reflect the effective Range used on the last attempt
old_range_flag = "    if (resumeOffset > 0 && responseCode == 206) lastRangeAccepted_ = true;"
new_range_flag = "    if (ctx.resumeOffset > 0 && responseCode == 206) lastRangeAccepted_ = true;"
if old_range_flag not in cpp:
    raise SystemExit("lastRangeAccepted_ line not found")
cpp = cpp.replace(old_range_flag, new_range_flag, 1)

CPP.write_text(cpp, encoding="utf-8")
print("OK range fallback applied")

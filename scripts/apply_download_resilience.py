#!/usr/bin/env python3
"""Apply Archive.org download resilience + extract retry (idempotent)."""
from __future__ import annotations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HC = ROOT / "Client PSVitaAlive/source/network/http_client.cpp"
DM = ROOT / "Client PSVitaAlive/source/network/download_manager.cpp"
HI = ROOT / "Client PSVitaAlive/source/installer/homebrew_installer.cpp"


def patch_hc() -> None:
    text = HC.read_text(encoding="utf-8")
    if "CONNECT_TIMEOUT_ARCHIVE_SECONDS" in text:
        print("http_client: already patched")
        return

    old_const = """constexpr size_t DOWNLOAD_BUFFER_SIZE = 512 * 1024;
constexpr long CONNECT_TIMEOUT_SECONDS = 45;
constexpr long LOW_SPEED_LIMIT = 1;
constexpr long LOW_SPEED_TIME_SECONDS = 120;"""
    new_const = """constexpr size_t DOWNLOAD_BUFFER_SIZE = 512 * 1024;
constexpr long CONNECT_TIMEOUT_SECONDS = 45;
constexpr long CONNECT_TIMEOUT_ARCHIVE_SECONDS = 90; // archive.org often slow to accept
constexpr long LOW_SPEED_LIMIT = 1;
constexpr long LOW_SPEED_TIME_SECONDS = 120;
constexpr long LOW_SPEED_TIME_ARCHIVE_SECONDS = 180; // allow longer stalls on IA"""
    if old_const not in text:
        raise SystemExit("http_client: const block not found")
    text = text.replace(old_const, new_const, 1)

    old_hdr = """    headers = curl_slist_append(headers, "Accept: */*");
    headers = curl_slist_append(headers, "Accept-Encoding: identity");
    headers = curl_slist_append(headers, "Connection: close");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 12L);"""
    new_hdr = """    headers = curl_slist_append(headers, "Accept: */*");
    headers = curl_slist_append(headers, "Accept-Encoding: identity");
    headers = curl_slist_append(headers, "Connection: close");
    // Mildly improves first-byte reliability on some Internet Archive edges.
    if (url.find("archive.org") != std::string::npos) {
        headers = curl_slist_append(headers, "Referer: https://archive.org/");
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 12L);"""
    if old_hdr not in text:
        raise SystemExit("http_client: headers block not found")
    text = text.replace(old_hdr, new_hdr, 1)

    old_loop = """    const bool isGitlab = url.find("gitlab.com") != std::string::npos
        || url.find("gitlab.io") != std::string::npos;
    const bool isArchive = url.find("archive.org") != std::string::npos;
    const bool isGithub = url.find("github.com") != std::string::npos
        || url.find("githubusercontent.com") != std::string::npos;

    // Host-aware TLS order + generic network retries (fresh connection each try).
    const long sslAttempts[] = {
        isGitlab ? CURL_SSLVERSION_TLSv1_2 : CURL_SSLVERSION_DEFAULT,
        CURL_SSLVERSION_TLSv1_2,
        CURL_SSLVERSION_DEFAULT,
        CURL_SSLVERSION_TLSv1_1,
    };
    constexpr int kMaxAttempts = 4;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        curl_easy_setopt(curl, CURLOPT_SSLVERSION, sslAttempts[attempt]);
        curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, attempt > 0 ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, attempt > 0 ? 1L : 0L);
        if (attempt > 0) {
            char retryMsg[140];
            sceClibSnprintf(retryMsg, sizeof(retryMsg), "download retry %d/%d hostHints=gitlab:%d archive:%d github:%d",
                attempt + 1, kMaxAttempts, isGitlab ? 1 : 0, isArchive ? 1 : 0, isGithub ? 1 : 0);
            httpDiagnostic(retryMsg);
            // Brief pause before retry (network / TLS recovery)
            sceKernelDelayThread(400 * 1000);
            ctx.cancelled = false;
            if (ctx.downloaded == 0 && resumeOffset == 0 && ctx.fd >= 0) {
                sceIoLseek(ctx.fd, 0, SCE_SEEK_SET);
            }
        }

        result = curl_easy_perform(curl);

        if (ctx.cancelled) break;
        if (result == CURLE_OK) break;

        const bool retryable =
            result == CURLE_SSL_CONNECT_ERROR ||
            result == CURLE_PEER_FAILED_VERIFICATION ||
            result == CURLE_COULDNT_CONNECT ||
            result == CURLE_COULDNT_RESOLVE_HOST ||
            result == CURLE_OPERATION_TIMEDOUT ||
            result == CURLE_RECV_ERROR ||
            result == CURLE_SEND_ERROR ||
            result == CURLE_GOT_NOTHING ||
            result == CURLE_PARTIAL_FILE;
        char failMsg[160];
        sceClibSnprintf(failMsg, sizeof(failMsg), "attempt %d failed curl=%d %s retryable=%d",
            attempt + 1, static_cast<int>(result), curl_easy_strerror(result), retryable ? 1 : 0);
        httpDiagnostic(failMsg);
        if (!retryable) break;
    }
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    lastStatus_ = static_cast<int>(responseCode);
    if (resumeOffset > 0 && responseCode == 206) lastRangeAccepted_ = true;"""

    new_loop = """    const bool isGitlab = url.find("gitlab.com") != std::string::npos
        || url.find("gitlab.io") != std::string::npos;
    const bool isArchive = url.find("archive.org") != std::string::npos;
    const bool isGithub = url.find("github.com") != std::string::npos
        || url.find("githubusercontent.com") != std::string::npos;

    // archive.org: slower connect, more patience on stalls, more attempts.
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
    const int kMaxAttempts = isArchive ? 6 : 4;
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
            ctx.cancelled = false;
            if (ctx.downloaded == 0 && resumeOffset == 0 && ctx.fd >= 0) {
                sceIoLseek(ctx.fd, 0, SCE_SEEK_SET);
            }
        }

        result = curl_easy_perform(curl);
        responseCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

        if (ctx.cancelled) break;
        if (result == CURLE_OK) {
            const bool transientHttp =
                responseCode == 429 || responseCode == 502 || responseCode == 503 ||
                responseCode == 504 || responseCode == 520 || responseCode == 522 ||
                responseCode == 524;
            if (!transientHttp) break;
            char httpRetry[120];
            sceClibSnprintf(httpRetry, sizeof(httpRetry),
                "attempt %d HTTP %ld transient — will retry", attempt + 1, responseCode);
            httpDiagnostic(httpRetry);
            if (resumeOffset == 0 && ctx.fd >= 0 && ctx.downloaded > 0) {
                sceIoClose(ctx.fd);
                ctx.fd = sceIoOpen(destinationPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
                ctx.downloaded = 0;
            }
            continue;
        }

        const bool retryable =
            result == CURLE_SSL_CONNECT_ERROR ||
            result == CURLE_PEER_FAILED_VERIFICATION ||
            result == CURLE_COULDNT_CONNECT ||
            result == CURLE_COULDNT_RESOLVE_HOST ||
            result == CURLE_OPERATION_TIMEDOUT ||
            result == CURLE_RECV_ERROR ||
            result == CURLE_SEND_ERROR ||
            result == CURLE_GOT_NOTHING ||
            result == CURLE_PARTIAL_FILE ||
            result == CURLE_HTTP_RETURNED_ERROR;
        char failMsg[160];
        sceClibSnprintf(failMsg, sizeof(failMsg), "attempt %d failed curl=%d %s retryable=%d",
            attempt + 1, static_cast<int>(result), curl_easy_strerror(result), retryable ? 1 : 0);
        httpDiagnostic(failMsg);
        if (!retryable) break;
    }
    lastStatus_ = static_cast<int>(responseCode);
    if (resumeOffset > 0 && responseCode == 206) lastRangeAccepted_ = true;"""

    if old_loop not in text:
        raise SystemExit("http_client: retry loop not found")
    text = text.replace(old_loop, new_loop, 1)
    HC.write_text(text, encoding="utf-8")
    print("http_client: patched")


def patch_dm() -> None:
    text = DM.read_text(encoding="utf-8")
    if "outerAttempts" in text:
        print("download_manager: already patched")
        return

    old = """    HttpResult hr = http_.downloadToFile(effectiveUrl, job.temporaryPath, offset, progress, cancelFn);
    if (hr != HttpResult::Ok && hr != HttpResult::Cancelled && !job.cancelRequested && !sizeLimitHit) {
        const std::string firstErr = http_.lastError();
        sceClibPrintf("[DownloadManager] first attempt failed: %s - retrying once\\n", firstErr.c_str());
        diagnostics::log(std::string("[DownloadManager] first attempt failed: ") + firstErr + " — retrying once");
        sceKernelDelayThread(800 * 1000);
        // MediaFire CDN URLs expire: always re-resolve and restart clean on retry.
        if (mediafire) {
            st.removeFile(job.temporaryPath);
            offset = 0;
            job.downloadedSize = 0;
            std::string direct;
            std::string mfErr;
            uint64_t mfSize = 0;
            if (resolveMediaFireDirectUrl(http_, job.url, direct, mfErr, &mfSize) && !direct.empty()) {
                effectiveUrl = direct;
                if (mfSize > 0) job.expectedSize = mfSize;
                diagnostics::log("[DownloadManager] MediaFire re-resolved for retry");
            } else {
                diagnostics::log(std::string("[DownloadManager] MediaFire re-resolve failed: ") + mfErr);
            }
        } else if (job.downloadedSize == 0) {
            st.removeFile(job.temporaryPath);
            offset = 0;
        } else {
            const int64_t sz = st.fileSize(job.temporaryPath);
            offset = sz > 0 ? static_cast<uint64_t>(sz) : 0;
            job.downloadedSize = offset;
        }
        sizeLimitHit = false;
        job.cancelRequested = false;
        hr = http_.downloadToFile(effectiveUrl, job.temporaryPath, offset, progress, cancelFn);
    }
    job.lastHttpStatus = http_.lastStatusCode();"""

    new = """    const bool isArchiveUrl =
        job.url.find("archive.org") != std::string::npos ||
        effectiveUrl.find("archive.org") != std::string::npos;
    // Outer attempts on top of HttpClient's internal retries.
    // archive.org is flaky under load — allow a couple of full restarts.
    const int outerAttempts = isArchiveUrl ? 3 : 2;
    HttpResult hr = HttpResult::NetworkError;
    for (int outer = 0; outer < outerAttempts; ++outer) {
        if (outer > 0) {
            const std::string prevErr = http_.lastError();
            char msg[160];
            sceClibSnprintf(msg, sizeof(msg),
                "[DownloadManager] attempt %d/%d failed: %s — retrying",
                outer, outerAttempts, prevErr.c_str());
            sceClibPrintf("%s\\n", msg);
            diagnostics::log(msg);
            const int delayMs = isArchiveUrl ? (2000 * outer) : 800;
            sceKernelDelayThread(delayMs * 1000);
            if (mediafire) {
                st.removeFile(job.temporaryPath);
                offset = 0;
                job.downloadedSize = 0;
                std::string direct;
                std::string mfErr;
                uint64_t mfSize = 0;
                if (resolveMediaFireDirectUrl(http_, job.url, direct, mfErr, &mfSize) && !direct.empty()) {
                    effectiveUrl = direct;
                    if (mfSize > 0) job.expectedSize = mfSize;
                    diagnostics::log("[DownloadManager] MediaFire re-resolved for retry");
                } else {
                    diagnostics::log(std::string("[DownloadManager] MediaFire re-resolve failed: ") + mfErr);
                }
            } else if (job.downloadedSize == 0) {
                st.removeFile(job.temporaryPath);
                offset = 0;
            } else {
                const int64_t sz = st.fileSize(job.temporaryPath);
                offset = sz > 0 ? static_cast<uint64_t>(sz) : 0;
                job.downloadedSize = offset;
            }
            sizeLimitHit = false;
            job.cancelRequested = false;
        }
        hr = http_.downloadToFile(effectiveUrl, job.temporaryPath, offset, progress, cancelFn);
        if (hr == HttpResult::Ok || hr == HttpResult::Cancelled || job.cancelRequested || sizeLimitHit)
            break;
    }
    job.lastHttpStatus = http_.lastStatusCode();"""

    if old not in text:
        raise SystemExit("download_manager: retry block not found")
    DM.write_text(text.replace(old, new, 1), encoding="utf-8")
    print("download_manager: patched")


def patch_hi() -> None:
    text = HI.read_text(encoding="utf-8")
    if "extractAttempt" in text:
        print("homebrew_installer: already patched")
        return

    old = """    ZipExtractor zip;
    const ZipResult zr = zip.extract(
        vpkPath,
        tmpDir,
        [&](const ZipProgress& zp) {
            if (!onProgress) return;
            InstallProgress p;
            p.stage = InstallProgress::Extracting;
            p.entriesDone = zp.entriesDone;
            p.entriesTotal = zp.entriesTotal;
            p.bytesWritten = zp.bytesWritten;
            p.bytesTotal = zp.bytesTotal;
            p.message = zp.currentEntry;
            onProgress(p);
        },
        shouldCancel
    );

    logLine(std::string("ZipExtractor result=") + std::to_string(static_cast<int>(zr)) + " error=" + zip.lastError());
    if (zr == ZipResult::Cancelled) {
        removeTree(tmpDir);
        setError("extract cancelled");
        return InstallResult::Cancelled;
    }
    if (zr != ZipResult::Ok) {
        removeTree(tmpDir);
        setError(std::string("extract failed: ") + zip.lastError());
        return InstallResult::ExtractFailed;
    }"""

    new = """    // Extract with one automatic retry: transient RAM pressure / flaky deflate
    // on Vita sometimes fails the first pass; a clean second try often works.
    ZipResult zr = ZipResult::IoError;
    std::string zipErr;
    for (int extractAttempt = 0; extractAttempt < 2; ++extractAttempt) {
        if (extractAttempt > 0) {
            logLine(std::string("ZipExtractor retry after: ") + zipErr);
            removeTree(tmpDir);
            if (!st.createDirectories(tmpDir)) {
                setError("cannot recreate VPK promote directory for extract retry");
                return InstallResult::IoError;
            }
            sceKernelDelayThread(500 * 1000);
            if (onProgress) {
                InstallProgress p;
                p.stage = InstallProgress::Extracting;
                p.message = "retrying extract";
                onProgress(p);
            }
        }

        ZipExtractor zip;
        zr = zip.extract(
            vpkPath,
            tmpDir,
            [&](const ZipProgress& zp) {
                if (!onProgress) return;
                InstallProgress p;
                p.stage = InstallProgress::Extracting;
                p.entriesDone = zp.entriesDone;
                p.entriesTotal = zp.entriesTotal;
                p.bytesWritten = zp.bytesWritten;
                p.bytesTotal = zp.bytesTotal;
                p.message = zp.currentEntry;
                onProgress(p);
            },
            shouldCancel
        );
        zipErr = zip.lastError();
        logLine(std::string("ZipExtractor result=") + std::to_string(static_cast<int>(zr)) +
                " attempt=" + std::to_string(extractAttempt + 1) + " error=" + zipErr);
        if (zr == ZipResult::Ok || zr == ZipResult::Cancelled)
            break;
        if (zr == ZipResult::UnsafePath)
            break;
    }

    if (zr == ZipResult::Cancelled) {
        removeTree(tmpDir);
        setError("extract cancelled");
        return InstallResult::Cancelled;
    }
    if (zr != ZipResult::Ok) {
        removeTree(tmpDir);
        setError(std::string("extract failed: ") + zipErr);
        return InstallResult::ExtractFailed;
    }"""

    if old not in text:
        raise SystemExit("homebrew_installer: extract block not found")
    HI.write_text(text.replace(old, new, 1), encoding="utf-8")
    print("homebrew_installer: patched")


def main() -> int:
    for path in (HC, DM, HI):
        t = path.read_text(encoding="utf-8")
        if t.strip().startswith("{{FILE:") or t.strip() == "PLACEHOLDER":
            raise SystemExit(f"placeholder content in {path}")
    patch_hc()
    patch_dm()
    patch_hi()
    print("OK: download/extract resilience applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

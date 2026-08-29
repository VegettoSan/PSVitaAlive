#!/usr/bin/env python3
"""Fix HTTP mid-download retries appending a second copy (size-limit false positive)."""
from __future__ import annotations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HC = ROOT / "Client PSVitaAlive/source/network/http_client.cpp"
DM = ROOT / "Client PSVitaAlive/source/network/download_manager.cpp"


def patch_hc() -> None:
    text = HC.read_text(encoding="utf-8")
    if "retry resume from absolute=" in text:
        print("http_client: already patched")
        return

    old = '''        if (attempt > 0) {
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
        }'''

    new = '''        if (attempt > 0) {
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
        }'''

    if old not in text:
        raise SystemExit("http_client: retry block not found")
    HC.write_text(text.replace(old, new, 1), encoding="utf-8")
    print("http_client: patched")


def patch_dm() -> None:
    text = DM.read_text(encoding="utf-8")
    old = '''        job.lastError = "download exceeded expected size (possible MediaFire error page)";'''
    new = '''        // Usually a bad mid-retry append or a wrong Content-Length; MediaFire is only one cause.
        job.lastError = mediafire
            ? "download exceeded expected size (possible MediaFire error page)"
            : "download exceeded expected size (interrupted transfer; please retry)";'''
    if old not in text:
        if "interrupted transfer" in text:
            print("download_manager: error message already updated")
            return
        raise SystemExit("download_manager: size error message not found")
    DM.write_text(text.replace(old, new, 1), encoding="utf-8")
    print("download_manager: patched")


def main() -> int:
    patch_hc()
    patch_dm()
    print("OK: resume-on-retry fix applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

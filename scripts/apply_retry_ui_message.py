#!/usr/bin/env python3
"""Wire 'retrying download (N/M)...' into DownloadManager + InstallController UI."""
from __future__ import annotations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DM = ROOT / "Client PSVitaAlive/source/network/download_manager.cpp"
IC = ROOT / "Client PSVitaAlive/source/installer/install_controller.cpp"


def patch_dm() -> None:
    text = DM.read_text(encoding="utf-8")
    if "retrying download (" in text:
        print("download_manager: already has retry UI message")
        return
    old = """    for (int outer = 0; outer < outerAttempts; ++outer) {
        if (outer > 0) {
            const std::string prevErr = http_.lastError();
            char msg[160];
            sceClibSnprintf(msg, sizeof(msg),
                "[DownloadManager] attempt %d/%d failed: %s — retrying",
                outer, outerAttempts, prevErr.c_str());
            sceClibPrintf("%s\\n", msg);
            diagnostics::log(msg);
            const int delayMs = isArchiveUrl ? (2000 * outer) : 800;
            sceKernelDelayThread(delayMs * 1000);"""
    new = """    for (int outer = 0; outer < outerAttempts; ++outer) {
        if (outer > 0) {
            const std::string prevErr = http_.lastError();
            char msg[160];
            sceClibSnprintf(msg, sizeof(msg),
                "[DownloadManager] attempt %d/%d failed: %s — retrying",
                outer, outerAttempts, prevErr.c_str());
            sceClibPrintf("%s\\n", msg);
            diagnostics::log(msg);
            // Let the install UI show a clear retry line (e.g. "retrying download (2/3)...").
            if (onProgress_) {
                DownloadProgressEvent ev;
                ev.jobId = job.id;
                ev.fileName = job.fileName;
                ev.downloaded = job.downloadedSize;
                ev.total = job.expectedSize;
                ev.bytesPerSecond = 0;
                ev.state = DownloadState::Downloading;
                char uiMsg[48];
                sceClibSnprintf(uiMsg, sizeof(uiMsg), "retrying download (%d/%d)...",
                    outer + 1, outerAttempts);
                ev.message = uiMsg;
                onProgress_(ev);
            }
            const int delayMs = isArchiveUrl ? (2000 * outer) : 800;
            sceKernelDelayThread(delayMs * 1000);"""
    if old not in text:
        raise SystemExit("download_manager: retry block not found")
    DM.write_text(text.replace(old, new, 1), encoding="utf-8")
    print("download_manager: patched")


def patch_ic() -> None:
    text = IC.read_text(encoding="utf-8")
    if "event.message.empty()" in text:
        print("install_controller: already uses event.message")
        return
    old = """    downloads_.setProgressCallback([this](const DownloadProgressEvent& event) {
        current_.store(event.downloaded);
        total_.store(event.total);
        speed_.store(event.bytesPerSecond);
        setFileName(event.fileName.c_str());
        setStage("Downloading");
        state_.store(static_cast<int>(InstallStatus::State::Downloading));
        setMessage("Downloading...");
    });"""
    new = """    downloads_.setProgressCallback([this](const DownloadProgressEvent& event) {
        current_.store(event.downloaded);
        total_.store(event.total);
        speed_.store(event.bytesPerSecond);
        setFileName(event.fileName.c_str());
        setStage("Downloading");
        state_.store(static_cast<int>(InstallStatus::State::Downloading));
        // Prefer explicit status from DownloadManager (retries); else generic line.
        if (!event.message.empty())
            setMessage(event.message.c_str());
        else
            setMessage("Downloading...");
    });"""
    if old not in text:
        raise SystemExit("install_controller: progress callback not found")
    IC.write_text(text.replace(old, new, 1), encoding="utf-8")
    print("install_controller: patched")


def main() -> int:
    patch_dm()
    patch_ic()
    print("OK: retry UI message wired")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

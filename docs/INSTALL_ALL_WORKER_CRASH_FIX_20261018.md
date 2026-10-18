# PS Vita Alive — Install All worker overlap crash and extraction UI hitch fix

**Date:** 18 October 2026  
**Scope:** `InstallController`, `FullCatalogScreen`, `ZipExtractor` on real PS Vita  
**Status:** emergency corrective change applied directly to `main`, pending device regression verification.

## Input evidence

The reporter supplied `install.log`, `session.log` and a compressed `.psp2dmp` crash dump. The dump is a gzip-compressed 32-bit ARM ELF core; it was decompressed and its Vita thread/CPU-register notes inspected.

**Observed sequence in `session.log`:**

- Spider-Man VPK: ranged download passed assembly/checksum, VPK extracted and promoted; LiveArea verification succeeded.
- Conker Game Files ZIP: ranged download and ZIP extraction completed.
- Kingdom Hearts 358/2 Days, `khdays-vita-0.7.0.vpk`: six archive entries extracted, Promoter finished and `ux0:app/KHDD00358` verified.
- `[6014292 ms]` UI acknowledged the successful VPK and immediately advanced **Install All** to step 2, Game Files ZIP.
- `[6014488 ms]` the previous worker was **still writing the local install receipt and logging completion**.
- `[6014548 ms]` the controller accepted the new Game Files download request.
- The trace stops immediately after the new request; there is **no documented failure extracting the new Game Files ZIP**, since it was not opened in the trace.

The core's `THREAD_INFO` includes **two simultaneous `PSVitaAliveInstall` threads**, IDs `0x402801C1` and `0x403A01E9`. CPU registers in the older installer thread indicate an abort-like process signal (`r2=6`, **SIGABRT**). This supports a synchronization/use-after-state race. Without matching debug symbols, the exact aborting instruction/call stack and allocation cannot be proven; do not claim symbol-level certainty.

The performance log reports `worst_us` spikes up to about **1,326,479 µs** during installation, i.e. **~1.33 s** on the slowest frame. Media textures were suspended during the large download; this suggests storage scheduling/CPU contention rather than a proven graphics texture corruption.

## Root cause in application logic

In `InstallController::workerMain()`, `setState(Completed)` used to run **before** writing the installed release receipt and before `DownloadManager::cleanupCompletedJob()`. The UI detects `Completed` and `FullCatalogScreen::installAllTryAdvanceFromProgress()` immediately acknowledges the result and issues the next request.

The former `requestInstall()` code reaped the old thread **only if** `workerDone_ == true`; however it **did not refuse a new job when `workerDone_` was still false**. It then overwrote `workerThread_` with a new handle and reused `activeJobId_`, catalog receipt metadata and `DownloadManager` while the first worker still accessed them.

This permits concurrent modification of a job vector and job metadata, an unsafe condition that can lead to a crash. The core proves installer overlap, not the exact individual C++ instruction that aborted.

## Corrections applied

### Worker lifecycle barrier (`install_controller.cpp`)

1. On a new `requestInstall()`, if the old worker has **not** published `workerDone_`, **do not accept or start a new request**. A finished worker is first joined with `sceKernelWaitThreadEnd`, its thread handle deleted, then shared job fields may be reused. The owner of `workerThread_` is the controller on the main thread, not the worker itself.
2. `status()` temporarily masks prematurely terminal `Completed/Failed/Cancelled` as `Installing` while an installer thread is finishing. This prevents **Install All** auto-advancement until the worker is safe to reap, regardless of how long the device's `ux0:` cleanup takes.
3. `acknowledgeResult()` does not clear result/ZIP recovery fields while the worker is still finishing. Automatic dismissal similarly waits until cleanup.
4. `busy()` also treats an unfinished worker as busy, and `setPendingCatalogMeta()` will not overwrite metadata while the previous worker is still preparing its version receipt. This closes a secondary race preceding `requestInstall()`.
5. The successful VPK/ZIP path now persists its version receipt, logs completion, deletes the finished download job and resets `activeJobId_` **before** publishing the terminal `Completed` state.
6. The worker signals `workerDone_` with release ordering; the UI/controller reads it with acquire ordering. Worker thread handles are never cleared from inside their own worker entry point.

These barriers also protect slower storage media: a successful Promoter call is **not** equivalent to a completely finished download/installer worker.

### Rendering responsiveness (`zip_extractor.cpp`)

- Keep the faster **256 KiB** ZIP extraction buffer, ZIP64 handling, strict integrity checks and regular progress updates.
- After approximately **1 MiB of actual extracted bytes**, the installer worker calls `sceKernelDelayThread(1000)` (1 ms) to yield CPU time to rendering and system services. This is bounded and may slightly reduce throughput; it cannot prevent a single synchronous filesystem call from blocking.
- No new worker or large RAM allocation was introduced, and cancellation is still checked per extraction chunk.

## Regression tests

- Repeat **Install All** for Kingdom Hearts 358/2 Days (VPK then Game Files ZIP) several times on real hardware.
- Exercise VPK only, ZIP only, VPK → ZIP, ZIP → VPK, consecutive Install All sequences, slow SD2Vita, cancel/retry, failed VPK and recoverable ZIP errors.
- Verify that each new install begins after log marker `previous worker reaped before next request`, never while the previous receipt/cleanup is still active; no second live installer worker should overlap a prior one.
- Confirm that `install.log` records successful extraction/promotion and `session.log` continues through the **Game Files** download and final extraction.
- Compare `[Perf] ... worst_us=...` peaks. This CPU yield is a conservative mitigation; jitter may remain due to device I/O or GPU/display thread scheduling.
- Check no regression in fast adaptive 2-/4-range transfers, cancellation or file integrity.
- Compile with VitaSDK and validate catalogs. A green build does not prove the hardware crash is eliminated; only repeated on-device testing can establish confidence.

## Project compatibility

This fix changes only the native PS Vita client and documentation. It does not edit generated catalog files; `apps/`, `authors/`, `categories/` remain canonical. System BGDL and external publication architecture are untouched. No other store or codebase is implicated in the diagnosis.

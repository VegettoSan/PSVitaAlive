# Experimental HTTP 206 resume fix + pause/resume (26 October 2026)

**Branch:** `fix/http206-resume-pause-test-20261026`  
**Status:** experimental; do not merge into `main` or publish until validated on real PS Vita and Vita3K.

## Problem and scope

A reported direct PSP/PS1 PKG transfer failed after `HTTP 206` resume: `download exceeded expected size`.
In the serial `HttpClient::headerCallback`, the HTTP 206 `Content-Range` total could be overwritten by a later `Content-Length` (which measures only the partial response body). This made the size guard compare absolute bytes to a remaining-body length.

Changes are restricted to the PS Vita client HTTP path, download job state, install worker/UI and tests.
The 8 MiB size guard, free-space checks, encrypted/official PKG BGDL handling, 2/4-range thresholds, checksum/range validators, ZIP extraction and catalog formats remain unchanged.

## HTTP 206 validation

- `Content-Length` is treated as full payload size only for HTTP 200; for HTTP 206 the authoritative whole-file length is from valid `Content-Range`.
- Fresh or resumed 206 must identify the correct requested start and provide valid end and total.
- Writer rejects receiving more bytes than the declared 206 interval.
- Existing one-time clean restart for invalid `Content-Range` and exact final file size checks remain in place.
- A small pure C++ header and `tests/test_http_range_size_policy.cpp` cover both header orders, wrong starts, overflowing ranges and the report's byte values.

## Pause / Resume (test implementation)

- In the **native in-app Downloading phase**, a visible button and **SQUARE** request **Pause**.
- Pausing cooperatively stops the active libcurl operation; the worker returns normally and the serial write buffer is flushed.
- For serial downloads, the `payload.part` length is persisted and **SQUARE / Resume** uses the *same* DownloadManager job with a new 64-bit HTTP Range request; no new install is enqueued.
- **CIRCLE / Cancel** while paused deliberately removes the temporary job/files. Pause is never reported as Install Failed or Cancelled.
- An interrupted **2/4-range** transfer cannot safely reuse unfinished segments with the existing implementation. Its partial range files are cleaned by the established fallback; Resume performs a fresh transfer from zero (with the same eligibility checks). This is deliberate: preserving unsafe fragments risks corruption.
- Pauses last **only while PSVitaAlive remains running**. On the next launch, the unchanged startup orphan-download cleanup removes abandoned job files.
- Pausing is not available for system BGDL, self-update, installation/extraction, ZIP recovery or plugins outside the native Downloading stage.
- No extra background worker is introduced. Resume joins the completed previous worker before launching a replacement on the same job.
- No main branch, catalog generator, generated JSON or website files are changed.

## On-device validation checklist

1. Install the branch's test VPK *only on a test console or after backing up the installed client*, because it has the same title ID `PSVAS1178`; disable automatic client updates while testing.
2. Direct serial HTTP 200 file: pause after measurable progress, confirm progress stops and the partial file remains; resume, verify a 206 starts at the saved offset and final exact size matches.
3. Repeat pause/resume twice during one serial transfer; confirm no double appending or incorrect percentage.
4. Force/observe a server that ignores Range (resumed 200) and a mismatched 206; confirm clean restart without false install success.
5. Pause a 2/4-range eligible ZIP/VPK; expect pause state with **0 reusable bytes** and a clean fresh download after Resume. Verify extraction/install integrity.
6. Cancel while paused; verify job and partials are removed and no installation runs.
7. Test Download All sequence: pausing a step must not mark it Failed or advance to the next step.
8. Repeat a direct HTTP PSP/PS1 transfer like the failing report (not Sony Vita BGDL); inspect `content-range`, `HTTP RESULT`, `DownloadManager` and `Installer` markers in session.log.
9. Regression-check MediaFire, Archive.org, GitHub Releases, direct ZIP/VPK, install/extract, TLS retry, insufficient space and cancellation.
10. Test normal user-facing text and touch/SQUARE controls in Spanish and English. Other languages currently fall back to English for the new strings.

## Expected log markers

```text
[DownloadManager] pause requested job=...
[DownloadManager] paused job=... saved_bytes=...
[Installer] download paused without installation job=...
[DownloadManager] resume queued job=... offset=...
[Installer] resumed existing job=...
HTTP BEGIN ... resume=...
content-range start=... end=... total=... resume=...
HTTP RESULT curl=... status=206 ... absolute=... total=...
```

**Do not merge into main until** the build passes and hardware/Vita3K logs demonstrate both successful resume and no regressions.

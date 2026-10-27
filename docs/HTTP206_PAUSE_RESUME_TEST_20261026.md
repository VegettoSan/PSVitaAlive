# HTTP 206 seguro, pausa y reanudación de descargas (26 de octubre de 2026)

**Branch:** `fix/http206-resume-pause-test-20261026`  
**Estado:** validado por el mantenedor en PS Vita real y aprobado para integración en `main` el 26 de octubre de 2026. El botón **Pausar/Reanudar es permanente**. Mantener este documento como referencia de pruebas y diagnóstico; una compilación exitosa no sustituye las pruebas específicas de cada fuente.

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

## Pause / Resume (permanent client feature)

- In the **native in-app Downloading phase**, the brightly highlighted **TOCAR / TAP** button requests **Pause** from front touch; the same area becomes **Resume** after the worker stops. Hardware SQUARE remains available but is not necessary. This does not unlock navigation, catalog switching, extraction, installation, or shell/PS safety controls.
- The OLED protection display intentionally consumes the **first tap to wake** the full progress UI; touch the bright Pause button **after** the normal dialog reappears. This preserves the existing accidental-tap safety policy.
- Pausing cooperatively stops the active libcurl operation; the worker returns normally and the serial write buffer is flushed.
- For serial downloads, the `payload.part` length is persisted and **SQUARE / Resume** uses the *same* DownloadManager job with a new 64-bit HTTP Range request; no new install is enqueued.
- **CIRCLE / Cancel** while paused deliberately removes the temporary job/files. Pause is never reported as Install Failed or Cancelled.
- An interrupted **2/4-range** transfer now saves its *flushed, contiguous prefixes* separately for every range when a stable ETag or Last-Modified validator is available. Resume reprobes the resource, verifies the size/validator and requests only missing bytes. Completed segments are skipped. Checkpoint is kept in `payload.part.range.resume` until completion or explicit cancel. On validator mismatch, missing/invalid checkpoint, or a server refusing correct HTTP 206 ranges, the partials are deliberately discarded and the original serial fallback remains the safety net.
- Automatic multi-range retries (up to two new rounds) first attempt to continue unfinished pieces. If network retry exhaustion leaves a valid checkpoint, retain its ranges and surface a connectivity pause; **do not** fall back to serial in a way that destroys safe partial data. A missing/mismatched validator or invalid range uses the existing conservative integrity fallback. The **manual touch and SQUARE buttons remain enabled on main**.
- Pauses last **only while PSVitaAlive remains running**. On the next launch, the unchanged startup orphan-download cleanup removes abandoned job files.
- Pausing is not available for system BGDL, self-update, installation/extraction, ZIP recovery or plugins outside the native Downloading stage.
- No extra background worker is introduced. Resume joins the completed previous worker before launching a replacement on the same job.
- This change belongs exclusively to the native PS Vita client and its tests/docs. Catalog source data, generated JSON, GitHub Pages and serverless infrastructure are not modified.

## On-device validation checklist

1. Install the branch's test VPK *only on a test console or after backing up the installed client*, because it has the same title ID `PSVAS1178`; disable automatic client updates while testing.
2. Direct serial HTTP 200 file: pause after measurable progress, confirm progress stops and the partial file remains; resume, verify a 206 starts at the saved offset and final exact size matches.
3. Repeat pause/resume twice during one serial transfer; confirm no double appending or incorrect percentage.
4. Force/observe a server that ignores Range (resumed 200) and a mismatched 206; confirm clean restart without false install success.
5. Pause a 2/4-range eligible ZIP/VPK with a stable server validator; expect **nonzero reused bytes per part**, independent Range starts and skipped already-complete parts. Verify extraction/install integrity. If the server provides no stable validator or changes the resource, confirm safe restart rather than unsafe append.
6. Cancel while paused; verify job and partials are removed and no installation runs.
7. Test Download All sequence: pausing a step must not mark it Failed or advance to the next step.
8. Repeat a direct HTTP PSP/PS1 transfer like the failing report (not Sony Vita BGDL); inspect `content-range`, `HTTP RESULT`, `DownloadManager` and `Installer` markers in session.log.
9. Regression-check MediaFire, Archive.org, GitHub Releases, direct ZIP/VPK, install/extract, TLS retry, insufficient space and cancellation.
10. Test the bright tactile button during a running download and after pausing, using **front touch only**; verify its hitbox never cancels and a swipe across it does not activate it. Check that OLED protection still needs one wake tap and physical-navigation safety still blocks exit/catalog switching. Verify UI in Spanish and English. Other languages currently fall back to English for new translations.

## Parallel checkpoint safety

The metadata binds canonical source URL, authoritative total, number of segments, ZIP/VPK extraction mode, and a strong ETag or a Last-Modified validator. Per-file lengths must fit their assigned intervals; libcurl 206 responses must agree with exact pending offsets and the cached validator. Checkpoints are private temporary installation inputs and intentionally do not survive the existing next-launch cleanup. The pause/resume button is part of the approved user experience in `main`: it supports SQUARE and front touch, while preserving the OLED wake-first tap, installation protections and safe checkpoint validation.

## Expected log markers

```text
[DownloadManager] pause requested job=...
[DownloadManager] paused job=... saved_bytes=...
[Installer] download paused without installation job=...
[DownloadManager] resume queued job=... offset=...
[Installer] resumed existing job=...
[HTTP] parallel verified checkpoint; reuse_bytes=...
[HTTP] parallel retry only incomplete segments attempt=...
HTTP BEGIN ... resume=...
content-range start=... end=... total=... resume=...
HTTP RESULT curl=... status=206 ... absolute=... total=...
```

**Integración autorizada:** el mantenedor reportó resultados correctos en PS Vita real para pausa/reanudación, interrupción Wi-Fi y recuperación. La CI del cliente y la validación de catálogo pasan. Los escenarios no observados en estos reportes siguen requiriendo pruebas de regresión cuando se cambie la implementación.

## Archive.org resume performance and UI status (experimental, 2026-10-26)

The October 26 real Vita `session.log` confirmed three 4-range checkpoint resumes and a complete 255779755-byte Archive ZIP extraction. Each old resume repeated 3 storage-node probes (~16–18 seconds before the multi-range network started).

- **Resume fast path:** keep the previously selected storage edge in this HttpClient instance, keyed by exact canonical URL and job destination. Attempt a *new exact 1-byte HTTP 206 probe* of that edge at each resume. Only skip the expensive multi-node ranking if size, allowed host and protocol match. The range worker itself still validates the saved checkpoint identity and every pending part. If this one-byte probe fails, the original full node selector and serial fallback remain in place. The hint is not saved to disk.
- **Progress:** while searching or verifying the server, the UI now shows localized messages and retains the saved-byte progress rather than appearing idle. The pause/cancel and OLED/shell safety rules have not been changed.
- **Speed:** count only newly transferred bytes from the current HTTP session, never the saved checkpoint total. Calculate a smoothed rate on 750ms-or-longer windows. The existing diagnostic `range_total avg_Bps` remains the measured mean over bytes actually transferred this attempt.
- **Initial selector:** intentionally unchanged pending on-device A/B timing tests; no guessed shortcut to bypass current Archive.org node ranking.

### Recheck on PS Vita

1. Instalar la compilación actual del cliente y probar las reanudaciones en PS Vita real.
2. Start the same ~243.93 MiB Archive.org ZIP, pause near 30%, resume.
3. In `session.log`, check `fast resume edge accepted after fresh 206 probe; full selector skipped` and `parallel verified checkpoint; reuse_bytes=`.
4. Observe visible Spanish/English status messages while waiting, and stable speed during the first few seconds.
5. Repeat near 60% and 85%. Compare timestamp gaps between `[Installer] resumed existing job` and `[HTTP] starting 4-range` with the previous 16–18 seconds.
6. Test connection failure during resume: the log should say `fast resume edge probe failed; retrying normal Archive selector` before using the pre-existing fallback. Verify final download size and extraction integrity.
7. Confirmar que el botón táctil y SQUARE para Pausar/Reanudar **siguen visibles y funcionales** después de la fusión a `main`.

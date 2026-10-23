# ZIP/VPK I/O timing — phase 2 hardware test

Status: experimental / PS Vita hardware validation pending. This branch is based on `experiment/zip-small-files-cache-20261022`. Do **not** merge until testing.

## Goal

Collect aggregate (not per-file) timings from the **unchanged extraction algorithm** in order to distinguish metadata/FS latency, libzip decompression and reads, output writes, and small-file open/close costs.

## Diagnostic output

After extraction, in `ux0:data/psvitaalive/logs/session.log`:

- `[ZipTiming]`: all durations are **microseconds** (`_us`). Divide by 1,000,000 for seconds.
  - `total_us`: complete `ZipExtractor::extract` duration until summary logging, including initial destination validation and archive open.
  - `preflight_us`: archive metadata scans, path safety/staging validation and directory preparation **after archive open**.
  - `loop_us`: entire per-entry extraction loop (including progress, cancellation, diagnostics and cooperative yields).
  - `finalize_us`: libzip archive close and any deep-stage promotion/cleanup after the extraction loop.
  - `dir_us`: measured parent or directory checks/creation in the **per-entry loop only**. Preflight directory preparation is part of `preflight_us`, not `dir_us`.
  - `path_stat_us`: `StorageManager::exists` fallback check on a failed exclusive file creation; **not every filesystem getstat**, and not scans of temporary/backup paths.
  - `zip_stat_us`: `zip_stat_index` within the extraction loop.
  - `zip_open_us`: `zip_fopen_index` per file.
  - `zip_read_us`: `zip_fread` inclusive of libzip decompression, CRC checking and input I/O. Do not treat it as pure storage reading.
  - `io_open_us`: `sceIoOpen` for output files, including failed exclusive attempts.
  - `io_write_us`: `sceIoWrite` for output, including filesystem wait time.
  - `zip_fclose_us`, `io_close_us`: `zip_fclose`, `sceIoClose` per file, including failures.
  - `rename_us`: only individual `sceIoRename` operations for safe replacement of existing files; no temp/backup creation/removal.
  - `yield_us`: 1ms cooperative `sceKernelDelayThread` waits.
- `[ZipTimingCalls]`: number of measured invocations for each operation type.
- `[ZipPerf]`: phase 1 statistics (directory-cache hits, checks, exclusive creations, existing-file replacements, fallbacks and status). Preserved unchanged.

The sums of per-operation times do **not** equal `loop_us`: string/path work, logging, progress, some fallback checks and other costs remain unattributed. Timer overhead itself slightly affects benchmarking. This phase measures where time goes before making further performance changes.

## Test plan on real PS Vita

1. Keep a backup of the current working PS Vita client VPK. New VPK retains Title ID `PSVAS1178` and internal version `02.13`, and will replace that installed app.
2. Pick a repeatable ZIP with **1,000+ small files**. Run on a clean destination using the phase 1 build and record elapsed time, then use the phase 2 profiling build on the same media and with the same test conditions.
3. Record completed entries, extracted bytes, `[ZipPerf]`, `[ZipTiming]`, `[ZipTimingCalls]`, and any errors; compare repeated trials if feasible.
4. Separately validate a ZIP with existing destination files to exercise temp/backup + CRC path. Never delete unrelated data.
5. Check UI responsiveness, cancel behavior, ZIP64/segmented archives and staged deep folders. Do not claim a speedup from lower UI remaining-time estimates.
6. Share `session.log`. Investigate operations with the largest wall time, not just the highest call count.

No generated catalog, web or production release assets are modified.

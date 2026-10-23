# Experimental ZIP small-file extraction — PS Vita hardware test

Branch: `experiment/zip-small-files-cache-20261022`. Not for release. Never merge without hardware validation.

This patch only changes the PS Vita client's ZIP/VPK extraction path:
- Bounded 256-entry per-extraction directory cache, reducing repeated `sceIoGetstat` calls when ZIP entries alternate between parent folders.
- Exclusive-create fast path for NEW output files (avoids `exists()` stat per file).
- Existing-file replacement still uses verified temporary file, backup, and rename.
- If a Vita filesystem rejects exclusive creation for an apparently new path, the extractor reverts to the prior direct-create behavior for that extraction. This protects compatibility but must be measured on real `ux0:` and `uma0:`.
- One compact end-of-extraction diagnostic line includes cache hits/checks and exclusive-create/fallback counts.
- The 256 KiB extraction buffer, libzip CRC/size checks, ZIP64/segmented reading, deep-path staging, thread yielding, 250 ms progress and cancellation logic remain unchanged.

## Hardware validation
1. Save `session.log` from the original release for a repeatable ZIP with 1,000+ tiny files.
2. Install the experimental VPK on your own console (it retains the existing Title ID, so it replaces the installed client). **Keep an official VPK backup** and do not distribute this unvalidated binary as a release.
3. Extract the same ZIP to a CLEAN folder (not populated save/game data). Compare elapsed time, per-entry count, and destination file hashes with the baseline.
4. Test a ZIP with alternating nested directories and another with existing destination files; confirm the replaced content and any unrelated files are preserved.
5. Verify cancellation, resume/retry, CRC failure, unsupported ZIP compression, ZIP64, segmented VPK, and deep-folder staging.
6. Compare `[ZipPerf]` log line(s). Exclusive fallback should normally be zero on new-file installs. Any fallback must be investigated; do not claim a speedup without hardware measurements.

This is an experiment only; no registry, generated catalogs, web assets or GitHub Pages have been edited.

# Large ZIP extraction reliability and saved-archive retry — 18 October 2026

**Status:** Experimental VitaSDK build; real-device regression test still required.
**Scope:** PSVitaAlive native client only. No changes to canonical catalog JSON or GitHub Pages.

## Reproducing the reported failure

`session.log` of PSVitaAlive 02.11 shows:
- **Simpsons Hit & Run**, VPK `SimpsonsPAL.vpk` installed successfully, Title ID `ROADRAGE2`.
- **Game Files Spanish.zip**: valid 4-range Archive.org transfer, total **1,957,085,950 bytes**.
- Network transfer complete at `[2746834 ms]` but verified, joined archive not available until `[3468087 ms]`: approximately **721 seconds** (12.0 minutes) of local hash and append work.
- ZIP64-capable sceIo source successfully opens the ZIP; preflight reports approximately 1.956 GB of uncompressed bytes and roughly 39 GB free space. This excludes low storage as the observed primary error.
- Each of five extraction attempts makes progress through ~1,472 ZIP entries before `cannot create file: ux0:data/simpsons/art/frontend/scrooby2/resource/images/cards/db/_stubs/card00_t.png`. The same destination fails in every attempt, so it is a **deterministic path/creation failure**, not the signature of temporary ZIP tail lag or a network retry.
- The archive is **preserved** in `ux0:data/psvitaalive/manual/ROADRAGE2/Game Files Spanish.zip`; the companion `.info.txt` stores `App ID: simpsons-hit-run`, title ID, and `Manual extraction target: ux0:data/`.

The original log has no `sceIoOpen` numeric error, so the precise filesystem reason (missing directory, file-vs-directory collision, Vita filesystem constraint) is not proven yet. Do not claim ZIP corruption from this trace.

## Changes

### Deterministic folder preflight

The native extractor now walks the ZIP central directory before writing payload data. It resolves **every entry** with the existing path-safety rules, collects parent directories (and explicit directory entries), sorts/deduplicates them and attempts to create them **before** the first file is extracted.

If a required parent fails this check, extraction stops immediately, logs the exact failing directory and preserves the ZIP without five redundant full extracts. Existing files at directory paths are treated as collisions; files are never automatically removed to make room for archive entries.

The `sceIoOpen` create-file error now includes the full return code and parent directory status. Deterministic `cannot prepare ZIP directory`, `cannot create file`, `mkdir failed` and `sceIoWrite failed` errors stop the automatic install retry loop; transient ZIP EOCD/ZIP64 and other recoverable errors retain their bounded retry policy.

This early folder layout check can help with directory creation sequencing on PS Vita storage, but real-device tests determine whether it fixes the concrete `_stubs` path.

### Local retry of a saved ZIP (no new download)

When an app has a saved recovery ZIP under `ux0:data/psvitaalive/manual/<TITLE_ID or app ID>/`, and a small `.info.txt` matches both `App ID` and `Title ID`, the detail screen displays a **Saved ZIP** action above **External Download / QR**.

- The action is available through D-pad link navigation and touch.
- The scan is bounded to a matching recovery folder, a maximum of 128 entries and metadata files no bigger than 8 KiB, with a five-second in-memory UI cache to avoid scanning every frame.
- The action uses the **original recorded extraction target** and launches a standard Vita install worker; it does not access the internet, perform any download or recombine any ranges.
- Failure preserves the ZIP and offers the existing Keep/Delete recovery dialog.
- Success deletes the preserved ZIP and its companion metadata to return space on `ux0:`; no version receipt is changed for data-only ZIPs.
- Navigation and status safety barriers prevent simultaneous installer workers. Localization is available in all eight bundled languages.

If a saved ZIP was manually moved or its metadata is missing, the section is hidden: this prevents operating on a mismatched application or destination.

### Large-file range assembly

The existing ordered four-range merger uses a bounded **1 MiB streaming buffer** (previously 256 KiB) to reduce `sceIoRead`/`sceIoWrite` calls on multi-gigabyte files. **The first range's SHA-1 is now computed while its bytes are being received**, so the post-download join no longer needs to reopen and hash approximately one-quarter of the file. The remaining ranges are hashed in order while they are appended. SHA-1 validation required by the Archive.org direct-HTTP path remains enabled; all expected file-size/range checks remain.

The code now logs the time spent hashing the first range and separately the time spent appending remaining ranges. **No quantified speedup is claimed yet.** A virtual fragmented reader that avoids physical assembly is a possible *future* architecture but requires careful ZIP64 and integrity validation and has not been adopted in this change.

## PS Vita regression plan

1. Install this experimental VPK over 02.11, open **Simpsons Hit & Run**. Verify that **Saved ZIP / Retry extraction** appears before the external-download section.
2. Without downloading, activate the button. Confirm `[Installer] offline ZIP retry started`, `[ZipExtractor] directory preflight unique=` and progress UI. The 1.96 GB archive must be preserved if the filesystem still refuses the `_stubs` directory.
3. If failure repeats, report `cannot prepare ZIP directory` or the new `sceIoOpen=0x...` code; do not delete the saved ZIP until diagnostics are captured.
4. If successful, verify all files and that the saved archive and `.info.txt` have been removed after extraction.
5. Test a fresh large Archive.org download and look for `range0 SHA1 streamed during transfer` (or fallback `range0 SHA1 read done elapsed_ms`) plus `ranges appended elapsed_ms` with the previous 721-second combined measurement.
6. Test VPK installs, ZIP64 archives, ZIPs with nested directories and 1,000+ entries, extraction failures, resource pressure, QR selection and the existing `Install All` lifecycle guard.
7. Compile with VitaSDK and run existing repository tests. CI proves compilation, not real-world extraction correctness.

## Compatibility

The only source-of-truth structure remains `apps/`, `authors/`, `categories/` → GitHub Actions → generated catalogs → client/web. Generated JSONs are not manually edited. The feature does not change catalog metadata or any existing download links.

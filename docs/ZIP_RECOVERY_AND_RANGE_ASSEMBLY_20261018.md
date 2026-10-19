# ZIP recovery, directory-depth mitigation and saved archives — 19 October 2026

## Hardware-validated result

Physical PS Vita test of experimental PR #37:
- Recovery action selected `ux0:data/psvitaalive/manual/ROADRAGE2/Game Files Spanish.zip` (1,957,085,950 bytes), and began direct offline extraction, without networking.
- At `[4687339 ms]` archive preflight enabled **one staged branch** under `ux0:data/PSVA_ZIP_STAGE_29741690_0`.
- At `[5640195 ms]` directory rename succeeded to the original `ux0:data/simpsons/art/frontend/scrooby2/resource/images/cards/db/_stubs` with **56 files**.
- At `[5640220 ms]` the extractor reported **3,877 entries, 1,956,255,106 bytes**, completed successfully.
- The maintainer verified correct game-folder size and reported the Simpsons Hit & Run port **launches correctly** on PS Vita.
- Extract duration approximately **15m 56s** (`[4684429 ms]` to `[5640220 ms]`). This is a local re-extraction benchmark, not network download time.
- Original experimental build logged `archive_deleted=no` after success, because it intentionally preserved a ZIP whenever folder staging was used. The new cleanup changes deliberately remove this exception.

The test demonstrates a successful complete-folder move followed by functioning game startup **for this particular archive and PS Vita**. It does not prove arbitrary ports, ZIP layouts, devices, or pre-populated deep directories universally work. Previous separate VitaShell testing could move deep directories even when subsequent manual deep-file access returned `0x80010016`; installer success cannot guarantee every loader's subsequent reads. Do not overwrite valid user data to mask that distinction.

## Algorithm

1. Open archive through the Vita ZIP64-capable `sceIo` source and validate member names, compression and space.
2. Count path components after the device prefix. When a member exceeds ten in the current device-tested policy, group it into a distinct branch at component depth ten.
3. Create a unique shallow staging area **on the same device** (`ux0:` or `uma0:`), with a subdirectory per affected branch, and preflight all real and temporary parent directories. Reject traversal paths.
4. Write ordinary members directly and affected members into their staging branch. ZIP archive entries, bytes and relative directory structure are preserved.
5. After successful extraction and archive closure, move each *complete* branch via `sceIoRename` to its original directory, without per-file deep writes. Existing nonempty destinations are never silently merged or deleted; if blocked, report error and preserve work for recovery.
6. Log each branch move and the final extraction count. For a successful extraction *including all branch moves*, clean the source ZIP; keep ZIPs on failure.

## Per-application Saved ZIP UI

- The section is hidden unless at least one valid matching ZIP is discoverable.
- Each ZIP appears as its **own button with its filename**, supporting touch and controller selection. Count changes recompute layout height, link focus indices and QR position.
- Bounded scan (128 directory entries per matching folder, maximum 32 saved archives), `.zip.info.txt` sidecars no larger than 8 KiB, nonempty ZIP, matching **App ID** and **Title ID** when known, and safe absolute extraction target.
- Candidates are sorted by filename, checked again immediately on selection, and extracted using original target **without re-downloading**. Other apps' ZIPs cannot be selected by passing a foreign filename.
- Successful extraction removes only the selected ZIP and its sidecar, including when a deep-directory move occurred. Failed retries retain the archive and report its path; no silent deletion of unrelated ZIPs.
- Fresh ZIP downloads use ordinary download-job cleanup once extraction succeeds. Older ZIPs retained by previous builds are not automatically deleted; they remain available as separate recovery buttons.

## Limitations and regression checklist

- Existing nonempty destination branches are deliberately protected against destructive overwrite. Updating an already populated deep branch can fail cleanly instead of merging; this needs separate safe-update design.
- Multiple branch moves are not all-or-nothing: an error after one move may leave some new data in its destination and other data in staging. Logs indicate the preserved staging base, and source ZIP stays available.
- A short-lived UI cache may take up to five seconds to rescan after external filesystem changes; successful in-app retry invalidates the cache.
- Verify touchscreen and gamepad selecting the first/last of multiple ZIPs, scroll/QR offsets, zip membership isolation, saved ZIP cleanup and partial errors, and ordinary ZIP/VPK updates.
- The storage design still uses the canonical `apps/`, `authors/`, `categories/` inputs and generated catalogs. None of the generated JSONs are manually edited.
- Range joining retains SHA-1 validation. Separate Conker ZIP benchmark (67,109,198 bytes) previously measured **56.91 s download, 20.99 s append/checksum, 15.29 s extraction**; a 2 GB post-optimization assembly benchmark remains pending.

## Core files

- `Client PSVitaAlive/source/archive/zip_extractor.cpp`: deep-folder stage and whole-directory rename.
- `Client PSVitaAlive/source/installer/install_controller.cpp` + `include/installer/install_controller.hpp`: recovery listing, validated selection, offline worker, cleanup.
- `Client PSVitaAlive/source/ui/full_catalog_screen.cpp` + `include/ui/full_catalog_screen.hpp`: variable list layout, touch/gamepad, name display and cache.
- `Client PSVitaAlive/source/main.cpp`: callbacks.
- `Client PSVitaAlive/source/network/parallel_archive_download.cpp`: SHA-1 streamed for the first range and larger merge buffer.

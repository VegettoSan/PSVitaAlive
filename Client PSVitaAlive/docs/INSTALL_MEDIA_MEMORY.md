# GPU media memory during downloads and installations

## Scope

PSVitaAlive temporarily releases **catalog image GPU textures** (Homebrew icons,
game covers and screenshots) while an actual transfer/extraction/installation is
running. This is a runtime-only RAM/VRAM optimization; it does **not** delete
normalized images from `ux0:data/psvitaalive/cache/images/v3/`, alter JSON
catalogs, or change image URLs. UI fonts, brand/header art and install progress
graphics stay available so the installer remains readable.

## Lifecycle

1. **Before starting an install:** Both the primary Install callback and
   individual link actions (VPK, ZIP, PKG, PSP/PS1, plugin, Install All) call
   `FullCatalogScreen::prepareForHeavyWork()` *after* user choices and
   validation but *before* `InstallController::requestInstall()`. The call
   blocks briefly on completed GPU rendering, then drains `textures_` and
   `deferredFreeTextures_` using the existing `releaseTextures()` path.
2. The image cache cancels queued/in-flight **image** fetches. This does not
   cancel the user's file download or delete completed cached images. The
   existing `ImageCache::setNetworkPaused` behavior in `main.cpp` still
   pauses image networking during `Downloading` / `Installing`.
3. **While active:** The UI never decodes new catalog textures or draws stale
   icon/cover/screenshot textures. It shows the existing neutral rectangle
   placeholder instead. No per-frame texture free/reallocation loop occurs.
4. **When activity ends:** The state is reevaluated from the install progress,
   in-flight self-update, Install All queue and essential-plugin queue.
   `Completed`, `Failed`, `Cancelled` or a rejected start eventually
   lift the suspension when there is no other active job. The result modal may
   remain visible; textures reload on demand afterwards, at the existing
   **maximum of one decoded texture per frame**. Disk cache hits need no
   re-download.
5. **Self-update:** The existing self-update progress call uses the same
   suspension logic before its download/install worker starts. A multi-step
   Install All keeps suspension across the gap between successful steps.

## Boundaries and safety

- `textures_` contains normalized catalog media, with `/app_` for
  icons/covers and `/shot_` for screenshots; the cache keeps no raw full
  resolution textures in the UI.
- All texture frees run on the **UI thread**, via the existing GPU-waiting
  `releaseTextures()` path. Do not free them from `InstallController`,
  `ImageCache`, or any download worker.
- `prepareForHeavyWork()` is idempotent. Repeated progress polls do not
  release textures each frame.
- No persistent setting or metadata change is needed. The existing bounded
  texture LRU and deferred free rules apply normally after resuming.
- GPU texture count is logged, but a precise number of **bytes** saved is not
  claimed: vita2d allocation overhead / tiling varies by device and emulator.

## Diagnostic messages

Search `session.log` for:

- `[UI] catalog media suspended` — transition into low-memory mode;
  includes the number of resident/deferred textures released.
- `[UI] catalog media resumed` — return to on-demand image rendering.
- `[ImageCache] network paused` / `network resumed` — existing
  installation bandwidth controls, independent of GPU texture suspension.

## Manual validation on Vita / Vita3K

1. Open Homebrew full catalog, browse enough entries to load icons, then open a
   detail page with screenshots. Start a VPK download. Confirm the suspension
   log appears once and icons/screenshots become placeholders; the download,
   progress bar, cancellation and extraction continue.
2. Finish successfully, close the result overlay, return to catalog/detail.
   Verify icons and screenshots reappear gradually without re-downloading
   files already in the on-disk image cache.
3. Cancel a download, and separately test a failed download or ZIP extraction.
   Verify images return after closing the result UI.
4. Start **Install All** with multiple steps. Confirm there is no
   suspend/resume/suspend churn between consecutive installs and a single
   resume after the last step.
5. Try a plugin install, a PSP/PS1 link and a self-update. Confirm progress
   overlays remain visible and media textures are not loaded during work.
6. Attempt an install rejected by preflight (e.g. no space). Confirm the UI
   eventually resumes image rendering rather than remaining stuck in
   low-memory mode.
7. Use the existing VitaSDK GitHub Actions client build and run these checks
   on real hardware where possible. No measured RAM/VRAM savings are asserted
   until tested with device telemetry.

## Source locations

- `include/ui/full_catalog_screen.hpp`: state and public preflight hook.
- `source/ui/full_catalog_screen.cpp`: transition logic, GPU freeing and
  draw/decode suppression.
- `source/main.cpp`: preflight hook from both installer entry points.

This behavior is intentionally independent of the GitHub JSON catalog
generation pipeline.

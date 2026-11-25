# PSVitaAlive — UI redesign integration (2026-11-24)

## Scope and acceptance

- Target: `main`; source: `feature/ui-redesign`; existing pull request [#114](https://github.com/VegettoSan/PSVitaAlive/pull/114).
- Hardware-approved source revision: [f1ab96f5](https://github.com/VegettoSan/PSVitaAlive/commit/f1ab96f5cf363c3856b46ad67e24229001501ab1).
- [Build #788](https://github.com/VegettoSan/PSVitaAlive/actions/runs/45517675244): successful VitaSDK compilation and CI tests, including UI skin checks. The project maintainer tested the resulting VPK on a PS Vita and approved the final appearance and integration. This is qualitative acceptance, not a quantified benchmark or a guarantee for every device.
- Before documentation, the comparison against main showed 25 branch-only commits and 14 main-only commits. The main-only changes were catalog entries, generated catalog and Discord state; the branch-only changes were client UI, localization, build checks and tests. No changed path overlapped in that comparison. Preserve both histories with a normal merge.

## Visual system

The client keeps its existing layouts, navigation model, brand identity, and dark surfaces. The sprite-based skin uses the existing UI, icon and control atlases; the renderer draws scalable nine-slice surfaces for panels, dialogs, buttons, badges, focus rings, progress and scroll indicators. Reuse the existing sprites rather than adding duplicated assets. Keep the fallback rendering paths and clipping behavior.

The branch improves visual consistency in the full catalog and split detail view, feedback and QR dialogs, loading screens, action hints, installed-data and storage views, settings and theme picker. File-type badges, controller hints and localized text keep their semantic distinctions. The catalog card focus must only be drawn when the catalog navigation zone owns focus; the split panel frame must not obscure content. Topmost storage modals render without the underlying catalog or hidden storage dialog.

### Theme picker — final approved behavior

The same layout geometry is used for drawing and touch hit-testing. Each theme button retains its own accent swatch and dark interior. A check mark denotes the active theme, not the navigation focus.

- `themeSetupFocus_`: navigation cursor (including the Save row).
- `settingsEdit_.colorTheme` and `themeSetupAppliedFocus_`: explicitly applied palette; initialized from the current theme on entry.
- Persistent settings: written through `settingsSave_(settingsEdit_)` only on confirmation/exit from the picker.
- Moving with the D-pad or scrolling does not apply or save a candidate.
- Press X on a different theme to apply it in memory. The existing palette cross-fade changes the UI and the focus accent.
- Press X on the already-applied theme, press Circle, or activate Save to persist the last applied theme and close. Touch selection/confirmation obeys the same rule.
- Example: start with PSVitaAlive, apply Cyan with X, move focus to Rose, press Circle: Cyan remains active and is saved. Reopening/restarting must retain Cyan. Navigating without applying a candidate preserves the original theme.
- Focus uses the current global `ACCENT` (including its existing palette transition), **not** the candidate's swatch. The focused card and Save use the existing filled button sprite as a rounded 4-pixel frame, with a dark inner surface (10% accent tint) and a subtle lighter inner outline. Labels remain light. Unfocused cards have a subdued accent outline. No white-filled focus button, additional atlas, or new animation.
- The save path must never copy `themeSetupFocus_` into the persistent theme. `applyThemeSetupFocus()` is the explicit application path. The same renderer helper is used for the grid and Save.

## Performance and resource management

- `skin_renderer.hpp`: nine-slice geometry is submitted in one textured draw, with GPU-visible vertices allocated from the vita2d pool. Use 16-byte alignment, geometry and capacity checks. The 2 MiB frame pool reserves at least 1 MiB at frame start; recycle only after a proven GPU idle, not an unconditional per-frame wait/reset. Maintain scene and clipping ownership. No extra framebuffer or atlas is introduced by the final theme fix.
- `detail_wrap_cache.hpp`: bounded detail wrapping cache avoids repeated layout work. Keep text measurement compatible with the existing font and display settings.
- Detail links outside the visible viewport skip drawing while retaining layout height and navigation indices. Saved ZIP lookup results are cached rather than repeatedly probing during drawing.
- `image_cache.cpp`: UI requests enqueue work; disk checks, normalization validation and pruning stay off the request path. Decode attempts are budgeted, including texture replacement; keep LRU and failure handling. PNG normalization uses the fast flag and production error logging remains.
- Storage scanning uses existing progress sprites and actual worker-phase labels; the indeterminate indicator is not a simulated percentage. Maintain the current installer and file safety semantics.

These are implementation changes, not measured FPS/RAM results. Benchmark and test on hardware before claiming performance figures.

## Main files

- `Client PSVitaAlive/include/ui/skin_renderer.hpp`, `detail_wrap_cache.hpp`, `navigation_focus.hpp`, `storage_layout.hpp`, `image_cache.hpp`, `full_catalog_screen.hpp`
- `Client PSVitaAlive/source/main.cpp`, `source/ui/full_catalog_screen.cpp`, `source/ui/image_cache.cpp`, `source/ui/installed_data_screen.inc`
- `Client PSVitaAlive/assets/lang/` (eight existing language files: de, en, es, fr, it, pt-BR, pt-PT, ru)
- `tests/test_ui_skin_coverage.py`, `tests/test_detail_wrap_cache.cpp`, `tests/test_navigation_focus.cpp`, `tests/test_storage_layout.cpp`
- `.github/workflows/client-build.yml` (build and regression checks)

## Validation and maintenance

Run the client-build workflow with the official VitaSDK image. The skin source regression checks cover theme focus and save semantics, sprite use, clipping, GPU arena recycling, image worker and decode limits, storage phases and navigation focus. They complement, not replace, physical-console tests.

Manual regression checklist:

1. Open the theme picker, move focus without pressing X, and exit with Circle; verify the original theme is retained.
2. Apply Cyan, navigate to Rose, and exit with Circle; verify Cyan persists after restart.
3. Apply another theme, then press X again or use Save; verify persistence. Repeat with touch.
4. Check focus contrast for dark, vivid and light-accent themes, including the Save row. Check row scrolling, text clipping and the independent check mark.
5. Exercise split catalog/detail navigation, screenshots, settings, feedback, QR, storage/installed-data views and loading indicators; check that existing actions still work and no modal draws stale layers.
6. Monitor compilation, crashes, responsiveness and memory on Vita; do not infer performance measurements from static tests.

## Catalog and release boundaries

This integration changes client code, UI tests and documentation only relative to the existing main catalog. Keep the canonical `apps/` + `authors/` + `categories/` → GitHub Actions → generated catalogs architecture. Never manually edit generated catalogs; preserve unrelated main-side catalog updates.

Merging the branch does not itself request a new version number, tag or public Release. Publish a release separately only after reviewing the merged-main build and its release requirements. Preserve the merge history for traceability.

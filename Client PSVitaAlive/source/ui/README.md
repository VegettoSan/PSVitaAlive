# `source/ui/` — Native UI

Rendered with **vita2d** (960×544). Default accent is the store green (`#3BFF00`); users can switch **color themes**, **UI fonts**, and **language** in Settings.

## Main surface

`FullCatalogScreen` covers:

- Full catalog grid and split detail view
- Catalog/startup loading overlays (splash art when configured)
- Settings (install method, PSP/PS1 target, PSP media, **language**, **UI font**, **color theme**, plugin warnings, image warmup, self-update)
- Download / install progress and result overlays (success, failure, **Download cancelled**, ZIP complete)
- Install All wizard and mirror/link pickers
- Search and catalog switching
- Embedded, themed `SearchKeyboard`: English QWERTY with localized controls, used for search and custom ZIP destination paths (no Sony IME)
- News modal (`news.txt`)
- First-run / Settings **theme picker**
- Essential plugins modal and plugin **reboot** modal
- Report / data-request confirms

## Built-in QWERTY modal (Sony IME retired)

**Code:** `include/ui/search_keyboard.hpp`, `source/ui/search_keyboard.cpp`, `source/ui/full_catalog_screen.cpp` and `source/main.cpp`.

`SearchKeyboard` is a **single UI-thread input state machine**. `FullCatalogScreen::openSearchKeyboard()` starts catalog searches; `FullCatalogScreen::promptTextBlocking()` reuses the same keyboard when the ZIP destination picker requires a custom path. Confirmation returns entered text; cancellation leaves the current search / destination unchanged.

- `FullCatalogScreen::updateAndDraw()` routes gamepad/touch events exclusively to the keyboard while active. Input and image-texture preparation for the catalog are not advanced under the modal.
- `FullCatalogScreen::draw()` renders the current catalog view. `finishUiFrame()` then calls `keyboard_.drawOverlay()` **before one normal frame end/swap**, keeping the catalog visible through a dim layer instead of clearing the screen to black. No separate keyboard texture or `vitaGL` dependency.
- Palette: `BG`, `SURFACE`, `SURFACE2`, `BORDER`, `TEXT`, `DIM` and `ACCENT` follow the selected theme. Keyboard titles, hints and action captions load `KEYBOARD_*` keys through the usual localization manager; the keymap remains English QWERTY.
- Geometry: right-hand **SHIFT, DEL, CANCEL, OK** column, equal **22 px** left/right inner margins, **7 px** separator. Existing character row order is preserved. Compile-time geometry assertions prevent overlap.
- Navigation: D-pad **or left joystick** (dead-zone thresholds matching the catalog), X selection, touch positions mapped from native 1920×1088 logical units. Triangle switches ABC/123, `MORE` exposes extra symbols, L or SHIFT toggles **persistent case**. Start/OK confirms; Circle/CANCEL discards.
- Backspace: a tap removes one character; hold Square or the visible DEL button and it repeats after ~420 ms, roughly every 105 ms until release. Case labels follow the next character to be entered.
- Entry/exit uses a ~230 ms smoothstep slide. Layout and buttons are not language-specific; UI labels support all eight packaged languages. No candidate/predictive/complex IME is provided.

**Do not reintroduce** `sceImeDialog*` or `vita2d_common_dialog_update` into the search flow. The binary still links `SceCommonDialog_stub` for **vita2d static linkage only**; Sony promoter/PAF modules required by installation/update remain independent. For crash history, rationale and validation: [native IME replacement decision](../../../docs/KEYBOARD_NATIVE_IME_REPLACEMENT_20261020.md).

## Battery indicator

`FullCatalogScreen::drawHeader()` centers the icon and percentage as a **single measured block** between the filters/search edge and the clock. Filter width (192 px), clock position and touch hitboxes remain unchanged; search yields the reserved space. If filters are hidden, the expanding search box becomes the left boundary.

The OLED protection overlay reuses `drawBatteryIndicator()`. `batteryDisplayState()` caches VitaSDK power readings for 15 s, indicates charging and <=15% battery, and uses `--%` for unavailable data. There are no new textures/threads/network requests and no changes to installer or suspend/power locks.

Implementation, hardware validation and regression scenarios: [`../../docs/OLED_PROTECTION_MODE.md`](../../docs/OLED_PROTECTION_MODE.md), [PR #41](https://github.com/VegettoSan/PSVitaAlive/pull/41).

## Image cache v3

The UI image cache is implemented in `image_cache.cpp` / `image_cache.hpp` and is documented in detail in [`../../../docs/IMAGE_CACHE.md`](../../../docs/IMAGE_CACHE.md).

Current behaviour:

- Images are loaded **on demand** and reused from disk when present.
- Catalog-aware scopes are `H` (Homebrew), `PV` (PS Vita), `PSP`, and `PS1`.
- Each icon/cover/screenshot has a **stable resource identity** independent of its current URL.
- Cache files include both the stable resource identity and the current URL hash.
- App/icon/cover images are normalized with a maximum dimension of **128 px**; screenshots are normalized with a maximum dimension of **256 px**, preserving aspect ratio.
- If an image URL changes, the new version downloads first; after successful validation, only older versions of that **same resource** are removed.
- A changed icon cannot remove a cover or screenshot, and one catalog cannot remove another catalog's images.
- Resource files are spread over **256 buckets** under `ux0:data/psvitaalive/cache/images/v3/` so per-image replacement does not scan the whole cache.
- Existing `/app_` and `/shot_` path classification remains compatible with the texture-management logic in `FullCatalogScreen`.
- UI-first scheduling defers new network image requests while catalog/detail scrolling is still animated, while allowing already-ready cached textures to continue loading under the one-texture-per-frame budget. Off-screen active image transfers are cancelled through the existing libcurl cancellation callback, and worker progress publication is limited to 10 Hz to reduce mutex pressure during navigation.

### Startup disk cap

Global cache-size maintenance happens **only at startup**, before the ImageCache worker is created:

```text
cache <= 200 MiB
→ no global eviction

cache > 200 MiB
→ remove oldest complete image files
→ leave approximately <= 40 MiB
```

The first pass only measures the cache and removes abandoned `.normalized` temporaries. The more expensive metadata collection + sort pass is created only when the 200 MiB limit has actually been exceeded.

Deletion is always whole-file `sceIoRemove()`; images are never truncated to hit an exact byte target. If a deletion fails, its size is not deducted from the running total.

### Startup progress UI

`ImageCache::init()` accepts a startup-maintenance callback. `main.cpp` maps the cache phases to the existing loading overlay:

```text
Checking image cache...
Cleaning old cached images...
Image cache ready
```

The overlay is redrawn with `FullCatalogScreen::updateAndDraw()` while maintenance runs. Progress is based on scanned buckets during checking and whole-file cleanup work during eviction—not partial bytes from an image file.

After startup maintenance, the normal image path is unchanged: an evicted image is simply a cache miss and downloads again when requested.

## Color themes

- Many distinct named palettes (`ColorTheme` in `app_settings.hpp`).
- First launch: theme grid before News (`theme_setup_done`).
- Settings opens the same grid (not a simple Left/Right cycle).
- **Preview then confirm:** first X/tap previews; second activation on the same theme **or** **Save** commits.
- **Cross-fade (~420 ms):** `applyColorTheme(..., animate)` interpolates BG, SURFACE*, PANEL, BORDER, TEXT, DIM, ACCENT* with smoothstep (`tickThemeBlend` in `updateAnimations`).
- Startup / config load uses `animate=false` (instant).
- Brand full-colour logo/splash only for **NeonLime / PsVitaAlive**; other themes use monochrome assets + accent tint.

## UI fonts

`ui_font.cpp` / `ui_font.hpp`:

| Style | Load path |
|-------|-----------|
| Default | `vita2d_load_default_pgf()` |
| Serif / Sans / bold variants | `sa0:data/font/ltn{0,2,4,6}.pgf` |

Config: `ui_font_style`. Missing system files fall back to Default.

## Multilanguage

Strings go through `LocalizationManager` + `TextId` + `app0:lang/*.lang`.

Currently packaged language files are:

```text
en, es, fr, de, it, pt-PT, pt-BR, ru
```

The internal language registry supports additional Vita languages, but a language is selectable only when its `.lang` asset is actually present.

Catalog JSON is not translated. See [`../../../docs/MULTILANGUAGE.md`](../../../docs/MULTILANGUAGE.md).

### Startup localization ordering

`LocalizationManager` is initialized in `main.cpp` immediately after installer/settings initialization and **before** image-cache startup maintenance. This allows the cache checking/cleaning messages to appear in the user's selected or System-resolved language before normal catalog loading begins.

The cache-maintenance strings have compatibility fallbacks for all currently packaged languages. If a `.lang` later defines those same keys, the normal `.lang` value wins.

## Progress overlay & lock messaging

While a download/install job is active:

- Phase hints (connecting, downloading, extracting, installing, retries)
- Large-type **LOCKED** banner: PS button and soft power menu disabled; screen stays on
- CIRCLE cancels (in progress) or acknowledges (result); other keys toast LOCKED
- Touch must match resized panels (do not leave hitboxes on old coordinates)

Startup image-cache maintenance uses the same loading-overlay infrastructure but is not an install/download lock state.

## Theme / News / Settings scroll

Theme picker and Settings list use the same **stepped touch scroll** model as the main catalog (drag moves focus like D-Pad). News uses its own line scroll.

## Catalog card text

Long titles use ellipsis / marquee with **parent clipping** so names do not spill outside card bounds while scrolling.

Install/update status badges follow the same ownership rule. `drawInstallBadge()` receives the clipping rectangle owned by its caller:

- catalog cards pass **card ∩ catalog panel**;
- the detail header passes the **detail panel**;
- badge background and breathing glow are clipped before drawing;
- long localized `BADGE_UPDATE` strings keep their marquee, but its scissor is intersected with the caller clip;
- after marquee drawing, the caller-owned clip is restored before returning.

This prevents localized labels such as `ACTUALIZACIÓN`, `AGGIORNAMENTO` or `ОБНОВЛЕНИЕ` from appearing above/below the catalog while a card is partially entering or leaving the viewport during smooth scrolling. Do not restore a badge marquee to full-screen clipping.

Update-state semantics, receipt compatibility and Vita `APP_VER` normalization are documented in [`../../../docs/UPDATE_DETECTION.md`](../../../docs/UPDATE_DETECTION.md).

## Plugin UI

- Link row: **Installed** badge when file (+ config line when required) already present; press → toast, no re-download
- Install All offers an explicit **Install/Reinstall or Skip** decision for **each**
  catalog Plugin link, including a link detected as already installed; merely
  finding a binary/config line does not verify its version.
- Post-install **Restart required** modal blocks background touch until soft reset
- Essential plugins modal: large type, pulsing **Install plugins** border

Catalog plugin detection now requires a **nonempty file + its active exact `config.txt` line** when a section is declared; `section: none` remains file-only. Badges use a cached per-detail snapshot rather than filesystem calls per frame. The AutoPlugin orphan-file fix was **validated on real PS Vita**; details and regression checklist: [taiHEN plugin detection correction](../../../docs/PLUGIN_DETECTION_CONFIG_20261023.md).

## Settings INFO

INFO panel documents each focused option (install method, PSP target/media, language, font, theme, plugins, images, updates). SYSTEM block lists plugin detection status with larger type.

## Settings performance

Settings is deliberately **render-only after entry**. Opening Settings cancels queued/active catalog image work, snapshots plugin status once, and then renders the SYSTEM status block from RAM. `drawSettings()` must not call filesystem-backed plugin probes such as `essentialPluginFullyInstalled()` or `TaiConfigEditor::configContainsLine()` because those operations open/read `ur0:tai/config.txt` and can collapse frame rate on real hardware.

`updateAndDraw()` also skips `prepareVisibleTextures()` while `UiMode::SETTINGS` is active. Existing app textures remain resident in the bounded LRU and image preparation resumes automatically after Settings closes.

### D-pad navigation-aware image scheduler

`FullCatalogScreen` treats held UP/DOWN as continuous navigation even during the controller repeat delay. While navigation is busy it cancels image work once, starts no new network request or GPU decode, and keeps resident textures drawable. App textures that leave the viewport remain in the existing 18-entry LRU; off-screen screenshots are still released immediately.

## Multiple recovered ZIP buttons in the app detail (October 2026)

The optional **Saved ZIP** block appears only when `InstallController` discovers recovered archives with matching app identity and safe `.info.txt` metadata. It renders one button per ZIP using its actual filename, above External Download / QR. Each row is independently focusable by gamepad and touch; height, scrolling and indices account for the actual number of saved archives.

The discovery list is cached for five seconds on the UI thread to avoid repeated filesystem scans every frame. Selecting a row passes that exact archive path back to the installer, which checks it against current validated recovery metadata before starting the offline worker. After successful extraction the ZIP/metadata is removed and the cached list refreshes. Failure retains the files for retry.

See [ZIP recovery and folder relocation](../../../docs/ZIP_RECOVERY_AND_RANGE_ASSEMBLY_20261018.md).

## Install All and Saved ZIP actions (current)

`Install All` builds its queue from **chosen** links, not the largest
alternative in the catalog. When any selected link has no size, the queue
switches to per-download storage checks; when all sizes exist, it first
checks the combined budget. Insufficient space appears as a dedicated
localized overlay. [Technical contract](../../../docs/INSTALL_ALL_SPACE_PREFLIGHT_20261025.md).

Saved ZIP rows may represent a complete ZIP or 2/4 raw byte-range files.
Selecting one shows **Retry extraction / Delete / Cancel**; deletion needs a
separate confirmation and covers every range and metadata. Offline retry
preserves the archive after failure and cleans it after success.
[Archive format and recovery](../../../docs/SAVED_ZIP_SEGMENTS_20261025.md).

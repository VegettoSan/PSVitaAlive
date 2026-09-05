# `source/ui/` — Native UI

Rendered with **vita2d** (960×544). Default accent is the store green (`#3BFF00`); users switch **color themes**, **UI fonts**, and **language** in Settings.

## Main surface

`FullCatalogScreen` covers:

- Full catalog grid and split detail view
- Catalog loading (splash art when configured)
- Settings (install method, PSP/PS1 target, PSP media, **language**, **UI font**, **color theme**, plugin warnings, image warmup, self-update)
- Download / install progress and result overlays (success, failure, **Download cancelled**, ZIP complete)
- Install All wizard and mirror/link pickers
- Search and catalog switching
- News modal (`news.txt`)
- First-run / Settings **theme picker**
- Essential plugins modal and plugin **reboot** modal
- Report / data-request confirms

## Color themes

- Many distinct named palettes (`ColorTheme` in `app_settings.hpp`).
- First launch: theme grid before News (`theme_setup_done`).
- Settings opens the same grid (not a simple Left/Right cycle).
- **Preview then confirm:** first X/tap previews; second activation on the same theme **or** **Save** commits.
- **Cross-fade (~420 ms):** `applyColorTheme(..., animate)` interpolates BG, SURFACE*, PANEL, BORDER, TEXT, DIM, ACCENT* with smoothstep (`tickThemeBlend` in `updateAnimations`).
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
UI chrome is translated (EN/ES); catalog JSON is not. See [docs/MULTILANGUAGE.md](../../../docs/MULTILANGUAGE.md).

## Progress overlay & lock messaging

While a download/install job is active:

- Phase hints (connecting, downloading, extracting, installing, retries)
- Large-type **LOCKED** banner: PS button and soft power menu disabled; screen stays on
- CIRCLE cancels (in progress) or acknowledges (result); other keys toast LOCKED
- Touch must match resized panels (do not leave hitboxes on old coordinates)

## Theme / News / Settings scroll

Theme picker and Settings list use the same **stepped touch scroll** model as the main catalog (drag moves focus like D-Pad). News uses its own line scroll.

## Catalog card text

Long titles use ellipsis / marquee with **parent clipping** so names do not spill outside card bounds while scrolling.

## Plugin UI

- Link row: **Installed** badge when file (+ config line when required) already present; press → toast, no re-download
- Install All skips already-installed plugins
- Post-install **Restart required** modal blocks background touch until soft reset
- Essential plugins modal: large type, pulsing **Install plugins** border

## Settings INFO

INFO panel documents each focused option (install method, PSP target/media, language, font, theme, plugins, images, updates). SYSTEM block lists plugin detection status with larger type.

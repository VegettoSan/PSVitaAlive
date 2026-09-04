# `source/ui/` — Native UI

Rendered with **vita2d** (960×544). Default accent aligns with the store green (`#3BFF00`); users can switch **color themes** in Settings.

## Main surface

`FullCatalogScreen` covers:

- Full catalog grid
- Split detail view
- Catalog loading (including full-screen loading art when configured)
- Settings (install method, PSP target, color theme, plugin warnings, …)
- Download / install progress and result overlays (success, failure, **Download cancelled**)
- Search and catalog switching
- News modal (`news.txt`); Report confirm flow for real errors

## Supporting pieces

| File | Role |
|------|------|
| `image_cache.cpp` | Async icon/screenshot cache; release textures when leaving views |
| `ui_types.cpp` | Shared UI types |

## Rules

- UI requests actions (install, cancel, acknowledge); it does not promote packages itself.
- Touch and controls should share the same actions where implemented.
- Heavy textures should not stay resident when the user leaves a catalog/detail context.

## Catalog list memory

For large catalogs (Vita Games), browsing with an empty search should use `catalogView()` backed by `allItems_` so the filtered `items_` vector is not a full second copy of the catalog in RAM.

## Color themes

Settings can cycle predefined **color palettes** (e.g. default neon lime, cyan, rose, amber, violet, mono, OLED-oriented). Selection is stored in `AppSettingsData` / `config.json` and applied via `applyColorTheme` so accent, soft accent, and related chrome update without restarting the process. The **Report** control stays red for visibility regardless of palette.

## Progress overlay & lock messaging

While `installProgressActive_` and the job has not finished:

- Panel shows phase-specific wait hints (connecting, downloading, extracting, installing, retries).
- A high-visibility **LOCKED** banner states that the PS button and soft power menu are disabled and the screen stays on.
- Footer text: only CIRCLE (cancel) is expected until completion.
- Input layer:
  - CIRCLE → cancel (in progress) or acknowledge (result)
  - START / SELECT / L / R / face buttons → **LOCKED** toasts explaining why the action is refused
  - After result (success/fail/cancel), normal navigation returns when the user closes the panel

Catalog loading splash remains separate from install progress; install locks apply only to install-controller busy states.

## Catalog card text

Long titles use ellipsis / marquee helpers with clipping so names do not spill outside card bounds. Right padding leaves room for badges (e.g. Game/Data Files chips).


## Color themes

`ColorTheme` enum + `applyColorTheme` map ACCENT / SURFACE / TEXT tokens used by all panels. Brand theme keeps full-color logo and loading splash; other themes tint monochrome assets.

- **First-run** modal: grid of theme buttons (each painted with its palette), adaptive scroll, Save persists `themeSetupDone`.
- **Settings**: Color theme row opens the same picker (no longer D-pad cycle only).

## Essential plugins modal

`tryShowEssentialPluginsPrompt()` runs after theme setup + News (see `main.cpp` `startupEssentialPending`).

- Large type for 960×544 readability.
- Lists only **missing** plugins (kubridge / fd_fix require file **and** config line; libshacccg file only).
- **Install plugins** uses pulsing border (same language as Install All) and drives `linkAction_` with synthetic Plugin links.
- **Remind me later** dismisses without installing.
- Sequential install; reboot modal only after the last success (or if a mid-queue failure happened after at least one success).

## Plugin link UI

Detail link rows:

- Badge **Installed** when the plugin file (and config line when applicable) is present.
- Toast **Already installed** if activated again.
- Install All skips installed plugins when enqueueing.

## Reboot modal

Full-screen dim + **Restart PS Vita**. `handleTouch` returns early so taps cannot hit catalog/settings behind the dialog. Soft reset: `scePowerRequestColdReset`.

## Settings INFO panel

Right-hand **SYSTEM** block lists:

- NoNpDrm / NoPspEmuDrm (from `PluginDetector`)
- kubridge / fd_fix / libshacccg (same rules as the essential prompt)
- Active `config.txt` path
- Larger type scales for real-hardware legibility

## Progress overlay & lock messaging

While `installProgressActive_` and the job has not finished:

- Phase-specific wait hints (connecting, downloading, extracting, installing, retries).
- **LOCKED** banner: PS button and soft power menu disabled; screen stays on.
- CIRCLE cancels (in progress) or acknowledges (result); other keys toast LOCKED.

## Catalog card text

Long titles use ellipsis / marquee with clipping so names do not spill outside card bounds.

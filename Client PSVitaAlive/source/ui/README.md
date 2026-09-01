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

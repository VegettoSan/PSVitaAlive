# `web/` — Static website

Browser frontend for PS Vita Alive Store (GitHub Pages).

## Pages

| File | Purpose |
|------|---------|
| `index.html` | Homebrew catalog home |
| `app.html` | Homebrew app detail |
| `game.html` | Commercial game detail (Vita / multi-catalog) |
| `author.html` / `authors.html` | Author profiles |
| `category.html` | Category browsing |

## Scripts

- `js/catalog-loader.js` / `catalog-switcher.js` — load homebrew vs commercial catalogs  
- `js/app-page.js` — homebrew detail links  
- `js/game-page.js` — **game detail**: splits links into **Downloads**, **DLC**, **Updates**, **Mods**, **Other Links**  
- `js/search.js`, `filters.js`, `components.js` — UX helpers  

## Game link sections

`game-page.js` routes by `link.type` (case-insensitive):

| Type | Section |
|------|---------|
| `Download` / `PKG` | Downloads |
| `Data Files` | Data Files |
| `Game Files` | Game Files |
| `DLC` | DLC |
| `Update` / `Patch` | Updates |
| `Mod` / `Mod Pack` | Mods (hidden until at least one exists) |
| Everything else | Other Links |

Update cards can show `version` and `required_fw` when present in JSON.

## Assets

Logos and backgrounds under `web/assets/`. Styling under `web/css/` (`game.css`, `main.css`, …).

## Tooling

- `web/tools/app-generator/` — helper UI to draft app JSON for contributors.
- `web/tools/mascot-generator/` — browser-only mascot package editor: PNG animation import, PS Vita preview, manifest validation and ready ZIP export.

Both tools are static and compatible with GitHub Pages; they do not require a backend.

## News

Repo-root [`news.txt`](../news.txt) drives the in-client News modal (id / title / body). Bump the `id` when publishing a new announcement so users see it once.

## Discord community

The footer on all six public pages links to the [official PSVitaAlive Discord server](https://discord.gg/byn8FDuDrw). It uses Discord purple independently of the site's color theme. See [`docs/DISCORD_COMMUNITY_QR_20261027.md`](../docs/DISCORD_COMMUNITY_QR_20261027.md).

## App generator

[`tools/app-generator/`](tools/app-generator/) — form UI for contributors: auto Internal ID, size units, link types including **Plugin** (`section` / `line` / `extract_path`), `extract_path` for ZIP-oriented types.

## Mascot generator

[`tools/mascot-generator/`](tools/mascot-generator/) — create/edit the planned protection-mode `mascot.json` format, import PNG frames or an existing mascot ZIP, simulate Idle/Run movement on a 960 × 544 canvas, validate 100 × 100 frames, and export `<id>/mascot.json` + PNG assets as a ready ZIP. See the tool's own README for the schema and compatibility rules.

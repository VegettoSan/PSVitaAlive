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

`web/tools/app-generator/` — helper UI to draft app JSON for contributors.

## News

Repo-root [`news.txt`](../news.txt) drives the in-client News modal (id / title / body). Bump the `id` when publishing a new announcement so users see it once.

## App generator

[`tools/app-generator/`](tools/app-generator/) — form UI for contributors: auto Internal ID, size units, link-type help, `extract_path` only for ZIP-oriented types.

# App generator tool

Static helper to draft homebrew `apps/*.json` records for PS Vita Alive Store.

## Usage

Open [`index.html`](index.html) (GitHub Pages or local). Choose **Create** or **Edit** (import JSON).

### Create mode

- Fill **Name** — **Internal ID** is generated automatically (`My Cool App` → `my-cool-app`) and is **read-only**.
- Title ID, descriptions, category, links, media, etc. follow the on-form help text.

### Edit mode

- Import an existing `apps/*.json` to modify it.
- Internal ID is editable (changing it renames the file when you submit a PR).

## Links

- **Types:** Download, PKG, DLC, Update, Patch, Mod, Mod Pack, Data Files, Game Files, Mirror, Repository, Official Website, Documentation, Issues, Community, **Plugin**, Other.
- **Plugin** links: `extract_path` (default `ur0:tai/`), `section` (`*main` / `*KERNEL` / `*ALL` / custom / `none`), and `line` (exact config.txt entry to append).
- **`extract_path`** is shown only for Data Files, Game Files, Mod, Mod Pack, Patch (default `ux0:data/`).
- **Size:** enter a number + unit (B / KB / MB / GB); JSON stores **integer bytes**.
- At most one link may be **Recommended**.

## Media

Use **direct** image URLs only (PNG preferred): GitHub `raw.githubusercontent.com`, Archive.org file links, Imgur direct links — not HTML gallery pages.

## Notes

- Output must still pass repository validation / CI.
- Align `category_id` / `subcategory_ids` with `categories/`.
- Prefer absolute media URLs.


## Plugin link fields (UI)

When type is **Plugin**, the form shows a full-width block:

- **Config section** — dropdown: `*main`, `*KERNEL`, `*ALL`, Custom, None
- **Config line** — text appended under that section (e.g. `ur0:tai/my.skprx`)
- **Custom section** — shown only when Custom is selected
- **extract_path** — destination directory (defaults to `ur0:tai/`)

Layout uses a dedicated CSS grid area so controls are not collapsed on desktop.

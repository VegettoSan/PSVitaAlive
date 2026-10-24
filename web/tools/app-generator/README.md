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


## Usability improvements

- **Section navigation:** jump directly to Identity, Authors & category, Release, Media, Links or the final check.
- **Actionable validation:** each missing/invalid field appears as a button in the final summary. Select it to scroll to the matching field and focus it. Advanced metadata expands automatically when necessary.
- **Main file size:** the mandatory VPK/main-download size is called out explicitly and an empty or zero value links back to that input.
- **Extraction destinations:** only **Data Files, Game Files, Mod, Mod Pack, Patch** and **Plugin** show a destination input. Non-file links (e.g. Download, Mirror, Repository, PKG, DLC, Update) never serialize a hidden \`extract_path\`. Plugin keeps its existing \`ur0:tai/\` default, config section, line and requirement controls.
- **Create another JSON:** a second reset action appears next to Download. It returns to Create mode, clears the form and new-author drafts, and focuses Name. Unsaved changes prompt for confirmation; data already downloaded does not.
- **Download confirmation:** after export, a local confirmation shows which JSON file(s) were prepared. This is not proof that a pull request passed GitHub Actions.

### Manual verification checklist

1. Add a **Download**, **Repository**, **Mirror**, **PKG**, **DLC**, **Update** and **Other** link. No extraction field should appear, and JSON should not contain \`extract_path\` for these links.
2. Change link type to **Data Files**, **Game Files**, **Mod**, **Mod Pack**, and **Patch**. Destination appears with default \`ux0:data/\`; change to a custom path, switch away and back, and confirm it is retained for ZIP types.
3. Change the same link to **Plugin**. The existing \`ur0:tai/\` destination, section, line, and requirement fields must remain; switching between Plugin and ZIP types must not copy a destination from one type to the other.
4. Leave the main VPK size empty. The final summary must show a size-specific issue; select it and confirm the page scrolls to and focuses the size input.
5. Repeat the jump for a missing author, category, subcategory and invalid link URL. For invalid optional metadata URLs, selecting the issue should also expand **Optional metadata**.
6. Complete all required fields and verify the **Download** action enables and produces \`apps/<id>.json\` with numeric sizes in bytes and an unchanged schema.
7. After downloading, select **Create another JSON**. Verify that the form returns to Create mode, authors and links reset, the confirmation disappears, and Name receives focus. Editing an unfinished record should warn about discarding changes.
8. Verify links and error navigation on a narrow mobile viewport and using keyboard Tab/Enter.

Repository scripts and GitHub Actions remain authoritative for final data validation. The generator does not edit the generated catalog files.

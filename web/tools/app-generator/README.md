# App generator tool

> **Catalog Schema V2:** This editor accepts V1 and V2 JSON imports.
> Its internal form logic keeps readable V1 property names, but preview,
> copy and downloadable `apps/*.json` / `authors/*.json` output use
> compact V2 field names. The public catalog remains V1 until the
> dual-reading Vita client is released and V2 publication enabled.

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
- **Recommended main VPK name:** when adding the primary `.vpk` download, choose `Download` and set **Link name** to exactly `VPK` (not `App v1.0 VPK`). This enables the existing GitHub/GitLab release synchronizer to identify that button when all its other safety requirements pass. Other downloads, variants and mirrors may keep descriptive names; this is a recommendation, not a JSON validation requirement.
- **Plugin** links: `extract_path` (default `ur0:tai/`), `section` (`*main` / `*KERNEL` / `*ALL` / custom / `none`), and `line` (exact config.txt entry to append).
- **`extract_path`** is shown only for Data Files, Game Files, Mod, Mod Pack, Patch (default `ux0:data/`).
- **Size:** enter a number + unit (B / KB / MB / GB); JSON stores **integer bytes**.
- At most one link may be **Recommended**.
- **Optional pre-release channel:** under Release, check **Follow beta/RC updates immediately** only for projects that intentionally ship beta/RC builds. The generated app JSON contains `"apr": true` in compact V2 (logical `allow_prereleases: true`). Unchecked retains the automatic seven-day/no-stable policy; checked accepts newer beta/RC builds sooner. This does not relax VPK identity, version comparison or backup safety rules.

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


## Conditional per-link file size

The application-level **Main file size (VPK)** remains required. The per-link \`size\` is optional and stores an integer number of bytes.

- File size is displayed only for **Download**, **Mirror**, **PKG**, **DLC**, **Update**, **Patch**, **Mod**, **Mod Pack**, **Data Files**, **Game Files**, and **Plugin**.
- **Repository**, **Official Website**, **Documentation**, **Issues**, **Community** and **Other** never show a size and never serialize a stale hidden \`size\`.
- Switching to an informational link hides its size, and switching back to a downloadable type restores the typed value during that editing session. Importing older JSONs does not change their canonical source files.
- Existing extraction behavior for ZIP-oriented types and plugin-specific taiHEN configuration stays unchanged.

## Search authors by name or GitHub/GitLab link

The main **Authors & category** search accepts names, author IDs, and account/repository URLs. Paste a URL such as `github.com/example-dev`, `https://github.com/example-dev/vita-project` or `https://gitlab.com/example-dev/port`.

- The picker checks the loaded official author profiles and locally created/imported profiles. An **exact saved link** is preferred, followed by a matching linked account, then the username matching an existing author ID or name. Potential matches are displayed for confirmation, not silently selected.
- If no author matches, choose **Create author from this link**. The author dialog opens with the account username and ID, stores the original project link as an editable author link when applicable, and automatically invokes the existing public profile lookup for name/avatar/bio and account URL. This does not submit or publish anything.
- The account can be detected from a repository URL, but a repository does not prove authorship. On GitLab, the leading namespace may be a group instead of a user; lookup failures leave the form editable and should be reviewed before saving.
- If the URL/API cannot provide full metadata, all fields remain editable. The normal name/ID search, manual creation and imported JSON reuse still work unchanged.
- The lookup reuses the already-loaded `authors.json` data and does not fetch a second catalog, change any canonical source file or create a separate database. Final author validation and publication remain under GitHub Actions.

### Search verification

1. Search by partial name or ID; verify the existing list behaves as before.
2. Paste a repository URL already saved in an author's links; verify the matching author is offered and can be selected without creating a duplicate.
3. Paste a profile URL where the author has a matching account link but a different internal ID; verify the existing profile is offered.
4. Paste a username whose slug matches an existing author ID/name but not its links; verify it is suggested for manual confirmation.
5. Paste a new repository URL; choose **Create author from this link**, verify profile metadata auto-fills and the project link is retained as an editable extra link.
6. Simulate an API failure or missing profile; verify the username, suggested ID, profile URL and project link remain in the editable dialog.
7. Create a new author by typing only a name; verify the bio is initially empty and manual links still work.
8. Export and import author JSON; verify the same compact V2 schema remains intact.

## Creating authors from GitHub or GitLab

The **Create new author** dialog has the same dark, grey and green styling as the rest of the JSON generator, including a mobile-friendly layout and live avatar/name preview.

Paste a public **user profile** URL such as `https://github.com/octocat` or `https://gitlab.com/username`. The dialog fetches public metadata automatically after the URL is entered, or via **Auto-fill**, without any token, server or paid service. It fills:

- **Display name:** the public profile name, falling back to its username.
- **Author ID:** normalized from the URL username to a stable lowercase ID (for example, `My.Dev` becomes `my-dev`).
- **Avatar:** the public profile avatar URL, or the existing PSVitaAlive default avatar when unavailable.
- **Biography:** the publicly available bio, or the exact fallback `Developer`.
- **Profile link:** one recommended link with `type: "Repository"`, `name: "GitHub"` or `"GitLab"`, and the profile URL.

**Manual entry remains unchanged:** if the profile URL field is empty, the form never calls an API. Name and ID can be entered as before; the bio starts empty and may be left empty. The `Developer` fallback is only applied when autofill is attempted and the biography is unavailable. If the API is unavailable or rejects a lookup, the entered account URL and inferred username can still prefill the matching fields while the remaining fields stay editable. Nothing is automatically published or verified.

All fields are editable before the author JSON is created. The redundant separate **Icon URL** field was removed; the official `avatar` field is used and the catalog normalizer can derive any legacy icon fallback. The editor still accepts older imported author JSON files containing `icon`, and exports compact V2 keys like `av`, `b`, `l` and `rec`.

The URL must identify a public **user profile**, not a repository, project or unsupported hostname. Existing author IDs are flagged to avoid duplicates. Remote API errors, missing profiles or rate limits do not prevent manual author creation. GitLab's unauthenticated API may not expose the user's biography, in which case the editor uses `Developer` and keeps the bio field editable. Autofill is informational and does **not** prove developer ownership or publish an author automatically. Final review and GitHub Actions validation still apply.

### Profile lookup verification

1. Paste a valid GitHub user URL: confirm that display name, normalized ID, avatar, bio (or `Developer`) and a recommended `Repository / GitHub` link appear.
2. Repeat with a GitLab user URL, including an account without a publicly retrievable bio.
3. Check a profile without a display name or avatar, then manually change fields before saving; confirm the preview follows edits.
4. Enter a project URL, unsupported host, unknown user and a malformed URL; confirm understandable feedback and manual entry remains possible.
5. Try an already registered author ID; confirm the duplicate warning and that final validation rejects creating a duplicate.
6. Add extra author links and export the new author JSON; verify its compact V2 data contains the chosen avatar and links, without duplicated icon input.
7. On a narrow screen, verify the modal scrolls internally, its buttons remain reachable, Escape closes it, and keyboard focus remains within the modal.
8. Verify selecting an existing author, importing an unpublished author JSON, and downloading an app JSON still work as before.

## Import unpublished author JSON profiles

Inside **Authors & category**, select **Import author JSON** and choose one or more files saved from \`authors/*.json\`. The generator checks the imported profile, prevents conflicting IDs/names, and selects it. Previously imported profiles remain available during this browser session.

Select **Create another JSON** to begin another application while keeping selected imported authors, so the same \`author_ids\` can be reused. The generated application references the imported ID. **It does not re-download an imported author JSON** on every app export: the original file already exists on the contributor's computer and must be submitted to \`authors/\` separately if unpublished.

New authors created in the generator (rather than imported) continue to be included in the app download until submitted. This is a browser-session helper, not a shared database or an additional generated catalog. Author registration and final validation still occur through GitHub Actions.

### Additional checks

1. Enter a size for Download; change to Repository or Community and confirm that the size disappears and is omitted from exported JSON.
2. Switch back to Download, Game Files, Plugin and Mirror; confirm that the input reappears, its value is retained and sizes are converted to bytes.
3. Import an unpublished author JSON, check its chip/ID selection, download an app and ensure only the app JSON is downloaded (the imported author file must not be duplicated).
4. Choose Create another JSON, check that the imported author remains selected, and generate a second app referencing the same ID.
5. Try importing the same author again, an app JSON, malformed JSON and profiles with conflicting ID/name; verify informative feedback and no duplicate author options.
6. Confirm the old create-new-author flow still exports an author JSON alongside the app, and official authors continue to behave as before.

## Installed Game/Data Files paths (`installed_paths`)

The editor now supports `installed_paths` **per Game Files/Data Files link**, preserving it during Create, Edit, Import and Export. Enter one path relative to **`ux0:data/`** per line. For example, `extract_path: "ux0:data/AnimalCrossing/rom/"` with `installed_paths: ["AnimalCrossing"]`. The field identifies the installed root for space management; it **never changes ZIP extraction**.

- **New external Game Files links** need at least one valid `installed_paths` entry. Paths are relative to `ux0:data/`, not to `extract_path`. The form prevents unsafe absolute/traversal/protected paths and duplicates. Older imported app records missing this field remain editable for gradual migration.
- **Game Files** extracted directly under `ux0:app/` or `ux0:app/<title_id>/` (as appropriate to the ZIP layout) do **not** need `installed_paths`; these belong to Vita's package installation and are uninstalled through its package manager.
- **Data Files** may omit `installed_paths` if they share an unambiguous Game Files root; when separate, provide their own paths. The client never assumes exclusive ownership from absence of a Data Files path.
- Different download servers/languages are alternative links; retain each source's metadata. The Vita client picks **one Game Files source per installation** and **one Data Files source**, never all mirrors.
- Existing source JSONs, field omissions and `extract_path` are never rewritten silently; GitHub Actions creates the generated catalog. Never edit `catalog.json` manually.

Cross-application sharing, partial scans, and conflicting destinations block automatic deletion in the Vita app. See the [implementation notes](../../../docs/INSTALLED_DATA_MANAGER_IMPLEMENTATION_20261105.md) and the [original safety plan](../../../docs/INSTALLED_GAME_DATA_MANAGEMENT_PLAN_20261105.md).

## Game/Data Files revision (`dr`)

In **Release**, optionally set **Game/Data Files revision** to a positive integer 1–65535. The generator emits only `"dr": 2` (for example); a blank field **omits** the property. An app with `dr` must include at least one Game Files or Data Files link. Increase `dr` only if replacing data packages is necessary for compatibility; do not increase it for VPK-only changes. Importing/editing a JSON retains its existing value. This is **not** the VPK version or an automatic hash of the archive.

Changing or first adding `dr` **never creates an update notification on its own**. Only when the client detects a newer VPK version does it use `dr` to choose between VPK-only **UPDATE** and **UPDATE ALL** (VPK + Game/Data Files). An absent or lower recorded data revision selects UPDATE ALL; matching revisions select VPK-only UPDATE. During Install All, multiple Game Files links are **alternatives** (mirrors/languages), as are multiple Data Files links: the user picks one per type, not all of them. The client records the new `dr` only after the selected data ZIP steps finish successfully. Apps without `dr` retain their previous behavior.

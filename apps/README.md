# `apps/` — Canonical homebrew application records

> **Catalog Schema V2 (feature branch):** JSON records in `apps/` use short
> field keys, e.g. `title_id → tid`, `name → n`, `author_ids → aids`,
> `category_id → cid`, `subcategory_ids → sids`, `version → v`,
> `version_date → vd`, `links → l`, `type → t`, `url → u`.
> This README's long field names describe the *logical* V1 names, not
> necessarily the keys stored in V2 files. The complete versioned mapping
> is in [catalog_schema_v2.py](../scripts/catalog_schema_v2.py).
> Never change the actual IDs, URLs, names or other values during migration.
>
> The public generated schema remains V1 until the dual-reading client has
> been released and the `PSVA_PUBLIC_SCHEMA_V2` cutover flag is enabled.

One JSON file per homebrew application. This is the editable layer before `catalog.json` is generated.

## Pipeline

```text
apps/*.json
    ↓
external merge + normalization + overrides
    ↓
validation
    ↓
catalog.json
```

Do **not** edit `catalog.json` by hand.

## Core fields

| Field | Notes |
|-------|--------|
| `id` | Internal store ID (stable slug) |
| `title_id` | Vita Title ID when applicable |
| `name`, `description`, `long_description` | Display text |
| `author_ids` | List of author profile IDs |
| `category_id`, `subcategory_ids` | Must match `categories/` |
| `version`, `version_date` | Release info |
| `icon`, `screenshots` | Prefer absolute public URLs |
| `links` | Array of typed links |
| `status` | e.g. `Verified`, `Legacy`, `Archive` |

Optional: `changelog`, `requirements`, `size`, `data_size`, `data_url`, hashes, scores, etc. Consumers must tolerate missing optional fields.

### Pre-release updates (opt-in)

By default, automatic release synchronization prefers stable GitHub/GitLab
releases. A Homebrew whose developer distributes beta/RC VPKs can opt in
by adding `"allow_prereleases": true` to the readable V1 record or
`"apr": true` to the compact V2 source JSON in `apps/`.
This flag is **per application** and optional; the web app generator also
offers an opt-in checkbox.

Recognized version suffixes include `dev`, `test`, `alpha`, `beta`,
`pre`, `preview`, and `rc`. Stable `1.2` ranks above `1.2-rc1`
and `1.2-beta2`. Arbitrary labels such as `continuous` are not ordered
automatically. A prerelease still requires a single trusted VPK, a unique
registered repository and a unique `Download` link named `VPK`.
Manual Archive.org backups are never overwritten at an equal or older version.


## Links

```json
"links": [
  {
    "type": "Download",
    "name": "VPK",
    "url": "https://example.org/app.vpk",
    "recommended": true
  },
  {
    "type": "Repository",
    "name": "Source",
    "url": "https://github.com/example/project"
  }
]
```

**Recommended for automatic VPK updates:** give the main installable `.vpk` link `type: "Download"` and the exact `name: "VPK"` (in compact V2: `t: "Download"`, `n: "VPK"`). The GitHub/GitLab release synchronizer currently identifies only this exact name. Do not put the app title or version in that link name (for example, avoid `My App v1.0 VPK`). This is a recommendation, not a catalog validation requirement: links for alternate builds, data, plugins and mirrors retain their own descriptive names. Other safety requirements for automatic updates still apply; see [release synchronization policy](../docs/AUTOMATIC_RELEASE_CATALOG_SYNC.md).

Common types: `Download`, `Mirror`, `Repository`, `Official Website`, `Documentation`, `Issues`, `Community`.  
Commercial catalogs additionally use `DLC`, `Update`, and `Mod` (see root README).

Only **one** `recommended: true` download per app is preferred for validation/UX consistency.

### VitaDB link migration

The automatic **VitaDB** catalog feed is no longer used. This does **not** mean that all existing VitaDB URLs in `apps/` have already been removed.

Existing application links may continue to be reviewed and removed or replaced **progressively**, app by app. This cleanup is separate from disabling the automatic feed and should not be done by rewriting generated catalogs.

New automated external acquisition must follow the currently configured sources described in [`../sources/README.md`](../sources/README.md). Existing VitaDB-derived metadata or links may remain temporarily for compatibility, historical reference, preservation and ongoing migration.

## Authors

Use individual IDs:

```json
"author_ids": ["author-a", "author-b"]
```

## Media

Use absolute HTTP(S) URLs for external icons/screenshots. Relative paths only if the file is published inside this repository’s public tree.

## License

Homebrew **catalog metadata** in this folder is dedicated to the public domain under **CC0 1.0** (see [`CATALOG_LICENSE.md`](../CATALOG_LICENSE.md)).

- You may reuse this data for any purpose **without attribution**.
- This project **does not own** the homebrew apps themselves; credits belong to each entry’s authors (`author_ids` and author profiles).
- Binaries, icons, and screenshots remain under their authors’ rights.

## Link `type` values (homebrew)

| type | Use for |
|------|---------|
| `Download` | Primary installable (usually `.vpk`) |
| `Data Files` | Extra data ZIP → often `extract_path`: `ux0:data/` |
| `Game Files` | Large game data ZIP |
| `Mod` / `Mod Pack` / `Patch` | Mods / patches (ZIP + optional `extract_path`) |
| `PKG` / `DLC` / `Update` | Package-style content when applicable |
| `Mirror` | Alternate file URL |
| `Repository` / `Official Website` / `Documentation` / `Issues` / `Community` / `Other` | Info only |

### Optional per-link fields

- `size` — integer **bytes**
- `extract_path` — auto extract destination for ZIP types (omit to let the client ask)
- `recommended` — at most one `true` per app

Draft records with [`web/tools/app-generator/`](../web/tools/app-generator/) (Create mode auto-fills Internal ID from Name).

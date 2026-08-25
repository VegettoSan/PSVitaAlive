# `apps/` — Canonical homebrew application records

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

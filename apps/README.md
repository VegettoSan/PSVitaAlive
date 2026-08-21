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

## Authors

Use individual IDs:

```json
"author_ids": ["author-a", "author-b"]
```

## Media

Use absolute HTTP(S) URLs for external icons/screenshots. Relative paths only if the file is published inside this repository’s public tree.

## License / attribution

Homebrew entry data in this tree is part of the **PS Vita Alive Store** catalog and is
offered under **CC BY 4.0**. Reuse is welcome if you **credit the project**.

See [`../CATALOG_LICENSE.md`](../CATALOG_LICENSE.md).

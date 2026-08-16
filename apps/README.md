# `apps/` — Canonical application records

This directory contains **one JSON file per application**. It is the canonical editable application layer used by the catalog pipeline before `catalog.json` is generated.

## Core rule

Do not create or manually edit `catalog.json`. Application changes belong here, or in `catalog_overrides/` when the data is enrichment that must survive external imports.

```text
apps/*.json
    ↓
external merge + normalization + overrides
    ↓
validation
    ↓
catalog.json
```

## Application identity

The main application contract includes:

- `id` — unique PS Vita Alive Store internal identifier. It does not have to match an external source ID.
- `title_id` — Vita Title ID. Required and unique across the catalog.
- `name` — display name.
- `description` — short description.
- `author_ids` — one or more individual author IDs.
- `category_id` — official category ID.
- `subcategory_ids` — subcategories allowed by that category.
- `version` and `version_date` — published release information.
- `icon` and `screenshots` — public media URLs.
- `links` — download, mirror, repository, website, documentation, issues, community and other useful sources.
- `status` — `Verified`, `Legacy` or `Archive`.

Optional preservation/enrichment fields may include `long_description`, `changelog`, `requirements`, `downloads`, `hash`, `hash2`, `size`, `data_size`, `data_url`, `score` and `updated_at`.

Consumers and validation code should tolerate optional fields being absent.

## Authors

Use individual `author_ids` rather than a single string containing multiple developers:

```json
"author_ids": [
  "author-a",
  "author-b"
]
```

This allows the website and PS Vita client to open each author's profile independently.

## Links

Applications may expose multiple sources. A recommended link is optional.

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
    "url": "https://github.com/example/project",
    "recommended": false
  }
]
```

Do not assume that a download must come from GitHub Releases.

## External media

Use absolute public URLs for externally hosted icons and screenshots. Do not preserve a relative path such as `screenshots/foo.png` unless that path is genuinely part of a resource tree published by PS Vita Alive Store.

## Versions and updates

`title_id` is the primary Vita application identity. `version` and `version_date` describe the release. When external sources provide newer information, the aggregation layer may enrich or update the record while preserving protected local information and Overrides.

## Status values

- `Verified` — project registered through the maintained/verified publishing flow.
- `Legacy` — older project preserved from historical sources.
- `Archive` — primarily preserved for historical/reference purposes.

## Publishing a maintained application

1. Create or update the application's JSON in `apps/`.
2. Verify `title_id` uniqueness.
3. Verify authors and official category/subcategories.
4. Validate all required URLs and media.
5. Run the relevant local validation.
6. Commit and push or open the appropriate Pull Request.
7. GitHub Actions validates and regenerates the published catalogs.

## Never do

- Manually edit `catalog.json`.
- Create another application with the same `title_id` to represent the same Vita application.
- Invent screenshot or icon URLs.
- Collapse multiple authors into a combined profile.
- Depend on an external numeric ID as the canonical PS Vita Alive Store `id`.

# `scripts/external/` — External aggregation

Imports homebrew metadata from configured upstream catalogs and merges it with local `apps/`.

## Modules (overview)

| Module | Role |
|--------|------|
| `sources.py` | Fetch + normalize candidates (VitaDB, VitaDBtoo, local, …) |
| `identity.py` | Title ID / name / repo identity helpers |
| `merge.py` | Merge candidates into records |
| `overrides.py` | Apply `catalog_overrides/` |
| `normalizer.py` | Field cleanup |
| `aggregate.py` | Orchestrates the full build used by `generate_catalog.py` |

## Design rules

1. Prefer explicit absolute media URLs.
2. Do not treat empty upstream responses as success without logging.
3. Deduplicate carefully (Title ID, name, repository URL).
4. Local `apps/` wins on intentional curated fields when policy says so; overrides refine post-merge.
5. NeoVitaDB is disabled as an active source.

## Output

Merged application list + authors + categories (+ optional conflict report under `reports/` when generated).

## VitaDB `data` field

Companion archives from VitaDB/VitaDBtoo `data` URLs are emitted as link type **`Data Files`** (not `Download`). The VPK stays `Download`.

# `scripts/external/` — External catalog integration engine

This directory contains the source-specific logic that reads external catalogs, converts their records into the internal PS Vita Alive Store model, groups equivalent applications, resolves authors and applies merge rules.

## Internal model

External readers should hide source-specific details behind the internal `Candidate` model:

```text
VitaDB / VitaDBtoo
        ↓
source adapter
        ↓
Candidate
        ↓
group_candidates()
        ↓
merge_group()
        ↓
canonical PS Vita Alive Store application
```

The aggregation process is non-destructive: applications already maintained in `apps/` remain part of the merge even when an external source temporarily omits them.

## Main components

- `sources.py` — acquisition and normalization of the currently supported external sources.
- `aggregate.py` — loads local data, obtains external candidates, deduplicates, merges, resolves authors/categories and persists results.
- `identity.py` — author canonicalization and identity comparison.
- `merge.py` — combines equivalent application records.
- `overrides.py` — applies manual catalog Overrides.
- `normalizer.py` — normalizes text, repositories, versions and related values.

## Application identity

`title_id` is the primary Vita identity used to group equivalent applications. External numeric IDs must not automatically become the canonical PS Vita Alive Store `id`.

If records are ambiguous or have conflicting Title IDs, the system should prefer explicit review over silent replacement.

## Source freshness and recommended downloads

The highest-priority source does not automatically win. Current aggregation considers version and date freshness before using source priority as a tie-breaker.

This allows a newer valid VPK from a lower-priority source to become the recommended download when appropriate.

## Author identity

External names may contain multiple developers in a single field. The identity layer attempts conservative splitting and uses repositories/public identifiers to resolve existing authors.

When identity is not known, a minimal provisional profile may be created instead of silently merging unrelated developers.

## Media resources

Adapters should return absolute public URLs when icons and screenshots are actually published by the source. Never manufacture a URL from an assumed directory structure.

## Adding a new adapter

1. Study the real source format and endpoint behavior.
2. Implement a source-specific fetcher when a plain JSON reader is insufficient.
3. Convert records into the existing `Candidate` interface.
4. Normalize Title ID, author identity, version, category and media.
5. Add useful logs for counts and failures.
6. Add a smoke test when practical.
7. Update `sources/README.md` with the source behavior.
8. Run full validation without manually editing generated catalogs.

## Preservation rule

The original external source must remain intact. PS Vita Alive Store's converter/normalizer is responsible for adapting it to the canonical model; external files must never be modified merely to make them fit the VitaHub schema.

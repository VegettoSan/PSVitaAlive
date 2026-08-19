# `external_authors/` — Imported author side data

Holds author-related data produced or cached from external homebrew catalogs during aggregation.

## Role

- Feed the merge layer with author names/avatars/links from VitaDB, VitaDBtoo, etc.
- Does not replace canonical `authors/` for long-term curated profiles

## Rules

- Treat as pipeline input, not the primary place for permanent human edits.
- Prefer promoting stable curated profiles into `authors/` when an author becomes first-class in the store.

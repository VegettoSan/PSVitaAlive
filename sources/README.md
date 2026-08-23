# `sources/` — External catalog sources

Configuration and notes for upstream homebrew databases used by the aggregation scripts.

## Primary sources (current)

| Source | Role |
|--------|------|
| **VitaDB** | Official / legacy Vita homebrew metadata and complementary data |
| **VitaHomebrewDB** | Community-preserved Vita homebrew metadata, icons and continuity data |
| Local `apps/` | Canonical hand-maintained records |

**NeoVitaDB** is **not** used as an active feed (historical date noise); orphaned entries may still exist in data history but are not re-imported from that source.

## Files

- `external_sources.json` — endpoint / enable flags for the importer
- `category_map.json` — map upstream types/tags into store categories/subcategories
- `PROTECTION_RULES.md` / `README.md` — operational notes
- `../registry/retired_ids.json` — permanent historical protection for removed IDs

## External data policy

VitaDB and VitaHomebrewDB are external sources only. Their data is used for discovery, import, cross-checking, enrichment, normalization and preservation. VitaHub does not claim ownership of those upstream databases or of third-party homebrew works represented by them.

VitaHub's own catalog curation is licensed under CC BY 4.0 as described in `../CATALOG_LICENSE.md`. Upstream and third-party rights remain separate and must be respected.

## Commercial catalogs

PS Vita / PSP / PS1 commercial package lists are **not** driven by the homebrew sources above. They use separate JSON catalogs (e.g. NoPayStation-derived PKG/DLC/Update links for Vita games).

## Rule

Aggregation must never silently invent media URLs. Invalid remote icons/screenshots are dropped or replaced according to generator policy.

Applications registered in `registry/retired_ids.json` are never allowed to be republished under a different or refreshed external record. This protects author-requested removals from being reintroduced by scheduled imports.

# `sources/` — External catalog sources

Configuration and notes for upstream homebrew databases used by the aggregation scripts.

## Primary sources (current)

| Source | Role |
|--------|------|
| **VitaDBtoo** | Preferred modern homebrew metadata / icons |
| **VitaDB** | Legacy / complementary homebrew feed when available |
| Local `apps/` | Canonical hand-maintained records |

**NeoVitaDB** is **not** used as an active feed (historical date noise); orphaned entries may still exist in data history but are not re-imported from that source.

## Files

- `external_sources.json` — endpoint / enable flags for the importer
- `category_map.json` — map upstream types/tags into store categories/subcategories
- `PROTECTION_RULES.md` / `README.md` — operational notes

## Commercial catalogs

PS Vita / PSP / PS1 commercial package lists are **not** driven by VitaDB. They use separate JSON catalogs (e.g. NoPayStation-derived PKG/DLC/Update links for Vita games).

## Rule

Aggregation must never silently invent media URLs. Invalid remote icons/screenshots are dropped or replaced according to generator policy.

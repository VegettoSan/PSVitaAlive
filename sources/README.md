# `sources/` — External catalog sources

> **Migration:** The automatic **VitaDB** feed was removed in **August 2026**. External acquisition no longer uses the VitaDB API/feed. **VitaHomebrewDB** remains the currently enabled external source while the catalog is migrated toward independent maintenance. See the root [README — Data sources & credits](../README.md#data-sources--credits).

Configuration and notes for upstream homebrew databases used by the aggregation scripts.

## Current sources

| Source | Role |
|--------|------|
| **VitaHomebrewDB** | Current optional external source for community-preserved Vita homebrew metadata, icons and continuity data |
| Local `apps/` | Canonical hand-maintained records |

### Historical source: VitaDB

**VitaDB is no longer an active external feed.** It may still be referenced by existing application records, historical data, documentation or preservation work, but it is not automatically fetched as part of the current catalog source configuration.

**NeoVitaDB** is **not** used as an active feed (historical date noise); orphaned entries may still exist in data history but are not re-imported from that source.

## Files

- `external_sources.json` — endpoint / enable flags for the importer
- `category_map.json` — map upstream types/tags into store categories/subcategories
- `PROTECTION_RULES.md` / `README.md` — operational notes
- `../registry/retired_ids.json` — permanent historical protection for removed IDs

## External data policy

VitaHomebrewDB and any other explicitly enabled external sources are inputs for discovery, import, cross-checking, enrichment, normalization and preservation. **PSVitaAlive does not claim ownership of upstream databases or of third-party homebrew works represented by them.**

VitaDB is retained only as a historical/legacy reference at this stage. Removing its automatic feed does not automatically remove existing VitaDB-derived records or links from `apps/`.

**Catalog licensing is now mixed.** Historical PSVitaAlive homebrew catalog
material dedicated under **CC0 1.0** remains CC0 irrevocably; *new original*
rights-covered contributions published after the 18 October 2026 transition
may be under **CC BY 4.0**, requiring attribution when those licensed rights
are exercised. Factual data and referenced third-party work do not
automatically become PSVitaAlive property or carry either license solely by
appearing in a JSON record. See the authoritative
[CATALOG_LICENSE.md](../CATALOG_LICENSE.md).

## Migration of existing records

The removal of the automatic VitaDB feed is intentionally separate from the cleanup of existing application links.

- Existing `apps/` records are not deleted or rewritten by this documentation change.
- Existing VitaDB-related links may remain temporarily for compatibility and preservation.
- Those links can be reviewed and removed or replaced progressively through normal application maintenance.
- New automated acquisition must use only the currently configured sources.

This avoids silently changing application download paths while the catalog is being migrated.

## Commercial catalogs

PS Vita / PSP / PS1 commercial package lists are **not** driven by the homebrew sources above. They use separate JSON catalogs (e.g. NoPayStation-derived PKG/DLC/Update links for Vita games).

## Rule

Aggregation must never silently invent media URLs. Invalid remote icons/screenshots are dropped or replaced according to generator policy.

Applications registered in `registry/retired_ids.json` are never allowed to be republished under a different or refreshed external record. This protects author-requested removals from being reintroduced by scheduled imports.

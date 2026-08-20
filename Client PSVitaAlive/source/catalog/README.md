# `source/catalog/` — Catalog manager

Loads public JSON catalogs from the PS Vita Alive Store repository (or configured URLs).

## Responsibilities

- Download / validate cache (ETag / validators when available)
- Parse application records for Homebrew and commercial catalogs
- Keep **validated catalogs in RAM** after first successful load (fast tab switch)
- Download the separate **Vita zRIF index** when loading Vita Games
- Expose ready state to UI and startup flow
- Coordinate with startup update check ordering when enabled

## Files

| File | Role |
|------|------|
| `catalog_manager.cpp` | Orchestration, disk + RAM cache, zRIF index download |
| `catalog_parser.cpp` | JSON → in-memory records / links (does not keep zRIF in RAM) |

## Catalogs

| Type | Remote file |
|------|-------------|
| Homebrew | `catalog.json` |
| Vita Games | `catalog_psvita_games.json` |
| PSP | `catalog_psp_games.json` |
| PS1 | `catalog_ps1_games.json` |

### zRIF sidecar (Vita Games)

License strings are **not** stored inside `catalog_psvita_games.json` (avoids OOM when all four catalogs stay in memory).

| Item | Location |
|------|----------|
| Remote | `catalog_psvita_games.zrifidx` (repo root) |
| Device cache | `ux0:data/psvitaalive/cache/catalog/catalog_psvita_games.zrifidx` |
| Line format | `content_id<TAB>zrif` |

`CatalogManager` downloads the index when the Vita Games catalog is loaded (if missing/too small).  
`LicenseHelper::lookupZrifForUrl()` resolves a zRIF at **install time** only.

## Memory notes

- Multi-catalog **RAM cache** is intentional for UX after startup.
- Heavy fields (long descriptions / changelogs) may be trimmed at parse time.
- UI browse path should avoid duplicating the full list while search is empty (`catalogView()`).

## Compatibility

Optional JSON fields may be missing. Parser must use safe defaults and keep loading.

Media URLs may be HTTP(S); failures must not block catalog readiness.

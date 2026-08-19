# `source/catalog/` — Catalog manager

Loads public JSON catalogs from the PS Vita Alive Store repository (or configured URLs).

## Responsibilities

- Download / validate cache (ETag / validators when available)
- Parse application records for Homebrew and commercial catalogs
- Expose ready state to UI and startup flow
- Integrate with startup update check ordering when enabled

## Files

| File | Role |
|------|------|
| `catalog_manager.cpp` | Orchestration, cache, multi-catalog readiness |
| `catalog_parser.cpp` | JSON → in-memory records / links |

## Compatibility

Optional JSON fields may be missing. Parser must use safe defaults and keep loading.

Media URLs may be HTTP(S); failures must not block navigation.

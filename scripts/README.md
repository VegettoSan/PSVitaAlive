# `scripts/` — Catalog tooling

Python utilities that build and validate the public catalogs.

## Main entry points

| Script | Purpose |
|--------|---------|
| `generate_catalog.py` | Build `catalog.json`, `authors.json`, `categories.json` from apps + external merge |
| `validate_catalog.py` / `validate_catalog_ci.py` | Structural and policy checks |
| `validate_registry.py` | Registry-related checks when used |
| `normalize_persisted_sources.py` | Normalize persisted external payloads |
| `external_smoke_test.py` | Smoke-test external endpoints |

## `scripts/external/`

Aggregation subsystem: fetch upstream JSON, normalize candidates, identity/dedup, merge, apply overrides.

See `scripts/external/README.md`.

## Local usage (typical)

```bash
python3 scripts/generate_catalog.py
python3 scripts/validate_catalog.py
```

CI workflows under `.github/workflows/` run validation (and related jobs) on pushes/PRs as configured.

## Rule

Fix issues in **apps / sources / overrides / scripts**, not by hand-editing generated root catalogs.

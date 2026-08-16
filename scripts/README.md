# `scripts/` — Catalog automation and validation

This directory contains the automation that turns canonical data and external sources into the generated PS Vita Alive Store catalogs and validates the result before publication.

## Main flow

```text
external sources + apps/ + authors/ + categories/
                     ↓
          scripts/external/aggregate.py
                     ↓
              merge + deduplication
                     ↓
              canonical persistence
                     ↓
scripts/normalize_persisted_sources.py
                     ↓
             resource normalization
                     ↓
scripts/validate_catalog_ci.py
                     ↓
          generated catalog validation
                     ↓
catalog.json / authors.json / categories.json
```

The exact workflow order is controlled by `.github/workflows/validate.yml`; this README describes the responsibilities rather than replacing the workflow itself.

## Important scripts

### `generate_catalog.py`

Coordinates catalog generation and the active aggregation/persistence process.

### `normalize_persisted_sources.py`

Persists normalization decisions that should survive future runs, including media URL normalization, fallbacks and author consistency where supported by the current implementation.

### `external_smoke_test.py`

Performs quick checks that external sources respond in a form the adapters can process.

### `validate_catalog.py`

Performs catalog-content validation.

### `validate_catalog_ci.py`

Runs the CI-specific validation used by GitHub Actions before publication.

### `validate_registry.py`

Performs additional registry and generated-catalog consistency checks.

## Safe development procedure

Before changing aggregation logic:

1. Read `scripts/external/README.md`.
2. Identify the source or layer responsible for the behavior.
3. Test the affected reader/normalizer.
4. Run smoke tests.
5. Run the full catalog validation.
6. Confirm that generated catalogs are still generated rather than hand-edited.
7. Confirm that locally protected applications remain present.

## Generated files are not source files

The following are generated outputs:

```text
catalog.json
authors.json
categories.json
```

Do not manually patch them to fix a problem. Correct the canonical JSON, Override, source adapter or normalization logic responsible for the data.

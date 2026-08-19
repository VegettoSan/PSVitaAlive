# `catalog_overrides/` — Manual enrichment

Overrides and patches applied **after** external import and **before** final validation / generation.

## When to use

- Fix wrong dates, names, or links that external sources keep reintroducing
- Add recommended download URLs
- Attach media or metadata not present upstream
- Survive re-aggregation without editing generated `catalog.json`

## When not to use

- New apps that should live in `apps/` as first-class records
- Category taxonomy changes (use `categories/`)
- Temporary local experiments that should not ship

## Guidance

Keep overrides minimal and documented (comments in adjacent docs or clear field intent). Prefer stable IDs matching the app `id` or external identity used by the merge layer.

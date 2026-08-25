# `.github/workflows/` — CI and publication

## Typical workflows

| Workflow | Role |
|----------|------|
| `validate.yml` | Validate catalog structure and policies on changes |
| `pages.yml` | GitHub Pages / static site publication |

Exact job names and triggers live in the YAML files; this README only describes intent.

## External catalog sources

The automatic **VitaDB API/feed is no longer used** by the catalog acquisition flow. It was removed in **August 2026**.

The configured external-source system can still use **VitaHomebrewDB** during the migration toward independent maintenance of `apps/`, `authors/` and `categories/`.

Disabling the VitaDB feed and cleaning existing VitaDB links are **separate tasks**. Existing application records are not silently rewritten by the source-feed change; their old external links can be reviewed and removed or replaced progressively.

The workflow layer must not modify source databases or manually edit generated catalogs. External data must pass through the normal import/normalization/validation flow before becoming canonical repository data.

## Expectations

- Pushes that break catalog validation should fail CI.
- Generated catalogs must remain consistent with `apps/` + configured external aggregation rules.
- Do not commit workflow secrets into the repository; use GitHub Actions secrets when needed.
- Do not reintroduce automatic VitaDB acquisition when updating source/import jobs unless the source policy is intentionally changed and documented.

## Related

- `scripts/generate_catalog.py`
- `scripts/validate_catalog_ci.py`
- `../..//sources/README.md`

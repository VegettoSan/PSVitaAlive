# `.github/workflows/` — CI and publication

## Typical workflows

| Workflow | Role |
|----------|------|
| `validate.yml` | Validate catalog structure and policies on changes |
| `pages.yml` | GitHub Pages / static site publication |

Exact job names and triggers live in the YAML files; this README only describes intent.

## Expectations

- Pushes that break catalog validation should fail CI.
- Generated catalogs must remain consistent with `apps/` + external aggregation rules.
- Do not commit workflow secrets into the repository; use GitHub Actions secrets when needed.

## Related

- `scripts/generate_catalog.py`
- `scripts/validate_catalog_ci.py`

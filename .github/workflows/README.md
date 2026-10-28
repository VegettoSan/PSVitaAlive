# `.github/workflows/` — CI and publication

## Typical workflows

| Workflow | Role |
|----------|------|
| `validate.yml` | Validate catalog structure and policies on changes |
| `pages.yml` | GitHub Pages / static site publication |
| `client-build.yml` | Host C++ policy tests + translations + **VitaSDK 2026.08** client build; uploads a VPK smoke-test artifact |
| `publish-client-release.yml` | Publish a new stable GitHub Release from the successful client CI VPK after verifying version, Title ID, ZIP CRC and notes; attach SHA-256 |
| `discord-notifications.yml` | GitHub Actions notifications for Discord catalog changes; independent of client release publishing |
| `app-generator-check.yml` | JS syntax and offline unit tests for the browser App JSON generator |
| `internet-archive-diagnostics.yml` | Diagnoses Internet Archive endpoints (see workflow triggers/steps) |

Exact job names and triggers live in the YAML files; this README only describes intent.

## Release publication (02.17 and later)

1. Set `VITA_VERSION` to the new `NN.NN` in `Client PSVitaAlive/CMakeLists.txt` and add `docs/releases/NN.NN.md` with the release body.
2. Push to `main`. The normal `Build PS Vita Client` runs C++ tests and compiles `PSVitaAlive.vpk` using official VitaSDK.
3. On a **successful push** build, `Publish PSVitaAlive Release` downloads that exact artifact, verifies VPK CRC and `sce_sys/param.sfo` (`APP_VER=NN.NN`, `TITLE_ID=PSVAS1178`) and creates `PSVitaAlive.vpk.sha256`.
4. The workflow publishes the version tag `NN.NN` with `PSVitaAlive.vpk` and its checksum. Missing release notes or an existing release prevent duplicate publication; an existing tag without a release is a blocking error.
5. Confirm the GitHub `releases/latest` tag, VPK asset and hardware updater. PR/failed builds cannot publish. Keep credentials in GitHub Actions, never in the repo.

**First verified workflow release:** [02.17](https://github.com/VegettoSan/PSVitaAlive/releases/tag/02.17) · [body](../../docs/releases/02.17.md) · [versioning rules](../../docs/self-update-versioning.md). No private servers or manual catalog regeneration are required.

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
- `../../sources/README.md`

## Catalog CI (current policy)

Validation focuses on **repository-owned** `apps/` data and generated catalogs. External VitaDB acquisition is disabled. Prefer fixing local JSON over relying on remote import for new entries.

# PSVitaAlive — Catalog schema V2 migration (work branch)

> Status: **source migration committed on feature branch**, PR #65 remains a draft.
> Production `main`, existing public catalogs, website and client releases remain unchanged.
> CI integration verified reversible conversion and identical V1/V2 generated values.
> **Hardware validation remains pending.**

## Goal

Shorten **all JSON field names belonging to catalog records** (applications, authors,
categories, nested links, subcategories, and any catalog-only metadata) while
preserving **every value** and the existing canonical pipeline. The dictionaries
must be complete before any conversion.

Do not confuse *JSON keys* with their values: never change title IDs, application
IDs, author/category identifiers, names, descriptions, URLs, recommendation flags,
link type values, statuses, dates, sizes, hashes or data paths.

## Official source of truth

`apps/ + authors/ + categories/ -> GitHub Actions -> catalog.json + authors.json + categories.json -> PS Vita + Web`

Generated catalogs are never edited manually. Import sources retain their
original schema; the converter performs source-specific normalization.

Related public catalogs (PS Vita, PSP, PS1), their own generators, scripts,
install paths, data revision `dr`, plugin metadata and validation rules require
a separate inventory before any catalog-wide cutover.

## Phases — each must pass its own gate

0. **Inventory (current)**: run `python scripts/catalog_schema_v2_audit.py --output /tmp/catalog-v2-keys.json`.
   Review each occurrence by its full JSON path, including nested arrays; enumerate
   other generated catalogs and all readers/writers. Freeze one *context-aware* V1/V2
   dictionary and schema version. Never use a blind global text replacement.
1. **Pure transformation**: implement `to_v2` and `to_v1` with collision detection.
   Test `to_v1(to_v2(original)) == original` structurally for every source JSON.
   Assert fields, values, types, array order, URL text and Unicode are unchanged.
2. **Consumers**: make the Vita parser accept V1 and V2, including V1 cached JSON;
   make the web loader normalize both versions before handing objects to existing
   components. Prove all app/link/plugin/data/author/category paths.
3. **Producers**: migrate editable source JSON files, author/category generators,
   the app editor, validators, source importers and GitHub Actions to read/write
   the intended V2 schema. Generate the **three canonical catalogs** automatically
   in the chosen public format. No hand-edited output files.
4. **Other dependencies**: adapt the independent
   `VegettoSan/PsVita-Homebrews-New-Releases` monitor and its dashboard; audit
   the commercial catalog consumers and generators, and all other tooling.
5. **Verification**: compare V1/V2 catalog data after reverse-normalizing, run
   schema validators, website checks, complete VitaSDK build, Vita3K and physical
   PS Vita tests. Test online update, offline V1 cache, V2 cache, optional/failed/
   cancelled update, invalid JSON and stale caches. Record raw/download bytes,
   DOM parse time and RAM use.
6. **Controlled release**: ship the new dual-read Vita client *first* (the existing
   GitHub-release self-updater normally runs before catalog loading). After
   confirming the published VPK/update path, activate V2 generation/publication.
   The old client may still load if update check is disabled, cancelled or fails:
   handle this residual compatibility risk explicitly before cutover.

## Safety and rollback

- Never merge incomplete or unvalidated changes into `main`.
- Publishing the V2 catalog before a working V2 client is available is forbidden.
- Do not rely solely on self-update: the setting `startup_update_check` can be
  disabled and the update can fail/cancel.
- `main` and its release artifacts remain unchanged until sign-off.
- Maintain an easy rollback to V1 generated catalog outputs and previous VPK;
  preserve V1 cached-content parsing in V2.
- The complete field dictionary must be created **from inventory**, not guessed.


## Implementation progress (feature branches only)

- [x] Catalog key inventory across 1,198 app JSON, 679 author JSON, 15 category JSON.
- [x] Reversible `scripts/catalog_schema_v2.py`; strict `scripts/migrate_catalog_schema_v2.py`.
- [x] GitHub Actions converted and committed canonical source JSONs on the feature branch.
- [x] Client parser supports V1/V2 direct field lookup (retains offline V1 cache).
- [x] Web adapter normalizes V1/V2 to the current UI model; web app/author generator exports V2.
- [x] Core catalog generator, validator, normalizer and GitHub workflow updated.
- [x] Ancillary catalog scripts and Discord notifications adapted.
- [x] Release tracker changes prepared in a **separate branch** of the monitor repository.
- [x] CI checkout migration and V1/V2 output parity (all three canonical JSON files).
- [ ] Final PR head CI jobs, especially VitaSDK build, after all commits.
- [ ] Vita3K / physical PS Vita tests and release of dual-reading VPK.
- [ ] Public V2 activation, after VPK release.

## Publication switch

`scripts/generate_catalog.py` generates **V1 by default** while its source
files are V2. Set `PSVA_PUBLIC_SCHEMA_V2=true` only at the controlled catalog
cutover; then the same generated `catalog.json`, `authors.json` and
`categories.json` paths contain compact V2. The workflow's inline
source-synchronization steps respect the same flag.

The old public schema is intentionally retained until a new client with
both V1 and V2 readers is tested, released, and verified to update at startup.
No secondary authoritative catalog is introduced.

## Related draft branches / PRs

- [PSVitaAlive PR #65](https://github.com/VegettoSan/PSVitaAlive/pull/65)
- [Release tracker V1/V2 branch](https://github.com/VegettoSan/PsVita-Homebrews-New-Releases/tree/feature/catalog-schema-v2)

Do not merge the migration until both repositories and the Vita hardware path
are validated. Enabling V2 on public URLs will still break old builds that
disable, cancel or fail their startup self-update; document this tradeoff before
the cutover.

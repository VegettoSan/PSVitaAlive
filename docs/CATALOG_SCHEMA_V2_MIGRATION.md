# PSVitaAlive — Catalog schema V2 migration (work branch)

> Status: **phase 0 / inventory only**. No source catalog has been renamed.
> Production `main`, public catalogs, website and client releases remain unchanged.

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

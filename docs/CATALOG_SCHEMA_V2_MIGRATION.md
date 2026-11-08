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


## Experimental PS Vita / PSP / PS1 game catalog V2 compaction

Game catalogs are **separate public datasets**, not part of the official three
Homebrew catalogs derived from `apps/`, `authors/` and `categories/`. Do not
confuse them with those canonical sources. The commercial game JSON files are
left in V1 until the controlled V2 client/publication switch.

`scripts/compact_game_catalogs_v2.py` reads the three current JSONs
*without editing them*, validates every original field path, and writes only
to an explicitly separate output directory. CI performs full V1→V2→V1
roundtrip checks, browser JavaScript decoding of **all 4,007 game records**,
and verification of the Vita C++ parser's field aliases. It preserves every
region key (US, EU, JP, ASIA, INT), Title ID, content ID, PKG URL, link
order, `recommended`, update metadata, RAP and other scalar values.

Special game-only fields now have aliases:
`required_fw → rfw`, `pkg_name → pn`. `rap` is already short and
is kept unchanged. Regional labels are **data**, not keys to rename.

The `catalog_psvita_games.zrifidx` license sidecar remains **byte-for-byte
unchanged**; its content-ID lookups must be regression-tested separately.

Measured raw UTF-8 file sizes in GitHub Actions (V2 files use minified JSON):

| Catalog | Existing V1 bytes | Experimental V2 bytes | Reduction |
|---|---:|---:|---:|
| PS Vita (1,969 games) | 8,230,576 | 5,701,324 | 30.73% |
| PSP (1,228 games) | 1,712,289 | 1,171,943 | 31.56% |
| PS1 (810 games) | 945,351 | 650,339 | 31.21% |
| **Total** | **10,888,216** | **7,523,606** | **30.90%** |

This reduces game JSON payloads by **3,364,610 bytes** before HTTP
compression. The V2 files are built only as disposable workflow artifacts
`PSVitaAlive-game-catalogs-V2-test` and are **not committed over the
public V1 files**. See
[game catalog V2 workflow](../.github/workflows/catalog-schema-v2-games.yml).

Remaining validation before production: PS Vita / Vita3K hardware and license
sidecar tests, offline V1 and V2 cache tests, update/install flows and
deliberate publication after the compatible VPK has been released.

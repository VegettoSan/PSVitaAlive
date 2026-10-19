# `docs/` — Project documentation

Supplementary documentation for architecture, pipelines and client behaviour.

## How to use this folder

1. Prefer **code and generated artifacts** as source of truth when docs drift.
2. Update the matching README next to the code when behaviour changes.
3. Keep long design/implementation notes here when they do not belong in a module README.
4. For client subsystems that cross more than one module (for example catalog parser + UI + startup), keep one detailed document here and link to it from the affected module READMEs.

## Documentation map

| Location | Topic |
|----------|--------|
| Root `README.md` | Project overview, public JSON API, multi-catalog, recommended Vita setup, download/install locks, commercial PKG routing, client feature summary |
| `docs/BGDL_PREPARATION_FRAME.md` | **BGDL preparation-frame handoff**: keep ShellSvc enqueue on the main thread while presenting a visible Preparing overlay first |
| `docs/ADAPTIVE_DOWNLOAD_PIPELINE_20261018.md` | **Adaptive transfer and ZIP/VPK optimizations (18 October 2026):** thresholds, HTTP validators/checksums, buffering, progress UI, measurements and regression tests |
| `docs/INSTALL_ALL_WORKER_CRASH_FIX_20261018.md` | **Install All SIGABRT regression:** dump evidence, worker-lifecycle synchronization, VPK→ZIP transition and extraction render-yield mitigation |
| `docs/GAME_DATA_ZIP_INSTALL_FIX_20261019.md` | **Game/Data Files ZIP dispatch fix**: type-aware extraction over VPK-looking magic, missing-destination guard, ZIP recovery and regression test |
| `docs/DOWNLOAD_RESILIENCE.md` | **In-app download + archive hardening**: HTTP response isolation, retries, safe Range resume, MediaFire re-resolution, Archive.org failover, runtime free-space guard, metadata throttling, large ZIP / ZIP64 extraction and diagnostics |
| `docs/IMAGE_CACHE.md` | **Image cache v3**: per-resource identity, H/PV/PSP/PS1 scopes, 256 buckets, URL-change replacement, startup 200 MiB → ~40 MiB trim, loading-bar progress, localization and validation |
| `docs/MULTILANGUAGE.md` | Current UI localization architecture, packaged languages, system/manual selection, English fallback and startup localization ordering |
| `docs/NETWORK_TLS.md` | libcurl / OpenSSL / Archive.org TLS failover / optional mbedTLS; TLS-specific companion to `DOWNLOAD_RESILIENCE.md` |
| `docs/PLUGIN_UPDATES.md` | **Planned** remote plugin update system (manifest / versioned Plugin links) — deferred |
| `docs/UPDATE_DETECTION.md` | Installed/update version detection notes |
| `Client PSVitaAlive/README.md` | Native client overview, runtime data and subsystem behaviour |
| `Client PSVitaAlive/source/catalog/README.md` | Multi-catalog cache, zRIF sidecar and image-cache tagging responsibilities |
| `Client PSVitaAlive/source/network/README.md` | Native network module: HttpClient, DownloadManager, provider recovery, resume invariants, runtime guards and diagnostics |
| `Client PSVitaAlive/source/installer/README.md` | VPK / BGDL PKG, Plugin + tai config, Adrenaline unpack, ZIP integrity, keep-awake + shell locks |
| `Client PSVitaAlive/source/ui/README.md` | Native UI, image cache integration, loading overlays, LOCKED UI, themes, fonts, i18n hooks, essential plugins modal |
| `Client PSVitaAlive/source/update/README.md` | Self-update (PSVAUPDT1) handoff rules |
| `apps/`, `authors/`, `categories/` | Canonical Homebrew data contracts |
| `scripts/` | Generation and validation |
| `sources/` | External feeds |
| `web/` | Website |
| `.github/workflows/` | CI / Pages |

## Image cache documentation rule

The authoritative high-level behaviour is documented in [`IMAGE_CACHE.md`](IMAGE_CACHE.md). When changing image-cache code, keep these surfaces synchronized:

```text
catalog_parser.cpp
        ↓
internal image identity/tag
        ↓
ImageCache v3
        ↓
startup maintenance + FullCatalogScreen progress
```

Do not add cache-only fields to the public catalog JSON schema. The cache metadata remains an in-memory client implementation detail.

## Download / archive documentation rule

The cross-module resilience behaviour is documented in [`DOWNLOAD_RESILIENCE.md`](DOWNLOAD_RESILIENCE.md), with TLS-specific details in [`NETWORK_TLS.md`](NETWORK_TLS.md) and module-level implementation notes in [`../Client PSVitaAlive/source/network/README.md`](../Client%20PSVitaAlive/source/network/README.md).

When changing any of these components, keep the docs synchronized:

```text
DownloadManager
      ↓
HttpClient / provider resolver
      ↓
payload.part + resume metadata
      ↓
FormatDetector / InstallController
      ↓
ZipExtractor / installer
```

Do not weaken strict Range, payload-size or ZIP-integrity checks merely to make a failing provider appear successful. Provider-specific recovery should preserve the same integrity rules.

## Change classification

When fixing a bug, identify the layer:

1. External acquisition  
2. Normalization / identity / merge  
3. Overrides  
4. Catalog generation / validation  
5. Website  
6. PS Vita client (network, install, UI, image cache, update)

Patch the responsible layer—not a generated catalog—whenever possible.

- [BGDL_PREPARATION_FRAME.md](BGDL_PREPARATION_FRAME.md) — main-thread BGDL enqueue with one-frame visible preparation handoff
- [ADAPTIVE_DOWNLOAD_PIPELINE_20261018.md](ADAPTIVE_DOWNLOAD_PIPELINE_20261018.md) — adaptive downloader design, on-device results and maintenance testing
- [DOWNLOAD_RESILIENCE.md](DOWNLOAD_RESILIENCE.md) — hardened HTTP downloads, retries, resume, providers, storage guards and ZIP/archive integrity
- [IMAGE_CACHE.md](IMAGE_CACHE.md) — client image cache v3 / disk policy / per-image replacement
- [MULTILANGUAGE.md](MULTILANGUAGE.md) — localization architecture and current packaged languages
- [NETWORK_TLS.md](NETWORK_TLS.md) — libcurl / OpenSSL / Archive.org TLS failover / optional mbedTLS


## ZIP folder-relocation fix and saved archive maintenance

See [ZIP_RECOVERY_AND_RANGE_ASSEMBLY_20261018.md](ZIP_RECOVERY_AND_RANGE_ASSEMBLY_20261018.md) for the verified 19 October 2026 Simpsons extraction, directory-stage/rename algorithm, per-archive offline recovery buttons, success-only ZIP deletion, limitations on overwriting existing directory branches, and source files changed. Related README updates: [native client](../Client%20PSVitaAlive/README.md), [installer](../Client%20PSVitaAlive/source/installer/README.md), [UI](../Client%20PSVitaAlive/source/ui/README.md).

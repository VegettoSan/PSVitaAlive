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
| `docs/MULTILANGUAGE.md` | Current UI localization architecture, packaged languages, system/manual selection, English fallback, startup ordering and keyboard UI labels |
| `docs/KEYBOARD_NATIVE_IME_REPLACEMENT_20261020.md` | **Accepted architecture decision:** hardware IME/PAF failure history; rationale for replacing Sony IME; built-in QWERTY/touch/gamepad controls, themed modal, ZIP input, localization, system-library distinction and tests |
| `docs/NETWORK_TLS.md` | libcurl / OpenSSL / Archive.org TLS failover / optional mbedTLS; TLS-specific companion to `DOWNLOAD_RESILIENCE.md` |
| [`docs/VITASDK_OPENSSL_LINKER_FIX_20261027.md`](VITASDK_OPENSSL_LINKER_FIX_20261027.md) | **27 Oct 2026 VitaSDK CI linker failure:** incompatible libcurl/OpenSSL 1.0.2 downgrade, corrected workflow and validation checklist |
| [`docs/AUDITORIA_FUNCIONAL_20261025.md`](AUDITORIA_FUNCIONAL_20261025.md) | **Auditoría actual de funcionalidades**: implementación comprobada por código, cambios de conducta, documentación desfasada, límites y pruebas pendientes |
| [`docs/INSTALL_ALL_SPACE_PREFLIGHT_20261025.md`](INSTALL_ALL_SPACE_PREFLIGHT_20261025.md) | **Install All**: suma de enlaces seleccionados, modo individual por tamaños ausentes, cabeceras HTTP previas al cuerpo, seguridad y validación |
| [`docs/SAVED_ZIP_SEGMENTS_20261025.md`](SAVED_ZIP_SEGMENTS_20261025.md) | **ZIP guardados**: ZIP completo o rangos `.001`...`.004`, reextracción offline, borrar/cancelar y confirmaciones |
| [`docs/ZIP_SOBREESCRITURA_SEGURA_20261025.md`](ZIP_SOBREESCRITURA_SEGURA_20261025.md) | Reutilización de espacio al sobreescribir entradas ZIP y advertencias sobre archivos truncados / staging |
| [`docs/PLUGIN_DETECTION_CONFIG_20261023.md`](PLUGIN_DETECTION_CONFIG_20261023.md) | Detección real de plugins taiHEN (archivo + config) y límites |
| [`docs/CATALOG_PLUGIN_CONFLICTS_20261027.md`](CATALOG_PLUGIN_CONFLICTS_20261027.md) | **Avisos de conflictos entre enlaces Plugin del catálogo**: ventana existente con contenido desplazable, reglas y fuentes, límites de detección y pruebas/CI pendientes |

| `docs/PLUGIN_UPDATES.md` | **Planned** remote plugin update system (manifest / versioned Plugin links) — deferred |
| [`docs/DISCORD_COMMUNITY_QR_20261027.md`](DISCORD_COMMUNITY_QR_20261027.md) | Botón Discord en cliente, invitación QR offline, colores fijos y enlaces desde web |
| [`docs/WEB_QR_HANDOFF_PLAN.md`](WEB_QR_HANDOFF_PLAN.md) | QR/web handoff **ya integrado en main**; documento conserva planificación histórica y pruebas pendientes |
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

## Keyboard maintenance documentation rule

The supported text-input path is the in-app QWERTY keyboard for **both catalog search and custom ZIP destination**. When changing it, keep the decision record, [UI module README](../Client%20PSVitaAlive/source/ui/README.md), [native client README](../Client%20PSVitaAlive/README.md), and [localization guide](MULTILANGUAGE.md) synchronized. Do not resurrect the former Sony IME controller or remove Sony promoter/PAF utilities used by unrelated installers. `SceCommonDialog_stub` remains necessary for `vita2d`'s static link, not for PSVitaAlive text input.

Historical diagnostic notes dated **before 20 October 2026** describe the *previous* IME architecture; they are not current implementation guidance. Retain them as historical evidence unless explicitly superseded in a new record.

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

## Regla de documentación de instalaciones y espacio (estado vigente)

- Para `Install All`, el cálculo conjunto suma **solo los enlaces añadidos a
  la cola**, incluidos ambos Game Files/Data Files si están seleccionados.
- Si falta el tamaño de cualquiera de ellos, la cola cambia a revisión
  **individual**. El descargador reutiliza las cabeceras HTTP antes de
  escribir y conserva el guardia de espacio durante la descarga.
- El tamaño del ZIP descargado **no** sustituye su espacio descomprimido: el
  extractor comprueba el crecimiento real al abrir el archivo.
- El flujo de recuperación de ZIP puede conservar un ZIP completo o 2/4 rangos
  crudos y presenta reextraer/eliminar/cancelar; no debe perder compatibilidad
  con formatos guardados por versiones anteriores.
- Mantener sincronizados
  [espacio](INSTALL_ALL_SPACE_PREFLIGHT_20261025.md),
  [recuperación](SAVED_ZIP_SEGMENTS_20261025.md),
  [sobrescritura](ZIP_SOBREESCRITURA_SEGURA_20261025.md),
  [cliente](../Client%20PSVitaAlive/README.md),
  [red](../Client%20PSVitaAlive/source/network/README.md) e
  [instalador](../Client%20PSVitaAlive/source/installer/README.md).
- Afirmaciones «compila en CI» y «probado en PS Vita real» son distintas.
  Documentar ambas por separado. Revisar el inventario en
  [AUDITORIA_FUNCIONAL_20261025.md](AUDITORIA_FUNCIONAL_20261025.md).

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
- [MULTILANGUAGE.md](MULTILANGUAGE.md) — localization architecture and current packaged languages, including localized keyboard chrome
- [KEYBOARD_NATIVE_IME_REPLACEMENT_20261020.md](KEYBOARD_NATIVE_IME_REPLACEMENT_20261020.md) — decision record: native Sony IME retired from PSVitaAlive text input after repeated real-hardware PAF crashes; integrated QWERTY implementation, touch/gamepad navigation and verification
- [NETWORK_TLS.md](NETWORK_TLS.md) — libcurl / OpenSSL / Archive.org TLS failover / optional mbedTLS


## ZIP folder-relocation fix and saved archive maintenance

See [ZIP_RECOVERY_AND_RANGE_ASSEMBLY_20261018.md](ZIP_RECOVERY_AND_RANGE_ASSEMBLY_20261018.md) for the verified 19 October 2026 Simpsons extraction, directory-stage/rename algorithm, per-archive offline recovery buttons, success-only ZIP deletion, limitations on overwriting existing directory branches, and source files changed. Related README updates: [native client](../Client%20PSVitaAlive/README.md), [installer](../Client%20PSVitaAlive/source/installer/README.md), [UI](../Client%20PSVitaAlive/source/ui/README.md).

- [Estabilidad del instalador — fase 1 (2026-10-19)](INSTALLER_PHASE1_STABILITY_20261019.md): recuperación ZIP, protección de archivos, cancelación y reintentos.

- [Investigación futura: versiones instaladas externamente](FUTURE_EXTERNAL_VERSION_IDENTIFICATION_20261019.md): APP_VER/VERSION, recibos, VitaDB MD5, huellas por release y límites. **Solo documentado, no implementado.**

- [Estabilidad del instalador — fase 2 (2026-10-19)](INSTALLER_PHASE2_SPACE_PREFLIGHT_20261019.md): preflight según destino/tipo, tamaños correctos de enlaces y límites pendientes.

## Correcciones y referencias posteriores (25 octubre 2026)

El comportamiento actual de `Install All` se describe en
[INSTALL_ALL_SPACE_PREFLIGHT_20261025.md](INSTALL_ALL_SPACE_PREFLIGHT_20261025.md);
los límites de sobreescritura de archivos existentes en
[ZIP_SOBREESCRITURA_SEGURA_20261025.md](ZIP_SOBREESCRITURA_SEGURA_20261025.md).
La gestión actual de ZIP completos y segmentados guardados está en
[SAVED_ZIP_SEGMENTS_20261025.md](SAVED_ZIP_SEGMENTS_20261025.md).
Los documentos de fechas anteriores se conservan como historial, no
como especificación preferente frente al código y estos registros.


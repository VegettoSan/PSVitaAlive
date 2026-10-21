# PSVitaAlive — Image cache v3

> **Status:** implemented in the native PS Vita client.
> **Scope:** icons, covers and screenshots used by the client UI. Catalog JSON files and remote image URLs are not rewritten on disk.
> **Primary code:** `Client PSVitaAlive/source/ui/image_cache.cpp`, `Client PSVitaAlive/include/ui/image_cache.hpp`, `Client PSVitaAlive/source/catalog/catalog_parser.cpp`, and startup integration in `Client PSVitaAlive/source/main.cpp`.

## Goals

The image cache is designed around PS Vita constraints:

- images load **on demand** while the user browses;
- cached images are reused without another network download;
- changing an image URL in a catalog must invalidate only that concrete image;
- old versions of an updated image must not accumulate indefinitely;
- Homebrew, Vita Games, PSP and PS1 image namespaces must not collide;
- normal browsing must not run a global cache-size scan or eviction pass;
- disk use must be bounded without adding a database or continuous LRU writes;
- startup maintenance must be visible to the user and localized.

The current implementation is cache layout **v3**.

## Catalog scopes

Each parsed catalog receives an internal image scope:

| Catalog | Prefix |
|---|---|
| Homebrew (`catalog.json`) | `H` |
| PS Vita games (`catalog_psvita_games.json`) | `PV` |
| PSP (`catalog_psp_games.json`) | `PSP` |
| PS1 (`catalog_ps1_games.json`) | `PS1` |

Unknown/default catalog paths fall back to Homebrew (`H`).

These prefixes are internal client metadata. They are not new public JSON fields and do not change the catalog schema.

## Stable identity per image resource

A cache entry is not identified only by its URL. The parser creates a stable resource identity from:

```text
catalog prefix + stable app/game owner + image role
```

The owner is:

```text
title_id when available
otherwise id
```

The role differentiates resources belonging to the same item, for example:

```text
icon
cover
shot0
shot1
shot2
shot3
...
```

The identity string is hashed with 64-bit FNV-1a and represented as 16 hexadecimal characters.

Conceptually:

```text
H | VITAQUAKE | icon
        ↓
64-bit stable resource key
```

Changing the remote URL does **not** change this resource key as long as the item identity and role stay the same.

## Internal URL tag

`catalog_parser.cpp` appends cache-only metadata to in-memory image URLs:

```text
#psva_cache=<CATALOG>:<RESOURCE_KEY>
```

or `&psva_cache=...` when the URL already contains a fragment.

Example:

```text
https://example.org/icon-v2.png#psva_cache=H:7F31A820D4C9E112
```

This tag is an implementation detail:

- it is added **after parsing**;
- it is not written back to JSON;
- it is not part of VitaHub's public catalog contract;
- `ImageCache` strips it before handing the real URL to libcurl.

Keeping the metadata attached to the in-memory URL avoids widening every UI call site with extra cache parameters.

## Disk layout

The root is:

```text
ux0:data/psvitaalive/cache/images/v3/
```

Stable resource keys are distributed into **256 buckets** using the first two hexadecimal characters of the 64-bit resource key:

```text
images/v3/
├── 00/
├── 01/
├── ...
├── C5/
└── FF/
```

A catalog-aware cached image uses this general form:

```text
<namespace>_<catalog>_<resource-key>_<url-hash>.<ext>
```

Examples:

```text
app_H_C556534F2CF739BB_4EA29AB1.png
shot_H_2BB82D98283979E2_8E4A2C10.png
app_PV_7794C335B29E8741_4429D8A1.png
shot_PSP_6D61D2D608065D3F_1E1B4AC2.png
app_PS1_E82D1E84B4D298C1_770A80C3.png
```

`app_` and `shot_` stay at the start of the filename so existing `FullCatalogScreen` logic that distinguishes app images from screenshots remains compatible.

The URL hash is 32-bit FNV-1a of the normalized real URL. The resource key is the stable 64-bit identity described above.

Untagged/generic images remain supported with the `U` scope for compatibility.

## Why URL changes are detected before the new image is downloaded

Suppose the cached file is:

```text
app_H_7F31A820D4C9E112_11111111.png
```

A catalog update changes only the icon URL. The new requested path becomes:

```text
app_H_7F31A820D4C9E112_99999999.png
```

The first hash is unchanged because it identifies the resource (`app + icon`). The second hash changes because the URL changed.

Therefore the client already knows that the files are two versions of the **same image resource**, even before the new URL has been downloaded.

No per-catalog manifest is required for this replacement path.

## On-demand request flow

During normal browsing:

```text
catalog image requested
        ↓
calculate current v3 path
        ↓
is exact current file cached and valid?
   ├─ yes → use it
   └─ no  → queue/download only this image
                    ↓
             normalize/validate
                    ↓
               success?
              ├─ no  → keep failure/retry behaviour
              └─ yes → mark ready
                         ↓
                    remove older files
                    with same resource key
```

A failed new download does **not** delete the previous version first. Superseded cached versions are pruned only after the current file exists and has passed image normalization/validation.

If an older queued request for the same resource is still pending when a newer URL is requested, the obsolete queued work is removed. If that old resource is actively downloading, cancellation is requested so the new version can take over.

## UI-first scheduling while browsing

Image network work is intentionally subordinate to catalog navigation on real PS Vita hardware. `FullCatalogScreen` still defines the exact viewport set first (9 app images in Full Catalog and 3 app images in the split Detail list, plus only screenshots intersecting the visible Detail body), releases/cancels work outside that set, and then decides whether new image work may start.

While the animated catalog scroll has not settled near `catalogScrollRow`, missing app images are **not newly queued for network download**. Images already marked ready in the local cache may still be decoded into a GPU texture, limited to the existing one-new-texture-per-frame budget. This keeps already-cached browsing responsive without allowing rapid Up/Down navigation to create a download/cancel storm for intermediate rows.

The same policy applies to screenshots while the Detail body itself is still scrolling: a missing screenshot waits until the Detail scroll settles, while an already-ready cached screenshot may be prepared normally.

`cancelQueuedExcept()` also covers the currently active image transfer. If `currentPath_` leaves the viewport keep-set, `cancelRequested_` is raised and the existing libcurl cancellation callback stops the transfer. The worker keeps the active identity through normalization and checks cancellation again before publishing the result, so work that became obsolete at the transfer/normalize boundary is discarded instead of being marked ready.

Image download progress is published to shared UI state at most every **100 ms (10 Hz)**, with a final known-total update allowed immediately. The worker still tracks the latest byte counters locally for accounting; throttling only reduces cross-thread mutex traffic and does not limit network throughput.

Conceptually:

```text
rapid catalog navigation
        ↓
compute final visible keep-set
        ↓
prune queued off-screen work
        ↓
cancel active transfer if it left the keep-set
        ↓
scroll still moving?
   ├─ yes → reuse/decode ready cache only; no new network requests
   └─ no  → queue missing images for the settled viewport
```

## Settings image-work suspension

Settings is treated as a UI-only surface. When it opens, `FullCatalogScreen` cancels queued/active catalog image work once and `updateAndDraw()` stops calling `prepareVisibleTextures()` until Settings closes. Already-created app textures stay resident in the existing bounded LRU, so returning to the catalog does not force unnecessary decode/reload churn.

This also keeps Settings isolated from image-network and GPU-decode contention while the user changes options. No catalog schema or disk-cache format changes are involved.

## Embedded keyboard and image-cache interaction (since 20 October 2026)

The client no longer invokes Sony's IME when searching or typing a custom ZIP destination. The on-screen QWERTY modal is drawn over the existing catalog frame and **does not require an IME-specific release of every GPU texture or a blocking worker-quiescence wait**. Those were experiments for the old native IME crash, not part of the supported keyboard architecture.

- While `FullCatalogScreen::keyboardVisible()` is true, `main.cpp` includes that condition in `images.setNetworkPaused(...)`.
- `FullCatalogScreen::updateAndDraw()` gives the keyboard exclusive input and skips `prepareVisibleTextures()`, animations and normal catalog input until the modal closes. The already-resident background/catalog textures remain drawable under the overlay.
- The keyboard itself uses text and `vita2d_draw_rectangle`; it does not allocate new per-key textures or start background network work.
- After close, normal visibility-based scheduling resumes. Do **not** reintroduce `ImageCache::isQuiescentForIme`, IME `prepareForIme`, or Sony CommonDialog frame updates merely to support search.
- Remaining image-cache worker cancel/pause/mutex protections still serve normal catalog performance and installation; they must not be removed as a blanket IME cleanup.

See [keyboard replacement decision](KEYBOARD_NATIVE_IME_REPLACEMENT_20261020.md) for the hardware investigation and [UI implementation](../Client%20PSVitaAlive/source/ui/README.md) for frame ownership.

## Image normalization dimensions

Downloaded PNG/JPEG images are normalized locally before they are marked ready for the UI. The current maximum dimensions are:

```text
app / icon / cover: 128 px maximum side
screenshots:         256 px maximum side
```

Aspect ratio is preserved. Images smaller than the applicable limit are not enlarged; images above the limit are downscaled so neither width nor height exceeds the configured maximum.

The normalized cache output is PNG. Reducing screenshots from the earlier development value of 512 px to **256 px** lowers cached disk usage and the amount of image data the Vita must decode/use for the relatively small screenshot viewport, while keeping substantially more detail than the UI can normally display at once.

The finalized `v3` layout was still unpublished when this limit changed, so no additional cache-layout revision or public migration was introduced. Development caches that contain a larger normalized screenshot are rejected by `request()`'s dimension validation when that resource is requested; the stale file is removed and the resource is downloaded/normalized again at the current 256 px limit.

This normalization limit does **not** reduce the first network transfer size: the original remote image is downloaded first and then normalized on-device for subsequent cache use.

## Per-image replacement

After the new image succeeds, `pruneSupersededVersions()` opens only the image's bucket and looks for siblings sharing the same identity stem:

```text
app_H_7F31A820D4C9E112_
```

The current path is kept; other files with the same stem are removed.

This means:

- changing `icon` does not delete `cover`;
- changing `shot1` does not delete `shot0`;
- changing a Homebrew image cannot delete a Vita/PSP/PS1 image;
- there is no full-catalog cleanup when a single image changes.

The bucket design prevents replacement from scanning a directory containing every cached image.

## Startup disk limit

Normal per-image replacement prevents multiple URL versions of an actively used resource from accumulating, but on-demand browsing can still grow the cache over time. To bound disk usage, v3 also performs **startup-only maintenance**.

Constants:

```text
maximum before cleanup: 200 MiB
cleanup target:           40 MiB
```

That means the cache may grow during a session. It is checked on the **next launch**. If it exceeds 200 MiB, the startup cleanup keeps roughly 20% of the configured maximum (about 40 MiB), leaving about 160 MiB of headroom for future browsing.

### Important threshold rule

```text
cache <= 200 MiB
→ no global eviction

cache > 200 MiB
→ delete oldest complete image files
→ stop when cache is approximately <= 40 MiB
```

The target is approximate because files are indivisible. The code never truncates an image to hit an exact byte target.

## Startup ordering

The maintenance pass runs from `ImageCache::init()` **before the image worker thread is created**.

Conceptually:

```text
application starts
    ↓
settings + localization
    ↓
startup self-update phase
    ↓
CatalogManager init
    ↓
ImageCache::init()
    ├─ legacy/unpublished-layout maintenance
    ├─ startup disk-limit check/cleanup
    └─ create ImageCache worker
    ↓
request/load Homebrew catalog
    ↓
normal browsing
```

Because the worker does not exist yet during the disk cleanup, startup eviction cannot race a libcurl image download, normalization job, or queued image request.

## Startup scan strategy and RAM use

The startup limit is intentionally implemented in two stages.

### Pass 1 — size check

The client walks the v3 root and existing two-character bucket directories, sums actual image file sizes, and removes abandoned `.normalized` temporary files when possible.

It does **not** build and sort a full file list when the cache is within the limit.

```text
scan → total <= 200 MiB → done
```

This keeps the common startup path small.

### Pass 2 — only when over the limit

If pass 1 reports more than 200 MiB, the client collects temporary metadata for cache payload files:

```text
path
size
mtime (ctime fallback)
```

The list is sorted from oldest to newest.

Eviction then processes complete files in that order until the measured remaining cache is at or below roughly 40 MiB.

The timestamp represents when the cached file was written/downloaded, **not** the last time the user viewed it. PSVitaAlive deliberately avoids touching timestamps or maintaining an LRU database during browsing.

## Whole-file deletion safety

The cache size is measured in bytes, but an eviction operation is always a whole-file `sceIoRemove()`.

The client never truncates an image and never tries to remove a byte fraction from a file.

If deleting a candidate fails:

- its size is **not** subtracted from the measured total;
- the file remains on disk;
- cleanup continues with later candidates where possible;
- the failure count is written to the diagnostic log.

Because complete files are removed one at a time, the final cache can be slightly below the 40 MiB target. That is expected.

## Startup loading UI and progress

Startup maintenance reuses the existing catalog/loading overlay rather than introducing a second UI system.

Phases exposed by `ImageCache::StartupMaintenancePhase`:

```text
Checking
Cleaning
Ready
```

User-facing messages are equivalent to:

```text
Image cache
Checking image cache...

Image cache
Cleaning old cached images...

Image cache
Image cache ready
```

The callback updates `FullCatalogScreen::setCatalogLoading()` and calls `updateAndDraw()` so the startup screen remains visibly responsive while maintenance runs.

### Progress semantics

The progress bar does not represent partial deletion of an image.

- **Checking:** progress follows scanned cache directories/buckets.
- **Cleaning:** progress follows whole candidate files successfully removed/processed by the cleanup loop.
- **Ready:** completes the maintenance phase before normal catalog loading continues.

Updates are throttled (for example every several scanned buckets / several cleanup operations) so rendering progress does not become the dominant cost of maintenance.

## Localization during startup maintenance

`LocalizationManager` is initialized earlier in `main.cpp`, immediately after installer/settings initialization and before the cache maintenance stage.

This ensures the loading overlay can use the selected/System-resolved language even though the normal catalog has not started loading yet.

The currently packaged language files are:

```text
en.lang
es.lang
fr.lang
de.lang
it.lang
pt-PT.lang
pt-BR.lang
ru.lang
```

The image-cache startup messages have localized compatibility fallbacks in the localization layer for these installed languages. If a `.lang` file later defines the same image-cache key, the normal `.lang` lookup wins first, so the fallback does not block the data-driven localization model.

Catalog metadata remains untranslated.

## Behaviour after startup eviction

Startup eviction does not modify catalog entries, URLs or in-memory cache identities.

After maintenance:

```text
requested image still exists
→ isCached()/request() reuse it normally

requested image was evicted
→ it behaves like a first-time image
→ request() downloads it again on demand
→ it returns to the cache
```

No special recovery path is needed. Evicted images are simply cache misses.

The 200 MiB check is **not** repeated while browsing. A session may grow beyond the threshold; cleanup waits until the next application start.

## Migration / legacy cleanup

### v2 → v3

Old v2 files did not contain enough catalog/resource identity to support precise replacement. When a legacy v2 tree is found, it is removed during initialization and v3 repopulates lazily.

### Unpublished early v3 draft

An earlier development-only v3 layout used catalog + URL hash without the stable per-resource key. Because it was not released, the finalized v3 initialization removes those incompatible draft files/manifests once and writes a `.per_image_v1` marker.

The final v3 layout described in this document is the authoritative format.

## Interaction with the catalog parser

The parser only adds internal cache metadata to media URLs in memory. It does **not** change the official VitaHub data architecture:

```text
apps/ + authors/ + categories/
        ↓
GitHub Actions
        ↓
catalog.json + authors.json + categories.json
        ↓
Client + Web
```

Commercial catalog files remain separate as before.

Image-cache identity is a client implementation detail and must not become a required catalog JSON field.

## Logging

Important diagnostic entries include messages equivalent to:

```text
[ImageCache] worker initialized cache=v3 replacement=per-image ...
[ImageCache] image replacement identity=... removed=... freed=... current=...
[ImageCache] superseded work identity=... queued=... active_cancel=...
[ImageCache] startup cache size=... limit=... temp_removed=...
[ImageCache] startup trim begin before=... keep=... planned_files=...
[ImageCache] startup trim complete removed=... failed=... before=... after=... target=...
```

These are especially useful when validating behaviour on Vita3K and real hardware.

## Known trade-offs

### Same URL, changed bytes

The cache key includes the URL hash. If a server replaces the content behind exactly the same URL, that alone does not create a new cache path.

Current VitaHub maintenance policy is therefore:

> when the actual image changes, change its catalog image URL as well.

This keeps runtime validation cheap and avoids ETag/hash traffic for every image.

### Removed apps / removed screenshot slots

Per-image replacement is demand-driven. If an app disappears from a catalog entirely, or a screenshot slot is removed and never requested again, there is no replacement request that can immediately prune that old file.

Those orphaned files are eventually eligible for the **startup global size trim** once the cache exceeds 200 MiB.

### Recent means recently written

Startup eviction uses filesystem modification time (creation time fallback), not a continuously updated last-viewed timestamp. This avoids storage writes every time an image is rendered or scrolled past.

### No global maintenance during use

This is intentional. Browsing performance is prioritized over enforcing an exact real-time disk ceiling.

## Validation checklist

When changing this subsystem, test at least:

1. Empty cache → icons/screenshots download normally.
2. Existing current image → reused without network download.
3. Change one icon URL → new icon downloads; only previous icon version is removed.
4. Change `shot1` → `shot0`, cover and icon remain untouched.
5. Same resource has obsolete queued download → old queued work is removed/cancelled.
6. New image download fails → previous cached sibling is not proactively deleted.
7. Cache below 200 MiB → startup scan completes without eviction/sort pass.
8. Cache above 200 MiB → oldest whole files are removed until approximately 40 MiB remains.
9. Forced `sceIoRemove()` failure → failed file size is not falsely deducted.
10. Evicted image requested after startup → re-downloads normally.
11. Homebrew cleanup/replacement cannot remove PV/PSP/PS1 resource versions.
12. Loading overlay remains responsive during scan/cleanup.
13. Startup maintenance text resolves in every currently packaged language.
14. `app_` / `shot_` path classification still works in `FullCatalogScreen`.
15. Screenshot normalization produces a maximum side of 256 px while app/icon/cover images remain capped at 128 px.
16. Build and run on Vita3K; validate on real PS Vita before release when possible.

## Files to review when modifying the cache

| File | Responsibility |
|---|---|
| `Client PSVitaAlive/source/catalog/catalog_parser.cpp` | catalog scope + stable resource identity + internal URL tag |
| `Client PSVitaAlive/include/ui/image_cache.hpp` | public cache API + startup maintenance callback |
| `Client PSVitaAlive/source/ui/image_cache.cpp` | paths, downloads, normalization limits, validation, replacement, startup size policy |
| `Client PSVitaAlive/source/main.cpp` | localization/startup ordering + progress UI callback |
| `Client PSVitaAlive/include/localization/localization.hpp` | localized startup cache fallback |
| `Client PSVitaAlive/source/ui/full_catalog_screen.cpp` | loading overlay and texture/cache consumers |

The client code remains the source of truth if this document ever drifts.

## Navigation-aware image scheduling and app texture LRU

Real-hardware testing showed two remaining hitch sources after the initial UI-first image work: the D-pad auto-repeat gap and immediate GPU texture release.

The client now treats vertical navigation as busy while **UP/DOWN is physically held**, while catalog/detail scroll animation is still moving, and for **150 ms after the last held-navigation frame**. This is intentionally independent of the D-pad repeat cadence (320 ms initial delay / 420 ms repeat interval), so the image worker cannot mistake the pause between repeat steps for the user having stopped.

While navigation is busy:

- queued/active image work is cancelled once on entry;
- no new image network request is started;
- no new PNG/JPEG GPU texture decode is started;
- already resident GPU textures continue to render normally.

Once navigation settles, the final viewport resumes on-demand image preparation at the existing one-new-texture-per-frame limit.

Application/icon textures now use the existing bounded GPU LRU as intended. Leaving the viewport no longer immediately frees an `app_` texture. Up to `MAX_APP_TEXTURES = 18` recent app textures may remain resident; loading the next app texture evicts the least-recently-used app texture only when that bound is reached. With normalized app images capped at 128 px, a 128x128 RGBA surface is about 64 KiB, so 18 raw surfaces are roughly 1.1 MiB before vita2d/GXM overhead.

Screenshots keep the previous aggressive policy: off-screen `shot_` textures are released and `MAX_SCREENSHOT_TEXTURES = 6` remains unchanged. Catalog switches still call the full texture release path, so textures never leak across catalogs.

## Estado de rendimiento tras la prueba del índice binario (12-10-2026)

Las correcciones aprobadas de caché de imágenes, creación de directorios en worker y el manejo de navegación están en `main` desde [PR #28](https://github.com/VegettoSan/PSVitaAlive/pull/28). Esto **no equivale** a haber eliminado todas las pausas de la interfaz.

En dos sesiones de PS Vita real del VPK **experimental de índice `.fastidx` (no de `main`)** se midieron numerosos costes de `vita2d_load_PNG_file` de aproximadamente 100–473 ms para iconos, hasta 1,41 s para una captura y fotogramas máximos de 3,37/4,48 s. Los iconos recirculan al superar `MAX_APP_TEXTURES = 18`; existe también coste de `diagnostics::log()` sincrónico. Estos registros no aíslan cuánto corresponde al decodificador, GPU, almacenamiento, hilo de trabajo o logging y **no demuestran** que el índice sea la causa de los picos de textura.

**No modificar los límites de GPU ni mover `vita2d` a otro hilo sin ensayo de RAM, permisos GXM, lifetime de texturas y comportamiento en Vita real.** Separar primero la fase de imágenes de la del parse JSON. Resultados completos, versiones comparadas y ruta de recuperación: [estado integrado y prueba negativa](CLIENT_PERFORMANCE_STATUS_20261012.md).

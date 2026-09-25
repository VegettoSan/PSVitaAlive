# Client PSVitaAlive — Native PS Vita client

Native catalog client for PlayStation Vita / PSTV (and Vita3K for testing).

## Identity

| Item | Value |
|------|--------|
| Client Title ID | **PSVAS1178** |
| Updater Title ID | **PSVAUPDT1** (temporary helper bubble) |
| Build system | CMake + VitaSDK |
| Rendering | vita2d (no Dear ImGui) |

## Features (high level)

- Catalogs: **Homebrew**, **Vita Games**, **PSP**, **PS1** (all four can stay cached in RAM after first load)
- Search, Settings (install method, **PSP/PS1 target**, **PSP media** Folder/ISO, **color theme**, **UI font**, **language** System/manual), touch + buttons
- Header **content filters**: Homebrew **G/D Files**; Vita Games & PSP **DLC** (same toggle chip behaviour)
- **Multilanguage UI** (`app0:lang/*.lang`): currently packaged **EN / ES / FR / DE / IT / PT-PT / PT-BR / RU**; missing keys fall back to English; catalog content stays original language
- **News** from repo `news.txt`; optional Discord **Report** on real errors (and dedicated data-request webhook path)
- **Image cache v3** with UI-first on-demand loading, catalog/resource-aware replacement and startup disk cap: app/icon/cover images are normalized to max **128 px**, screenshots to max **256 px**; rapid catalog/detail scrolling suppresses new network image requests, off-screen active image transfers are cancellable, and shared download progress is throttled to 10 Hz; if cache exceeds **200 MiB**, startup trims oldest complete images to about **40 MiB**; see [`../docs/IMAGE_CACHE.md`](../docs/IMAGE_CACHE.md)
- **Data Files / Game Files** indicators on app cards
- Downloads via libcurl (MediaFire CDN/size resolution, **Archive.org edge failover**, GitHub, …) with retry behaviour on slow links and SSL connect errors
- Install pipeline:
  - **VPK** promote (including a nested `.vpk` inside a release ZIP)
  - **ZIP** extract (`extract_path` from catalog or quick-path UI), including **large / >2 GB** archives (libzip + custom `sceIo` source); EOCD/ZIP64 incomplete retries with delay
  - Pre-open ZIP integrity check (EOCD / ZIP64 marker) to fail incomplete downloads early
  - Licensed commercial **Vita PKG via system BGDL**
  - **PSP/PS1 PKG**: LiveArea BGDL **or** Adrenaline unpack (pkg2zip-style → `ux0:pspemu`, Folder or ISO from Settings)
  - **Plugin** links: download → copy to `extract_path` → append `line` to taiHEN `config.txt` under `section` (or skip config if `none`)
- **Essential plugins** (kubridge, fd_fix, libshacccg) checked after theme setup + News; install queue + reboot modal
- Free-space check before download (~2.1× expected size)
- **Job safety (Downloading / Installing):**
  - Keep-awake thread: `sceKernelPowerTick` — disable auto-suspend **and** keep OLED on (no dim / no off)
  - `sceShellUtilLock(PS_BTN)` + `POWEROFF_MENU` so the user cannot leave to LiveArea or open the soft power-off menu mid-job
  - Locks released on Completed / Failed / Cancelled / shutdown
  - Progress UI shows a **LOCKED** banner; toasts if START / SELECT / L-R / other keys are pressed
- Voluntary cancel shows **Download cancelled** (not a false install failure)
- **Automatic self-update** from [GitHub Releases](https://github.com/VegettoSan/PSVitaAlive/releases) via helper **PSVAUPDT1**
- Plugin detection (AutoPlugin2-style parser; prefer **ur0:tai** over ux0); Settings **INFO → SYSTEM** shows NoNpDrm, NoPspEmuDrm, kubridge, fd_fix, libshacccg. Filesystem-backed plugin checks are snapshotted once when Settings opens, so the render loop does not repeatedly read `ur0:tai/config.txt` or probe plugin files.
- Brand logo / loading splash can use monochrome assets tinted by the active theme
- Logs: `session.log`, `install.log`, `archive_nodes.log`, `updater.log`

## Image cache v3

Images are stored under:

```text
ux0:data/psvitaalive/cache/images/v3/
```

The cache is disk-backed and normal browsing remains **on demand**.

### Stable image identity

The catalog parser adds private in-memory metadata that identifies:

```text
catalog + app/game + image role
```

Catalog scopes are:

```text
H   = Homebrew
PV  = PS Vita
PSP = PSP
PS1 = PS1
```

Roles distinguish `icon`, `cover`, `shot0`, `shot1`, etc. The stable resource identity does not depend on the current URL.

A cached filename includes both the stable resource identity and a hash of the current URL:

```text
app_H_<resource-key>_<url-hash>.png
shot_PSP_<resource-key>_<url-hash>.png
```

Resource files are distributed over **256 bucket directories** by the first byte of the resource-key hash.

### URL changes / image replacement

If an app changes only its image URL, the stable resource key remains the same while the URL hash changes. Therefore the client can distinguish the old and new versions before the new file is downloaded.

The replacement flow is:

```text
new URL requested
    ↓
new path not cached
    ↓
download + normalize/validate new image
    ↓
success
    ↓
delete only older cached siblings
with the same resource identity
```

The old version is **not deleted first**. If the new download fails, the cache does not proactively erase the previous resource version.

A changed icon does not purge cover/screenshots; a changed screenshot slot does not purge other slots; one catalog scope cannot purge another.

### Normalization sizes

Image normalization preserves aspect ratio and never enlarges images that are already below the configured limit:

```text
app / icon / cover: 128 px maximum side
screenshots:         256 px maximum side
```

The remote image is downloaded first and then normalized locally, so the 256 px screenshot limit reduces cached disk usage and subsequent decode/texture workload, not the original network transfer size.

Because this finalized v3 design had not yet been released when screenshots were reduced from 512 px to 256 px, no extra cache version or migration was added. A larger development-cache screenshot is rejected by the request-time dimension validation and is regenerated on demand at the current limit.

### Startup disk cap

Global size maintenance runs **only during application startup**, inside `ImageCache::init()`, before the image worker thread is created.

Policy:

```text
cache <= 200 MiB
→ scan/measure only; no global eviction

cache > 200 MiB
→ sort cache payloads oldest → newest
→ delete complete files until approximately <= 40 MiB remain
```

This deliberately keeps about 20% of the configured maximum after a cleanup, leaving roughly 160 MiB for future on-demand browsing before another startup trim is required.

The first size pass does not allocate/sort the full file list when the cache is already below the threshold. The expensive list/sort pass is created only when cleanup is actually necessary.

Deletion always uses whole-file `sceIoRemove()`. Byte sizes are used only for accounting; an image is never truncated to hit an exact target. If a deletion fails, its size is not deducted.

### Startup UI

The existing loading overlay displays localized phases such as:

```text
Checking image cache...
Cleaning old cached images...
Image cache ready
```

Checking progress is based on scanned cache buckets; cleaning progress advances through whole-file cleanup work. The loading screen is redrawn while the synchronous maintenance pass runs.

`LocalizationManager` is initialized before this phase so the messages use the selected/System-resolved language.

### After eviction

Nothing special is required. If a later screen requests an image that survived cleanup, it is reused. If that image was evicted, `isCached()` reports a miss and `request()` downloads it again normally.

There is **no global 200 MiB scan during browsing**. A session can grow past the threshold and will be trimmed on the next application launch.

Full technical design, migration notes, logging and validation checklist: [`../docs/IMAGE_CACHE.md`](../docs/IMAGE_CACHE.md).

## Job safety during download / install / extract

In-app HTTP downloads and ZIP extraction are **process-bound**. If the Vita suspends or the user exits to LiveArea mid-transfer, partial files are common (especially multi-GB Game Files). Incomplete ZIPs typically fail later with missing EOCD / `zip_open` errors.

### Runtime behaviour

Implemented in `InstallController` (`install_controller.cpp`):

1. **`sceShellUtilInitEvents`** once at installer init.
2. On `setState(Downloading|Installing)` → `lockShellDuringJob()`:
   - `SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN`
   - `SCE_SHELL_UTIL_LOCK_TYPE_POWEROFF_MENU`
3. Keep-awake thread (started at init) while `busy()`:
   - `SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND`
   - `SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF`
   - `SCE_KERNEL_POWER_TICK_DISABLE_OLED_DIMMING`
4. On terminal states / `shutdown()` → `unlockShellDuringJob()`.

**Not blocked:** long hardware force power-off. Users must not use it during jobs.

### UI messaging

`FullCatalogScreen` progress overlay shows a red **LOCKED** strip:

- PS button and power menu disabled  
- Screen stays ON — do not force power-off  

Toasts fire for START, SELECT (Settings), L/R catalog switch, and other face/D-Pad buttons while a job is active (only CIRCLE cancel remains intentional).

### Recommendations (device)

See the root [README — Recommended setup](../README.md#recommended-setup-real-ps-vita) for **iTLS-Enso (full)**, DNS `8.8.8.8` / `8.8.4.4`, plugins, and storage notes.

## Network / TLS (libcurl)

Default stack: **VitaSDK libcurl + OpenSSL 1.0.2** (EOL). Certificate verification is disabled on device (no usable system CA store).

### Archive.org resilience

Some Archive storage edges (`dn*.ca.archive.org`) are slow or unreliable on the Vita TLS stack. For fresh installer payloads the client now:

1. fetches `https://archive.org/metadata/<identifier>`;
2. builds direct candidates from `server` / `d1` / `d2`, preferring non-`dn` / non-`.ca` nodes;
3. uses the catalog/link expected size only to decide whether the 16 MiB benchmark threshold is crossed; if no size is available, it falls back to the one-byte Range size probe;
4. for files >= 16 MiB, benchmarks candidates sequentially with 256 KiB Range probes and starts on the fastest measured node;
5. keeps the remaining candidates as failover for TLS, transport and `5xx` failures.

Small payloads use the first responsive direct node without the speed benchmark. Resumes and image/cache requests skip proactive selection. If probing fails, the canonical Archive URL remains the fallback. A dedicated `ux0:data/psvitaalive/logs/archive_nodes.log` is reset each launch and records metadata hosts, candidate URLs, every Range probe, TTFB, IP, redirects, measured B/s + KiB/s + MiB/s, ranking, selected node, each real transfer attempt, failover reason/from/to, effective URL and final outcome. `session.log` remains the general application/network diagnostic log.

SSL defaults (`VERIFYPEER/HOST=0`, clear `CAINFO`/`CAPATH`) are re-applied every attempt.

### Optional mbedTLS build

```bash
cmake .. -DPSVITAALIVE_USE_MBEDTLS_CURL=ON   # requires vdpm mbedtls + curl-mbedtls
```

Default remains OpenSSL. See [docs/NETWORK_TLS.md](../docs/NETWORK_TLS.md).

### Resume / partial downloads

- `CURLOPT_RESUME_FROM_LARGE`, `Content-Range` totals, HTTP 416 “already complete”
- One-shot fallback on `CURLE_RANGE_ERROR` (truncate + full GET)
- Optional `If-Range` when job metadata has a matching validator URL

## Link types in the client

Detail view groups actionable links (△ / touch). Types include:

`Download`, `Data Files`, `Game Files`, `Mod` / `Mod Pack`, `DLC`, `Update` / `Patch`, `PKG`, and informational types (`Mirror`, `Repository`, …).

ZIP-oriented types may set `extract_path` in catalog JSON so extraction skips the path picker.

See the root [README.md](../README.md) for the full link-type matrix.

## Install paths

| Payload | Handler | Destination |
|---------|---------|-------------|
| **VPK** (Vita only) | HomebrewInstaller + Promoter | LiveArea bubble (`ux0:app/<TITLEID>`) — **unchanged** |
| **ISO / CSO / PBP** | PspInstaller | `ux0:pspemu/...` (Adrenaline) |
| Data **ZIP** | ZipExtractor | Catalog `extract_path` or user path |
| **Vita PKG** | BGDL / VitaInstaller (promoter) | LiveArea / system install — **never** Adrenaline unpack |
| **PSP / PS1 official PKG** + Settings **LiveArea** | BGDL + synthetic RIF (PKGj-style) | LiveArea bubble (NoPspEmuDrm recommended) |
| **PSP / PS1 official PKG** + Settings **Adrenaline** | Direct download + **pkg2zip unpack** (`third_party/pkg2zip`) | See **PSP media** below — **no** LiveArea bubble |

### Settings → PSP / PS1 target

- **LiveArea**: PSP/PS1 `.pkg` → system BGDL (bubble on LiveArea), same idea as PKGj when installing to LiveArea.
- **Adrenaline**: PSP/PS1 `.pkg` only → skip BGDL; download file and unpack into `ux0:pspemu` (no LiveArea bubble).
- **Vita Game PKGs** always use the LiveArea/BGDL path, even if Adrenaline is selected. The client probes PKG `content_type` (`psp_pkg_probe_is_psp_psx`) so non-PSP/PSX packages are never sent to pkg2zip unpack.
- **VPK is only for Vita** homebrew/games and always uses the promoter path.

### Settings → PSP media (Adrenaline)

Only applies when **PSP / PS1 target = Adrenaline** and the PKG content type is PSP/PSX.

| Value | Default | Layout |
|-------|---------|--------|
| **Folder** | yes | PSP → `ux0:pspemu/PSP/GAME/<ID>/EBOOT.PBP` (+ KEYS/DOCUMENT when present). PS1 → `ux0:pspemu/PSP/GAME/<ID>/` |
| **ISO** | | PSP → `ux0:pspemu/ISO/<title> [<ID>].iso` (EBOOT→ISO). PS1 still uses GAME folder |

Persisted in `ux0:data/psvitaalive/config.json` as `psp_media_format` (`folder` | `iso`). Folder mode matches PKGj `install_psp_as_pbp`. Some PSP EBOOT.PBP titles need **npdrm_free** inside Adrenaline; ISO mode avoids that for many retail packages.

## Self-update

Automatic path uses helper Title ID **PSVAUPDT1**:

- Client downloads the new VPK to `ux0:data/psvitaalive/update/`
- Installs the updater bubble, launches it, exits
- Updater promotes the new client, relaunches the store, removes itself

Rules of thumb (see `source/update/README.md`):

- Tear down catalog/image workers before handoff.
- Prefer **async** `PromotePkg(sync=0)` + `GetState` poll for PSVAUPDT1; sync promote was observed to hang after returning success on device.
- After promote, prefer soft `scePromoterUtilityExit()`; aggressive PAF unload after promote was linked to hangs.

Manual recovery: if the client does not switch, open **PSVAUPDT1** from LiveArea. Staged VPK path for manual install: `ux0:data/psvitaalive/update/PSVitaAlive.vpk`.

## zRIF and commercial catalogs

Vita commercial catalog JSON does **not** embed zRIF strings (memory). Licenses live in:

```text
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/catalog_psvita_games.zrifidx
```

Cached on device as:

```text
ux0:data/psvitaalive/cache/catalog/catalog_psvita_games.zrifidx
```

Format: `content_id<TAB>zrif` per line. Looked up only when starting a licensed PKG install.

## Build (typical)

```bash
cd "Client PSVitaAlive"
rm -rf build && mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

Requires a working VitaSDK toolchain (`arm-vita-eabi-gcc`, etc.). The build also produces the updater eboot packaged into the client VPK under `app0:updater/`.

## Source layout

| Path | Role |
|------|------|
| `source/main.cpp` | Entry, lifecycle, early localization, cache-maintenance progress, update handoff |
| `source/catalog/` | Catalog download/parse/cache + zRIF index download + internal image-cache identity tagging |
| `source/network/` | HTTP, downloads, MediaFire |
| `source/installer/` | Install/dispatch/promote, BGDL PKG, plugins, keep-awake / shell locks |
| `source/archive/` | ZIP / format detection |
| `source/ui/` | Full catalog UI, image cache v3, startup cache trim, lock messaging, themes |
| `source/update/` | GitHub release check, applyUpdate, launch helper |
| `source/storage/` | Paths and storage helpers |
| `updater/` | Standalone PSVAUPDT1 sources |
| `assets/` | LiveArea, UI images, language packs |

See module READMEs under `source/*/`.

## Runtime data

```text
ux0:data/psvitaalive/
  logs/             session.log, install.log, updater.log
  cache/catalog/    catalog JSON + catalog_psvita_games.zrifidx
  cache/images/v3/  bucketed icon/cover/screenshot cache
  downloads/        job work dirs
  update/           staged self-update VPK
ux0:data/psva_vpk/   shallow promote path (homebrew + self-update)
```

The image cache is allowed to grow during a session. Its global disk-size policy is enforced only on the next application startup; see [`../docs/IMAGE_CACHE.md`](../docs/IMAGE_CACHE.md).

## Scope notes

- Licensed PKG install uses system BGDL + zRIF/RIF helpers; it does not invent DRM bypasses beyond NoPayStation-style license data the user already needs for NPS content.
- LiveArea registration relies on promoter utilities; hardware and Vita3K may differ.
- Image-cache URL-change invalidation assumes catalog maintainers change the URL when the actual image content changes. Same-URL byte replacement is not automatically revalidated.
- Prefer reading current code when docs and behaviour diverge.

## Plugin links

Catalog entries may declare:

```json
{
  "type": "Plugin",
  "name": "Example plugin",
  "url": "https://example.org/plugin.skprx",
  "size": 12345,
  "extract_path": "ur0:tai/",
  "section": "*KERNEL",
  "line": "ur0:tai/plugin.skprx",
  "recommended": false
}
```

| Field | Behaviour |
|-------|-----------|
| `extract_path` | Directory where the binary is written (created if needed) |
| `section` | `*KERNEL` / `*main` / `*ALL` / custom header, or `none` to skip config |
| `line` | Exact text appended at the end of that section (duplicate lines are not re-added) |

**Install All** installs Plugin links **after** VPK / Game Files / Data Files. Plugins already present on disk (and, for taiHEN plugins, already listed in `config.txt`) are skipped; the UI shows an **Installed** badge and a toast if the user presses the link again.

After plugin install(s), a full-screen **Restart required** modal blocks LiveArea exit and underlying touch until the user confirms soft reset (`scePowerRequestColdReset`). Message includes the **hold L at boot** recovery hint.

### Essential plugins prompt

After the first-run color-theme picker and the News modal, the client checks:

| Plugin | Installed when |
|--------|----------------|
| `kubridge.skprx` | File under `ur0:tai/` or `ux0:tai/` **and** line in active `config.txt` |
| `fd_fix.skprx` | File + config line — **skipped if RePatch is active** (RePatch satisfies this need) |
| `repatch*.skprx` | Detected only (not auto-installed); blocks FdFix recommendation when present |
| `libshacccg.suprx` | File under `ur0:data/` only (never written to config) |

Missing items open a large-type modal (**Install plugins** with pulsing border, or **Remind me later**). Install uses Archive.org mirrors configured in the client and the standard Plugin path.

## PSP / PS1 Adrenaline unpack

When Settings → **PSP / PS1 target** is **Adrenaline**:

- PSP/PS1 **PKG** downloads are **not** queued to system BGDL.
- Content is unpacked with an embedded **pkg2zip-style** pipeline (`third_party/pkg2zip/`) into `ux0:pspemu`.
- **PSP media**: **Folder** (default, EBOOT.PBP under GAME) or **ISO**.
- Only packages whose probed content type is PSP/PSX use this path; **Vita game PKGs always use BGDL/promote**, never pspemu unpack.

## Networking / TLS

- libcurl on VitaSDK; OpenSSL 1.0.2-class backend.
- SSL peer verification disabled on device by design.
- Archive.org: metadata-driven edge failover when `dn*` / regional edges fail TLS.
- Recommended: **[iTLS-Enso](https://github.com/SKGleba/iTLS-Enso)** full + DNS **8.8.8.8** / **8.8.4.4**.

## Color themes

Many named palettes (brand **PsVitaAlive** / Neon Lime, **PS Vita**, OLED, Matrix, and dozens of distinct accents).

| Behaviour | Detail |
|-----------|--------|
| First run | Full-screen theme grid **before** News (once; `theme_setup_done` in config) |
| Settings | **Color theme** opens the **same** picker (not a D-pad only cycle) |
| Preview | First **X** / tap applies a **live preview**; second press on the same theme **or** **Save** commits |
| Transition | ~420 ms **cross-fade** of BG, surfaces, borders, text and accent (smoothstep). Startup load is instant |
| Brand art | Full-colour logo / catalog splash only on the original theme; other themes use **monochrome** assets tinted with the accent |

Persisted as `color_theme` in `ux0:data/psvitaalive/config.json`.

## UI font styles

Settings → **UI font** (Left/Right):

| Style | System source (when present) |
|-------|------------------------------|
| Default | `vita2d_load_default_pgf()` |
| Serif | `sa0:data/font/ltn0.pgf` |
| Sans | `sa0:data/font/ltn2.pgf` |
| Serif Bold | `sa0:data/font/ltn4.pgf` |
| Sans Bold | `sa0:data/font/ltn6.pgf` |

- `config.json` key: `ui_font_style` (`default` / `serif` / `sans` / `serif_bold` / `sans_bold`)
- Live preview on change; missing files fall back to Default without crashing
- Loader: `source/ui/ui_font.cpp` (`loadUiFont`)

## Multilanguage (UI)

| Item | Detail |
|------|--------|
| Packaged files | `en.lang`, `es.lang`, `fr.lang`, `de.lang`, `it.lang`, `pt-PT.lang`, `pt-BR.lang`, `ru.lang` → `app0:lang/` in the VPK |
| Settings | **Language**: System / Automatic or manual selection from installed packs |
| Config | `language_mode` (`system`\|`manual`), `language` (stable code such as `en`, `es`, `fr`, ...) |
| Scope | Chrome only (buttons, Settings INFO, overlays, toasts, theme/startup UI). **Catalog JSON text is not translated** |
| Fallback | Missing key → English; unavailable pack → English |
| Startup cache UI | Cache-check/cleanup strings are localized for every currently packaged language before normal catalog loading |

The internal registry contains more Vita languages than are currently packaged. Availability is determined by the presence of the matching `.lang` asset.

Design/current implementation: [`../docs/MULTILANGUAGE.md`](../docs/MULTILANGUAGE.md).

## PSP DLC (Adrenaline ISO)

PSP **DLC** link buttons require **LiveArea** install target **or** Adrenaline **Folder** media. If the user is on Adrenaline **ISO**, the client shows a toast asking to switch to Folder or LiveArea (ISO DLC is not recognised correctly by Adrenaline).

## Related docs

| Doc | Topic |
|-----|--------|
| [`../docs/IMAGE_CACHE.md`](../docs/IMAGE_CACHE.md) | Image cache v3, per-image replacement, normalization limits, startup disk cap/progress |
| [source/ui/README.md](source/ui/README.md) | Themes, fonts, image cache integration, LOCKED UI, modals |
| [source/catalog/README.md](source/catalog/README.md) | Catalog parser/cache, image identity handoff, zRIF sidecar |
| [source/installer/README.md](source/installer/README.md) | Install paths, plugins, shell locks |
| [docs/NETWORK_TLS.md](../docs/NETWORK_TLS.md) | libcurl / archive.org failover |
| [docs/MULTILANGUAGE.md](../docs/MULTILANGUAGE.md) | Localization architecture |
| Root [README.md](../README.md) | Catalogs, device recommendations |

### Navigation-aware image scheduling

Catalog image work is paused while vertical navigation is physically held, while scroll animation is moving, and for a short post-navigation grace period. App/icon GPU textures use a bounded 18-entry LRU so reversing direction can reuse recent 128 px textures instead of immediately freeing and decoding them again; screenshots remain aggressively released with their existing 6-texture limit. See `../docs/IMAGE_CACHE.md` for the full policy.

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
- Search, Settings (install method, PSP target, **color theme** palettes with live preview), touch + buttons
- **News** from repo `news.txt`; optional Discord **Report** on real errors (and dedicated data-request webhook path)
- Image cache with on-demand loading; **Data Files / Game Files** indicators on app cards
- Downloads via libcurl (MediaFire CDN/size resolution, Archive.org, GitHub, …) with retry behaviour on slow links
- Install pipeline:
  - **VPK** promote (including a nested `.vpk` inside a release ZIP)
  - **ZIP** extract (`extract_path` from catalog or quick-path UI), including **large / >2 GB** archives (libzip + custom `sceIo` source)
  - Pre-open ZIP integrity check (EOCD / ZIP64 marker) to fail incomplete downloads early
  - Licensed commercial **PKG via system BGDL** (verified on real Vita)
- Free-space check before download (~2.1× expected size)
- **Job safety (Downloading / Installing):**
  - Keep-awake thread: `sceKernelPowerTick` — disable auto-suspend **and** keep OLED on (no dim / no off)
  - `sceShellUtilLock(PS_BTN)` + `POWEROFF_MENU` so the user cannot leave to LiveArea or open the soft power-off menu mid-job
  - Locks released on Completed / Failed / Cancelled / shutdown
  - Progress UI shows a **LOCKED** banner; toasts if START / SELECT / L-R / other keys are pressed
- Voluntary cancel shows **Download cancelled** (not a false install failure)
- **Automatic self-update** from [GitHub Releases](https://github.com/VegettoSan/PSVitaAlive/releases) via helper **PSVAUPDT1** — open the client; if a new version exists it can download and install without a PC
- Plugin detection (AutoPlugin2-style parser; prefer **ur0:tai** over ux0)
- Logs: `session.log`, `install.log`, `updater.log`

## Job safety during download / install / extract

In-app HTTP downloads and ZIP extraction are **process-bound**. If the Vita suspends or the user exits to LiveArea mid-transfer, partial files are common (especially multi‑GB Game Files). Incomplete ZIPs typically fail later with missing EOCD / `zip_open` errors.

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

Many storage edges (`dn*.ca.archive.org`) fail TLS handshakes on this stack. On `CURLE_SSL_CONNECT_ERROR` for `archive.org/download/...` URLs the client:

1. Fetches `https://archive.org/metadata/<identifier>`
2. Retries on alternate hosts from metadata (`server` / `d1` / `d2`), preferring non-`dn` / non-`.ca` nodes (`ia*.us.archive.org`, etc.)

Logs: `archive failover built N alternate URL(s)` and `archive failover switch -> https://...`.

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
| **VPK** (default / LiveArea target) | HomebrewInstaller + Promoter | LiveArea bubble (`ux0:app/<TITLEID>`) |
| **VPK** with ISO/CSO/PBP inside + **Adrenaline** target | Extract → PspInstaller | `ux0:pspemu/ISO` or `ux0:pspemu/PSP/GAME` — **no** LiveArea bubble |
| **ISO / CSO / PBP** | PspInstaller | `ux0:pspemu/...` (Adrenaline) |
| Data **ZIP** | ZipExtractor | Catalog `extract_path` or user path |
| **Vita PKG** | BGDL (system queue) | LiveArea / system install |
| **PSP / PS1 official PKG** | BGDL | Always a **LiveArea** system entry (not Adrenaline-only). Prefer ISO/CSO/PBP or a VPK that embeds them when Settings → PSP target is **Adrenaline**. |

### Settings → PSP / PS1 target

- **Adrenaline** (default): media goes to `ux0:pspemu` only. VPK packages that embed ISO/CSO/PBP are installed that way and **do not** create a LiveArea bubble. Official `.pkg` links still use BGDL (system limitation).
- **LiveArea**: VPK promote and PKG BGDL as before (bubbles; NoPspEmuDrm recommended for PSP bubbles).

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
| `source/main.cpp` | Entry, lifecycle, update handoff |
| `source/catalog/` | Catalog download/parse/cache + zRIF index download |
| `source/network/` | HTTP, downloads, MediaFire |
| `source/installer/` | Install/dispatch/promote, BGDL PKG, plugins, keep-awake / shell locks |
| `source/archive/` | ZIP / format detection |
| `source/ui/` | Full catalog UI, image cache, lock messaging, themes |
| `source/update/` | GitHub release check, applyUpdate, launch helper |
| `source/storage/` | Paths and storage helpers |
| `updater/` | Standalone PSVAUPDT1 sources |
| `assets/` | LiveArea, UI images |

See module READMEs under `source/*/`.

## Runtime data

```text
ux0:data/psvitaalive/
  logs/           session.log, install.log, updater.log
  cache/catalog/  catalog JSON + catalog_psvita_games.zrifidx
  cache/images/   icon/screenshot cache
  downloads/      job work dirs
  update/         staged self-update VPK
ux0:data/psva_vpk/   shallow promote path (homebrew + self-update)
```

## Scope notes

- Licensed PKG install uses system BGDL + zRIF/RIF helpers; it does not invent DRM bypasses beyond NoPayStation-style license data the user already needs for NPS content.
- LiveArea registration relies on promoter utilities; hardware and Vita3K may differ.
- Prefer reading current code when docs and behaviour diverge.

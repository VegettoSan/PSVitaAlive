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

## Link types in the client

Detail view groups actionable links (△ / touch). Types include:

`Download`, `Data Files`, `Game Files`, `Mod` / `Mod Pack`, `DLC`, `Update` / `Patch`, `PKG`, and informational types (`Mirror`, `Repository`, …).

ZIP-oriented types may set `extract_path` in catalog JSON so extraction skips the path picker.

See the root [README.md](../README.md) for the full link-type matrix.

## Install paths

| Content | Path | Notes |
|---------|------|--------|
| Homebrew **VPK** | Extract + `scePromoterUtility` | Shallow dir `ux0:data/psva_vpk` |
| Data **ZIP** | ZipExtractor | User picks folder (quick paths for data/app/repatch/PSP) |
| **Vita / PSP / PS1 PKG** | **BGDL** (system queue) | Needs zRIF or synthetic RIF; progress in **LiveArea notifications** |

### Commercial PKG (verified)

On a real PS Vita with CFW + **NoNpDrm** (and **NoPspEmuDrm** when needed):

1. User selects a Download / DLC / Update link that points to a `.pkg`
2. Client resolves license from `catalog_psvita_games.zrifidx` using link **`content_id`** (preferred) or title-id fallback
3. Writes RIF and enqueues BGDL; UI shows **Queued: &lt;app name&gt;**
4. User completes install from LiveArea system notifications

Do not promote a raw retail `.pkg` without a matching license — that path fails and only wastes bandwidth.

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

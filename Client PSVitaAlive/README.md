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
- Search, settings, touch + buttons
- Image cache with on-demand loading
- Downloads via libcurl (MediaFire resolution supported where implemented)
- Install pipeline: VPK promote, ZIP extract, licensed PKG via **BGDL**
- Self-update against **GitHub Releases** via helper **PSVAUPDT1**
- Plugin detection (AutoPlugin2-style parser; prefer **ur0:tai** over ux0)
- Logs: `session.log`, `install.log`, `updater.log`

## Self-update architecture

A running app **must not** promote itself. Flow:

```text
PSVAS1178 (client)
  │  check GitHub Releases /latest
  │  download PSVitaAlive.vpk → ux0:data/psvitaalive/update/
  │  extract staged package → ux0:data/psva_vpk (homebrew-aligned path)
  │  install helper bubble PSVAUPDT1 from app0:updater/
  │  hand off (see below)
  ▼
PSVAUPDT1 (updater)
  │  PromotePkg async on staged client package
  │  launch client
  ▼
PSVAS1178 (updated)
  │  optional: delete PSVAUPDT1 bubble
```

### Critical handoff rule (client → updater)

**Updater → client works** with a minimal sequence. **Client → updater** must use the **same** pattern:

```text
sceAppMgrLaunchAppByUri(0xFFFFF, "psgm:play?titleid=PSVAUPDT1");
sceKernelExitProcess(0);
```

Do **not** call `sceAppMgrDestroyOtherApp()` before launching the updater from the client — that path was associated with freezes/crashes on real hardware. `DestroyOtherApp` remains appropriate **inside the updater** before promoting packages, not before launching the other title.

Also:

- Run version check / handoff launch on the **main thread** (not a worker).
- Stop catalog/image workers before handoff.
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

Format: `pkg_url<TAB>zrif` per line. Looked up only when starting a licensed PKG install.

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
| `source/installer/` | Install/dispatch/promote, BGDL PKG, plugins |
| `source/archive/` | ZIP / format detection |
| `source/ui/` | Full catalog UI, image cache |
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

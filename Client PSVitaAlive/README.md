# Client PSVitaAlive — Native PS Vita client

Native catalog client for PlayStation Vita / PSTV (and Vita3K for testing).

## Identity

| Item | Value |
|------|--------|
| Title ID | **PSVAS1178** |
| Build system | CMake + VitaSDK |
| Rendering | vita2d (no Dear ImGui) |

## Features (high level)

- Catalogs: **Homebrew**, **Vita Games**, **PSP**, **PS1**
- Search, settings, touch + buttons
- Image cache with on-demand loading
- Downloads via libcurl (MediaFire resolution supported where implemented)
- Install pipeline: VPK / ZIP extraction / PKG-related paths
- Self-update check against **GitHub Releases** (see `source/update/`)
- Plugin detection (prefer **ur0** tai config over ux0)
- Logs: `ux0:data/psvitaalive/logs/session.log`, `install.log`

## Build (typical)

```bash
cd "Client PSVitaAlive"
rm -rf build && mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

Requires a working VitaSDK toolchain (`arm-vita-eabi-gcc`, etc.).

## Source layout

| Path | Role |
|------|------|
| `source/main.cpp` | Entry, lifecycle |
| `source/catalog/` | Catalog download/parse/cache |
| `source/network/` | HTTP, downloads, MediaFire |
| `source/installer/` | Install/dispatch/promote |
| `source/archive/` | ZIP / format detection |
| `source/ui/` | Full catalog UI, image cache |
| `source/update/` | GitHub release update checker |
| `source/storage/` | Paths and storage helpers |
| `assets/` | LiveArea, UI images |

See module READMEs under `source/*/`.

## Runtime data

```text
ux0:data/psvitaalive/
  logs/
  downloads/
  …
```

## Scope notes

- Does not implement DRM/RIF piracy bypasses.
- LiveArea registration relies on promoter utilities; hardware and Vita3K may differ.
- Prefer reading current code when docs and behaviour diverge.

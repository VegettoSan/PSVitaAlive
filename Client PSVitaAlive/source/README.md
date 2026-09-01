# `Client PSVitaAlive/source/` — Client sources

Native C++ sources for the PS Vita client.

## Modules

| Directory | Responsibility |
|-----------|----------------|
| `catalog/` | Fetch, parse, cache multi-catalog JSON; download Vita **zRIF** sidecar |
| `network/` | libcurl HTTP, download manager, MediaFire resolver, curl lifecycle |
| `installer/` | Install controller, dispatcher, VPK/BGDL PKG/PSP helpers, plugins, LiveArea refresh, **keep-awake + shell locks** during jobs |
| `archive/` | Format detection, ZIP extract |
| `ui/` | Full-catalog UI, image cache, progress **LOCKED** banner / toasts, color themes |
| `update/` | GitHub Releases check, stage VPK, install **PSVAUPDT1**, main-thread handoff (user opens client → auto update when a new release exists) |
| `storage/` | Storage paths and helpers |
| `main.cpp` | Application entry, startup order, update handoff |
| `diagnostic_logger.cpp` | Session logging |

## Startup order (typical)

1. Init diagnostics, AppUtil/IME, libcurl process-global
2. Optional **plugin detection** (config flag)
3. Installer init (`sceShellUtilInitEvents`, start keep-awake thread)
4. UI init
5. Optional **update check** on main thread (config flag)
   - If update applied: install helper → **updater-style** `LaunchAppByUri` + `ExitProcess`
6. Catalog manager + image cache workers
7. Preload catalogs / image warmup choice

## Design rules

- UI does not call promoter or write packages directly; it talks to controller layers.
- Network code must init/cleanup libcurl carefully (avoid double global cleanup crashes).
- Optional features (plugin scan, update check) respect settings / config flags when present.
- Self-update handoff must mirror `updater/main.c` `launchClientAndExit` (no `DestroyOtherApp` before launch from the client).
- In-app download/extract jobs must unlock shell locks on every terminal path (success, fail, cancel, shutdown).

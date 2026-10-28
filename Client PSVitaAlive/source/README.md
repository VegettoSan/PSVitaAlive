# `Client PSVitaAlive/source/` — Client sources

Native C++ sources for the PS Vita client.

## Modules

| Directory | Responsibility |
|-----------|----------------|
| `catalog/` | Fetch, parse, cache multi-catalog JSON; download Vita **zRIF** sidecar |
| `network/` | libcurl HTTP, download manager, MediaFire resolver, curl lifecycle |
| `installer/` | Install controller, dispatcher, VPK/BGDL PKG/PSP helpers, plugins, LiveArea refresh, **keep-awake + shell locks** during jobs |
| `archive/` | Format detection, ZIP extract |
| `ui/` | Full-catalog UI, embedded themed QWERTY keyboard (search and custom ZIP paths), image cache, progress **LOCKED** banner / toasts, color themes |
| `update/` | GitHub Releases check, stage VPK, install **PSVAUPDT1**, main-thread handoff (user opens client → auto update when a new release exists) |
| `storage/` | Storage paths and helpers |
| `main.cpp` | Application entry, startup order, update handoff |
| `diagnostic_logger.cpp` | Session logging |

## Startup order (typical)

1. Init diagnostics, AppUtil (for system services; **no native IME initialization**), libcurl process-global
2. Optional **plugin detection** (config flag)
3. Installer init (`sceShellUtilInitEvents`, start keep-awake thread)
4. UI init
5. Optional **update check** on main thread (config flag)
   - If update applied: install helper → **updater-style** `LaunchAppByUri` + `ExitProcess`
6. Catalog manager + image cache workers
7. Load the Homebrew catalog, then fetch icon/cover/screenshots on demand through the bounded image cache
8. **Theme setup** (first run only, if `themeSetupDone` is false)
9. **News** modal (when a new `news.txt` id is available)
10. **Essential plugins** prompt (kubridge / fd_fix / libshacccg) if any are missing

## Design rules

- UI does not call promoter or write packages directly; it talks to controller layers.
- Text input uses the in-app `SearchKeyboard` (`ui/search_keyboard.cpp`), not Sony IME. The catalog search callback and ZIP path picker share this UI component. `SceCommonDialog_stub` remains a link requirement of `vita2d`, not a client IME call.
- Network code must init/cleanup libcurl carefully (avoid double global cleanup crashes).
- Optional features (plugin scan, update check) respect settings / config flags when present.
- Self-update handoff must mirror `updater/main.c` `launchClientAndExit` (no `DestroyOtherApp` before launch from the client).
- In-app download/extract jobs must unlock shell locks on every terminal path (success, fail, cancel, shutdown).


## 02.17 responsibilities and performance rules

- `ui/full_catalog_screen.cpp`: catalog cards omit `versionDate` but the detail view retains it. An inactive filter renders the localized **Filters** label; an active filter renders its localized option with marquee. Display uses the existing card width and clips.
- `ui/installed_game_index.hpp`: retains the mount for detected PSP/PS1 ISO/CSO/PBP IDs on available `ux0`, `uma0`, `imc0` and `xmc0`; the UI reports that mount without scanning every frame.
- `installer/psp_installer.cpp`, `installer/install_dispatcher.cpp`, `installer/install_controller.cpp`: propagate destination paths for PSP/PS1 progress and final confirmation. The path is not copied with every file-progress tick.
- `network/error_reporter.cpp`: manual Problem/Suggestion submissions use Discord forums; automated diagnostics keep their independent webhook and error-log path.
- Release metadata comes from `../CMakeLists.txt`; the GitHub release is made from the exact successful CI VPK by `.github/workflows/publish-client-release.yml`, not from manually edited catalog files.

[02.17 implementation reference](../../docs/RELEASE_02_17_TECHNICAL_20261028.md).

## Localization

`localization/` — `LocalizationManager`, `TextId`, loads `app0:lang/*.lang`.  
`ui/ui_font.cpp` — system PGF font styles.  
See [docs/MULTILANGUAGE.md](../../docs/MULTILANGUAGE.md). Keyboard decision, hardware crash history, UI/language contract and regression tests: [docs/KEYBOARD_NATIVE_IME_REPLACEMENT_20261020.md](../../docs/KEYBOARD_NATIVE_IME_REPLACEMENT_20261020.md).

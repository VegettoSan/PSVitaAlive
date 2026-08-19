# `source/` — Client modules

Native C++ sources for the PS Vita client.

## Modules

| Directory | Responsibility |
|-----------|----------------|
| `catalog/` | Fetch, parse, cache multi-catalog JSON |
| `network/` | libcurl HTTP, download manager, MediaFire resolver, curl lifecycle |
| `installer/` | Install controller, dispatcher, VPK/PKG/PSP helpers, plugins, LiveArea refresh |
| `archive/` | Format detection, ZIP extract |
| `ui/` | Full-catalog UI, image cache |
| `update/` | GitHub Releases self-update check / startup flow |
| `storage/` | Storage paths and helpers |
| `main.cpp` | Application entry and wiring |
| `diagnostic_logger.cpp` | Session logging |

## Design rules

- UI does not call promoter or write packages directly; it talks to controller layers.
- Network code must init/cleanup libcurl carefully (avoid double global cleanup crashes).
- Optional features (plugin scan, update check) respect settings / config flags when present.

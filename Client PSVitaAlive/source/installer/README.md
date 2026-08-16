# `source/installer/` — Client-side installation

These modules turn downloaded local payloads into installed content on the Vita.

## Components

| File | Responsibility |
|---|---|
| `install_controller.cpp` | Orchestrates download → install and exposes installation status to the UI. |
| `install_dispatcher.cpp` | Selects the installer according to detected format. |
| `homebrew_installer.cpp` | Handles VPK Homebrew extraction, package preparation, promotion and post-install verification. |
| `vita_installer.cpp` | Handles Vita PKG staging/promotion without implementing DRM/RIF bypass. |
| `fake_package_builder.cpp` | Builds the minimum metadata needed by the Homebrew promotion flow. |

## Result flow

After success or failure, the controller keeps an explicit result state instead of immediately returning to idle:

1. `Completed` or `Failed` exposes a message, install path, Title ID and LiveArea verification result.
2. The UI displays a large success or error panel.
3. The user can acknowledge the result or the result can expire after the configured timeout.

For a promoted VPK, `liveAreaOk` is true only when the expected application tree contains at least:

```text
ux0:app/<TITLE_ID>/
ux0:app/<TITLE_ID>/sce_sys/param.sfo
ux0:app/<TITLE_ID>/sce_sys/icon0.png
```

The LiveArea resources packaged by PSVitaAlive are documented separately under `assets/sce_sys/README.md`.

## Logs

```text
ux0:data/psvitaalive/logs/session.log
ux0:data/psvitaalive/logs/install.log
```

The session/install logs are reset when the application starts according to the current implementation.

## Security and scope

The installer must not:

- bypass DRM;
- generate fake licenses/RIFs for protected content;
- load entire large packages into RAM when streaming/staging is possible;
- extract ZIP paths without traversal checks.

ZIP extraction must reject absolute paths, `..` traversal and any final path that escapes the intended destination.

## Not currently implemented here

The current installer documentation does not claim full PBP/ISO/CSO support. BGDL/system-background download is also outside this module's current responsibility.

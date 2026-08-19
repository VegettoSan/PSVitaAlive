# `source/installer/` — Installation pipeline

Turns downloaded payloads into installed content on the device.

## Components

| File | Responsibility |
|------|----------------|
| `install_controller.cpp` | Orchestrates download → install; status for UI |
| `install_dispatcher.cpp` | Picks installer by detected format |
| `homebrew_installer.cpp` | VPK extract, head/package prep, promote, verify tree |
| `vita_installer.cpp` | Vita PKG staging/promotion (no DRM bypass) |
| `psp_installer.cpp` | PSP-oriented install paths (e.g. Adrenaline) |
| `fake_package_builder.cpp` | Minimal package metadata for homebrew promote |
| `plugin_detector.cpp` | Detect taiHEN plugins / config (**prefer ur0**, then ux0) |
| `refresh_manager.cpp` | LiveArea refresh helpers (fallback / VitaShell-style patterns) |
| `bgdl_client.cpp` | Optional BGDL hooks when ShellSvc exports exist |
| `app_settings.cpp` | Install-related settings |
| `license_helper.cpp` | License-related helpers without fake RIF generation for piracy |

## VPK / LiveArea

Successful promote should yield an app tree such as:

```text
ux0:app/<TITLE_ID>/
ux0:app/<TITLE_ID>/sce_sys/param.sfo
```

`liveAreaOk` reflects whether verification sees the expected tree. On some environments (e.g. emulators), promoter success and bubble visibility can diverge; fallback copy + optional refresh paths exist for recovery.

## Logs

```text
ux0:data/psvitaalive/logs/session.log
ux0:data/psvitaalive/logs/install.log
```

## Security

Must not:

- Bypass DRM or mint fake licenses for protected content as a feature
- Extract ZIP entries with path traversal (`..`, absolute paths)
- Load entire huge packages into RAM when streaming is possible

# `source/installer/` — Install pipeline

Install and LiveArea-related operations for the native client.

## Responsibilities

- Queue and drive download → detect format → install
- **Homebrew VPK**: extract + promoter (async `PromotePkg` + poll), shallow path `ux0:data/psva_vpk`
- **ZIP data**: extract to user-chosen path (quick paths include `ux0:data/`, `ux0:app/`, `ux0:repatch/`, PSP/PS1 folders)
- **Licensed Vita PKG**: system **BGDL** + RIF from zRIF (NoPayStation-style), separate from homebrew promote
- **PSP/PS1 PKG**: synthetic RIF helpers where content ID is known
- Plugin detection (AutoPlugin2-style `tai/config.txt` parse; prefer **ur0** then ux0)
- Settings persistence for install method / PSP path preferences
- Optional LiveArea refresh helpers for edge cases where promote succeeds but the bubble is not visible

## Main components

| File | Role |
|------|------|
| `install_controller.cpp` | Public API used by UI |
| `install_dispatcher.cpp` | Format routing |
| `homebrew_installer.cpp` | VPK promote path |
| `pkg_bgdl_installer.cpp` | BGDL package + license enqueue |
| `license_helper.cpp` | zRIF decode / RIF write / **zRIF disk index lookup** |
| `plugin_detector.cpp` | Scan `ur0:tai` / `ux0:tai` config for NoNpDrm, NoPspEmuDrm, … |
| `bgdl_client.cpp` | Low-level BGDL task helpers |
| `vita_installer.cpp` / `psp_installer.cpp` | Platform-specific helpers |
| `fake_package_builder.cpp` | head.bin / package scaffolding |
| `refresh_manager.cpp` | LiveArea refresh helpers |
| `app_settings.cpp` | Client settings (including feature toggles) |

## zRIF lookup

Catalog items do not carry zRIF in RAM. At PKG install:

1. Use link zRIF if present (legacy)
2. Else `LicenseHelper::lookupZrifForUrl(url)` on `catalog_psvita_games.zrifidx` (and legacy sidecar names)

## Homebrew VPK notes

- Prefer the same shallow promote directory used by successful device tests (`ux0:data/psva_vpk`)
- Clean residual job/pkg directories after success or failure when possible
- Success and bubble visibility can diverge; fallback copy + optional refresh paths exist for recovery

## Logs

```text
ux0:data/psvitaalive/logs/session.log
ux0:data/psvitaalive/logs/install.log
```

## Security

Must not:

- Bypass DRM beyond supplying NPS-style license data the pipeline already expects
- Extract ZIP entries with path traversal (`..`, absolute paths)
- Load entire huge packages into RAM when streaming is possible

## BGDL (system download manager)

Licensed Vita/PSP PKG installs use **ShellSvc IPMI** the same way as **PKGj**:

1. Eboot must be built with **`vita_create_self(... UNSAFE)`** (required to reach `SceShellSvc` exports).
2. Link **`SceShellSvc_stub`**, **`SceVshBridge_stub`**, **`taihen_stub`**.
3. Load `vs0:sys/external/libshellsvc.suprx`, resolve NIDs `0x4E255C31` / `0xB282B430` under library `0xF4E34EDB`.
4. Write a real **RIF** under `ux0:bgdl/` (from zRIF or synthetic PSP RIF), then enqueue URL + type (`0x16` game, `0x17` DLC, `0x00` PSP).

If logs show `ShellSvc exports unavailable` (`0x90010002`), the build is almost always missing **UNSAFE** or taiHEN is inactive. Vita3K does not implement this path.

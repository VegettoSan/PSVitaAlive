# Background update notifier (planned)

Scaffold for a **taiHEN user plugin** that notifies when a newer **PS Vita Alive Store**
client build is available on GitHub Releases. Implementation is intentionally deferred.

This is **not** a full VitaDB-style homebrew scanner. It only watches **this client**
(Title ID `PSVAS1178`).

## Goals

| Item | Decision |
|------|----------|
| Scope | Self-update of PSVitaAlive only |
| Default | **Enabled** after first client install |
| User control | Toggle in client **Settings** (can disable) |
| When | On shell boot / plugin load, then every **6 hours** |
| Version source | `ux0:app/PSVAS1178/sce_sys/param.sfo` (`APP_VER` / version field) |
| Remote check | Same source as the client: GitHub `releases/latest` for `VegettoSan/PSVitaAlive` |
| Compare logic | Same rules as client `UpdateChecker` (tag / version normalization) |
| Action | System notification only — **does not** install the VPK |
| Install path | User opens the client → existing update flow + `PSVAUPDT1` |

## Why not copy VitaDB’s daemon

- VitaDB Downloader is **GPL**; copying the daemon would force license implications.
- Their daemon scans many installed homebrews and catalog hashes; we only need one Title ID.
- Prefer a **small original** `.suprx` plus shared version-compare ideas from our client.

## Plugin identity

| Field | Value |
|-------|--------|
| Module file | `psva_update.suprx` |
| On-device path | `ux0:data/psvitaalive/psva_update.suprx` |
| Packaged path (VPK) | `app0:plugins/psva_update.suprx` |
| taiHEN section | `*main` only (**never** under `*KERNEL`) |
| Config line | `ux0:data/psvitaalive/psva_update.suprx` |

## `config.txt` rules (do not break taiHEN)

1. Prefer active config: `ur0:tai/config.txt`, else `ux0:tai/config.txt`.
2. Only **append or uncomment** one line under `*main`.
3. Never place a `.suprx` under `*KERNEL` (kernel is for `.skprx` only).
4. Never rewrite unrelated lines; write via temp file + rename.
5. Disable = comment the line with `#` and set client flag off.
6. If the line already exists, do not duplicate it.

Example safe fragment:

```text
*main
ur0:tai/henkaku.suprx
ux0:data/psvitaalive/psva_update.suprx
```

## Client packaging & first-run install (planned)

```text
Build:   plugin_update/ → psva_update.suprx
VPK:     app0:plugins/psva_update.suprx
Runtime: copy → ux0:data/psvitaalive/psva_update.suprx
         ensure *main line in tai config
         config.json: background_update_notify = true (default)
User:    reboot once so taiHEN loads the plugin
```

On client update, overwrite the `.suprx` in `ux0:data/...` if the packaged one is newer.

## Runtime behaviour (planned)

```text
module_start
  → if background_update_notify is false → exit
  → read APP_VER from param.sfo of PSVAS1178
  → GET releases/latest
  → if remote > local and not already notified for that remote version
       → system notification
       → save last_notified_version
  → sleep / reschedule ~6 hours
```

Supporting files under `ux0:data/psvitaalive/`:

| File | Role |
|------|------|
| `config.json` | `background_update_notify` (bool, default true) |
| `last_notified_version` | Anti-spam per remote version |
| `psva_update.suprx` | Installed plugin binary |

## Implementation order (when resumed)

1. Shared helper: read version from `param.sfo` of `PSVAS1178`.
2. Prove system notification from the **main app** on real hardware.
3. Minimal `.suprx`: network check + notify + 6 h loop.
4. Client: copy plugin from `app0:plugins/`, safe `*main` edit, Settings toggle.
5. Wire CMake so the plugin is built and embedded into the VPK.

## This folder

| Path | Status |
|------|--------|
| `README.md` | This document |
| `src/` | Placeholder for plugin sources |
| `exports/` | Placeholder for vita module YAML / exports |

No production plugin binary is built from this tree yet.

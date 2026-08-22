# `source/installer/` — Install pipeline

Install and LiveArea-related operations for the native client.

## Status (real hardware)

**Verified on PS Vita:** licensed commercial **PKG** installs for **Vita Games**, **PSP**, and **PS1** catalogs work via the system download manager (**BGDL**), when a matching license (zRIF / synthetic RIF) can be resolved. Progress appears in LiveArea notifications; the client queues the job and shows a clear “Queued” result with the **app name** (not the CDN hash filename).

Homebrew **VPK** promote and **ZIP** extract remain separate paths and are unchanged by the PKG/BGDL pipeline.

## Responsibilities

- Queue and drive download → detect format → install
- **Homebrew VPK**: extract + promoter (async `PromotePkg` + poll), shallow path `ux0:data/psva_vpk`
- **ZIP data**: extract to user-chosen path (quick paths include `ux0:data/`, `ux0:app/`, `ux0:repatch/`, PSP/PS1 folders)
- **Licensed commercial PKG** (Vita / PSP / PS1): system **BGDL** + RIF, separate from homebrew promote
- Plugin detection (AutoPlugin2-style `tai/config.txt` parse; prefer **ur0** then ux0)
- Settings persistence for install method / PSP path preferences
- Optional LiveArea refresh helpers for edge cases where promote succeeds but the bubble is not visible

## Main components

| File | Role |
|------|------|
| `install_controller.cpp` | Public API used by UI; prefers BGDL for any `.pkg` when available |
| `install_dispatcher.cpp` | Format routing (VPK / ZIP / PKG direct — direct retail PKG is not used when license is required) |
| `homebrew_installer.cpp` | VPK promote path |
| `pkg_bgdl_installer.cpp` | BGDL package + license enqueue |
| `license_helper.cpp` | zRIF decode / RIF write / **disk index lookup** |
| `plugin_detector.cpp` | Scan `ur0:tai` / `ux0:tai` for NoNpDrm, NoPspEmuDrm, … |
| `bgdl_client.cpp` | ShellSvc IPMI download class (PKGj-aligned) |
| `vita_installer.cpp` / `psp_installer.cpp` | Platform-specific helpers |
| `fake_package_builder.cpp` | head.bin / package scaffolding |
| `refresh_manager.cpp` | LiveArea refresh helpers |
| `app_settings.cpp` | Client settings (including feature toggles) |

## Commercial PKG flow (BGDL)

```text
User taps Download on a PKG link
        │
        ▼
InstallController (method Auto / BGDL / Direct)
        │  probes BgdlClient (taiHEN + SceShellSvc exports; needs UNSAFE eboot)
        ▼
PkgBgdlInstaller
        │  resolve license (see below)
        │  write RIF → ux0:bgdl/temp.dat
        ▼
BgdlClient::enqueue(title=app name, url, rif, type)
        │
        ▼
System download manager (LiveArea notifications)
        │  downloads + installs with NoNpDrm / NoPspEmuDrm as configured
        ▼
Game / content appears on LiveArea when finished
```

### Requirements on device

| Requirement | Why |
|-------------|-----|
| Real PS Vita (or compatible CFW stack) | BGDL uses SceShellSvc via **taiHEN** |
| Client eboot built with **UNSAFE** | Same as PKGj — required to resolve ShellSvc IPMI exports |
| **NoNpDrm** (and **NoPspEmuDrm** for PSP LiveArea bubbles) | Listed + file present under `ur0:tai` (preferred) or `ux0:tai` |
| Network | PKG URLs are official CDN / NPS-style hosts |

`LoadStartModule(libshellsvc)` may return `0x8002D013` (already loaded); that is normal if SceShellSvc is already resident.

### License (zRIF) resolution order

Licenses are **not** kept in catalog RAM. At install time only:

1. **Link `zrif` field** (legacy / rare)
2. **`content_id` on the link** → exact match in `catalog_psvita_games.zrifidx`  
   (`content_id<TAB>zrif` per line)
3. **Fallback:** Title ID derived from URL / path (e.g. `PCSB00040` from `…/PCSB00040_00/…`) → first index key containing `-TITLEID_`
4. **PSP/PS1:** synthetic RIF from content ID when applicable (PKGj-style), not the Vita zRIF dictionary

**Prefer `content_id` on every PKG / DLC / Update link.** Each region and each product (base, DLC, update) has its own content ID and therefore its own license. Title-ID fallback is only a safety net when a link omits `content_id`; if several products share a Title ID, the first index hit may not be the intended package.

Example index line:

```text
EP0001-PCSB00040_00-ASPHALTINJECTION	KO5ifR1dQ+e7BsBMdQI7Amx/cICHo0+Ip5+Xq3OIp78f…
```

- Link with `content_id=EP0001-PCSB00040_00-ASPHALTINJECTION` → exact zRIF  
- Link without `content_id` but URL contains `PCSB00040` → title-id fallback may still find it  

### UI behaviour

- Immediate status: **Preparing license and system download…**
- On success: **Queued: &lt;app name&gt; — open LiveArea notifications…**
- BGDL notification title uses the **catalog app name**, not the CDN hash filename
- **No silent direct promote** of a raw retail `.pkg` without license (that path fails with `0x80010014` and only wastes a large download)

### BGDL task types

`PkgBgdlInstaller::typeFromLinkType` maps catalog `link.type` roughly to:

| Link type (examples) | BGDL type |
|----------------------|-----------|
| Download / game | Game |
| DLC / addcont | AddCont |
| Update / patch | Game |
| PSP / PS1 related | Psp |

## Homebrew VPK notes

- Prefer the same shallow promote directory used by successful device tests (`ux0:data/psva_vpk`)
- Clean residual job/pkg directories after success or failure when possible
- Success and bubble visibility can diverge; fallback copy + optional refresh paths exist for recovery

## Logs

```text
ux0:data/psvitaalive/logs/session.log
ux0:data/psvitaalive/logs/install.log
```

Useful tags: `[BGDL]`, `[PkgBgdl]`, `[LicenseHelper]`, `[Installer]`.

## Security

Must not:

- Bypass DRM beyond supplying NPS-style license data the pipeline already expects
- Extract ZIP entries with path traversal (`..`, absolute paths)
- Load entire huge packages into RAM when streaming is possible

## Related

- `source/catalog/README.md` — multi-catalog cache + zRIF sidecar
- Root `catalog_psvita_games.zrifidx` — license index on GitHub
- PKGj / FAPS bgdl research lineage for ShellSvc IPMI constants (implementation is original to this client)

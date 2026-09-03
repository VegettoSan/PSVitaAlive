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
- Settings persistence for install method / PSP path preferences / **color theme**
- Optional LiveArea refresh helpers for edge cases where promote succeeds but the bubble is not visible
- **Keep-awake + shell locks** while in-app jobs run (see below)

## Main components

| File | Role |
|------|------|
| `install_controller.cpp` | Public API used by UI; prefers BGDL for any `.pkg` when available; owns keep-awake thread and shell locks |
| `install_dispatcher.cpp` | Format routing (VPK / ZIP / PKG direct — direct retail PKG is not used when license is required) |
| `homebrew_installer.cpp` | VPK promote path |
| `pkg_bgdl_installer.cpp` | BGDL package + license enqueue |
| `license_helper.cpp` | zRIF decode / RIF write / **disk index lookup** |
| `plugin_detector.cpp` | Scan `ur0:tai` / `ux0:tai` for NoNpDrm, NoPspEmuDrm, … |
| `bgdl_client.cpp` | ShellSvc IPMI download class (PKGj-aligned) |
| `vita_installer.cpp` / `psp_installer.cpp` | Platform-specific helpers |
| `fake_package_builder.cpp` | head.bin / package scaffolding |
| `refresh_manager.cpp` | LiveArea refresh helpers |
| `app_settings.cpp` | Client settings (including feature toggles and color theme) |

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
BGDL system queue → LiveArea notifications
```

### License resolution order (typical)

1. Explicit zRIF on the link (rare)
2. **`content_id`** → line in `catalog_psvita_games.zrifidx` (`content_id<TAB>zrif` per line)
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

## Keep-awake & shell locks (process-bound jobs)

`InstallController` owns the lifecycle of in-app download/extract jobs. Commercial **BGDL** PKG enqueue is different (system download manager); the locks below primarily protect **HTTP + ZIP/VPK** work inside the client.

### Keep-awake thread

- Thread name: `PSVitaAliveKeepAwake`
- Started in `init()`, stopped in `shutdown()`
- Every ~5s while `busy()`:
  - `sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND)`
  - `sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF)`
  - `sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_DIMMING)`

Rationale: auto-suspend invalidates network/file progress; screen-off confuses users and still risks idle policies depending on firmware settings.

### Shell util locks

Requires `#include <psp2/shellutil.h>` and `SceShellSvc_stub` (already linked for BGDL).

```text
init → sceShellUtilInitEvents(0)
setState(Downloading|Installing) → lock PS_BTN + POWEROFF_MENU
setState(Completed|Failed|Cancelled|Idle) → unlock both
shutdown → unlock (idempotent)
```

`shellLocked_` / `shellUtilReady_` avoid double-lock and no-op when init failed.

### ZIP completeness pre-check

Before `zip_open`, archives may be rejected if the on-disk image has no EOCD / ZIP64 end marker near EOF (`zipLooksCompleteOnDisk`). Typical user-visible failure after a killed transfer:

```text
zip_open failed err=35
no EOCD/ZIP64 marker near end (download may be incomplete)
```

Mitigation is prevention (locks + keep-awake + screen on) plus early fail + cancel cleanup of partial job files.

### Color theme settings

`app_settings` persists `color_theme` (enum). Themes are applied at runtime to accent/surface colors used by neon frames and UI chrome. Changing theme does not affect install correctness.

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
- Root [README — Downloads & installations](../../../README.md#downloads--installations-ps-vita-client) — user-facing rationale
- PKGj / FAPS bgdl research lineage for ShellSvc IPMI constants (implementation is original to this client)


## PSP / PS1 target (`AppSettings::pspTarget`)

`InstallController` applies `settings_.pspTarget` on each install:

| Target | Vita VPK | PSP/PS1 PKG | ISO/CSO/PBP |
|--------|----------|-------------|-------------|
| **LiveArea** | Promote (unchanged) | BGDL → LiveArea bubble | `ux0:pspemu` |
| **Adrenaline** | Promote (unchanged — VPK is Vita-only) | **No BGDL** (no LiveArea bubble). PKG→pspemu unpack = follow-up (PKGj-style) | `ux0:pspemu` |

Do **not** route Vita VPK through Adrenaline-only logic.

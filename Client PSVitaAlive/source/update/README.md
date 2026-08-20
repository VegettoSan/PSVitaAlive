# `source/update/` — Self-update (GitHub Releases)

## Why a helper bubble?

A title **cannot reliably promote itself** while it is running. The client therefore:

1. Downloads the new `PSVitaAlive.vpk` from GitHub Releases
2. Stages extract at `ux0:data/psva_vpk`
3. Installs temporary helper **PSVAUPDT1** (from `app0:updater/`)
4. Hands off to PSVAUPDT1
5. PSVAUPDT1 promotes the staged client, launches **PSVAS1178**, and is removed later

## Files

| File | Role |
|------|------|
| `update_checker.cpp` | `checkLatest`, `applyUpdate`, promote helper, `launchUpdaterAndExit` |
| `startup_update_manager.cpp` | Startup orchestration / worker for download+stage only |

## Handoff (must stay in sync with `updater/main.c`)

**Works (updater → client):**

```c
sceAppMgrLaunchAppByUri(0xFFFFF, "psgm:play?titleid=PSVAS1178");
sceKernelExitProcess(0);
```

**Client → updater must use the same shape** (title id `PSVAUPDT1`). Avoid:

- `sceAppMgrDestroyOtherApp()` immediately before launch from the client
- Calling `LaunchAppByUri` from a worker thread
- Aggressive PAF unload immediately after promoting the helper on device (soft `scePromoterUtilityExit` only)

## Promote of PSVAUPDT1

Prefer async:

```text
PromotePkg(path, 0) → poll GetState → GetResult → soft Exit
```

Sync promote (`sync=1`) was observed to hang after reporting success on real hardware.

## Logs

```text
ux0:data/psvitaalive/logs/session.log   # client side
ux0:data/psvitaalive/logs/updater.log   # written by PSVAUPDT1
```

Manual VPK if handoff fails: `ux0:data/psvitaalive/update/PSVitaAlive.vpk`.

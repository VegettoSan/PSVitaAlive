# Client self-update (`source/update`)

## For end users

The PS Vita Alive Store client can **update itself** when a new version is published on GitHub Releases:

1. Open the client on the Vita (or Vita3K).
2. If update checking is enabled, the app queries `releases/latest`.
3. When a newer `PSVitaAlive.vpk` is available, it is downloaded and installed automatically through a short-lived helper title **PSVAUPDT1**.
4. The store restarts on the new version; the helper bubble is removed.

No computer is required for a normal update cycle. If the automatic handoff fails, install:

`ux0:data/psvitaalive/update/PSVitaAlive.vpk`

Releases: https://github.com/VegettoSan/PSVitaAlive/releases

---

# `source/update/` — Self-update (GitHub Releases)

## Why a helper bubble?

A title **cannot reliably promote itself** while it is running. The client therefore:

1. Downloads the new `PSVitaAlive.vpk` from GitHub Releases
2. Stages extract at `ux0:data/psva_vpk`
3. Installs temporary helper **PSVAUPDT1** (from `app0:updater/`)
4. Hands off to PSVAUPDT1
5. PSVAUPDT1 promotes the staged client, launches **PSVAS1178**, and is removed later

## Release 02.17 and automated publication

The latest stable tag is [`02.17`](https://github.com/VegettoSan/PSVitaAlive/releases/tag/02.17); its SFO has `APP_VER=02.17` and `TITLE_ID=PSVAS1178`. The client uses the **GitHub `tag_name`** rather than the release title/body to decide whether a new version is available.

Publication (distinct from in-client updating):

1. `.github/workflows/client-build.yml` executes native tests and the official VitaSDK build on push to `main`.
2. After a successful push build, `.github/workflows/publish-client-release.yml` checks for matching `docs/releases/<VITA_VERSION>.md`, rejects an existing orphan tag and leaves any existing release untouched.
3. The publishing workflow downloads the **exact successful CI artifact**, validates its ZIP CRC and `sce_sys/param.sfo` (version and Title ID), calculates SHA-256, and publishes `PSVitaAlive.vpk` plus `PSVitaAlive.vpk.sha256` under the matching stable tag.

This workflow does not change the runtime helper `PSVAUPDT1`, create a server or manually modify generated catalogs. [Release versioning safeguards](../../../docs/self-update-versioning.md) · [02.17 notes](../../../docs/releases/02.17.md).

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

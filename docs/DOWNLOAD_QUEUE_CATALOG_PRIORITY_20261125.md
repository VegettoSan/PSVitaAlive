# Catalog priority — download queue integration rule (2026-11-25)

## User-facing rule

The catalog takes priority over a new download, installation or launching an installed application from PSVitaAlive. The cache-first UI can still be browsed while a soft refresh runs. Show a localized toast for 5600 ms when an action is blocked:

> Actualizando el catálogo. Espera un momento para instalar o abrir aplicaciones.

The user must retry the action after refresh; never silently queue/replay a stale selection. Ordinary sync phase toasts cannot overwrite this explanatory toast during its display interval.

## Implementation

- CatalogManager::actionBlockedByCatalog uses the mutex-protected pending request, active worker operation and pending publication flags. Unlike isBusy(), it includes the cache-first handoff, validator checks, background download, parse and worker cleanup. It also blocks when the catalog mutex is unavailable. This intentionally covers the validator-check window before it is known whether the remote file differs. A failed or unchanged refresh unblocks when the worker and publication drain; a decorative completion bar is not a lock.
- FullCatalogScreen::guardCatalogAction is shared by touch and controller paths. Primary actions (including Open before the native URI launch), individual links, Install All and its confirmation, saved ZIP retry, PSP/plugin consent, manual self-update and essential-plugin consent are guarded. The main callbacks recheck before starting the installer. The startup essential-plugin prompt waits until the catalog is ready.
- Main consumes catalog publications and applies the catalog synchronously before accepting another input frame.
- Reverse exclusion prevents catalog changes and automatic empty-catalog recovery from racing legacy installation flows. Reject a switch before discarding current catalog data; the session gate remains the stronger exclusion once the actual queue admissions are wired.
- Coordinator::submit returns CatalogBusy before acquiring protection or inserting a job. A defensive dispatch barrier remains in place. No new background thread or per-frame storage probe is added.
- New toast key CATALOG_ACTION_WAIT is present in en, es, fr, de, it, pt-BR, pt-PT and ru.

## Validation

- Phase 4 native VitaSDK build completed successfully: https://github.com/VegettoSan/PSVitaAlive/actions/runs/45713159645.
- Catalog priority coordinator host tests, including sanitizer stages, passed: https://github.com/VegettoSan/PSVitaAlive/actions/runs/45714009545. The tests verify rejection without queue/shell locks or job creation, followed by successful admission when the catalog is available.
- The first general client workflows stopped at the pre-existing source-level recovery assertion because the condition had changed. Retained the old isBusy guard and added the stronger lifecycle guard. Expanded test_catalog_recovery.py to verify the new guards, Open ordering, non-destructive tab blocking, recovery exclusion and eight translations; no existing checks were removed.
- Current full build: https://github.com/VegettoSan/PSVitaAlive/actions/runs/45714150271 (in progress when recorded).

## Device acceptance cases

1. Cached catalog appears, validator check is active: try primary action, direct link and Open with touch and controller; no installation or launch.
2. Remote catalog download or JSON applying: same behavior, readable toast survives phase changes.
3. New catalog has removed or changed the selected title/link: only retry using the updated catalog, never replay the old request.
4. No change or network failure: unblock after worker cleanup; cached catalog remains usable.
5. Updated phase and decorative completion animation: no unnecessary blocking once data is applied.
6. Install All, individual plugin consent, PSP setup, saved ZIP retry and manual client self-update respect the same guard.
7. Startup essential plugins do not compete with the catalog refresh.
8. Installation active: catalog switch or empty-catalog recovery cannot discard or parse catalog data during worker execution.
9. Verify wrapping, duration and theme/font contrast on all eight languages; no unexpected focus movement.
10. When the queue UI is integrated, repeat the admission rejection without shell locks or pending jobs, then start after refresh.

This is an active guard in the existing client on the experimental branch, not an activation of the global queue. Main-loop queue routing, complete Install All migration, unknown-size/BGDL policies, global memory/OLED, queue UI and report remain pending. No queue-enabled VPK is supplied yet. Main, releases and generated catalogs remain untouched.

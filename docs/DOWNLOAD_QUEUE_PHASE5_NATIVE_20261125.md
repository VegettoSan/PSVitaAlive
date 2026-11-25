# Download queue — native integration and first UI (2026-11-25)

Experimental branch only; no main/release/catalog changes.

## Implemented in the actual client

- main.cpp owns one Coordinator<InstallController,CatalogManager> and routes normal internal installation requests and the actual selected Install All group through it. Before dispatch, frozen request metadata and destination are validated; individual and grouped legacy-only routes remain exclusive. The coordinator is ticked once per main-loop frame instead of the legacy installer poll when protected; settled steps invalidate installed state and, when appropriate, write the selected data revision independently of the VPK receipt. Startup prompts are deferred while the coordinator owns the session.
- Existing catalog-first action guard remains in place. Reverse exclusion prevents tab changes, catalog recovery, and incompatible storage/settings operations during the queue. The current experimental policy disables the storage manager until the session finishes; a waiting-for-space user may cancel queued work or retry. Safe storage maintenance during a blocked session is a remaining requirement.
- The full-screen manager has a footer chip between Report and Storage; controller chrome has four slots and the navigation unit test has been expanded. The view can be opened from a queued application's detail, shows job/step states, supports controller and touch input, swipes, pause/resume, explicit cancel, ZIP Keep/Delete confirmation with stable target, retry capacity, and an aggregate final report followed by the existing restart prompt. It reuses the current skin and theme but the active progress detail is a new view, not yet identical to the legacy progress dialog. Entry/exit use short fades without concurrent catalog rendering. The report is in-memory for the session; persistent history and recovery are not implemented.
- App cards and primary detail controls reflect waiting/active state. Download and recovery actions for that same application are hidden, including their layout/focus entries, while the existing Web QR and descriptions remain accessible. Native app launch is blocked during queue protection.
- The queue acquires global shell/power protection and applies the OLED inactivity timer of 120 seconds regardless of current catalog/manager screen, including wake-input consumption. Icon limit is nine; screenshot limit is two; only cached images are decoded and network requests are paused. The queue manager and OLED screen render exclusively, no underlying catalog draw.
- New UI keys translated to en/es/fr/de/it/pt-BR/pt-PT/ru.
- Missing-size internal jobs can be admitted provisionally at the tail with the existing type-specific minimums. No further job may be admitted behind an unfinished provisional group because its final occupancy is unknown. The normal HTTP/extraction checks remain authoritative. A question mark in the group row and a toast indicate uncertainty.
- BGDL and direct PSP/PS1 PKG conversion, ambiguous formats and some grouped missing-destination cases continue to use the legacy individual flow only when the queue is idle. Saved ZIP retry from the catalog is exclusive while the queue is active. No mixed internal/system transfer or optimistic storage reservation is introduced.

## Validation and limitations

- Prior catalog-priority native build: https://github.com/VegettoSan/PSVitaAlive/actions/runs/45714150271 — success.
- Session/coordinator host tests with ASan/UBSan, including provisional-size tail admission: https://github.com/VegettoSan/PSVitaAlive/actions/runs/45715193510 — success.
- Integrated native build (including UI refinements): https://github.com/VegettoSan/PSVitaAlive/actions/runs/45715309039 — running at time of record, all pre-build checks passed. Do not publish a release or treat as hardware validated.
- Main-loop integration has not been tested on physical Vita. Device validation is mandatory for shell lock behavior, RAM/FPS, loading/catalog update races, foreground/background UI, zip preservation, installed receipt correctness, reports, and plugin restart. The plan still requires full parity for BGDL/direct PSP PKG, saved ZIP enqueue, safe storage management during blocked queues, persistent queue/history, exact live allocation accounting, and final progress-dialog visual polish. Unknown-size jobs deliberately block later admission. A running active writer's storage reservation remains conservative until stable checkpoint evidence is available.

## First device test sequence (only after successful integrated build)

1. Install a small known-size Homebrew VPK; first request opens progress. Circle returns to manager; Circle again returns to catalog. Download continues.
2. Add another known-size VPK and a known-size Game Files ZIP; check chip count, labels, hidden install links, and automatic progression. Cancel the pending job via Triangle/confirm and verify the active task continues.
3. Pause/resume the transfer using Square or touch; confirm no LiveArea or settings transition and catalog switching shows localized toast.
4. Reopen manager using footer, including physical START/chrome focus, touch swipes, individual jobs and component results. Try a group with several known-size selected links and then a provisional size at the tail.
5. Run two groups including plugins only on a backed-up test device. Verify restart is offered only after the aggregate report, and cancelled/failed components are not reported installed.
6. Let the device idle for two minutes from the catalog, detail and queue manager; check exclusive OLED screen and wake-consumption. Record FPS and free RAM compared with stable 02.19.
7. Test near-full ux0 and a missing uma0; verify pending admission/dispatch failure does not write any payload, and cancellation releases virtual reservations.
8. Trigger a catalog refresh from a cached startup; try download/install/Open with physical and touch input. Retry after actual update completes. No stale action may auto-replay.
9. Try legacy-only formats while idle and while queue is active; verify the former retains existing behavior and the latter is safely rejected.

No release or production catalog mutation.


## Validated first VPK and subsequent route extension

The first integrated VitaSDK build **passed**, run 45715309039, commit `b9c716f37724ac9baa465ae2dae0cbd0320d8eea`. Artifact ID 15069028355 contains PSVitaAlive.vpk. Verified archive CRCs and internal VPK, APP_VER 02.19, TITLE_ID PSVAS1178. VPK size 10,294,713 bytes; SHA-256 `69b963abb260ea0a989bacd819f5abff520cee50215fdd8d3bbbe09b8b19fa7a`. This is the first hardware-test candidate, not a release.

Subsequent commit `cd62cdb470c02560f173b18d5ea4e2f337d0e4cd` expands the branch to include standalone BGDL handoff, PSP/PS1 PKG conversion and CSO, and saved ZIP offline extraction. These changes are **not in the b9c716f VPK**. Saved ZIP metadata, mount, and physical part lengths are revalidated at dispatch, and its input bytes are already allocated. System handoff and uncertain output routes are tail barriers; no later reservations depend on their predicted cleanup. BGDL mixed groups retain their legacy exclusive flow. Pending external BGDL activity after handoff is still managed by the OS, not by the local queue. Native build and tests for this extension: https://github.com/VegettoSan/PSVitaAlive/actions/runs/45715976503 and https://github.com/VegettoSan/PSVitaAlive/actions/runs/45715976701 (not yet confirmed at the time of record).

Remaining: persistent queue/history, exact live storage accounting, safe storage maintenance during blocked jobs, full mixed-route parity, final progress-dialog visual parity, and hardware validation. The first VPK is appropriate for testing the internal queue, not all these remaining capabilities. Do not merge or release based only on the build.

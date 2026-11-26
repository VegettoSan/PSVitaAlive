# Download queue — phase 4 coordinator and native planning (2026-11-25)

Branch feature/download-queue-manager only. The catalog has not yet switched its admissions to the coordinator. This is not a queue-enabled device-test build.

## Implemented

- A main-thread frame-driven Coordinator<Backend,Catalog> in include/installer/queue_coordinator.hpp. It acquires the catalog gate and shell/power protection before first admission, rolls back on rejection, drains outstanding catalog publications/work, and checks external work barriers before claiming. It launches at most one step and polls it on subsequent rendered frames (BGDL preparation deferral retained). It never creates another worker.
- Capacity failure is latched until explicit retry or queue mutation, not retried with filesystem probes every frame. Start refusal before native ownership is recorded as failure; ownership inconsistencies stop dispatch instead of starting another job. Pending and active cancellation are separated; token-scoped pause, resume and ZIP actions cannot address another job.
- For Install All ZIP recovery, attempt Keep once after ten seconds; if it fails, wait for an explicit user decision, never auto-delete. Obtain a fresh tagged observation after recovery before settlement. The final report event occurs only after the queue and its worker finish and both protections are released. Report release survives a transient catalog-gate release failure. A new batch waits until the previous report is acknowledged; UI should preserve history separately if desired.
- Added explicit opt-in continuation for independently selected group components on ordinary failure, matching the existing wizard's ordinary-failure behavior. Cancellation aborts the group; system BGDL handoff cannot satisfy a dependent step. The default remains conservative (stop on failure) until the real builder opts in for independent work.
- Added Component classification and shouldWriteDataRevision, mirroring the existing rule: all selected Game/Data Files must finish successfully and the group must not have been deliberately aborted. A failed independent plugin does not invalidate an already successful data batch. Native receipt write must still be connected on terminal job events.
- Native InstallController::prepareQueuedJob copies the preselected steps and routing, derives a canonical app key, classifies routes through the SAME classifier used by requestInstall, estimates per-mount peak and retained bytes, and retains the existing aggregate Install All preflight as a floor. It supports known-size internal VPK, ZIP extraction, plugin and explicit ISO/PBP routes; estimates are not authoritative installed sizes. Missing sizes, BGDL, direct PSP PKG conversion and ambiguous formats return explicit non-ready results, rather than pretending capacity is proven. The legacy route is unchanged until admission integration. Unknown-size handling and BGDL outstanding reservations remain prerequisites to full feature parity.
- Added explicit native template instantiation in source/installer/queue_coordinator.cpp and included it in CMake with footprint optimization. This validates the coordinator against the real adapter types without yet exposing incomplete protections/UI.

## Verification

- Prior phase-2 VitaSDK build: https://github.com/VegettoSan/PSVitaAlive/actions/runs/45711514598 — success.
- Phase-3 host tests: https://github.com/VegettoSan/PSVitaAlive/actions/runs/45712345162 — success. Native build was cancelled by subsequent branch updates (not a verified pass).
- Phase-4 host CI: https://github.com/VegettoSan/PSVitaAlive/actions/runs/45712972504 — success, including C++17 strict compilation and ASan/UBSan of the session core and new coordinator fake-backend tests. Covered rollback on first admission, catalog drain, no-space retry latch, cancellation isolation, stale control tokens, delayed terminal cleanup, ZIP keep failure and explicit resolution, continuation after ordinary group failure, data-revision policy, failed-start outcome, and final-report gate-release retry.
- Native build containing all phase-4 code: https://github.com/VegettoSan/PSVitaAlive/actions/runs/45713159645 — pending at the time of this record. Intermediate native jobs were automatically superseded by later commits. No claim of Vita hardware validation.

## Remaining integration

The coordinator and native planner are compiled by the branch build but are not yet invoked from main.cpp. Next, move the real wizard-selected links, filename and destination resolution into immutable Job/Step requests; use coordinator.submit and a single coordinator.tick in main, and use actual storage/maintenance, startup plugins, self-update and other modal workers as external barriers. Keep the existing install flows available until unknown-size, BGDL and direct PSP conversion policies are reconciled. Apply receipt writes and installed-state refresh on terminal events, not report dismissal.

Before activating admissions, add the full-screen queue interface, progress hide/back semantics, per-app controls and tags, cancel/retry decision paths, global OLED idle behavior, 9-icon resource policy, report/restart presentation and localized warnings for every input route. Preserve actual installed state after partial results. A usable VPK requires these integrations plus a successful native build and user testing; a compile-only artifact is not a functional download queue.

Main, releases and generated catalogs were not modified.

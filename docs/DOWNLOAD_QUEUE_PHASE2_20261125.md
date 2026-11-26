# Download queue — phase 2 native controller adapter (2026-11-25)

Experimental branch only. The main screen still uses the legacy installation callbacks; do not install a phase-2 compile artifact to test a download queue that is not yet visible or connected.

## What changed

- Extended the phase-1 queue model with per-request installation routing (method, PSP target, format, partition), zRIF carried in memory, release revision, and structured result details. No license strings should be emitted into logs or persisted by future storage adapters.
- Added a distinct HandedOff outcome/state for BGDL. A successful system submission is not a verified installation. A grouped job with a system handoff before remaining steps stops conservatively and reports a partial outcome.
- Added opt-in InstallController methods: canStartQueuedStep, startQueuedStep, pollQueuedStep, settleQueuedStep, cancelQueuedStep, pauseQueuedStep, resumeQueuedStep, resolveQueuedZip. The request validates the active session token and frozen routing, binds the worker only at dispatch, reuses the existing requestInstall implementation, and **always** passes false for stale batch space approval.
- A false return from the legacy requestInstall can still produce a terminal result; if it leaves the controller idle, the queued adapter explicitly records a failed start. A true return may represent synchronous preflight failure, so accepted ownership never means installation success.
- A queued result cannot auto-dismiss or be acknowledged by the legacy progress view. It remains owned until the token-matched terminal observation has been captured, worker cleanup has settled, and any recoverable ZIP decision has succeeded. Saved ZIP path and reboot flags are captured before explicit acknowledgment.
- While a step is owned, legacy requests/metadata/ZIP actions and legacy pause/resume/cancel are rejected. Token-scoped actions retain existing cooperative cancellation/resume. The special pending BGDL flag is inspected without consuming its deferred poll; a system task can be cancelled by the client only before actual submission.
- The controller refuses settings changes while an owned step is active. The future UI must block conflicting settings across the entire session (and must not present a rejected setting as saved).
- No additional worker, no change to the legacy status polling path for ordinary nonqueued installs, no modifications to catalog JSON or publication.

## Polling contract for the main-loop integration

Call pollQueuedStep exactly once per drawn frame for a queued owner, instead of the legacy status() call; do not call both. BGDL uses the first poll to publish preparation and the subsequent poll to actually submit on the main thread. Keep pollDownloadRecovery in the main loop. Only settle with the returned tagged observation; after ZIP resolution obtain a new observation before settlement. Do not claim another step until the prior worker is fully settled and the controller is idle.

The adapter is present but not yet wired into main.cpp. Before exposing it to users, implement the global coordination gate (catalog soft refresh, storage workers, self-update, essential-plugin flow), measured per-mount storage snapshot including actual temporaries and active group remainder, continuous shell protection and keep-awake, transfer of Install All execution/receipts, and the UI from plan V3. These safeguards are prerequisites, not optional follow-up polish.

## Tests

Local C++17 host tests and ASan/UBSan passed on exact branch blob copies:
- download_queue.hpp: 28fcbaa8c06614d13844903878fe05bfca8c8691
- test_download_queue.cpp: 949b4e31ca7eff1d8e04b3c012527d19664d02b2
- app_settings.hpp: 7a327bb762f22f0af1996fce0cf63e91f3d25b6c
- install_space_policy.hpp: 0a9a4a75f4c052e7ec3ed1fba16c744753786296

Additional tests cover routing changes, copied license metadata, structured result details, and standalone/grouped BGDL handoffs. Existing tests cover admission/order, cancellation, duplicate/late completion, barriers, reboot and partition separation.

GitHub Actions host run: https://github.com/VegettoSan/PSVitaAlive/actions/runs/45711385020 — success.
Full native VitaSDK compile workflow enabled on this experimental branch:
https://github.com/VegettoSan/PSVitaAlive/actions/runs/45711514598
Native source changes are included in that compile; the result was still in progress when this record was written. No claim of device validation.

## Outstanding issues

Storage estimates for unknown-size/ZIP expansion, active progress vs confirmed on-disk bytes and staged cross-mount writes need a conservative measured adapter. Do not credit download UI percentages as allocated bytes: InstallController reuses its progress counters for extraction. BGDL pending system reservations cannot be treated as reclaimed when a local handoff returns. The first conservative core stops a group on failure; actual Install All behavior and dependency-aware continuation must be explicitly reconciled. Shell locks, OLED protection, persistent queue/history and UI are not yet integrated. No test VPK is being supplied in this phase.

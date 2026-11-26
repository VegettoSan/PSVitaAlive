# Download queue experimental branch — phase 1 record

Base: `86b3c978c201aa776a8ca4845ceaa8f2386c9887` (main, 2026-11-25).
Specification: PSVitaAlive_PLAN_GESTOR_DESCARGAS_20261125_v3.md, particularly §§25–27.
This is NOT a release and does not yet change native installation behavior.

## Implemented

- Header-only, main-thread-owned session model in `include/installer/download_queue.hpp` (relative to Client PSVitaAlive).
- Unique session job/step tokens, immutable request copies, canonical app-key deduplication, FIFO grouped steps, bounded job/step counts.
- Generation-matched storage snapshot contract. Project cumulative retained space and each transient peak separately per ux0/uma0/ur0. Use existing install_space_policy.hpp for known single-mount ZIP/direct package estimates; unknown estimates, saturated values and unsupported BGDL/plugin convenience estimates are rejected. No optimistic credit for future cleanup.
- Revalidate capacity and external heavy-operation barrier before dispatching. Only one step claimed at once. Do not settle from an early terminal event; late/duplicate tokens cannot advance another job.
- Pending cancellation affects only that pending job; active cancellation is cooperative, retaining ownership until settlement. Record partial group results and accumulate reboot requirement.
- First conservative group policy stops remaining steps on a failed component, until dependency-aware Install All semantics can be migrated and tested.
- No persistent queue, UI, direct backend calls, shell lock changes or VitaSDK dependencies yet.

## Verified locally

Host compiler invocation (C++17, warnings as errors) and ASan/UBSan both executed successfully, printing `Queue core tests passed`. The local copies used had these exact Git blob hashes, verified against the branch:
- queue header: `350b9f751fc84bf86a4dfce4558ca39426bb4fc3`
- host tests: `6598dfa0655aec01815f44b4fc05769e69d3c22f`
- existing storage policy: `0a9a4a75f4c052e7ec3ed1fba16c744753786296`

Commands:
```sh
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -I"Client PSVitaAlive/include" tests/test_download_queue.cpp -o /tmp/psva-queue-test
/tmp/psva-queue-test
c++ -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer -I"Client PSVitaAlive/include" tests/test_download_queue.cpp -o /tmp/psva-queue-sanitize
/tmp/psva-queue-sanitize
```

A branch-only host workflow was added. Its remote execution result has not been confirmed. No VitaSDK build was performed: local runtime lacks VitaSDK and direct git access.

## Integration gates before a device-test VPK

1. Build the real InstallController adapter (immutable per-step metadata/settings, full outcome snapshot, explicit acknowledgment, settle only after workerDone and recovery ownership release). Do not call setPendingCatalogMeta on queued admissions. Ensure a false start, synchronous preflight failure and BGDL handoff have distinct outcomes.
2. Build the measured storage snapshot adapter; include partial downloads/retained ZIPs, current phase and all group steps, partition-specific destination and staging, and the current Install All conservative aggregate preflight. Keep HTTP and extraction checks. Implement user-facing rejection/attention policy for unknown size and failure to measure.
3. Move Install All execution/results/receipt revision/recovery/reboot from FullCatalogScreen into session lifecycle without changing selections. Keep saved ZIP and essential-plugin paths isolated until safe.
4. Add global catalog soft-refresh quiescence, storage-worker/maintenance and self-update exclusion, continuous shell locks and keep-awake, safe cancellation and global OLED protection. Validate no lock gap and no indefinite trap.
5. Add full-screen manager, button location, badges/detail action replacement, progress navigation, final report, localization and 9-icon mode with critical fallback, following revision 3. Preserve catalog selection/focus and exclusive rendering.
6. Integrate PSP/PS1/BGDL and plugins with explicit handoff/reboot outcomes; test on VitaSDK, Vita3K and real hardware. Only provide a VPK for queue testing after these integration gates; an unchanged-client VPK would not exercise the new feature.

Main, catalog schemas, generated catalogs, and releases were not modified.

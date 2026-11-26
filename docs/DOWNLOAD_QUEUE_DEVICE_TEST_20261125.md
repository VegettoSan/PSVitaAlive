# Download queue — hardware test candidate (2026-11-25)

Source candidate: `b57b48de78eb708eb3c734f1ff4e4359ed778826`. Branch: `feature/download-queue-manager`. Do not merge or publish a release before physical Vita validation.

CI:
- [Queue host tests](https://github.com/VegettoSan/PSVitaAlive/actions/runs/45719613406): passed (core, coordinator, history, recovery, sanitizers).
- [Official VitaSDK build and VPK artifact](https://github.com/VegettoSan/PSVitaAlive/actions/runs/45719613333): consult final run status and use only the artifact of this source revision if successful.

## Device test matrix

Use a backed-up device and legally obtained homebrew/test packages. Record firmware, storage mount, language, exact candidate commit, free space, source/format, and steps to reproduce; preserve session.log on failure. Do not fill the card completely or interrupt critical plugin writes.

| ID | Test | Expected result |
|---|---|---|
| Q01 | Queue three small VPKs, leave progress view, browse catalog and reopen queue | Correct FIFO dispatch, no duplicate jobs, responsive catalog; completed items in history |
| Q02 | Pause/resume HTTP transfer, cancel pending job, cancel a safe active transfer | Only appropriate actions are enabled; state and storage are consistent |
| Q03 | Queue a known-size download while serial transfer is writing, repeat for 2/4 ranges | Physical written prefix credited once, no optimistic use of buffered bytes, retry invalidates credit |
| Q04 | Disconnect Wi-Fi and reconnect; include unknown-length source | No false 100%; recoverable failure is visible; resume validates source |
| Q05 | Run Install All with homebrew plus Game/Data ZIP; try failed component | Correct order and results, safe paths, no deletion of retained data |
| Q06 | Queue saved ZIP, then test ISO/CSO/PBP and PSP/PS1 PKG routes | Native installer routes preserved and local input is not counted as a new download |
| Q07 | Test a BGDL handoff with remaining components | OS handoff is not installation success; compatible tail requires explicit recovery after actual LiveArea installation |
| Q08 | Restart with pending tasks, and separately after interruption during a noncritical download | Durable records survive; safe pending work can be explicitly restored; interrupted/unsafe work remains for manual review |
| Q09 | Test insufficient space without filling card; open storage via L/touch, clean eligible temporary data, retry | No destructive action on installed data or saved archives; queue rechecks free space and destination mount |
| Q10 | Change supported mount where appropriate; test missing mount | Missing/unverifiable mount blocks dispatch; no cross-mount credit |
| Q11 | Validate Spanish and English with long filenames, controller/touch, all progress and recovery screens | No text escape, invalid controls or accidental confirmation; scroll, ETA and indeterminate progress readable |
| Q12 | Test history after closing/reopening client and run several small jobs | History persists, remains bounded and does not trigger work |
| Q13 | Test catalog refresh, client-update guard, and report/other existing UI while jobs are present | Queue and legacy workflow interlocks prevent conflicting operations |

## Intentional limits

BGDL execution is owned by the OS; no verified OS polling/cancellation API was added. Compatible deferred tail tasks require explicit confirmation, an installation marker and current catalog validation. Other interrupted/ambiguous operations require manual review. Storage reservations during extraction/install remain conservative; exact expanded output can be known only when metadata or the installer provides it. During a blocked queue only safe temporary cleanup is permitted. Optional mounts depend on successful native capacity probes. UI appearance, real device resource use and native execution still require physical validation.

Earlier phases are documented in the adjacent DOWNLOAD_QUEUE_PHASE* files. The inaccessible original conversation/specification was not recovered in full; this matrix records the implemented behavior rather than asserting exact parity with an unseen document.

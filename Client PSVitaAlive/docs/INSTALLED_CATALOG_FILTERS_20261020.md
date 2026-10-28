# Installed catalog filters — 2026-10-20

## Behavior
- All four native-client catalogs expose **Installed**.
- Homebrew additionally exposes **Updates** and **Version not recognized**.
- **Installed** includes `Installed`, `UpdateAvailable` and `InstalledUnknown`. This is intentional: missing proof of the release version does not mean the app is absent.
- Homebrew retains the existing installed-release decision logic, so no update is announced from an unreliable APP_VER mismatch.
- Commercial games only check presence, never installed versions or game updates.
- The same installed badge renderer is shared by all four catalogs.

## PKGj-compatible game detection
- LiveArea installations: `ux0:app/<TITLE_ID>` (commercial Vita games and PSP/PS1 bubbles).
- PSP on Adrenaline: `pspemu/PSP/GAME/<ID>/EBOOT.PBP`, `pspemu/ISO/<ID>.iso` and ISO/CSO names ending in `[<ID>]`.
- PS1 on Adrenaline: `pspemu/PSP/GAME/<ID>/EBOOT.PBP`; when the folder uses Disc ID instead of PSN Title ID, the detector inspects the embedded PS1 content ID in the PBP.
- Available PSP mounts probed: `ux0:`, `uma0:`, `imc0:`, `xmc0:`.
- All regional title IDs and download-link Title IDs are parsed in memory for each game. DLC-specific IDs are excluded.
- Detection keeps an in-memory index of actually installed IDs, not a second catalog database.

## Performance
- Status filters scan at most 64 entries per frame.
- Unknown due to Homebrew I/O throttling is retried rather than excluded; Homebrew uses its existing 2 checks per 250 ms budget.
- Filter options remain statically compiled; no remote requests, new generated JSON or extra server.
- The local index is invalidated on tab switch and installation completion.

## Hardware verification still required
1. Homebrew Installed includes current, needs update, and installed/version unknown.
2. Homebrew Updates and Version not recognized each show only their respective states.
3. Native Vita game with different regions and a LiveArea installation.
4. PSP install as PBP folder, raw `<ID>.iso`, PSVitaAlive's `<name> [<ID>].iso`, CSO and PSP LiveArea bubble.
5. PS1 folder using Title ID, PS1 folder using Disc ID, PS1 LiveArea bubble.
6. Tests across enabled mountpoints and with unmounted partitions.
7. Filter + search, scroll, touch, language and badges after installation.
8. Unrelated DLC-only folders should not mark the base title installed.

This is a source change. VitaSDK CI compilation and Vita/Vita3K runtime tests
are independent checks, not automatically certified by a GitHub file commit.

## Follow-up in version 02.17 (28 October 2026)

The existing multi-device scanner now stores **which mount matched** each PSP ISO/CSO and PSP/PS1 PBP ID, not only the IDs.

- `InstalledGameIndex::locationFor()` exposes that mount; the installed badge can show **Installed ux0**, **Installed uma0**, etc., with the existing clipping and marquee so the card dimensions do not change.
- Accessible `ux0`, `uma0`, `imc0` and `xmc0` PSP paths can be indexed. A recognized ID/path is still required; arbitrary filenames must not cause false installed status.
- A successful PSP/PS1 install sets a deferred index-refresh flag. The actual invalidation happens when the completion dialog closes, not every frame. LiveArea `ux0:app` remains separate.
- The chosen Adrenaline destination is `ux0:` or `uma0:` (if mounted); the other mount labels are for detecting existing installations.

**Device regression:** install PSP/PS1 content on `uma0`, compare the destination during and after installation, confirm the mount label, then test `ux0`, ISO/PBP and unmounted partitions. [02.17 behavior](../../docs/RELEASE_02_17_TECHNICAL_20261028.md).

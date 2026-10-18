# Homebrew primary actions: Install All, Update and Open

PSVitaAlive uses its existing installed-release detector to determine the
primary action shown in the Homebrew detail page. No catalog JSON or game
catalog behavior has changed.

## States

| Detection result | Button | Action |
| --- | --- | --- |
| NotInstalled | INSTALL ALL (only when Game/Data Files are present) | Existing Install All wizard |
| UpdateAvailable | UPDATE | Download and install only a Download VPK |
| Installed (current catalog version) | OPEN | Launch the installed Title ID |
| InstalledUnknown / Unknown | Existing Install All if available, otherwise no main action | Avoid asserting a version is current without evidence |

`FullCatalogScreen::primaryActionFor` calls `queryLocalInstall`, which
continues to use `update::decideInstallState` plus the existing SFO and
install-receipt evidence. Version detection itself is unchanged.

## UPDATE

`preferredVpkDownloadIndex` filters catalog links to `Download` /
`Downloads` VPK entries (by URL path or link name). It chooses the first
`recommended: true` VPK; if none is recommended, it chooses the first
eligible VPK to support older catalog entries. A missing VPK produces a
localized error instead of installing an unrelated ZIP, data pack or mirror.

The chosen `CatalogLink` is passed to the *existing*
`FullCatalogScreen::linkAction_` callback, which reaches
`InstallController::requestInstall` in `source/main.cpp`.
This keeps the same progress, cancellation, installation, receipt tracking
and memory-release behavior as ordinary catalog links.

UPDATE deliberately does **not** run Install All: data and game files can
be large or locally customized and should not be downloaded again merely
to update a VPK. Every link remains separately selectable in the usual UI.

## OPEN

Only `LocalInstallState::Installed` enables OPEN. In the UI/main
thread, the launcher validates the nine-character alphanumeric Title ID,
checks `ux0:app/<TITLEID>/sce_sys/param.sfo` exists, then reuses the
updater's proven launch mechanism:

```cpp
sceAppMgrLaunchAppByUri(0xFFFFF, "psgm:play?titleid=<TITLEID>");
sceKernelExitProcess(0); // only if launch request succeeds
```

If the launch returns an error or the title is missing, PSVitaAlive stays
open and displays a localized error. The existing updater code is not
modified, nor is a launch ever attempted from a background worker.

## Navigation and languages

The primary action uses the existing Install All rectangle, selection
offset, scrolling and touch handling. OPEN/UPDATE also appear for an
eligible installed Homebrew even when it offers no Game/Data Files.
The Select Links / Request Data header positioning is adjusted to avoid
overlap when there are no normal links.

All eight bundled languages supply six new keys:

- `PRIMARY_UPDATE`
- `PRIMARY_OPEN`
- `PRIMARY_UPDATE_SUBTITLE`
- `PRIMARY_OPEN_SUBTITLE`
- `PRIMARY_VPK_UNAVAILABLE`
- `PRIMARY_LAUNCH_FAILED`

## Manual tests (PS Vita and Vita3K)

1. NotInstalled with Game/Data Files → INSTALL ALL wizard still works.
2. UpdateAvailable and recommended Download VPK → UPDATE runs that VPK
   only, preserving installed data files.
3. UpdateAvailable and no recommended VPK → first Download VPK fallback.
4. UpdateAvailable with only ZIP, Mirror or no VPK → localized warning,
   no unrelated file is installed.
5. Installed and verified current → OPEN launches the installed Homebrew
   and exits PSVitaAlive as the updater does.
6. InstalledUnknown → not falsely labelled as up-to-date.
7. Confirm navigation with D-pad and Cross as well as touch, including
   applications without other downloadable links.
8. Reopen PSVitaAlive after updating and confirm its install receipt
   makes the button switch from UPDATE to OPEN when the state is verified.
9. Check all eight translations for legibility, and ensure official
   PS Vita / PSP / PS1 catalog actions are unchanged.
10. Build with VitaSDK CI, then verify launches on a physical PS Vita.

No RAM improvement or launch reliability claims are made without
hardware validation.

# Reliable Homebrew Update Detection

## Fixed behavior

- `APP_VER` mismatch alone never shows **Update available** under the default `sfo_policy=fallback`.
- `InstalledUnknown` represents an installed application whose exact release cannot be proven (nightly, fork, stale/unknown SFO, etc.).
- Successful PSVitaAlive VPK installs write a receipt to `ux0:data/psvitaalive/installed/<TITLE_ID>.json`.
- Main evidence order remains: verified receipt → fingerprint → SFO according to policy.
- A newer local release must never be offered an older catalog package: ambiguous/newer evidence resolves to `InstalledUnknown`, not `UpdateAvailable`.

## Phase-1 receipt compatibility

Current PSVitaAlive receipts already preserve the catalog version that PSVitaAlive installed, even when the receipt does not yet contain a live installed-file fingerprint digest.

While an app has no `update_detection` fingerprint metadata, that PSVitaAlive-managed receipt is accepted as positive evidence of the last release installed by PSVitaAlive:

```text
receipt 1.7.1
catalog 1.7.2
→ UpdateAvailable
```

This compatibility path is deliberately limited to the phase where `update_detection` metadata is absent. Once catalog fingerprints are available, unverified receipts do not bypass the strict fingerprint rules.

A reliable SFO that proves the installed version is already current or newer still takes precedence over an older/stale phase-1 receipt, preventing accidental downgrade prompts.

## Vita APP_VER semantic encoding

Some Vita projects encode a semantic `X.Y.Z` release into the SFO `APP_VER` field's `XX.YY` format by using `XX.YZ`.

Example used by Golden Balloon:

```text
Catalog version 1.7.1 → APP_VER 01.71
Catalog version 1.7.2 → APP_VER 01.72
Catalog version 1.7.3 → APP_VER 01.73
```

The update decision layer therefore compares a Vita-encoded `APP_VER` against a semantic three-part catalog version using that encoding when it is unambiguous. It must not interpret `01.71` as semantic `1.71` when comparing it with `1.7.2`.

Expected behavior:

```text
receipt 1.7.1 + APP_VER 01.71 + catalog 1.7.2
→ UpdateAvailable

APP_VER 01.72 + catalog 1.7.2
→ Installed

APP_VER 01.73 + catalog 1.7.2
→ InstalledUnknown (never downgrade)
```

Host-side regression tests cover the Golden Balloon case and the current/newer Vita-encoded SFO cases.

## Build

Use this tree's `Client PSVitaAlive` sources, especially `source/update/update_decision.cpp` for the decision matrix and `source/ui/full_catalog_screen.cpp` for UI integration.

## Catalog card presentation

Update detection and card presentation remain separate concerns. `queryLocalInstall()` / `decideInstallState()` determine the state; the catalog renderer only presents that result.

- `Installed` → one localized `BADGE_INSTALLED` badge at the top-right.
- `InstalledUnknown` → the same installed badge on the card; detailed metadata keeps the version-unknown explanation.
- `UpdateAvailable` → one localized `BADGE_UPDATE` badge at the top-right with a subtle orange breathing glow.
- `NotInstalled` / `Unknown` → no card badge.

The previous second badge over the lower-left icon corner and the hardcoded `ON` / `UPD` abbreviations were removed. Card title width reserves the badge area, and long localized text uses clipping/marquee rather than overlapping app information.

### PSP/PS1 mount labels in 02.17

The installed-game index now associates recognized PSP ISO/CSO and PSP/PS1 PBP IDs with their detected mount. An installed badge can display the localized Installed text plus `ux0`, `uma0`, `imc0` or `xmc0` when available. It reuses the existing constrained badge width and marquee; LiveArea detection remains a separate path.

- `ui/installed_game_index.hpp` provides `locationFor(catalogType, primaryId, alternateId)` for supported PSP/PS1 layouts.
- A completed PSP/PS1 installation causes a **deferred** index invalidation when the result dialog closes, not a device scan on every frame.
- **Presence does not prove an installed game version.** Commercial catalogs continue to represent installed status, not commercial update availability.

### Badge marquee clipping contract

Localized labels such as Spanish `ACTUALIZACIÓN`, Italian `AGGIORNAMENTO`, Russian `ОБНОВЛЕНИЕ`, etc. may exceed the badge's inner width and therefore use the existing horizontal marquee.

The marquee must **never widen the caller's clipping region**. `drawInstallBadge()` receives the current card/detail clipping rectangle and renders using the intersection of:

```text
badge bounds
∩ card/detail bounds
∩ catalog/detail panel bounds
```

This is especially important during smooth vertical scrolling, when a card can be partially outside the catalog viewport. The badge background, glow and moving text must disappear at the panel boundary instead of painting into the header, tabs, footer or another panel.

After the badge marquee temporarily tightens the scissor, `drawInstallBadge()` restores the caller-owned clip before returning. The existing defensive clip re-assertions in card/detail rendering remain safe.

Do not replace this behavior with full-screen clipping and do not remove localization merely to avoid marquee overflow.

## Regression checklist

For update detection:

1. PSVitaAlive receipt `1.7.1`, catalog `1.7.2`, unreliable SFO → `UpdateAvailable`.
2. PSVitaAlive receipt `1.7.1`, `APP_VER=01.71`, catalog `1.7.2` → `UpdateAvailable`.
3. `APP_VER=01.72`, catalog `1.7.2` → `Installed`.
4. `APP_VER=01.73`, catalog `1.7.2` → never `UpdateAvailable`.
5. Receipt newer than catalog → never downgrade.
6. Once `update_detection` metadata exists, an unverified receipt stays non-authoritative.

For catalog rendering:

1. Use a language whose `BADGE_UPDATE` is wider than the badge inner area.
2. Focus an app with `UpdateAvailable` so the badge marquee is active.
3. Scroll the card partially through the top and bottom catalog boundaries.
4. Confirm background, glow and marquee text remain inside the panel.
5. Repeat in full catalog and split-detail modes.
6. Confirm short `Installed`/`Update` labels and normal title marquee behavior are unchanged.

## Catalog card metadata in version 02.17

Cards show only `v<version>` in the bottom-left when the catalog contains a version; they no longer append `versionDate`. Size and Game Files/Data Files chips remain on the right. The **detail panel retains the version date**, and catalog date sorting is unchanged.

This display change does **not** change Homebrew release receipts, Vita `APP_VER` interpretation, catalog matching or `UpdateAvailable` decisions. [02.17 technical notes](RELEASE_02_17_TECHNICAL_20261028.md).

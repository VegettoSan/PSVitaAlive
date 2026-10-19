# Game Files / Data Files ZIP dispatch fix — 2026-10-19

## Incident and root cause

Discord install failure for **Blasphemous Vita** (`BLPM12345`, client `v02.12`):
`Game Files.zip` (about 817 MB) was downloaded, but the log showed
`FormatDetector: format=Vpk extension=zip` and `installVpk BEGIN`.
After extraction, `ux0:data/psva_vpk/eboot.bin` and
`sce_sys/param.sfo` were missing; the installer retried five times
and reported an invalid VPK layout.

The canonical `apps/blasphemous-vita.json` **already** had a correct
`Game Files` link with `extract_path: "ux0:app/"`. The defect was in
runtime dispatch, not the catalog. `FormatDetector` inspects the first
ZIP entries for VPK-looking paths; data archives can also contain those
names. `InstallDispatcher` incorrectly checked the inferred VPK format
*before* the explicit ZIP destination, so even a correctly configured
data archive could enter `HomebrewInstaller::installVpk()`.

## New routing contract

1. The **link type** `Game Files` or `Data Files` (case-insensitive,
   also accepts singular and whitespace/underscore/hyphen variants)
   identifies a **ZIP data extraction**, never a LiveArea/VPK install.
2. The catalog's `extract_path` remains authoritative. If it is absent,
   the existing destination picker is used before downloading, even
   when the downloaded URL does not end in `.zip`.
3. `InstallController::requestInstall` refuses data/game ZIP requests
   lacking a destination, instead of downloading then promoting a VPK.
4. `InstallDispatcher` prioritizes ZIP extraction **only** for
   `Game Files` / `Data Files` types with a destination. For those
   link types, a `.zip` filename wins even if the content detector
   returns `FileFormat::Vpk`. Typed data/game files with opaque
   filenames also extract when ZIP/VPK container magic is recognized.
   A typed data/game file named `.vpk` or with unrecognized
   content fails as unsupported; it is **never promoted**.
5. ZIP extraction uses the existing `ZipExtractor`, preserving its
   path safety checks, progress reporting, ZIP64 handling, failure
   reporting, and destination-folder policy. No content is flattened or
   relocated by this routing fix.
6. ZIP manual recovery eligibility includes VPK-looking ZIPs **only
   for these two typed data links**, keeping the previous eligibility
   for all other link types.
7. Ordinary explicit `.vpk` downloads still use
   `HomebrewInstaller`. ZIPs with no destination from other link
   types retain the historical ZIP-backed VPK/release-archive fallback
   for compatibility; that fallback is **never available** to
   Game Files / Data Files. ZIPs with a configured destination on other
   link types retain the original format-detection precedence.

### Blasphemous expected path

```text
apps/blasphemous-vita.json
  Download  -> blasphemous-vita.vpk -> HomebrewInstaller
  Game Files -> Game Files.zip      -> ZipExtractor
                                       destination ux0:app/
                                       archive contains BLPM12345/...
```

Do **not** set the destination to `ux0:app/BLPM12345/` when the
archive already contains `BLPM12345/`; doing so would duplicate
the title directory.

## Files changed

- `Client PSVitaAlive/include/installer/install_archive_policy.hpp`:
  shared lightweight type/destination dispatch rules (standard C++ only).
- `Client PSVitaAlive/source/main.cpp`: choose/preserve extraction
  target based on link type, not extension alone.
- `Client PSVitaAlive/source/installer/install_controller.cpp`:
  reject missing destination for typed data, pass type to dispatcher,
  and retain ZIP recovery for VPK-looking ZIPs.
- `Client PSVitaAlive/include/installer/install_dispatcher.hpp` and
  `Client PSVitaAlive/source/installer/install_dispatcher.cpp`:
  add optional typed-ZIP parameter and prioritize explicit ZIP
  extraction over VPK magic.
- `tests/test_install_archive_policy.cpp`: host-only regression tests.
- `docs/README.md`: documentation index entry.

## Verification

Host regression test (no VitaSDK required):

```sh
g++ -std=c++11 -Wall -Wextra -Werror \
  -I"Client PSVitaAlive/include" \
  tests/test_install_archive_policy.cpp \
  -o /tmp/test_install_archive_policy
/tmp/test_install_archive_policy
```

Cases: Blasphemous `.zip` with VPK-looking magic and an extraction
destination, no-destination rejection contract, typed ZIP with opaque
URL, normal VPK non-extraction, case-insensitive link types, **and
explicit preservation of old behavior for non-Game/Data links**.

**CI automation:** `.github/workflows/client-build.yml` compiles and runs
the host-only dispatch policy test before the VitaSDK build.

**Device validation still required:** build with VitaSDK and check
`Install All` plus individual links for Blasphemous; confirm logs show
ZIP extraction to `ux0:app/`, not `installVpk BEGIN`, and inspect
`ux0:app/BLPM12345/`. Also retest a regular VPK, a `Data Files`
ZIP targeting `ux0:data/`, a ZIP without `extract_path` (picker),
and a deliberately corrupt ZIP to exercise manual recovery.

## Scope

Do not edit the generated `catalog.json`. No schema changes, no
additional dependencies, and no changes to network/TLS/retry policy.
This fix does not independently verify the remote archive's integrity.

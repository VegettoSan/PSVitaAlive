# Install All: combined ux0: storage preflight (2026-10-25)

## Bug fixed

The previous Install All wizard only logged an estimated aggregate size. It did
not check `ux0:` free bytes before the first download. It also substituted the
primary app's `item.size` for secondary Game Files / Data Files links with
missing `link.size`, producing incorrect totals.

## New budget and guard

At the end of Install All selection, before any requestInstall():

- Primary app download bytes = selected Download link's `size` when available,
  otherwise the top-level app `size`.
- Largest extra = maximum known `size` among *all* Game Files and Data Files
  catalog links, including alternative choices.
- Selected extra total = sum of the selected Game Files and Data Files sizes,
  in case both distinct extras are queued and installed.
- Plugin bytes = sum of known sizes for the explicitly selected plugins.
- Combined base = app bytes + max(largest extra, selected extra total)
  + selected plugin bytes (saturating uint64 arithmetic).
- Required space = maximum of
  (combined base + 25% + 128MiB reserve) and
  (2.1x primary app bytes + extras + plugins + 128MiB reserve).

A fresh on-demand `StorageManager::queryUx0Space()` measures free bytes. If
free < required, **no job is started** and the wizard shows a persistent
localized Space Warning with required/free/deficit and a Close button. If the
probe fails, it also blocks Install All instead of blindly assuming free space.
After freeing storage, reopen Install All to recalculate.

This policy is stricter than the bare primary + largest extra requirement when
both Game and Data Files are selected (both will occupy storage). It retains
the older VPK transient-install budget and only affects Install All preflight;
single-item VPK/ZIP/PKG/plugin checks are unchanged.

## Important limitations

A catalog `size` describes a download, not the uncompressed size of a ZIP.
No pre-download estimate based on compressed `size` alone can guarantee enough
space for extraction. The existing ZipExtractor's exact uncompressed-size
precheck and DownloadManager runtime free-space monitoring remain mandatory.

When a legacy item does not declare `size` metadata, the estimate uses only
known sizes; it cannot prove sufficiency for missing sizes. Catalog and
importer validators should encourage declared per-link byte sizes. The
client must not reuse the primary app size as a fabricated size for extras.
No generated catalog or JSON schema changes are required.

## Test coverage

`tests/test_install_space_policy.cpp` checks max-extra selection, both-extra
sum, VPK staging floor, plugin payload, zero/unknown and uint64 overflow. The
existing CI job compiles and executes it with host C++ and then VitaSDK compiles
the full client. Real-hardware verification with large ZIPs remains advisable.

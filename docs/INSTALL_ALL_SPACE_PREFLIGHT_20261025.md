# Install All: combined ux0: storage preflight (2026-10-25)

## Bug fixed

The previous Install All wizard only logged an estimated aggregate size. It did
not check `ux0:` free bytes before the first download. It also substituted the
primary app's `item.size` for secondary Game Files / Data Files links with
missing `link.size`, producing incorrect totals.

## New budget and guard

At the end of Install All selection, before any requestInstall():

- Each selected link (automatic or manually chosen) contributes its declared
  `size` *when it actually enters the Install All queue*. Unselected
  alternatives contribute nothing.
- Primary app bytes = size of the queued Download link. Only when it matches
  the application's primary download URL can its missing size fall back to the
  top-level `item.size`. No VPK queued means zero primary app bytes.
- Extra bytes = sum of the *queued* Game Files and Data Files links. When both
  are included, both contribute; alternative mirrors not selected do not.
- Plugin bytes = sum of explicitly approved and queued plugin sizes.
- Combined base = app bytes + queued extra bytes + queued plugin bytes
  (saturating uint64 arithmetic).
- Required space = maximum of
  (combined base + 25% + 128MiB reserve) and
  (2.1x primary app bytes + extras + plugins + 128MiB reserve).

When every queued link has a size, a fresh on-demand
`StorageManager::queryUx0Space()` measures free bytes for the combined
budget. If free < required, **no job is started** and the wizard shows the
localized Space Warning with required/free/deficit. A failed capacity probe
also blocks this initial combined check.

**If any selected link lacks a usable size**, the entire batch switches to
individual preflight. The existing `InstallController` policy checks known
sizes immediately before each step. For both known and unknown sizes, the
existing `HttpClient` header handler now forwards a final HTTP 200/206
`Content-Length` / `Content-Range` total to `DownloadManager` **before
the first payload byte is written**. `DownloadManager` reuses the
`preflightRequiredSpaceBytes` policy and actual `ux0:` availability;
insufficient space aborts the transfer and reports a normal space error.
A successful adaptive 206 Range probe likewise checks space before starting
the 2/4-part downloader. MediaFire resolution and provider redirects remain
within the existing paths, with no additional HEAD/GET round-trip.

If a server omits authoritative lengths, no pre-body size guarantee is
possible; the existing 32 MiB runtime checks and minimum 16 MiB reserve stay
active. A catalog size remains a hint, never a substitute for actual HTTP
lengths when present. An HTTP 200 replacement of a resumable partial file
does not credit the old partial's disk usage.

This supersedes the earlier "largest extra across the whole catalog" estimate,
which overestimated installs using smaller manual/automatic selections. It
retains the VPK transient-install budget. Only Install All's preflight changes;
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

`tests/test_install_space_policy.cpp` checks automatic/manual selections,
unselected alternatives, both-extra sum, no-app queues, VPK staging floor,
per-file HTTP size budgets, resumed-download credit, approved plugins,
zero/unknown sizes and uint64 overflow. The
existing CI job compiles and executes it with host C++ and then VitaSDK compiles
the full client. Real-hardware verification with large ZIPs remains advisable.

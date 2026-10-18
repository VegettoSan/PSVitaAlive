# Adaptive downloads, ZIP extraction and progress on PS Vita

**Date:** 18 October 2026  
**Scope:** PSVitaAlive native in-app downloads and installation. The system BGDL package manager is a separate path.

This maintenance note captures technical decisions, observed bottlenecks and real-hardware testing from the client's iterative development. It complements [download resilience](DOWNLOAD_RESILIENCE.md), [TLS behavior](NETWORK_TLS.md), the [native network module](../Client%20PSVitaAlive/source/network/README.md) and the [ZIP/VPK installer](../Client%20PSVitaAlive/source/installer/README.md).

## 1. Goals and architecture

The transfer strategy depends on **server capabilities and remote size**, not on whether a payload ends in `.vpk`, `.zip`, `.pkg` or another extension. Preserve old-homebrew compatibility, cancellation, provider recovery, 64-bit sizes, ZIP64 support and low memory use.

```text
Catalog link / user-selected source
  └─ InstallController worker: PSVitaAliveInstall
       └─ DownloadManager: provider URL, metadata, space, retries
            └─ HttpClient (libcurl)
                 ├─ serial GET + buffered writes + safe resume
                 └─ eligible adaptive byte ranges
                      ├─ two or four sequential part files
                      ├─ HTTP status, Content-Range and identity checks
                      └─ ordered assembly + integrity verification
            └─ validated payload.part → final job filename
       └─ InstallDispatcher / FormatDetector
            ├─ ZIP → ZipExtractor → chosen extract_path
            ├─ VPK → ZipExtractor → fake package → Promoter → LiveArea
            └─ other supported installer paths
```

Network and installation work already execute in a dedicated worker; spawning a second downloader thread is **not** a general fix for UI stalls. Remaining contention may occur during storage I/O, ZIP extraction, validation or rendering.

## 2. Adaptive size policy

| Authoritative remote size | Candidate transport | Notes |
|---|---|---|
| Under **1 MiB** | Single GET, bounded serial write buffer | Parallel setup usually costs more than it saves |
| **1 MiB** to under **8 MiB** | **2** ranges, where validated | Suitable for small and medium VPKs |
| **8 MiB** and above | **4** ranges, where validated | Intended for larger VPKs, ZIPs and data files |
| Unknown size / incompatible provider | Single GET / existing resumable retry | No forced multi-range behavior |

These thresholds are heuristics. Catalog sizes are **hints**, never authoritative. A small or large extension by itself does not determine the transfer method.

Requirements for range mode:

1. Fresh installation payload (`resumeOffset == 0`), normal retry configuration; existing image-cache and resume paths remain separate.
2. An initial bounded **one-byte** probe confirms `HTTP 206` and `Content-Range: bytes 0-0/TOTAL` (or an authoritative selector result for Archive.org).
3. Strict per-part `206`, exact start/end/total, received byte count and stable response validators. Never concatenate a `200` error page or mismatched range.
4. For non-Archive sources, the effective URL must be **HTTPS** and the response must provide a strong ETag or Last-Modified. Parts must retain the expected validator(s). No generic HTTPS→HTTP downgrade.
5. For Archive.org's optional direct **HTTP** path, an exact file SHA-1 from item metadata is required and checked against the complete assembled payload. Archive HTTPS transfers also check the SHA-1 when available.
6. On range/probe inconsistency, discard staging parts and use the existing serial fallback. On cancellation, cleanup and retain the Cancelled state.

Using Last-Modified alone is weaker than a cryptographic digest; it is not a guarantee against a server that changes content while preserving timestamps.

## 3. Buffering, storage and integrity

- Each parallel stream writes to its **own ordered part file** using a **256 KiB** buffer, not a sparse/random `ux0:` file. Four active streams use approximately 1 MiB for part buffers.
- Ordered assembly copies additional part files using a **256 KiB** read/write buffer. The first range is already in the target path.
- When an expected SHA-1 exists, hashing happens during ordered assembly to avoid a redundant full-file read. A mismatch rejects the result and triggers fallback.
- Before starting parallel transfers, the client checks staging capacity including extra part files plus a reserve. The serial path and install pipeline keep their own size/space checks.
- All serial downloads use a **256 KiB** write buffer when allocation succeeds, with direct `sceIoWrite` fallback on buffer allocation failure. Buffered tails are flushed before retry/resume offset calculation and size validation.
- Resume uses `CURLOPT_RESUME_FROM_LARGE` to handle >2 GiB offsets. `200` on a resumed request means clean restart, not duplicate append. Invalid `206`, stale `416`, interrupted downloads and provider-specific expired URLs follow bounded retry logic.
- Download metadata is checkpointed approximately every **8 MiB**; free-space checks run approximately every **32 MiB** while downloading.

### Security qualification

The SHA-1 exposed by Archive.org metadata is a **legacy integrity consistency check**, not a modern authentication guarantee. The PS Vita TLS configuration may disable certificate/hostname verification due to device-side TLS limitations; this further limits authenticity. Do not describe plain HTTP or checksum-matched data as protected from a malicious network intermediary. Prefer stronger independently trusted digests and verified TLS as future improvements.

## 4. Archive.org node selection

Archive metadata provides candidate storage hosts (`server`, `d1`, `d2`, and when available `workable_servers`). The client validates hosts and limits the number of candidates. For larger payloads, bounded **256 KiB** speed probes rank responsive effective hosts, prioritizing body throughput (TTFB as a tie-breaker). The **16 MiB** selector threshold controls **whether to benchmark nodes**, not the 1/8 MiB rule for two/four-range transfer.

When checksums and a valid storage endpoint permit it, the selected Archive.org node may use direct HTTP for range payloads, with complete-file SHA-1 verification. Otherwise the client retains HTTPS and the established Archive fallback/alternate-node strategy. Redirects must lead to allowed Archive hosts; invalid ranges trigger fallback to serial.

Avoid running expensive 256 KiB speed samples for tiny files: start from a responsive storage endpoint. Provider metadata/selection is skipped for image-cache transfers and safe resume jobs as designed.

## 5. Progress UI and installation stages

- **Downloading:** percent, bytes and transfer speed, plus estimated time when total size is known.
- **Verifying:** once all ranges arrive, the UI shows “Joining parts and checking file integrity…” with animated/indeterminate progress; it does not pretend that 99% means a stuck network connection. Report transfer completion only after a successful merge/check.
- **Extracting:** `ZipExtractor` reports processed **uncompressed bytes** and total, throttled near **250 ms**; `InstallController` computes an approximate rate-based ETA after enough data/time is observed. Estimates may vary with compressed files and many small entries.
- **Promoting:** the Sony Promoter API does not expose a reliable byte-based remaining time. The UI displays **elapsed time**, not a fabricated ETA.
- **Completed / Failed / Cancelled:** maintain explicit state, resource cleanup and normal return to the catalog. Strings for the new phases are available in English and Spanish; other UI languages use the existing English fallback.

The full pipeline remains asynchronous relative to the main render loop, except the established separate BGDL enqueue handoff on the main thread.

## 6. ZIP/VPK throughput optimizations

`zip_extractor.cpp` retains **libzip**, the custom 64-bit `sceIo` source and ZIP64 compatibility; extract buffer size is **256 KiB**. Parent-directory creation avoids repeated checks for consecutive siblings by remembering the last parent. Routine per-entry logs are sampled on ZIPs with many small files; error reports remain detailed. Progress reports are time-bounded and cancellation remains checked each extraction chunk.

Neither these optimizations nor multi-range downloads relax safe archive entry paths, ZIP consistency checks, free-space limits, error detection or VPK package promotion.

## 7. Observed real-device test results — 18 October 2026

The following timestamps come from a PS Vita `session.log` of the validated **four-range** build. They include local assembly/verification but **exclude** initial Archive metadata/node selection. Times are rounded and should not be treated as controlled cross-network benchmarks.

| Payload | Parallel transfer start → merged & validated | Network complete → merged & validated | Extraction |
|---|---:|---:|---:|
| Conker Game Files ZIP, 67,109,198 bytes | ~**90.1 s** | ~**23.5 s** | ~**16.3 s** (2 entries) |
| Spider-Man Game Files ZIP, 170,809,499 bytes | ~**203.2 s** | ~**60.4 s** | ~**139.4 s** (635 entries) |
| Spider-Man VPK, 3,476,547 bytes (previous serial mode) | ~**19.0 s** for actual HTTPS GET, excluding selection | No range merge | 8 entries, successfully promoted |

The same log confirmed `[System] diagnostic logger initialized (async, bounded, session.log reset)` and successful completion of all three install/extract operations. The UI performance logs still show occasional multi-second stalls during intense storage activity. The later **adaptive** test build was judged satisfactory by the maintainer on hardware; precise improvements across other CDNs and smaller payloads remain to be benchmarked independently.

## 8. Regression tests and useful log markers

Before adjusting size thresholds or buffer sizes, verify: under-1-MiB serial, 1–8-MiB two-part VPK, larger four-part ZIP, >2-GiB archive/ZIP64, valid/invalid `Content-Range`, changed ETag/Last-Modified, missing Range support, wrong SHA-1, failed final file size, canceled transfer, low storage, HTTP redirects, MediaFire expired URL recovery, GitHub Releases, interrupted resume, nested VPK and both STORE/Deflate archives.

Also check progress UI transition from Downloading to Verifying to Extracting/Promoting/Completed; ZIP ETA remains approximate, and Promoter uses elapsed rather than remaining time. Compare PS Vita and Vita3K behavior where applicable.

Useful `session.log` entries:
```text
[System] diagnostic logger initialized (async
HTTP archive selector probe
[HTTP] starting 2-range parallel payload download
[HTTP] starting 4-range parallel payload download
[HTTP] parallel transfer complete; joining and verifying parts
[HTTP] parallel payload validated and joined
[HTTP] parallel ranges unavailable; falling back to serial
[ZipExtractor] extracted
[Installer] PromotePkg
[Perf] 10s summary ... worst_us=...
```

When diagnosing “stuck 99%”, compare the **network completion** and **join/verification** timestamps before blaming transfer throughput. Correlate UI `worst_us` spikes with network, disk and ZIP stages; already running in a worker is not proof of nonblocking device I/O.

## 9. Catalog and release compatibility

These changes affect only the PS Vita client and its build configuration. They do **not** modify the public JSON format, category/author linkage, Title IDs, installation receipts, web UI or system BGDL. Canonical `apps/`, `authors/`, `categories/` and their GitHub Actions-generated outputs remain unchanged.

Review build and hardware logs before claiming reproducible speedups. A successful VitaSDK build validates packaging, not performance on all hardware.

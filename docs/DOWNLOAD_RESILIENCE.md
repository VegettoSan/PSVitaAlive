# PSVitaAlive — Download and archive resilience

This document describes the current resilience rules for **in-app HTTP downloads** and **ZIP/VPK extraction** in the native PS Vita client.

It complements [`NETWORK_TLS.md`](NETWORK_TLS.md) and the installer notes in [`Client PSVitaAlive/source/installer/README.md`](../Client%20PSVitaAlive/source/installer/README.md).

The goal is not to hide transport or archive corruption. The client should either complete a transfer with strong evidence that the payload is coherent, or fail with enough diagnostics to explain what happened.

---

## Scope

The rules below apply mainly to downloads handled by the client process itself:

- Homebrew VPK
- ZIP release archives that contain a VPK
- Data Files / Game Files
- Mods / patches distributed as ZIP
- Direct PSP / PS1 PKG downloads used by the Adrenaline unpack path
- Plugin binaries

Commercial PKG downloads queued through **system BGDL** are a different path. Once BGDL accepts a package, progress and transport are owned by the system download manager rather than the in-app `HttpClient` / `DownloadManager` stack.

---

## Download architecture

```text
catalog link
    │
    ▼
DownloadManager
    │  resolves provider-specific URLs when needed
    │  owns payload.part + metadata.json
    ▼
HttpClient / libcurl
    │  redirects / retries / Range / TLS / transport diagnostics
    ▼
payload.part
    │
    ├─ success + validation → rename to final payload
    └─ cancel / fatal error → cleanup
    │
    ▼
FormatDetector / InstallDispatcher
    │
    ├─ VPK → HomebrewInstaller
    ├─ ZIP → ZipExtractor
    ├─ PSP/PS1 PKG → Adrenaline unpack when selected
    └─ other supported install paths
```

`payload.part` is intentionally separate from the final file. A transfer only becomes the final payload after the download path has completed successfully.

---

## HTTP response isolation

### Header callbacks are length-aware

libcurl header callbacks receive an explicit byte count and do **not** guarantee a trailing NUL byte. The client therefore parses header data using the callback length rather than treating it as a C string.

This matters for correctness and for avoiding accidental reads past the callback buffer.

### Redirect/auth responses do not leak metadata

Every HTTP status line is treated as a new response boundary. Per-response values are reset before parsing the next response:

- `Content-Length`
- `Content-Range`
- ETag
- Last-Modified
- Retry-After
- resume-range state

A `302`, `401`, CDN challenge or other intermediate response therefore cannot contaminate the final `200` / `206` payload metadata.

### HTTP error bodies never become payload data

Only final `200` and `206` response bodies are written to `payload.part`.

Bodies for `4xx` / `5xx` responses are consumed for libcurl bookkeeping and retry classification, but discarded instead of being appended to the file. This prevents HTML error pages or CDN diagnostics from turning into a corrupt VPK/ZIP.

---

## Retry policy

The transfer layer treats these HTTP responses as transient and eligible for retry:

```text
408  Request Timeout
425  Too Early
429  Too Many Requests
500  Internal Server Error
502  Bad Gateway
503  Service Unavailable
504  Gateway Timeout
520  Cloudflare/Web-server unknown error
521  Web server is down
522  Connection timed out
523  Origin is unreachable
524  Timeout occurred
```

`Retry-After` is parsed when present and capped to a short bounded delay suitable for the Vita client.

Network-level retry candidates include connection, DNS, timeout, send/receive, partial-file and TLS-connect failures.

The client does **not** retry forever. Internal libcurl retries are bounded, and `DownloadManager` adds only a small outer retry layer so the UI can surface a clear retry state and provider-specific recovery can run.

---

## Long-transfer connection handling

Multi-gigabyte transfers are especially vulnerable to half-open Wi-Fi/NAT connections.

The client enables TCP keepalive and, when supported by the linked libcurl, configures keepalive idle/interval values so dead connections can be detected earlier.

Low-speed protection is also enabled. Archive.org receives a more patient low-speed window because very large public-archive transfers can pause temporarily without being permanently dead.

After serious transport/TLS failures the client can force a fresh TCP/TLS connection instead of repeatedly reusing the same broken connection/session.

---

## Responsive cancellation

Cancellation is checked in both places:

1. the write callback, while payload bytes are arriving;
2. libcurl's transfer-progress callback (`XFERINFO`), even when the server is connected but currently sending no body data.

This avoids the old failure mode where CIRCLE could be pressed during a stalled connection but the transfer could not observe the cancellation until another body chunk arrived.

---

## Protocol restrictions

Initial download URLs must begin with:

```text
http://
https://
```

libcurl is also restricted so redirects remain inside HTTP/HTTPS.

Catalog URLs are external input, so the downloader must never follow a redirect into another protocol merely because the linked libcurl happens to support it.

---

## Resume rules

### 64-bit offsets

Large Game Files can exceed 2 GiB. Resume uses `CURLOPT_RESUME_FROM_LARGE` / `curl_off_t`, not the 32-bit `long` variant.

### Safe HTTP 206 validation

When resuming at offset `N`, an HTTP `206` response is accepted only if its `Content-Range` starts exactly at `N`.

If the server returns an invalid or mismatched range, the body is aborted rather than appended.

### Server ignores Range

If a resume request receives a full `200` response, the client assumes the server ignored Range. The partial file is truncated and the transfer restarts from byte zero.

It never appends a complete response body after an existing partial file.

### HTTP 416

A stale/invalid resume offset can produce `416 Range Not Satisfiable`. If the response exposes the remote total and the local offset cannot safely be accepted, the client performs a bounded clean restart instead of permanently failing the job.

### Validators

ETag and Last-Modified may be persisted with the job. When the same resolved resource is resumed, a validator can be sent through `If-Range` so a changed resource naturally falls back to a full `200` rather than silently combining old and new bytes.

Validators are intentionally cleared when a provider resolves to a new short-lived URL where the old validator is no longer meaningful.

---

## Size integrity

Catalog sizes and provider page sizes are **hints**, not absolute truth.

A hard overrun guard is activated only after the server has provided an authoritative total through `Content-Length` or `Content-Range`.

The current hard limit allows a small safety margin above the authoritative total. If the transfer exceeds that limit, the download is cancelled and the partial file is discarded.

This prevents a malformed retry or wrong response body from growing far beyond the expected payload while avoiding false failures from approximate catalog metadata.

---

## Runtime free-space guard

The installer still performs its normal pre-download free-space estimate, but long transfers also re-check `ux0:` while bytes are being written.

Current runtime policy:

```text
check approximately every 32 MiB downloaded
keep at least 16 MiB free reserve
```

If free space falls below the reserve, the client cancels the job and reports a low-space failure instead of waiting for an opaque `sceIoWrite` error at the very end.

---

## Metadata write throttling

Each job persists resumable state in `metadata.json`.

To reduce unnecessary storage churn during multi-gigabyte downloads, progress metadata is not rewritten for every small progress update. The current persistence step is approximately:

```text
8 MiB of additional downloaded data
```

Terminal states and important transitions are still saved immediately.

---

## MediaFire resilience

MediaFire page URLs are resolved to a direct CDN URL before transfer.

The direct URL is short-lived, but an already downloaded partial file is still valuable. On an outer retry the client therefore:

1. reads the existing partial size;
2. resolves the MediaFire page again;
3. obtains a fresh direct URL;
4. clears validators tied to the expired direct URL;
5. attempts a normal Range resume from the existing byte count.

If the new CDN edge ignores Range or returns a changed resource, the normal resume rules clean-restart safely.

This avoids discarding gigabytes of valid partial data merely because a MediaFire direct URL expired.

---

## Internet Archive resilience

Archive.org is handled as a provider with multiple storage edges.

For a fresh normal payload download, the client can query:

```text
https://archive.org/metadata/<identifier>
```

and build alternate download URLs from the metadata `server` / `d1` / `d2` fields. Non-`dn` / non-`.ca` storage nodes are preferred when possible because some Vita OpenSSL 1.0.2 combinations fail against particular Archive.org edges.

Before a large fresh payload starts, the client performs bounded **sequential** Range probes rather than accepting whichever edge the public `archive.org` redirect happens to choose:

```text
1 byte      -> discover authoritative total
< 16 MiB    -> use first responsive direct node; no speed benchmark
>= 16 MiB   -> 256 KiB probe per candidate -> rank by measured B/s
real file   -> fastest measured node
failure     -> next ranked node -> remaining failover
```

Probe bytes are discarded and never enter `payload.part`. Resumed jobs and image/cache requests skip proactive benchmarking. If selection cannot establish a usable direct node, the canonical Archive URL and the previous failure-driven recovery remain intact.

Failover can still be triggered by more than explicit certificate errors. It also covers transport-like failures such as:

- connect failure
- timeout
- receive/send error
- empty response
- partial file
- server-side `5xx`

The original URL remains the source identity; the alternate host is only a transport recovery path.

See [`NETWORK_TLS.md`](NETWORK_TLS.md) for TLS-specific details.

---

## ZIP / VPK archive resilience

### Pre-open completeness check

Before extraction the installer checks for a plausible ZIP beginning and an EOCD / ZIP64 end marker near EOF.

A transfer that was truncated by suspend, force power-off or network loss can therefore fail before libzip starts extracting files.

### Large archive source

Large archives use a seekable `sceIo*`-backed libzip source with Vita file offsets suitable for multi-gigabyte files. The archive and individual entries are streamed rather than loaded wholly into RAM.

### Compression support is checked before extraction

For every ZIP entry with a known compression method, the client asks the linked libzip build whether decompression of that method is supported.

Unsupported methods fail before the extractor starts creating output for the archive, rather than being discovered halfway through a large extraction.

### Aggregate-size overflow and free-space checks

The extractor sums uncompressed sizes carefully and rejects arithmetic overflow.

Available `ux0:` space is compared against the expected extracted size plus a filesystem margin before extraction begins.

### Path safety

Archive entries are rejected when they attempt unsafe destinations such as:

- `..` traversal
- absolute paths
- mount-colon paths embedded in an archive entry

### Partial output cleanup

If `zip_fread` or `sceIoWrite` fails, the partial output file is removed.

When size metadata is available, extracted byte counts are checked against the uncompressed size reported by `zip_stat`.

The extractor also checks close/finalization errors (`zip_fclose`, Vita file close, archive close) instead of assuming that a successful read loop guarantees a complete file.

---

## Storage settle before install

After a download is renamed to its final payload, the installer gives slower SD2Vita / USB-backed storage a short settle period and touches the final file size before opening the archive/install path.

This reduces false incomplete-archive reports where the end-of-file metadata or final blocks are still being materialized by slow storage.

The settle step is not a substitute for integrity validation; it only reduces storage-timing races.

---

## Install retry boundary

Download retries and install/extraction retries are separate.

The installer may retry transient VPK/ZIP preparation failures a limited number of times. Integrity-class failures such as missing EOCD / ZIP64 are handled more conservatively than generic promoter/I/O failures.

A corrupt payload is never made acceptable simply by retrying extraction indefinitely.

---

## Job safety on the console

In-app transfers are process-bound. While the client is downloading/installing/extracting it uses:

- keep-awake power ticks;
- OLED dim/off suppression;
- PS button lock;
- soft power-off menu lock.

These protections are released on Completed / Failed / Cancelled / shutdown.

A long hardware force power-off cannot be blocked. Users should not force power-off during an active in-app job.

---

## Diagnostics

Primary logs:

```text
ux0:data/psvitaalive/logs/session.log
ux0:data/psvitaalive/logs/install.log
```

Useful network markers include:

```text
response status=...
content-range start=... end=... total=... resume=...
content-range MISMATCH ...
discarding non-payload response body status=...
resume validator attached via If-Range
server ignored Range (HTTP 200); restarted download from zero
range fallback ...
MediaFire re-resolved resume_offset=...
low-space guard ...
archive failover built N alternate URL(s)
archive failover switch HTTP=... -> ...
archive failover switch curl=... -> ...
RESULT curl=... status=... bytes=... total=... effective_url=...
```

Useful ZIP/install markers include:

```text
no EOCD/ZIP64 marker near end
unsupported ZIP compression method=...
entry uses compression method=...
storage settle size=...
retrying ...
```

When a rare field report occurs, preserve the full session log whenever possible. A single final error line can hide the retry/failover sequence that explains the real cause.

---

## Validation checklist

When changing `HttpClient`, `DownloadManager`, `ZipExtractor` or the install controller, test at least these cases:

- normal GitHub VPK download;
- HTTP redirect chain;
- server that does not provide Content-Length;
- resume from a small partial file;
- resume above 2 GiB;
- server ignores Range and returns `200`;
- mismatched `206 Content-Range`;
- `416` resume fallback;
- transient `5xx`;
- stalled connection + user cancellation;
- MediaFire direct-link expiration and re-resolution;
- Archive.org edge/TLS/transport failover;
- low `ux0:` free space during a running transfer;
- ZIP >2 GiB;
- ZIP with unsupported compression method;
- truncated ZIP with missing EOCD/ZIP64;
- extraction failure mid-entry and partial-output cleanup.

Prefer real-hardware validation for long-transfer and storage timing behaviour. Vita3K remains useful for fast functional checks but does not reproduce every Vita network/storage edge case.

---

## Related files

Implementation:

```text
Client PSVitaAlive/source/network/http_client.cpp
Client PSVitaAlive/source/network/download_manager.cpp
Client PSVitaAlive/source/network/mediafire_resolver.cpp
Client PSVitaAlive/source/archive/zip_extractor.cpp
Client PSVitaAlive/source/archive/format_detector.cpp
Client PSVitaAlive/source/installer/install_controller.cpp
```

Documentation:

- [`NETWORK_TLS.md`](NETWORK_TLS.md)
- [`../Client PSVitaAlive/README.md`](../Client%20PSVitaAlive/README.md)
- [`../Client PSVitaAlive/source/installer/README.md`](../Client%20PSVitaAlive/source/installer/README.md)


## Manual ZIP recovery after extraction failure

This recovery path applies **only to a fully downloaded data ZIP that had an explicit extraction destination**. It is not used for direct VPK installs, PKG, plugins, cancelled downloads or incomplete `.part` files.

If the HTTP download completed successfully but ZIP extraction fails — including a failure before the first entry can be extracted — the client moves the completed archive out of the transient download job when possible:

```text
ux0:data/psvitaalive/manual/<title_id-or-app_id>/<archive>.zip
```

An adjacent `<archive>.zip.info.txt` records the app/title identity, archive path, intended extraction destination and the automatic extraction error. Moving within `ux0:` avoids copying a multi-gigabyte archive.

The failure panel requires an explicit choice:

- **TRIANGLE — Keep ZIP:** leave the archive for manual extraction (for example with VitaShell or another tool).
- **CROSS — Delete ZIP:** delete the recovered archive and recovery metadata so large failed jobs do not become storage trash.
- **SQUARE — Report:** send the normal diagnostic report without closing the recovery decision screen.

When the extractor reports CRC, truncation, EOCD/ZIP64/incomplete-data or size-integrity symptoms, the UI warns that the archive itself may be damaged and that manual extraction may also fail.

A direct `.vpk` remains on the normal VPK/promoter failure path and is cleaned exactly as before. A ZIP treated as a VPK because it has no data-extraction destination is also excluded from this feature. Successful ZIP extraction still cleans its downloaded installation payload automatically.

The client does **not** recursively delete files already written to the chosen extraction destination after a failed extraction: those destinations can contain pre-existing user data, so a blind rollback would be unsafe.

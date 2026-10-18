# `source/network/` — Native client network stack

Networking for the PS Vita client is deliberately split into a low-level transfer layer and a higher-level job/provider layer.

Detailed cross-module behaviour lives in:

- [`../../../docs/DOWNLOAD_RESILIENCE.md`](../../../docs/DOWNLOAD_RESILIENCE.md) — HTTP/download/archive resilience
- [`../../../docs/NETWORK_TLS.md`](../../../docs/NETWORK_TLS.md) — Vita libcurl/OpenSSL and Archive.org TLS/failover notes
- [`../../../docs/ADAPTIVE_DOWNLOAD_PIPELINE_20261018.md`](../../../docs/ADAPTIVE_DOWNLOAD_PIPELINE_20261018.md) — adaptive two/four-range downloads, serial buffering, validator checks and real-Vita measurements

## Main components

| File | Role |
|------|------|
| `parallel_archive_download.cpp` | Bounded two/four-range downloads, ordered file assembly and SHA-1 checks |
| `http_client.cpp` | libcurl lifecycle per request; redirects, retries, TLS settings, Range resume, header/body callbacks, progress/cancel, diagnostics |
| `download_manager.cpp` | Job state, `.part`/final files, resume metadata, provider retries, size/free-space guards, progress events |
| `mediafire_resolver.cpp` | Resolve MediaFire page URLs to short-lived direct CDN URLs and obtain size hints |
| `curl_lifecycle.cpp` | Process-global libcurl lifecycle protection |
| `error_reporter.cpp` | Optional diagnostic/error reporting transport |
| `news_manager.cpp` | News fetch path |

## Download ownership

The network stack owns bytes until the transfer completes successfully:

```text
catalog URL
   ↓
DownloadManager
   ↓
HttpClient
   ↓
payload.part
   ↓  success
final payload
   ↓
installer / extractor
```

A `4xx`/`5xx` HTML body, invalid Range response, cancelled transfer or failed size/free-space guard must never be promoted into the final payload.

## Core invariants

### HTTP response isolation

- Header callbacks use the explicit libcurl byte count; header buffers are not assumed to be NUL-terminated.
- Each HTTP status line resets per-response metadata so redirects/auth responses cannot leak `Content-Length`, ETag, `Content-Range`, etc. into the final payload response.
- Only `200` and valid `206` response bodies are written to `payload.part`.

### Resume integrity

- Resume uses `CURLOPT_RESUME_FROM_LARGE` (`curl_off_t`) for multi-GB files.
- A resumed `206` must begin exactly at the requested offset.
- If a server ignores Range and returns `200`, truncate and restart from zero.
- Stale/invalid Range state may clean-restart in a bounded fallback path; never append ambiguous bytes.
- ETag / Last-Modified may be persisted and reused via `If-Range` only for the same resolved resource.

### Transport resilience

- TCP keepalive is enabled for long downloads.
- Low-speed detection prevents indefinitely dead connections.
- `XFERINFO` keeps user cancellation responsive even while a server is connected but not delivering body data.
- Initial and redirected protocols are restricted to HTTP/HTTPS.
- Serious connection/TLS failures can force a fresh connection/session for the retry.

### Provider recovery

- **MediaFire:** re-resolve an expired direct URL while preserving the existing partial file; attempt safe Range resume against the new URL.
- **Archive.org:** query item metadata and rotate storage edges on TLS, transport-like and server-side `5xx` failures.

Provider recovery must not bypass the generic Range/size integrity rules.

## DownloadManager runtime guards

Current long-transfer policy includes:

```text
metadata.json progress save step: ~8 MiB
ux0: free-space recheck step:      ~32 MiB
runtime free-space reserve:         16 MiB
```

Catalog/provider sizes remain hints until the server exposes an authoritative total through `Content-Length` / `Content-Range`.

Only then is the hard size-overrun guard enabled.

## Retry layers

There are intentionally two bounded layers:

1. `HttpClient` handles connection/TLS/HTTP retries and Archive storage-edge switching.
2. `DownloadManager` adds a small outer retry layer so provider-specific recovery (for example MediaFire URL refresh) and user-visible retry progress can run.

Do not add unbounded retry loops. A permanently bad payload/provider should eventually fail with useful diagnostics.

## Job files

Each in-app download uses a job directory under the client data area, containing:

```text
payload.part
metadata.json
<final payload name>
```

`metadata.json` stores enough state for job diagnostics/resume decisions, including URL, expected/downloaded size, validators and last HTTP status.

Progress metadata is throttled to reduce SD2Vita/memory-card write churn during multi-gigabyte transfers; terminal states are still persisted immediately.

## Cancellation

Cancellation is a normal terminal state, not an install failure.

The network layer checks cancellation from both body-write and transfer-progress callbacks. The higher-level installer should surface the voluntary **Download cancelled** state without generating a false error report.

## Diagnostics

Primary log:

```text
ux0:data/psvitaalive/logs/session.log
```

Useful markers include:

```text
BEGIN url=...
response status=...
content-range ...
discarding non-payload response body ...
server ignored Range ... restarted download from zero
range fallback ...
MediaFire re-resolved resume_offset=...
low-space guard ...
archive failover switch ...
RESULT curl=... status=... effective_url=...
```

For rare failures, inspect the entire attempt sequence rather than only the final result line.

## Related installer behaviour

After a successful in-app download, the installer/extractor is still responsible for format and archive integrity:

- ZIP/VPK completeness pre-check (EOCD / ZIP64)
- supported compression-method check
- large seekable `sceIo` libzip source
- extraction free-space calculation
- path traversal rejection
- partial output cleanup
- close/finalization checks
- post-download storage settle on slower SD2Vita/USB media

See [`../../../docs/DOWNLOAD_RESILIENCE.md`](../../../docs/DOWNLOAD_RESILIENCE.md) and [`../installer/README.md`](../installer/README.md).

## Change rule

When modifying networking:

1. preserve strict payload-integrity rules;
2. prefer provider-specific recovery over weakening generic validation;
3. keep diagnostics detailed enough to classify field failures;
4. update this README plus the relevant document under `docs/`;
5. test normal download, retry, cancellation and resume paths before release.

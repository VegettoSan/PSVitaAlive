# PSVitaAlive — TLS / libcurl notes

This document covers the TLS-specific behaviour of the native client network stack.

For the complete download pipeline — retries, Range validation, MediaFire re-resolution, free-space guards, ZIP/ZIP64 integrity and archive extraction — see [`DOWNLOAD_RESILIENCE.md`](DOWNLOAD_RESILIENCE.md).

## Default stack (production today)

- **libcurl** from VitaSDK (8.x-class package)
- **OpenSSL 1.0.2** Vita port as the normal TLS backend
- App sets `CURLOPT_SSL_VERIFYPEER/HOST = 0` on device because the Vita environment does not provide a modern CA setup that is reliable for this client path

This works for GitHub and many hosts. Some **Internet Archive** storage nodes, especially `dn*` / `.ca.archive.org` edges, can fail TLS handshakes or drop connections on the Vita OpenSSL stack.

The client therefore treats provider failover as a normal resilience mechanism rather than assuming every Archive.org storage edge behaves identically.

---

## Runtime TLS mitigations

The in-app download path applies the following rules:

1. Re-apply Vita SSL defaults on **every transfer attempt**.
2. Clear `CAINFO` / `CAPATH` when using the verify-off device path.
3. Keep a `CURLOPT_ERRORBUFFER` for detailed backend diagnostics.
4. Read `CURLINFO_SSL_VERIFYRESULT` after attempts for logging and classification.
5. Alternate between TLS 1.2 and libcurl/OpenSSL default negotiation across retries instead of permanently forcing one mode.
6. After serious TLS/transport failures, force a fresh connection and avoid reusing the broken SSL session.
7. Keep initial URLs and redirects restricted to **HTTP/HTTPS**.
8. Prefer IPv4 and HTTP/1.1 on the Vita client path because the Vita/Vita3K stack is more predictable there.

Normal successful transfers can still reuse connections. Fresh-connect/no-reuse is a recovery measure, not a permanent setting for every request.

---

## Internet Archive failover

For `archive.org/download/<identifier>/...` URLs, the client can resolve alternate storage hosts from:

```text
https://archive.org/metadata/<identifier>
```

Candidate URLs are built from metadata such as:

```text
server
d1
d2
```

When possible, the client prefers storage nodes that do not look like problematic `dn*` / `.ca` edges.

### Proactive node selection for fresh payloads

For a **fresh payload download** handled by the normal installer path, Archive.org is no longer treated only as failure-driven failover:

1. resolve `server` / `d1` / `d2` once from item metadata;
2. issue a one-byte Range probe to a direct storage node to learn the authoritative file total;
3. for files smaller than **16 MiB**, use the first responsive direct node without a speed benchmark;
4. for files of **16 MiB or larger**, probe each direct candidate **sequentially** with a bounded **256 KiB** Range request;
5. rank successful candidates by measured bytes/second and start the real transfer on the fastest one;
6. keep the remaining ranked candidates as the existing failover order.

The selector is intentionally skipped for resumed downloads and for small image/cache requests that use an explicit retry override. This avoids invalidating resume validators and prevents catalog artwork from paying metadata/benchmark overhead.

Each probe has short connect/total timeouts and never writes probe bytes to the destination file. If metadata, the size probe or all speed probes fail, the canonical `archive.org/download/...` URL remains the first real transfer and the existing failover logic is preserved.

### Failover triggers

Archive failover is broader than certificate errors alone.

It may rotate to another storage host after:

- direct SSL connect / certificate errors;
- connect failure;
- timeout;
- receive/send error;
- empty response;
- partial-file transport failure;
- server-side `5xx` responses.

This matters because the same bad Archive edge can present differently depending on exactly where the connection fails. A broken TLS/storage path may appear as curl 35, curl 56, timeout, partial transfer or a server-side error.

### What does not change

Failover changes the transport host, not the logical item being downloaded.

The original Archive.org item/path remains the source identity. The client does not silently switch to a different file or bypass the normal Range/size integrity checks.

---

## Why curl 56 still matters

`CURLE_RECV_ERROR` (`curl 56`) is a receive/transport error, not a dedicated certificate-verification code.

On the Vita OpenSSL stack we have observed curl 56 accompanied by explicit certificate/TLS diagnostics such as a self-signed-chain error. The client records both:

```text
curl result
CURL_ERRORBUFFER text
CURLINFO_SSL_VERIFYRESULT
```

That information is useful because it explains when a nominal receive failure is actually tied to the TLS backend.

However, Archive.org failover no longer depends exclusively on proving that a receive error is TLS-related. Transport-like failures can rotate storage edges even without certificate text, because the practical recovery action is the same: stop retrying the same broken edge.

---

## Long-transfer connection handling

Large Vita downloads can run for a long time over Wi-Fi.

The client enables TCP keepalive and, when supported by the linked libcurl, configures keepalive idle/interval values so half-open Wi-Fi/NAT connections are detected earlier.

Low-speed detection is enabled to prevent an indefinitely dead transfer. Archive.org uses a more patient low-speed window because public archive edges may pause temporarily during large transfers.

User cancellation is also checked through libcurl's transfer-progress callback (`XFERINFO`), so cancellation remains responsive even if the connection is established but no body bytes are currently arriving.

---

## Resume and TLS interaction

TLS recovery must not weaken file-integrity rules.

The client still requires:

- `CURLOPT_RESUME_FROM_LARGE` / `curl_off_t` for offsets beyond 2 GiB;
- a valid `206 Content-Range` beginning at the requested offset;
- clean restart when a server ignores Range and returns a full `200`;
- bounded fallback for stale/invalid resume state;
- validator-aware resume (`If-Range`) when the same resolved resource has an ETag/Last-Modified value.

When a provider changes the resolved URL — for example a refreshed MediaFire CDN URL — validators tied to the previous direct URL are cleared before attempting resume.

See [`DOWNLOAD_RESILIENCE.md`](DOWNLOAD_RESILIENCE.md) for the complete resume state machine.

---

## Useful diagnostics

Primary log:

```text
ux0:data/psvitaalive/logs/session.log
```

Useful network/TLS markers include:

```text
attempt ... tls_like=... ssl_verify=... detail=...
archive failover built N alternate URL(s)
archive selector probe ok=... HTTP=... bytes=... total=... speed=... url=...
archive selector chose speed=... -> https://...
archive selector start -> https://...
archive failover switch HTTP=... -> https://...
archive failover switch curl=... ssl_verify=... -> https://...
retry resume from absolute=...
resume validator attached via If-Range
RESULT curl=... status=... ssl_verify=... tls_like=... effective_url=... curl_detail=...
```

When investigating a field report, keep the **whole retry sequence**. The final curl code alone may not reveal which Archive edge, redirect or TLS session failed earlier.

---

## Download / archive integrity

TLS retries are only one layer. The downloader and ZIP extractor keep strict integrity checks after the network succeeds:

- response metadata is isolated per HTTP response;
- `4xx` / `5xx` bodies never become payload bytes;
- invalid/mismatched HTTP ranges are rejected;
- authoritative remote size is enforced with a small safety margin;
- long downloads periodically re-check free `ux0:` space;
- ZIP/ZIP64 archives are checked for EOCD/ZIP64 completion before extraction;
- large ZIPs use a seekable `sceIo*` libzip source;
- unsupported ZIP compression methods are rejected before extraction starts;
- path traversal is rejected;
- partial extracted files are removed after read/write failures;
- close/finalization errors are checked before an entry/archive is considered complete.

The detailed rules and validation checklist live in [`DOWNLOAD_RESILIENCE.md`](DOWNLOAD_RESILIENCE.md).

---

## Official library references

### libcurl

- Error codes: https://curl.se/libcurl/c/libcurl-errors.html
- Detailed error buffer: https://curl.se/libcurl/c/CURLOPT_ERRORBUFFER.html
- Peer verification: https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html
- SSL verification result: https://curl.se/libcurl/c/CURLINFO_SSL_VERIFYRESULT.html
- 64-bit resume offset: https://curl.se/libcurl/c/CURLOPT_RESUME_FROM_LARGE.html
- Transfer progress / cancellation: https://curl.se/libcurl/c/CURLOPT_XFERINFOFUNCTION.html
- Allowed initial protocols: https://curl.se/libcurl/c/CURLOPT_PROTOCOLS_STR.html
- Allowed redirect protocols: https://curl.se/libcurl/c/CURLOPT_REDIR_PROTOCOLS_STR.html
- SSL session cache: https://curl.se/libcurl/c/CURLOPT_SSL_SESSIONID_CACHE.html
- Fresh connection: https://curl.se/libcurl/c/CURLOPT_FRESH_CONNECT.html
- Forbid connection reuse: https://curl.se/libcurl/c/CURLOPT_FORBID_REUSE.html
- TCP keepalive: https://curl.se/libcurl/c/CURLOPT_TCP_KEEPALIVE.html

### libzip

- Reference documentation: https://libzip.org/documentation/
- Compression support query: https://libzip.org/documentation/zip_compression_method_supported.html
- Reading entry data: https://libzip.org/documentation/zip_fread.html
- File error handling: https://libzip.org/documentation/zip_file_get_error.html
- Custom sources: https://libzip.org/documentation/zip_source_function.html
- Source reads: https://libzip.org/documentation/zip_source_read.html
- ZIP errors: https://libzip.org/documentation/zip_errors.html

---

## Optional mbedTLS-backed curl (build-time)

The client keeps an optional mbedTLS-backed curl build path for experimentation:

```bash
# On the build machine (VitaSDK)
vdpm mbedtls
vdpm curl-mbedtls   # when available on the selected VitaSDK channel

cd "Client PSVitaAlive"
rm -rf build && mkdir build && cd build
cmake .. -DPSVITAALIVE_USE_MBEDTLS_CURL=ON
cmake --build . -j$(nproc)
```

The production default remains the OpenSSL path until a side build has been validated across the hosts PSVitaAlive actually uses.

Do not switch the official client to a different TLS backend merely because one host works better in isolation; test GitHub, MediaFire, Archive.org and representative catalog/CDN URLs first.

---

## Recommendation

1. Keep **Archive.org storage-edge failover** enabled by default.
2. Keep strict Range / size / payload-integrity rules enabled across all retries.
3. Treat provider-specific recovery as transport recovery, not permission to accept ambiguous bytes.
4. Preserve full session logs for rare field failures so the next fix can be based on evidence instead of guesswork.
5. Test mbedTLS separately before considering any production switch.

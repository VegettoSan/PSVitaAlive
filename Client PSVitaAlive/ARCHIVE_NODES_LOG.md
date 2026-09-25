# Internet Archive node diagnostics log

PSVitaAlive writes a dedicated Internet Archive selector trace to:

```text
ux0:data/psvitaalive/logs/archive_nodes.log
```

The file is reset on every client launch. It is intentionally separate from `session.log` so reports about slow Internet Archive downloads can be inspected without unrelated UI/catalog/image noise.

## What is recorded

For each Internet Archive payload the log can record:

- Archive identifier, file name, canonical URL and destination path.
- Resume offset, catalog/link size hint and whether proactive selection is eligible.
- Metadata request status plus `server`, `d1`, `d2` and `dir` values.
- Every direct candidate from metadata. `dn*` / `.ca.archive.org` nodes are no longer filtered out: they are tagged as historical-risk candidates and are allowed to compete in the benchmark.
- One-byte Range fallback probes when no catalog size hint is available.
- Sequential 256 KiB benchmark probes for payloads at or above the 16 MiB threshold.
- For every probe: curl result, HTTP result, bytes received, remote total, elapsed time, TTFB, body-transfer time, whole-request throughput, body throughput, redirect count, remote IP, requested URL, effective URL, risk tag and curl error detail.
- Fastest-first ranking based primarily on body throughput (`bytes / (elapsed - TTFB)`), with lower TTFB used as a tie-breaker.
- The effective Archive destination actually reached after redirects. Ranking is tied to that effective destination rather than blindly to the originally requested storage hostname.
- Effective-host deduplication, so multiple requested candidates that resolve to the same real Archive node do not create redundant failover entries.
- The selected effective URL used to start the real transfer.
- Every real transfer attempt: active/effective URL, curl/HTTP result, IP, redirects, TTFB, sampled speed, remote total and SSL verify result.
- Every failover with the reason plus `from` and `to` URLs.
- Final transfer result and explicit success/error/cancelled/size-mismatch outcome.

Probe traffic is diagnostic only and is never written into the payload file. Catalog sizes remain hints used only to decide whether the speed benchmark should run; `Content-Length` / `Content-Range` from the real HTTP transfer remain authoritative for download size and integrity.

## Risky Archive edges

Some `dn*` / `.ca.archive.org` nodes have historically shown TLS or transport problems with the Vita OpenSSL stack. They are therefore still marked as `risk=dn_or_ca`, but they are **not blocked**.

For a fresh payload of 16 MiB or larger, the selector probes them like any other candidate. A risky node is eligible to win when it successfully returns HTTP 206 for the bounded Range probe and its measured body throughput is the best result on that user's own Vita/network. If it fails TLS, HTTP Range or the transfer probe, it simply does not win proactive selection and the existing failover path remains available.

For small payloads that skip the speed benchmark, normal `ia*` nodes stay earlier in discovery order; a risky edge is only reached if earlier candidates do not validate.

## Whole-request speed vs body throughput

The log keeps both metrics because they answer different questions:

```text
total_speed = bytes / elapsed
body_speed  = bytes / (elapsed - TTFB)
```

`total_speed` includes DNS/connect/TLS/redirect/first-byte latency and remains useful diagnostically. `body_speed` better represents sustained payload throughput for a large download, so the selector uses `body_speed` for ranking and TTFB only as a tie-breaker.

This prevents a storage node with a slower first byte but much faster sustained transfer from being unfairly ranked below a genuinely slower node.

## Requested URL vs effective URL

Internet Archive may redirect one direct storage hostname to another. For example, a requested `ia60...` URL may finish its probe on an `ia80...` URL, or vice versa.

The log records both:

```text
requested_url=...
effective_url=...
```

After a successful probe, PSVitaAlive uses the usable `effective_url` as the candidate identity for ranking/selection. Candidates that end on the same effective host are deduplicated. This keeps the ranking aligned with the server that actually delivered the probe bytes and avoids immediately repeating a redirect that the benchmark already resolved.

## Useful markers

Typical markers include:

```text
DOWNLOAD_REQUEST
METADATA_REQUEST
METADATA_RESULT
METADATA_FIELDS
METADATA_HOST_RISKY
CANDIDATE_BUILD
CANDIDATE[0]
SELECTOR_BEGIN
PROBE
BENCHMARK_BEGIN
BENCHMARK_SKIPPED
RANK[1]
EFFECTIVE_DUPLICATE_SKIPPED
SMALL_FILE_EFFECTIVE_URL
SELECTED
REAL_DOWNLOAD_SELECTED
TRANSFER_ATTEMPT_BEGIN
TRANSFER_ATTEMPT_RESULT
FAILOVER
FINAL_TRANSFER
OUTCOME
```

When investigating a slow Archive.org download, reproduce it with a fresh download larger than 16 MiB and attach `archive_nodes.log`. Compare `body_speed_*` across `PROBE` / `RANK` lines, then verify that `SELECTED`, `REAL_DOWNLOAD_SELECTED` and `TRANSFER_ATTEMPT_RESULT effective_url=` all agree with the node that actually carried the download.

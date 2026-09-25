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
- Direct candidates that remain after filtering known problematic `dn*` / `.ca.archive.org` edges.
- One-byte Range fallback probes when no catalog size hint is available.
- Sequential 256 KiB benchmark probes for payloads at or above the 16 MiB threshold.
- For every probe: curl result, HTTP result, bytes received, remote total, elapsed time, TTFB, B/s, KiB/s, MiB/s, redirect count, remote IP, requested URL, effective URL and curl error detail.
- Fastest-first ranking and the node selected for the real transfer.
- Every real transfer attempt: active/effective URL, curl/HTTP result, IP, redirects, TTFB, sampled speed, remote total and SSL verify result.
- Every failover with the reason plus `from` and `to` URLs.
- Final transfer result and explicit success/error/cancelled/size-mismatch outcome.

Probe traffic is diagnostic only and is never written into the payload file. Catalog sizes remain hints used only to decide whether the speed benchmark should run; `Content-Length` / `Content-Range` from the real HTTP transfer remain authoritative for download size and integrity.

## Useful markers

Typical markers include:

```text
DOWNLOAD_REQUEST
METADATA_REQUEST
METADATA_RESULT
METADATA_FIELDS
CANDIDATE_BUILD
CANDIDATE[0]
SELECTOR_BEGIN
PROBE
BENCHMARK_BEGIN
BENCHMARK_SKIPPED
RANK[1]
SELECTED
REAL_DOWNLOAD_SELECTED
TRANSFER_ATTEMPT_BEGIN
TRANSFER_ATTEMPT_RESULT
FAILOVER
FINAL_TRANSFER
OUTCOME
```

When investigating a slow Archive.org download, reproduce it with a fresh download larger than 16 MiB and attach `archive_nodes.log`. The ranking and `REAL_DOWNLOAD_SELECTED` / `TRANSFER_ATTEMPT_RESULT` lines make it possible to compare the measured winner with the node that actually carried the download.

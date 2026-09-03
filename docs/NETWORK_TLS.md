# PSVitaAlive — TLS / libcurl notes

## Default stack (production today)

- **libcurl** from VitaSDK (e.g. 8.x)
- **OpenSSL 1.0.2** (EOL) as TLS backend
- App sets `CURLOPT_SSL_VERIFYPEER/HOST = 0` (no system CA store on Vita)

This works for GitHub and many hosts. Some **Internet Archive** storage nodes (`dn*.ca.archive.org`) fail TLS handshakes on this stack.

## Runtime mitigations (always on)

1. Re-apply verify-off + clear `CAINFO`/`CAPATH` every download attempt.
2. On archive.org SSL failure, fetch `https://archive.org/metadata/<id>` and retry on alternate hosts (`server` / `d1` / `d2`), preferring non-`dn` / non-`.ca` edges.

## Optional: mbedTLS-backed curl (build-time)

```bash
# On the build machine (VitaSDK)
vdpm mbedtls
vdpm curl-mbedtls   # if available on your channel

cd "Client PSVitaAlive"
rm -rf build && mkdir build && cd build
cmake .. -DPSVITAALIVE_USE_MBEDTLS_CURL=ON
cmake --build . -j$(nproc)
```

If `curl-mbedtls` is not installed, the default OpenSSL link remains the safe path (`-DPSVITAALIVE_USE_MBEDTLS_CURL=OFF`).

## Recommendation

1. Ship with **archive.org failover** (default code path).
2. Test mbedTLS on a side build before switching the official release.

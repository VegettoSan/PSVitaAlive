# `Client PSVitaAlive/source/` — PS Vita client source

This directory contains the implementation of the PSVitaAlive native Homebrew client.

## Current modules

- `catalog/` — catalog parsing, application records and catalog-facing behavior.
- `network/` — HTTP/HTTPS communication and download handling.
- `storage/` — local filesystem paths and persistence.
- `ui/` — vita2d screens, overlays and navigation.
- `installer/` — VPK/PKG/ZIP installation orchestration.
- `archive/` — archive and format-detection support.

## Catalog boundary

The client consumes only the generated public catalogs:

```text
catalog.json
authors.json
categories.json
```

It must not know how VitaDB, VitaDBtoo or another external source is fetched or merged.

```text
external sources
      ↓
gitHub Actions
      ↓
catalog.json / authors.json / categories.json
      ↓
PS Vita client
```

This separation keeps the client simple, cacheable and independent from source-specific APIs.

## Compatibility

The parser must tolerate optional fields and preserve compatibility with older application records. Unknown fields should be ignored rather than treated as fatal errors.

Use `title_id` as the Vita application identity for installation/update logic.

## Downloads

An application may expose multiple links. The client should prefer a `Download` link marked `recommended: true` when available, while still allowing other download/mirror sources where the UI supports them.

Do not assume every download is a GitHub Release.

## Installation boundary

Installation logic belongs to the client, not the catalog pipeline. UI code should request installation through the controller/dispatcher layer rather than calling filesystem or promoter APIs directly.

## Development

For catalog-related changes:

1. Inspect the published JSON contract.
2. Do not add direct external-source calls to the client.
3. Test with both local and remote catalog data.
4. Preserve VitaSDK/Vita3K compatibility.
5. Avoid SQLite unless a concrete performance or functionality requirement justifies it.
6. Keep UI and installation logic separated.

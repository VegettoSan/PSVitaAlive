# PSVitaAlive — Native PS Vita client

The PSVitaAlive client is the native PlayStation Vita/PSTV application for PS Vita Alive Store. It is built with VitaSDK, C/C++, CMake, vita2d and libcurl.

## Current responsibilities

The client currently provides the native catalog/UI and download/install flow for supported content. It consumes the generated catalogs published by this repository and does **not** contact VitaDB, VitaDBtoo or other external catalog providers directly.

Current documented behavior includes:

- loading published catalog JSON data;
- browsing catalog entries and application details;
- handling multiple application links;
- HTTP downloads for supported VPK/PKG/ZIP content;
- VPK installation through the Vita homebrew promotion flow;
- post-install verification of the expected application tree;
- installation progress and explicit success/error result panels;
- session and installation logging;
- a branded, working Vita LiveArea in the generated VPK.

The client is still under active development, so a documented module should be treated as the current implementation contract rather than a promise that every future feature is complete.

## Build environment

### Requirements

- VitaSDK installed and available through `VITASDK`.
- CMake and GNU Make.
- A PS Vita/PSTV test environment when possible; Vita3K can be used for development checks.
- VitaShell for installing test VPKs on real hardware.

### Build

```bash
export VITASDK=/usr/local/vitasdk   # or your VitaSDK path
cd "Client PSVitaAlive"
rm -rf build
mkdir build
cd build
cmake ..
make -j1
```

The build produces:

```text
PSVitaAlive.vpk
```

Title ID:

```text
PSVA00001
```

Using `-j1` is recommended when diagnosing build failures because it keeps the first real error visible. Parallel builds can otherwise end with a generic `Error 2` after the useful error has scrolled away.

## VPK and LiveArea packaging

The client uses an explicit VPK packaging command instead of `vita_create_vpk()` for the final resource packaging step. This is intentional: the project directory can contain spaces (`Client PSVitaAlive`), while the VitaSDK helper's generated `vita-pack-vpk` command can mishandle those absolute paths.

The current CMake configuration therefore invokes `vita-pack-vpk` from the build directory with a relative resource mapping:

```text
-a ../assets/sce_sys=sce_sys
```

The generated `eboot.bin` target is `eboot.bin-self`, and the VPK target depends on that target before invoking `vita-pack-vpk`.

The LiveArea resource contract is documented separately in:

[`assets/sce_sys/README.md`](assets/sce_sys/README.md)

## Data consumed by the client

The canonical Homebrew data contract is:

```text
catalog.json
authors.json
categories.json
```

The client must not scrape or query external catalog providers directly.

Applications reference `author_ids`, `category_id` and `subcategory_ids`. The parser must tolerate optional fields so older records remain usable.

## Local Vita data

The client uses the following working areas under `ux0:data/psvitaalive/`:

```text
ux0:data/psvitaalive/
├── logs/
│   ├── session.log
│   └── install.log
├── cache/
└── tmp/
```

Temporary files should be cleaned after successful operations and retained only when needed for diagnostics or resumable work.

## Modules

| Module | Responsibility |
|---|---|
| `source/catalog/` | Catalog parsing and catalog-facing data handling |
| `source/network/` | HTTP/HTTPS communication and download management |
| `source/storage/` | Local paths and filesystem persistence |
| `source/installer/` | VPK/PKG/ZIP installation orchestration |
| `source/archive/` | Archive/format detection and extraction support |
| `source/ui/` | vita2d rendering, screens, overlays and navigation |

Each important module has its own README.

## Installation requirements

For homebrew VPK installation, the Vita must support the normal homebrew environment and VitaShell. The client does not implement DRM bypass or license generation.

Commercial PKG behavior depends on the Vita environment and its installed plugins; the client does not create licenses or bypass NPDRM protections.

## Reusing the client/catalog

If you build another Vita application on top of this project:

1. Treat `catalog.json`, `authors.json` and `categories.json` as the public data contract.
2. Keep catalog acquisition separate from external-source adapters.
3. Ignore unknown optional JSON fields.
4. Use `title_id` for Vita installation/update identity.
5. Support multiple `links` rather than assuming one download provider.
6. Keep UI logic separate from filesystem/promoter operations.
7. Preserve the LiveArea packaging contract if you reuse the VPK build system.

## Development rule

Implement and validate changes in small phases. Prefer a known-good build over broad simultaneous changes, and verify the result on real hardware when practical.

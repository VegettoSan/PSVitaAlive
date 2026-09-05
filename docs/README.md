# `docs/` — Project documentation

Supplementary documentation for architecture, pipelines and client behaviour.

## How to use this folder

1. Prefer **code and generated artifacts** as source of truth when docs drift.
2. Update the matching README next to the code when behaviour changes.
3. Keep long design notes here if they do not belong in a module README.

## Documentation map

| Location | Topic |
|----------|--------|
| Root `README.md` | Project overview, public JSON API, multi-catalog, **recommended Vita setup** (iTLS-Enso, DNS), **download/install locks**, commercial PKG routing, client feature summary |
| `docs/MULTILANGUAGE.md` | UI localization architecture (EN/ES), phases, TextId / `.lang` files |
| `docs/NETWORK_TLS.md` | libcurl / OpenSSL / archive.org failover / optional mbedTLS |
| `Client PSVitaAlive/source/installer/README.md` | BGDL, VPK, **PSP/PS1 Adrenaline unpack** (Folder/ISO), keep-awake, shell locks |
| `apps/`, `authors/`, `categories/` | Data contracts |
| `scripts/` | Generation and validation |
| `sources/` | External feeds |
| `web/` | Website |
| `Client PSVitaAlive/` | Native client build and modules; job safety overview |
| `Client PSVitaAlive/source/update/README.md` | Self-update (PSVAUPDT1) handoff rules |
| `Client PSVitaAlive/source/catalog/README.md` | Multi-catalog cache + zRIF sidecar |
| `Client PSVitaAlive/source/installer/README.md` | VPK / **BGDL PKG**, **Plugin + tai config**, Adrenaline unpack, ZIP integrity, **keep-awake + shell locks** |
| `Client PSVitaAlive/source/ui/README.md` | **LOCKED** UI, color themes + cross-fade, UI fonts, i18n hooks, essential plugins modal |
| `.github/workflows/` | CI / Pages |

## Change classification

When fixing a bug, identify the layer:

1. External acquisition  
2. Normalization / identity / merge  
3. Overrides  
4. Catalog generation / validation  
5. Website  
6. PS Vita client (network, install, UI, update)

Patch the responsible layer—not a generated catalog—whenever possible.

- [NETWORK_TLS.md](NETWORK_TLS.md) — libcurl / OpenSSL / archive.org failover / optional mbedTLS

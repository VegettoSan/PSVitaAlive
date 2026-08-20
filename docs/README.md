# `docs/` — Project documentation

Supplementary documentation for architecture, pipelines and client behaviour.

## How to use this folder

1. Prefer **code and generated artifacts** as source of truth when docs drift.
2. Update the matching README next to the code when behaviour changes.
3. Keep long design notes here if they do not belong in a module README.

## Documentation map

| Location | Topic |
|----------|--------|
| Root `README.md` | Project overview, public JSON API, multi-catalog |
| `apps/`, `authors/`, `categories/` | Data contracts |
| `scripts/` | Generation and validation |
| `sources/` | External feeds |
| `web/` | Website |
| `Client PSVitaAlive/` | Native client build and modules |
| `Client PSVitaAlive/source/update/README.md` | Self-update (PSVAUPDT1) handoff rules |
| `Client PSVitaAlive/source/catalog/README.md` | Multi-catalog cache + zRIF sidecar |
| `Client PSVitaAlive/source/installer/README.md` | VPK / BGDL / plugins |
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

# PS Vita Alive Store

<p align="center">
  <img src="https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/web/assets/logo/PSVitaAlive_Store_logo_text.png" alt="PS Vita Alive Store" width="520">
</p>

<p align="center">
  <strong>A free, open catalog platform for PlayStation Vita: homebrew and commercial packages.</strong>
</p>

PS Vita Alive Store helps users discover, download and install content on PlayStation Vita. Infrastructure stays free and server-less: **GitHub**, **GitHub Actions** and **GitHub Pages**.

---

## Repository layout

| Area | Role |
|------|------|
| `apps/`, `authors/`, `categories/` | Canonical **homebrew** editable data |
| `scripts/`, `.github/workflows/` | Import, normalize, validate, generate catalogs |
| `catalog.json`, `authors.json`, `categories.json` | Generated **homebrew** public contract |
| `catalog_psvita_games.json` | Commercial **PS Vita** games (PKG / DLC / Updates / Mods) |
| `catalog_psp_games.json` | **PSP** commercial catalog |
| `catalog_ps1_games.json` | **PS1** commercial catalog |
| `web/` | Static website (GitHub Pages) |
| `Client PSVitaAlive/` | Native PS Vita client (VPK) |
| `sources/`, `catalog_overrides/` | External source config and manual enrichment |
| `docs/` | Extra documentation |

```text
apps/*.json  +  external sources (VitaDB, VitaDBtoo, …)
        │
        ▼
 normalize → merge → overrides → validate
        │
        ▼
 catalog.json / authors.json / categories.json
        │
        ├───────────────┐
        ▼               ▼
     Website        PS Vita client
```

Commercial catalogs (`catalog_psvita_games.json`, etc.) are maintained separately and also consumed by the website and client.

**Do not hand-edit generated `catalog.json` / `authors.json` / `categories.json`.** Edit `apps/` or overrides instead.

---

## Public catalog endpoints

### Homebrew

```text
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/catalog.json
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/authors.json
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/categories.json
```

### Commercial / multi-catalog

```text
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/catalog_psvita_games.json
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/catalog_psp_games.json
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/catalog_ps1_games.json
```

Any HTTP client can consume these JSON files without using this store’s UI.

---

## Link types (website & data)

On commercial game pages, links are grouped by `type`:

| `type` | Section |
|--------|---------|
| `Download` | Downloads (base PKG / VPK) |
| `DLC` | DLC |
| `Update` | Updates (patches) |
| `Mod` | Mods (optional; shown when present) |
| Other | Other Links |

PS Vita game entries may include per-SKU language notes (No-Intro when available, region-typical fallback otherwise) and official update PKG links from NoPayStation `PSV_UPDATES.tsv`.

---

## Clients

### Website (`web/`)

Static catalog browser: homebrew, authors, categories, and commercial game detail pages with separated download sections.

### PS Vita client (`Client PSVitaAlive/`)

Native client (Title ID **PSVAS1178**) with:

- Multi-catalog browsing (Homebrew, Vita Games, PSP, PS1)
- Search, settings, touch + controls
- Download (including MediaFire resolution where implemented)
- Install paths for VPK / ZIP / PKG-related flows
- Self-update check against **GitHub Releases**
- Plugin detection preferences (prefer `ur0` for taiHEN config)
- Session logs under `ux0:data/psvitaalive/logs/`

See `Client PSVitaAlive/README.md` for build and runtime details.

---

## Contributing data

1. Add or edit JSON under `apps/` (and `authors/` when needed).
2. Prefer `catalog_overrides/` for enrichment that must survive external re-imports.
3. Run validation workflows; fix reported issues.
4. Do not commit broken relative media paths for external icons/screenshots—use absolute public URLs.

---

## License

See `LICENSE` in the repository root.

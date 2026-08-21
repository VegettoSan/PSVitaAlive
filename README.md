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
| `catalog_psvita_games.json` | Commercial **PS Vita** games (PKG / DLC / Updates / Mods) — **no** embedded zRIF |
| `catalog_psvita_games.zrifidx` | Separate **zRIF** index for Vita PKG licenses (url → zRIF) |
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
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/catalog_psvita_games.zrifidx
```

The **zRIF index** (`catalog_psvita_games.zrifidx`) is a separate license sidecar for commercial Vita PKGs (NoPayStation). Format: one line per entry, `content_id<TAB>zrif`. It is **not** embedded in the main Vita catalog JSON so the client can keep four catalogs in RAM without saturating memory. The client downloads this file on demand when loading Vita Games and looks up licenses only at PKG install time.


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
- Self-update via **GitHub Releases** using a helper bubble **PSVAUPDT1** (client cannot safely promote itself while running)
- Plugin detection (AutoPlugin2-style `tai/config.txt` parse; prefer `ur0`)
- Licensed Vita PKG installs via system **BGDL** when zRIF is available
- Session logs under `ux0:data/psvitaalive/logs/` (including `updater.log`)

See `Client PSVitaAlive/README.md` for build, self-update handoff and runtime details.

---

## Contributing data

1. Add or edit JSON under `apps/` (and `authors/` when needed).
2. Prefer `catalog_overrides/` for enrichment that must survive external re-imports.
3. Run validation workflows; fix reported issues.
4. Do not commit broken relative media paths for external icons/screenshots—use absolute public URLs.

---

## Data sources & credits

This project **aggregates, normalizes, validates and curates** metadata from multiple
public homebrew databases and community work. Automation does bulk import and cleanup;
maintainers also apply **manual** fixes, categories, links and quality control.

### Upstream homebrew databases (acknowledged)

We gratefully acknowledge the communities and projects whose public data helped seed
or enrich parts of the homebrew catalog, including:

| Source | Role (typical) |
|--------|----------------|
| **[VitaDB](https://www.rinnegatamante.eu/vitadb/)** (Rinnegatamante & contributors) | Long-standing Vita homebrew database / client ecosystem |
| **[VitaHomebrewDB / VitaDBtoo](https://github.com/DrDecki/VitaDBtoo-db)** and related mirrors | Community catalog continuity and static hosting after VitaDB outages |
| Individual homebrew **authors** | Original apps, icons, screenshots and release pages |

Exact import rules live under `sources/` and the catalog workflows. Presence in our
catalog does **not** mean we claim ownership of any third-party homebrew binary or brand.

### What this project adds

- Unified JSON contracts (`catalog.json`, `authors.json`, `categories.json`)
- Validation, deduplication and category/subcategory consistency
- Manual curation and ongoing maintenance
- Public clients and documentation so others can **learn from the design**

### Commercial catalogs

PS Vita / PSP / PS1 package listings and license sidecars may incorporate public
metadata associated with communities such as **NoPayStation** and related tools.
Those files are **not** the same as the homebrew CC BY catalog; respect third-party
rights and do not treat zRIF or PKG links as redistributable “owned” content of this repo.

## License

| Part | Terms |
|------|--------|
| **Software** (clients, scripts, tooling, build system) | **Source available — no copying.** Public to read and use as **inspiration**; you may **not** copy, redistribute, or reuse this source code in other projects. See root [`LICENSE`](LICENSE). |
| **Homebrew catalog data** (`catalog.json`, `authors.json`, `categories.json`, `apps/`, …) | **CC BY 4.0** — reuse allowed **with attribution** to **PS Vita Alive Store**. Details: [`CATALOG_LICENSE.md`](CATALOG_LICENSE.md) |

### Software (clients & tooling)

The repository is **public** and the READMEs document how the system works so other
developers can use it as **inspiration** for their own projects.

That is **not** permission to copy-paste or redistribute this source code as the basis
of another app. Please implement your own code; use our docs and design as reference.

Official release binaries are for personal use on your devices unless a release says otherwise.

### Using the homebrew catalog in your own project

You may consume or mirror the public homebrew catalog JSON. Please credit the project, for example:

> Homebrew catalog data from **PS Vita Alive Store** — https://github.com/VegettoSan/PSVitaAlive (CC BY 4.0)

Commercial game catalogs and zRIF data are third-party aggregations; treat them separately and respect applicable rights.

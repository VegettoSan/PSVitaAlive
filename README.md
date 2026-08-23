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
| `catalog_psvita_games.zrifidx` | Separate **zRIF** index for Vita PKG licenses (`content_id` → zRIF) |
| `catalog_psp_games.json` | **PSP** commercial catalog |
| `catalog_ps1_games.json` | **PS1** commercial catalog |
| `web/` | Static website (GitHub Pages) |
| `Client PSVitaAlive/` | Native PS Vita client (VPK) |
| `sources/`, `catalog_overrides/` | External source config and manual enrichment |
| `docs/` | Extra documentation |

```text
apps/*.json  +  external sources (VitaDB, VitaHomebrewDB, …)
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

The **zRIF index** (`catalog_psvita_games.zrifidx`) is a separate license sidecar for commercial Vita PKGs (NoPayStation-style). Format: one line per entry, `content_id<TAB>zrif`. It is **not** embedded in the main Vita catalog JSON so the client can keep four catalogs in RAM without saturating memory. The client downloads this file on demand when loading Vita Games and looks up licenses only at PKG install time (`content_id` first, Title ID fallback if the link omits `content_id`).

### Native client — commercial PKG install (verified)

The **PS Vita client** installs commercial packages from **Vita Games**, **PSP**, and **PS1** catalogs through the console’s system download manager (**BGDL**), not by promoting a raw `.pkg` file:

1. Resolve zRIF / RIF for the **selected link** (per-region / per-DLC `content_id`)
2. Enqueue URL + license with the system UI (notification title = **game name**)
3. User monitors LiveArea notifications until install completes

Requires a real device with taiHEN, **NoNpDrm** (and related plugins as needed), and a client build with **UNSAFE** privileges for ShellSvc access. See [`Client PSVitaAlive/source/installer/README.md`](Client%20PSVitaAlive/source/installer/README.md).

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
- Licensed **Vita / PSP / PS1 PKG** installs via system **BGDL** (verified on real hardware) when license data resolves
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

This project **aggregates, normalizes, validates and curates** metadata from multiple public homebrew databases and community work. Automation does bulk import and cleanup; maintainers also apply **manual** fixes, categories, links and quality control.

### Upstream homebrew databases (acknowledged)

We gratefully acknowledge the communities and projects whose public data help seed or enrich parts of the homebrew catalog, including:

| Source | Role (typical) |
|--------|----------------|
| **[VitaDB](https://www.rinnegatamante.eu/vitadb/)** (Rinnegatamante & contributors) | Long-standing Vita homebrew database / client ecosystem |
| **VitaHomebrewDB** (DrDecki and contributors) | Community-preserved homebrew metadata, static resources, and continuity data |
| Individual homebrew **authors** | Original apps, icons, screenshots and release pages |

### Important ownership clarification

The fact that VitaDB or VitaHomebrewDB data appears in VitaHub does **not** mean PS Vita Alive Store owns that upstream data. Those databases and the individual works represented in them remain subject to their respective rights and licenses.

VitaHub uses upstream information only to **discover, import, cross-check, enrich, normalize and preserve** its own public catalog. VitaHub's CC BY 4.0 catalog license applies only to the rights VitaHub actually holds in its own curation and derived catalog work; it does not relicense third-party material that VitaHub does not own.

The project also does not claim ownership of third-party homebrew binaries, icons, screenshots, trademarks or release assets. Each homebrew should be used according to its original author's terms.

### What this project adds

- Unified JSON contracts (`catalog.json`, `authors.json`, `categories.json`)
- Validation, deduplication and category/subcategory consistency
- Manual curation and ongoing maintenance
- Public clients and documentation so others can reuse the catalog and learn from the design

### Commercial catalogs

PS Vita / PSP / PS1 package listings and license sidecars may incorporate public metadata associated with communities such as **NoPayStation** and related tools. Those files are **not** the same as the homebrew CC BY catalog; respect third-party rights and do not treat zRIF or PKG links as redistributable “owned” content of this repo.

## License

| Part | Terms |
|------|--------|
| **Software** (client, scripts, tooling, build system) | **MIT License** — free to use, copy, modify, merge, publish, distribute, sublicense and sell, subject to the MIT notice and warranty terms. See root [`LICENSE`](LICENSE). |
| **Homebrew catalog data** (`catalog.json`, `authors.json`, `categories.json`, `apps/`, …) | **CC BY 4.0** — reuse, adaptation and redistribution are allowed with attribution to **PS Vita Alive Store / PSVitaAlive**, while respecting upstream and third-party rights. See [`CATALOG_LICENSE.md`](CATALOG_LICENSE.md). |

### Using the homebrew catalog in your own project

You may reuse, mirror, adapt or build your own application, website, API or catalog from the covered VitaHub homebrew data. Give clear credit to PS Vita Alive Store / PSVitaAlive, and preserve the upstream credits for information derived from VitaDB, VitaHomebrewDB, or individual authors when applicable.

Example:

> Homebrew catalog data from **PS Vita Alive Store / PSVitaAlive** — https://github.com/VegettoSan/PSVitaAlive (CC BY 4.0)

The detailed terms, attribution requirements, and third-party data disclaimer are documented in [`CATALOG_LICENSE.md`](CATALOG_LICENSE.md).

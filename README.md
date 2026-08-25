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
apps/*.json  +  configured external sources (currently VitaHomebrewDB during migration)
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

### Current situation (transition in progress)

The automatic **VitaDB external feed is no longer used** for catalog acquisition. It was removed from the configured external sources in **August 2026**.

The current migration keeps **VitaHomebrewDB** as an optional external source for discovery, cross-checking, enrichment and preservation while the catalog moves toward independent maintenance through **PSVitaAlive’s own** `apps/`, `authors/` and `categories/` records.

| Project | Current role | Maintainer / community | Official pages |
|---------|--------------|------------------------|----------------|
| **[VitaDB](https://www.rinnegatamante.eu/vitadb/)** | Historical source / legacy references only | [Rinnegatamante](https://github.com/Rinnegatamante) and contributors | Site: [rinnegatamante.eu/vitadb](https://www.rinnegatamante.eu/vitadb/) · Related work on [GitHub](https://github.com/Rinnegatamante) |
| **[VitaHomebrewDB](https://drdecki.github.io/VitaHomebrewDB/)** | Current optional external source during migration | [DrDecki](https://github.com/DrDecki) and contributors | Site: [drdecki.github.io/VitaHomebrewDB](https://drdecki.github.io/VitaHomebrewDB/) · Data: [DrDecki/VitaDBtoo-db](https://github.com/DrDecki/VitaDBtoo-db) · Also [DrDecki/VitaHomebrewDB](https://github.com/DrDecki/VitaHomebrewDB) |

We **thank and credit** those projects and everyone who built the underlying homebrew ecosystem. PS Vita Alive Store is **not** a replacement for them and **does not own** their databases, branding, or hosting.

### VitaDB migration note

Removing the automatic VitaDB feed does **not** remove existing records or links immediately. Some current `apps/` entries may still contain historical VitaDB-derived metadata or links. Those records and external links will be reviewed and migrated/removed progressively as part of the catalog cleanup.

This README describes the source policy; it does **not** change existing application records by itself.

Individual **homebrew authors** remain the owners of their apps, icons, screenshots, and releases. Each catalog entry carries author information (`author_ids` / author profiles) and, when available, links to the author’s repository or release page.

### Important ownership clarification

Using VitaDB, VitaHomebrewDB or other public sources for discovery or enrichment does **not** mean PS Vita Alive Store owns that upstream data. Those databases and the works they describe remain under their respective rights and the terms of their maintainers.

This project uses public information to **discover, import, cross-check, normalize, and present** a unified catalog for easier installation on PS Vita. Our [CC0 1.0 catalog dedication](CATALOG_LICENSE.md) applies only to rights we can dedicate in our own compilation and curation—not to third-party binaries, artwork, or an upstream database as a whole.

### What this project adds

- Unified JSON contracts (`catalog.json`, `authors.json`, `categories.json`)
- Validation, deduplication and category/subcategory consistency
- Manual curation and ongoing maintenance
- Public clients and documentation so others can reuse the catalog and learn from the design

### Commercial catalogs

PS Vita / PSP / PS1 package listings and license sidecars may incorporate public metadata associated with communities such as **NoPayStation** and related tools. Those files are **not** the same as the homebrew CC BY catalog; respect third-party rights and do not treat zRIF or PKG links as redistributable “owned” content of this repo.

## License and ownership

### What this project is

PS Vita Alive Store is a **discovery and download helper** for the PS Vita scene. It does **not** claim ownership of homebrew apps, plugins, ports, icons, screenshots, or other third-party works. Those belong to their **authors**. Each homebrew entry keeps author information (`author_ids` / author profiles) and, when available, links to the author’s repository or releases.

### Software (client, scripts, tooling)

| Part | Terms |
|------|--------|
| **Software** (Vita client, updater, scripts, web tooling, build system) | **[MIT License](LICENSE)** — use, copy, modify, distribute, etc., with the MIT notice |

### Homebrew catalog data

| Part | Terms |
|------|--------|
| **Homebrew catalog data** (`apps/`, `authors/`, `categories/`, generated `catalog.json`, `authors.json`, `categories.json`, and this project’s compilation/curation of that data) | **[CC0 1.0](CATALOG_LICENSE.md)** (public domain dedication) |

**Anyone may use the homebrew catalog data for any purpose**, including other stores, mirrors, apps, and commercial use. **Credit is not required.** A mention is welcome if you want to, but it is optional.

Full text: [CATALOG_LICENSE.md](CATALOG_LICENSE.md).

### Third-party rights (unchanged)

- Homebrew **binaries, source, icons, and screenshots** remain under each author’s rights and licenses.
- This repository is not a claim of ownership over those works.
- Commercial catalogs and zRIF/license sidecars are **not** covered by the CC0 catalog dedication; treat them as third-party reference data.

### Upstream catalogs

Public databases and community work (including projects associated with VitaDB, VitaHomebrewDB, and many individual authors) have helped with discovery and enrichment over time. Credit for that work stays with those projects and authors. This store’s goal is to facilitate finding and installing content, not to replace or claim the creators’ work.

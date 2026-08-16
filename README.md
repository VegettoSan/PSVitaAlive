# PS Vita Alive Store

<p align="center">
  <img src="https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/web/assets/logo/PSVitaAlive_Store_logo_text.png" alt="PS Vita Alive Store" width="520">
</p>

<p align="center">
  <strong>A free, open and preservation-focused Homebrew catalog for PlayStation Vita.</strong>
</p>

PS Vita Alive Store is an open platform for discovering, preserving, downloading and maintaining PlayStation Vita Homebrew. It is designed to remain free, scalable and maintainable without dedicated servers: GitHub, GitHub Actions and GitHub Pages provide the infrastructure.

---

## What is in this repository?

The project has three main parts:

- **Canonical catalog data** in `apps/`, `authors/` and `categories/`.
- **Automation** in `scripts/` and `.github/workflows/`, which imports, normalizes, deduplicates, validates and publishes the catalog.
- **Clients**: the static website in `web/` and the native PS Vita client in `Client PSVitaAlive/`.

The core data architecture is:

```text
apps/*.json
 authors/*.json
 categories/*.json
        │
        ├──────── external sources
        │          (VitaDB, VitaDBtoo, ...)
        │
        ▼
 normalize → deduplicate → merge/enrich → overrides → validate
        │
        ▼
 catalog.json
 authors.json
 categories.json
        │
        ├───────────────┐
        ▼               ▼
      Website       PS Vita client
```

The three generated Homebrew catalogs are the public contract consumed by the website and PS Vita client. **Do not edit generated catalogs manually.**

---

## Public catalog API for other projects

You do **not** need to use the PS Vita Alive Store website or client to use the catalog. Any application capable of reading JSON over HTTP can consume the published files directly.

### Canonical Homebrew endpoints

Raw GitHub endpoints:

```text
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/catalog.json
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/authors.json
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/categories.json
```

GitHub Pages also exposes the repository's public site and generated data. For applications that need a machine-readable source, the raw GitHub URLs above are the simplest integration point.

### What each catalog contains

| File | Purpose |
|---|---|
| `catalog.json` | Homebrew application records |
| `authors.json` | Author profiles referenced by applications |
| `categories.json` | Official categories and their allowed subcategories |

The current `catalog.json` is a JSON array. Each application record contains a stable internal `id` and a unique `title_id`, plus metadata such as name, description, author IDs, category, version, media and download links.

### Minimal JavaScript example

```js
const CATALOG_URL =
  "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/catalog.json";

const response = await fetch(CATALOG_URL);
if (!response.ok) throw new Error(`Catalog HTTP ${response.status}`);

const apps = await response.json();

for (const app of apps) {
  console.log(app.name, app.title_id, app.version);
}
```

### Resolving authors and categories

Applications reference authors and categories by ID instead of duplicating their profiles:

```js
const [apps, authors, categories] = await Promise.all([
  fetch("https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/catalog.json").then(r => r.json()),
  fetch("https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/authors.json").then(r => r.json()),
  fetch("https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/categories.json").then(r => r.json())
]);

const authorById = new Map(authors.map(author => [author.id, author]));
const categoryById = new Map(categories.map(category => [category.id, category]));

const app = apps[0];
const appAuthors = (app.author_ids || [])
  .map(id => authorById.get(id))
  .filter(Boolean);
const category = categoryById.get(app.category_id);
```

### Application fields

The main application contract includes:

- `id` — PS Vita Alive Store internal identifier.
- `title_id` — unique Vita Title ID and the primary installation/update identity.
- `name` — display name.
- `description` — short description.
- `author_ids` — one or more individual author IDs.
- `category_id` — official category ID.
- `subcategory_ids` — valid subcategory IDs for that category.
- `version` and `version_date` — published version information.
- `icon` — application icon URL.
- `screenshots` — screenshot URLs.
- `links` — download, mirror, repository, website, documentation, issues, community and other useful links.
- `status` — `Verified`, `Legacy` or `Archive`.

Additional optional fields may be present for preservation and enrichment, including `long_description`, `changelog`, `requirements`, `downloads`, `hash`, `hash2`, `size`, `data_size`, `data_url`, `score` and `updated_at`.

**Consumers should ignore unknown fields and tolerate missing optional fields.** This is important for forward compatibility and for older Homebrew records.

### Download links

Do not assume that every application uses GitHub Releases. An application can expose multiple links:

```json
"links": [
  {
    "type": "Download",
    "name": "VPK",
    "url": "https://example.org/app.vpk",
    "recommended": true
  },
  {
    "type": "Mirror",
    "name": "Mirror",
    "url": "https://example.org/mirror/app.vpk",
    "recommended": false
  },
  {
    "type": "Repository",
    "name": "Source",
    "url": "https://github.com/example/project",
    "recommended": false
  }
]
```

A consumer should prefer a `Download` link marked `recommended: true` when present, while still allowing users to access other valid sources.

### Version and update handling

Use `title_id` to identify an installed Vita application. Use `version` and `version_date` to determine whether the catalog describes a newer release. Do not use the internal `id` as a replacement for `title_id` when implementing installation or update detection.

### Caching and reliability

The catalog is generated data, not a real-time API. A consumer should cache downloaded JSON locally, refresh it periodically, and handle temporary HTTP failures without destroying its last known good catalog.

If reproducible builds are required, pin a specific Git commit instead of relying on the moving `main` branch URL.

### Image handling

`icon` and `screenshots` are normally public absolute URLs. A consumer should handle missing or unavailable images gracefully and should not assume that every remote resource remains online forever.

---

## Canonical data model

### Applications

Every application has its own JSON file:

```text
apps/<application-id>.json
```

`title_id` must be unique. The internal `id` does not have to match an external catalog's numeric identifier.

### Authors

Every author has an individual profile:

```text
authors/<author-id>.json
```

Applications reference `author_ids`. Multiple developers are represented as multiple profiles, not as one combined author string.

### Categories

Official categories live in `categories/`. Subcategories are declared by the category itself. External sources are mapped into this controlled vocabulary rather than creating arbitrary categories during import.

### Overrides

`catalog_overrides/` stores manual enrichment that should survive external imports, such as extra screenshots, requirements, game-data links or additional download sources.

---

## External catalog integration

External catalogs are **inputs**, not the public data contract. The current source layer documents VitaDB and VitaDBtoo. Their records are normalized into the PS Vita Alive Store model before they reach the generated catalog.

The import process is designed to be non-destructive: an application already maintained locally should not disappear merely because an external source temporarily omits it.

`title_id` is the main identity used to deduplicate Vita applications. Version/date freshness is considered before source priority when selecting the most appropriate current data.

The original external data should remain untouched; adapters and normalizers perform the conversion.

---

## Website

The website is a static HTML/CSS/JavaScript application deployed with GitHub Pages. It consumes the generated catalogs and does not require a custom backend.

See [`web/README.md`](web/README.md).

---

## Native PS Vita client

The native client uses VitaSDK, C/C++, CMake, vita2d and libcurl. It consumes the generated catalogs rather than contacting external catalog providers directly.

The VPK currently includes a working Vita LiveArea with the project branding. Its build documentation, packaging rules and LiveArea asset requirements are documented in [`Client PSVitaAlive/README.md`](Client%20PSVitaAlive/README.md) and [`Client PSVitaAlive/assets/sce_sys/README.md`](Client%20PSVitaAlive/assets/sce_sys/README.md).

---

## GitHub Actions

The automation layer performs source acquisition, normalization, merge/deduplication, persistence, validation, catalog generation and GitHub Pages publication according to the active workflows.

Generated files include:

```text
catalog.json
authors.json
categories.json
```

See [`.github/workflows/README.md`](.github/workflows/README.md).

---

## Documentation map

- [`apps/README.md`](apps/README.md) — application records and publishing rules.
- [`authors/README.md`](authors/README.md) — individual author profiles.
- [`categories/README.md`](categories/README.md) — official taxonomy.
- [`catalog_overrides/README.md`](catalog_overrides/README.md) — manual enrichment.
- [`sources/README.md`](sources/README.md) — external sources and mappings.
- [`external_authors/README.md`](external_authors/README.md) — provisional external identities.
- [`scripts/README.md`](scripts/README.md) — catalog automation and validation.
- [`scripts/external/README.md`](scripts/external/README.md) — external-source integration engine.
- [`web/README.md`](web/README.md) — static website.
- [`Client PSVitaAlive/README.md`](Client%20PSVitaAlive/README.md) — native PS Vita client.
- [`.github/workflows/README.md`](.github/workflows/README.md) — CI/CD workflows.
- [`docs/README.md`](docs/README.md) — technical decisions and project memory.

---

## Contributing

Before changing the repository:

1. Read the README for the area you are modifying.
2. Never edit generated catalogs manually.
3. Change application data in `apps/` or `catalog_overrides/` as appropriate.
4. Change author data in `authors/`.
5. Change taxonomy in `categories/` and update mappings when required.
6. Change source adapters when an external format needs normalization.
7. Run the relevant validation before publishing.
8. Preserve compatibility with both the website and PS Vita client.

---

## Project principles

- Free to use.
- Open and GitHub-based.
- No dedicated backend required.
- Modular and scalable.
- Friendly to new and legacy Homebrew.
- Designed for preservation as well as distribution.
- Generated catalogs are treated as a public compatibility contract.

---

## License

See [`LICENSE`](LICENSE) for the repository license. Individual Homebrew projects remain subject to their own licenses and distribution terms.

<p align="center">
  <sub>PS Vita Alive Store — Free & Open PlayStation Vita Homebrew Catalog</sub>
</p>

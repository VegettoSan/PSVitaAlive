# Homebrew catalog data — public domain dedication (CC0 1.0)

## Scope

This document applies to the **homebrew catalog data** published by PS Vita Alive Store (PSVitaAlive), including where applicable:

- `apps/`, `authors/`, and `categories/` JSON records maintained in this repository
- generated `catalog.json`, `authors.json`, and `categories.json`
- normalization, deduplication, categorization, link selection, validation, and other compilation/curation work reflected in those files

It does **not** apply to the store client software, scripts, or build system (those remain under the MIT License in `LICENSE`).

## No ownership claim over homebrew

**PS Vita Alive Store does not own, and does not claim to own:**

- third-party homebrew applications, plugins, ports, or tools
- their source code or binaries (VPK and related assets)
- icons, screenshots, artwork, trademarks, or logos created by others
- the original releases published by each homebrew author

Each catalog entry is intended to **credit the real authors** through fields such as `author_ids` / author profiles and, when available, repository or release links. Credits belong to those authors and projects.

This project only aims to **make discovery and download easier** by gathering public metadata and links in one place.

## License for the catalog compilation

To the extent possible under law, the catalog compilation and curation work described under **Scope** is dedicated to the public domain under the
[Creative Commons CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/) dedication.

**You may copy, modify, merge, publish, distribute, use commercially, or build any application or service from this catalog data, with or without credit.** Attribution is appreciated but **not required**.

Full legal text: https://creativecommons.org/publicdomain/zero/1.0/legalcode

## Third-party material

Upstream databases, author repositories, and individual assets remain under their own terms. CC0 here only covers rights this project can dedicate in its own compilation and metadata arrangement. It does **not** relicense third-party binaries, artwork, or an upstream database as a whole.

Respect each homebrew’s own license and the rights of its author. If an author requests a correction or removal of their entry, open an issue on this repository.

## Upstream databases (current use + migration)

While this project builds toward an **independent** homebrew catalog, some entries may still be seeded or enriched from public data associated with:

- **[VitaDB](https://www.rinnegatamante.eu/vitadb/)** — [Rinnegatamante](https://github.com/Rinnegatamante) and contributors  
- **[VitaHomebrewDB](https://drdecki.github.io/VitaHomebrewDB/)** — [DrDecki](https://github.com/DrDecki) and contributors ([VitaDBtoo-db](https://github.com/DrDecki/VitaDBtoo-db), [VitaHomebrewDB](https://github.com/DrDecki/VitaHomebrewDB))

Those projects retain full credit for their own databases and hosting. PS Vita Alive Store does not claim ownership of them. Listing a title here does not transfer ownership of that title to this repository.


## Commercial catalogs

Commercial game catalogs (for example Vita/PSP/PS1 package lists) and any license sidecars (such as zRIF indexes) are **not** covered by this CC0 dedication. Treat them as third-party reference data and respect applicable rights and terms.

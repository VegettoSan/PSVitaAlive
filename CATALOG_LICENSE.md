# Homebrew catalog license (attribution required)

The **homebrew catalog data** published by this project is available for public use
under the terms below. This is separate from the MIT license that covers the
software source code in this repository (see root `LICENSE`).

## Covered data

Includes, without limitation:

- Generated homebrew catalog: `catalog.json`, `authors.json`, `categories.json`
- Editable sources: `apps/`, `authors/`, `categories/` (JSON entries used to build the catalog)
- Derived homebrew listing metadata produced by this repository’s automation and manual curation

**Not** covered by this document (unless stated otherwise elsewhere):

- Third-party homebrew binaries, icons, screenshots, or trademarks (rights remain with their authors)
- Commercial / NPS-oriented catalogs (`catalog_psvita_games.json`, `catalog_psp_games.json`, `catalog_ps1_games.json`) and the zRIF index, which aggregate third-party store metadata and license strings
- The PS Vita / web **client source code** (see root `LICENSE` — MIT)

## License

**Creative Commons Attribution 4.0 International (CC BY 4.0)**

Full legal text: https://creativecommons.org/licenses/by/4.0/legalcode  
Human-readable summary: https://creativecommons.org/licenses/by/4.0/

You may copy, redistribute, adapt, and use the catalog data for any purpose,
including commercial use, **provided that you give appropriate credit**.

## Required attribution

When you use this catalog data (in an app, website, mirror, fork, scraper, or tool),
you must give clear credit to the project, for example:

> Homebrew catalog data from **PS Vita Alive Store**  
> https://github.com/VegettoSan/PSVitaAlive  
> Licensed under CC BY 4.0

A shorter form is also acceptable if space is limited:

> Catalog: PS Vita Alive Store (CC BY 4.0) — https://github.com/VegettoSan/PSVitaAlive

### Where to show it

Prefer at least one visible place, such as:

- About / Credits / License screen in a client
- README or documentation of a mirror or API wrapper
- Website footer or “Data sources” page

### Why attribution is required

This catalog is maintained with automated pipelines **and** ongoing manual curation
(metadata fixes, validation, categories, links, and quality control). Attribution
recognizes that work and helps users find the canonical project.

## Good practice (optional but appreciated)

- Link to the live catalog URLs on this repository when possible
- Prefer the canonical JSON contracts rather than undocumented scrapes of the website alone
- Report corrections upstream so everyone benefits

## Disclaimer

The catalog is provided as-is. Entries describe third-party homebrew; this project
does not claim ownership of those programs. Always respect each homebrew’s own
license and the rights of its authors.

# `sources/` — External sources and import rules

External catalogs are discovery, enrichment, update and preservation inputs. They are **not** the canonical PS Vita Alive Store format and they are never consumed directly by the website or PS Vita client.

The active source configuration is defined in `sources/external_sources.json`.

## Current sources

The current source layer documents:

- **VitaDB** — primary external Vita Homebrew source.
- **VitaDBtoo** — secondary/community source used for cross-checking, enrichment and preservation.

If another source is enabled in the configuration later, its adapter and behavior must be documented here before relying on it in production.

## VitaDB

The current VitaDB adapter uses the documented endpoint behavior and may use HTTPS/HTTP fallbacks plus a browser-like User-Agent because the service can return an empty successful response to unrecognized agents.

Do not replace the source-specific reader with a generic GET without verifying the actual endpoint behavior first.

## VitaDBtoo

VitaDBtoo is consumed from its public `apps.json` catalog and is treated as a secondary source for completing and cross-checking application records.

## Source priority and versions

Source priority is a preference/tie-breaker, not an automatic winner.

The aggregation logic considers version and version date freshness before source priority when choosing the most appropriate current record or recommended download.

## Protecting local applications

Applications already maintained in `apps/` are local authority. A source rebuild must not delete a local application merely because an external source temporarily omits it.

The aggregator loads local applications into the merge process and validates that protected local entries remain present before persistence.

## Category mapping

`sources/category_map.json` translates external types/tags into the official PS Vita Alive Store taxonomy. External sources cannot freely create official categories or subcategories.

Typical mappings include:

| External type | Meaning | PS Vita Alive Store category |
|---:|---|---|
| `1` | Original Game | `games` |
| `2` | Game Port | `ports` |
| `4` | Utility | `utilities` |
| `5` | Emulator | `emulators` |

Textual slugs such as `game`, `port`, `utility` and `emulator` are also handled by the active mapping. Plugin records may arrive through a separate source list and are mapped to `plugins`.

External tags can be translated into official `subcategory_ids`. Only IDs declared by the destination category are retained; duplicates are removed and an `other` fallback may be used when no usable tag exists.

## Media resources

External sources may provide relative paths such as `icon.png` or `screenshot1.png`. The adapter must convert them to absolute public URLs only when the source actually publishes those resources.

Never invent a URL by concatenating an assumed directory structure.

## Adding a new source

1. Identify the real catalog format.
2. Document endpoint, HTTP method, authentication and rate limits when applicable.
3. Implement or adapt an isolated reader.
4. Convert each record to the internal `Candidate` model.
5. Resolve authors as individual identities.
6. Resolve categories through `category_map.json`.
7. Normalize icons, screenshots and download URLs.
8. Define source priority and freshness behavior.
9. Add smoke tests where useful.
10. Run the complete validation before enabling the source in production.

## Fundamental rule

An external source is an **input**. The final authority is PS Vita Alive Store's canonical model: `title_id`, individual authors, official taxonomy, local data, Overrides and merge rules.

The original source data must remain untouched. The adapter/normalizer performs the conversion into PS Vita Alive Store's model.

# `Client PSVitaAlive/source/catalog/` — Catalog consumption

This module is the boundary between the generated PS Vita Alive Store catalogs and the native client.

## Data sources

The client consumes only:

- `catalog.json`
- `authors.json`
- `categories.json`

Do not add scraping or direct API calls to VitaDB, VitaDBtoo or other external catalog providers here.

## Application fields

The parser currently understands fields including:

- `id`
- `title_id`
- `name`
- `description`
- `long_description`
- `author_ids`
- `category_id`
- `subcategory_ids`
- `version`
- `version_date`
- `icon`
- `cover`
- `screenshots`
- `links`
- `status`
- `requirements`
- `changelog`
- `size`

Other fields are allowed and must not break parsing.

## Authors

`author_ids` resolves individual profiles through `authors.json`. An application with multiple authors should not be reduced to one combined author name.

## Version/update identity

Use `title_id` as the primary Vita installation/update identity. Use `version` and `version_date` as release information shown to the user and used by higher-level update logic.

## Links

The parser supports multiple link records and recognizes `Download` links, including the optional `recommended` flag. If the recommended download is unavailable, the client can fall back to another valid Download link.

The client also preserves link metadata for mirrors, repositories, websites and documentation so the UI can expose them when supported.

## Remote resources

Icons and screenshots may be HTTP/HTTPS URLs. Network failures or missing media must not block catalog navigation.

## Compatibility rule

Optional fields may be absent in older records. The parser must use safe defaults and continue loading the rest of the catalog instead of assuming a fully populated record.

# `catalog_overrides/` — Manual enrichment and corrections

This directory contains controlled manual data that must survive external catalog imports.

## When to use an Override

Use an Override when an external source does not contain information that PS Vita Alive Store wants to preserve, for example:

- improved descriptions;
- long descriptions;
- additional screenshots;
- corrected icons;
- requirements;
- changelogs;
- additional download or game-data links;
- alternative mirrors;
- preservation-specific metadata.

## Structure

Create one JSON file per canonical application ID:

```text
catalog_overrides/adrenaline.json
```

The file should contain only the fields that need to be changed or enriched.

## List operations

Supported list operations can use:

- `replace` — replace the complete list;
- `add` — add elements while keeping existing values;
- `remove` — remove specific elements.

Example:

```json
{
  "id": "adrenaline",
  "links": {
    "add": [
      {
        "type": "Download",
        "name": "Game Data",
        "url": "https://example.com/data.zip",
        "recommended": false
      }
    ]
  }
}
```

## Priority

The conceptual order is:

```text
external source
      ↓
merge / enrichment
      ↓
Override
      ↓
canonical application
      ↓
generated catalog
```

A partial Override must not erase fields that it does not modify.

## Protected fields

Normal Overrides do not change core identity/update fields such as:

- `id`
- `title_id`
- `author_ids`
- `category_id`
- `subcategory_ids`
- `status`
- `version`
- `version_date`
- `size`

These restrictions protect application identity and update logic.

## Best practices

- Use the exact canonical application `id`.
- Add only the data that is actually needed.
- Use absolute URLs.
- Never edit `catalog.json` directly.
- Do not use an Override to hide a bug that belongs in a source adapter or normalizer.
- Explain unusual preservation decisions in the commit message or related documentation.

## Example: game data for a port

```json
{
  "id": "my-port",
  "links": {
    "add": [
      {
        "type": "Download",
        "name": "Game Data",
        "url": "https://example.org/game-data.zip",
        "recommended": false
      }
    ]
  }
}
```

The application can continue receiving version updates from external sources while the additional game-data link remains a PS Vita Alive Store-specific enrichment.
